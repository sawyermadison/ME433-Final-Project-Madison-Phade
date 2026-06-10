"""
haptic_viz.py  –  Haptic Paddle Visualizer
============================================
Reads angle (degrees) printed by the Pico over USB serial and renders:
  • A rotating paddle arm + ball showing current position
  • Orange arcs for the haptic walls  (WALL_RIGHT=15°, WALL_LEFT=345°)
  • Red radial lines for the ADC safety stops (hard limits)
  • A force arrow showing how hard the wall is pushing back
  • A live current / penetration readout in the corner
 
Serial protocol expected from the Pico
---------------------------------------
The C code already prints lines like:
    Angle: 123.45 degrees
    Right wall penetration: 2.30 degrees
    Left wall penetration: 1.10 degrees
 
This visualizer parses those lines. No changes to the C code required.
 
Usage
-----
    pip install arcade pyserial
    python haptic_viz.py                        # auto-detect port
    python haptic_viz.py --port /dev/ttyACM0    # explicit port
    python haptic_viz.py --demo                 # animated demo (no hardware)
"""
 
import arcade
import arcade.color
import math
import threading
import argparse
import sys
import time
 
# ── Mirror the constants from your C code ────────────────────────────────────
WALL_LEFT       = 330.0   # degrees
WALL_RIGHT      = 30.0    # degrees
WALL_STIFFNESS  = 30.0    # mA per degree of penetration
ADC_MIN         = 50      # safety stop threshold (raw ADC counts)
ADC_MAX         = 2000    # safety stop threshold (raw ADC counts)
 
# Map ADC safety-stop thresholds to approximate angles.
# Adjust these to match whatever your ADC linear travel actually covers.
SAFETY_ANGLE_LEFT  = 310.0   # degrees — left hard stop
SAFETY_ANGLE_RIGHT =  45.0   # degrees — right hard stop
 
# ── Window / layout ───────────────────────────────────────────────────────────
SCREEN_W, SCREEN_H = 800, 700
SCREEN_TITLE = "Haptic Paddle Visualizer"
 
CX = SCREEN_W // 2        # dial centre X
CY = SCREEN_H // 2 + 20  # dial centre Y
DIAL_R   = 220            # outer radius of the dial face
ARM_LEN  = 180            # paddle arm length
BALL_R   =  14            # ball radius at tip
 
# ── Colors ────────────────────────────────────────────────────────────────────
COL_BG          = (15,  17,  22)     # near-black background
COL_DIAL        = (30,  35,  45)     # dial face fill
COL_DIAL_RING   = (55,  65,  80)     # dial outer ring
COL_TICK        = (70,  85, 105)     # degree tick marks
COL_ARM         = (160, 175, 195)    # paddle arm
COL_BALL        = (100, 200, 255)    # position ball
COL_BALL_WALL   = (255, 180,  40)    # ball tint when hitting wall
COL_WALL_ARC    = (255, 160,   0)    # wall arc  (orange)
COL_WALL_LINE   = (255, 130,   0)    # wall radial line
COL_SAFETY      = (220,  40,  40)    # safety-stop red lines
COL_FREE_ARC    = ( 40, 200, 100)    # free-zone arc (green)
COL_FORCE       = (255,  80,  80)    # force arrow
COL_TEXT        = (200, 215, 230)
COL_LABEL       = (120, 140, 160)
COL_SAFE_FLASH  = (255,  50,  50)    # full-screen tint on safety trip
 
# ── Helpers ───────────────────────────────────────────────────────────────────
 
def deg_to_xy(cx, cy, radius, angle_deg):
    """Convert a dial angle (0° = top, clockwise) to screen (x, y)."""
    rad = math.radians(90 - angle_deg)   # rotate so 0° points up
    return cx + radius * math.cos(rad), cy + radius * math.sin(rad)
 
 
def get_desired_current(angle_deg):
    """Mirror of the C function."""
    if 0.5 < angle_deg < 180.0:
        penetration = angle_deg - WALL_RIGHT
        if penetration > 0:
            return -WALL_STIFFNESS * penetration
    elif angle_deg >= 180.0:
        penetration = WALL_LEFT - angle_deg
        if penetration > 0:
            return WALL_STIFFNESS * penetration
    return 0.0
 
 
def lerp_color(c1, c2, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3))
 
 
# ── Serial reader (background thread) ────────────────────────────────────────
 
class SerialReader(threading.Thread):
    def __init__(self, port, baud=115200):
        super().__init__(daemon=True)
        self.port  = port
        self.baud  = baud
        self.angle = 0.0
        self.penetration = 0.0
        self.safety_trip = False
        self._lock = threading.Lock()
        self.connected = False
 
    def run(self):
        try:
            import serial
            ser = serial.Serial(self.port, self.baud, timeout=0.1)
            self.connected = True
            while True:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if line.startswith("Angle:"):
                    try:
                        val = float(line.split(":")[1].replace("deg", "").strip())
                        with self._lock:
                            self.angle = val
                            self.safety_trip = False
                    except ValueError:
                        pass
                elif "detected" in line.lower():
                    try:
                        val = float(line.split(":")[1].replace("deg", "").strip())
                        with self._lock:
                            self.penetration = val
                    except ValueError:
                        pass
                elif "out of range" in line.lower():
                    with self._lock:
                        self.safety_trip = True
        except Exception as e:
            print(f"[serial] {e}", file=sys.stderr)
 
    def get_state(self):
        with self._lock:
            return self.angle, self.penetration, self.safety_trip
 
 
# ── Demo oscillator (no hardware) ────────────────────────────────────────────
 
class DemoSource:
    """Swings the paddle back and forth through both walls for a pretty demo."""
    def __init__(self):
        self._t0 = time.time()
 
    def get_state(self):
        t = time.time() - self._t0
        # slow oscillation that overshoots both walls slightly
        raw = 20.0 * math.sin(0.5 * t)
        angle = raw % 360
        desired = get_desired_current(angle)
        pen = 0.0
        if angle > WALL_RIGHT and angle < 180:
            pen = angle - WALL_RIGHT
        elif angle >= 180 and angle < WALL_LEFT:
            pen = WALL_LEFT - angle
        return angle, pen, False
 
 
# ── Main window ───────────────────────────────────────────────────────────────
 
class HapticVizWindow(arcade.Window):
 
    def __init__(self, source):
        super().__init__(SCREEN_W, SCREEN_H, SCREEN_TITLE, update_rate=1/60)
        arcade.set_background_color(COL_BG)
        self.source = source
 
        # state
        self.angle       = 0.0
        self.penetration = 0.0
        self.safety_trip = False
        self.desired_ma  = 0.0
        self._flash_alpha = 0  # for safety-stop flash
 
    # ── update ────────────────────────────────────────────────────────────────
    def on_update(self, dt):
        self.angle, self.penetration, self.safety_trip = self.source.get_state()
        self.desired_ma = get_desired_current(self.angle)
 
        if self.safety_trip:
            self._flash_alpha = min(180, self._flash_alpha + 40)
        else:
            self._flash_alpha = max(0, self._flash_alpha - 10)
 
    # ── draw ──────────────────────────────────────────────────────────────────
    def on_draw(self):
        self.clear()
 
        self._draw_dial_face()
        self._draw_free_zone_arc()
        self._draw_safety_lines()
        self._draw_wall_arcs()
        self._draw_arm_and_ball()
        self._draw_force_arrow()
        self._draw_hud()
        self._draw_legend()
 
        if self._flash_alpha > 0:
            self._draw_safety_flash()
 
    # ── dial face ─────────────────────────────────────────────────────────────
    def _draw_dial_face(self):
        # filled circle background
        arcade.draw_circle_filled(CX, CY, DIAL_R, COL_DIAL)
        arcade.draw_circle_outline(CX, CY, DIAL_R, COL_DIAL_RING, 2)
 
        # tick marks every 15°, labels every 45°
        for deg in range(0, 360, 5):
            is_major = (deg % 45 == 0)
            is_med   = (deg % 15 == 0)
            tick_len = 14 if is_major else (8 if is_med else 4)
            x0, y0 = deg_to_xy(CX, CY, DIAL_R - 2,          deg)
            x1, y1 = deg_to_xy(CX, CY, DIAL_R - 2 - tick_len, deg)
            color = (100, 115, 135) if is_major else COL_TICK
            arcade.draw_line(x0, y0, x1, y1, color, 1 + is_major)
 
            if is_major:
                lx, ly = deg_to_xy(CX, CY, DIAL_R - 28, deg)
                arcade.draw_text(f"{deg}°", lx, ly,
                                 COL_LABEL, 11,
                                 anchor_x="center", anchor_y="center")
 
        # centre dot
        arcade.draw_circle_filled(CX, CY, 5, COL_DIAL_RING)
 
    # ── free zone arc (green band) ────────────────────────────────────────────
    def _draw_free_zone_arc(self):
        # Free zone goes from WALL_RIGHT (15°) counter-clockwise to WALL_LEFT (345°)
        # In our coord system that's the short arc on the "top" of the dial.
        # We draw it as a thick arc just inside the tick ring.
        r = DIAL_R - 38
        arcade.draw_arc_outline(CX, CY, r, r,
                                COL_FREE_ARC,
                                start_angle = 90 - WALL_LEFT,   # screen angle
                                end_angle   = 90 - WALL_RIGHT,
                                border_width=6)
 
    # ── safety-stop lines (red) ───────────────────────────────────────────────
    def _draw_safety_lines(self):
        for angle, label in [(SAFETY_ANGLE_LEFT,  "STOP"), (SAFETY_ANGLE_RIGHT, "STOP")]:
            x0, y0 = deg_to_xy(CX, CY, 30,     angle)
            x1, y1 = deg_to_xy(CX, CY, DIAL_R, angle)
            arcade.draw_line(x0, y0, x1, y1, COL_SAFETY, 3)
            lx, ly = deg_to_xy(CX, CY, DIAL_R + 22, angle)
            arcade.draw_text(label, lx, ly, COL_SAFETY, 11,
                             bold=True, anchor_x="center", anchor_y="center")
 
    # ── wall arcs (orange) ────────────────────────────────────────────────────
    def _draw_wall_arcs(self):
        for wall_angle, label in [(WALL_LEFT, "WALL L"), (WALL_RIGHT, "WALL R")]:
            # radial line
            x0, y0 = deg_to_xy(CX, CY, 30,         wall_angle)
            x1, y1 = deg_to_xy(CX, CY, DIAL_R - 10, wall_angle)
            arcade.draw_line(x0, y0, x1, y1, COL_WALL_LINE, 2)
            # label
            lx, ly = deg_to_xy(CX, CY, DIAL_R - 55, wall_angle)
            arcade.draw_text(label, lx, ly, COL_WALL_ARC, 11,
                             bold=True, anchor_x="center", anchor_y="center")
 
        # arc band showing the "wall zone" (angles outside the free zone)
        r = DIAL_R - 38
        # right wall zone: 15° → 30° (safety stop)
        arcade.draw_arc_outline(CX, CY, r, r, COL_WALL_ARC,
                                start_angle=90 - SAFETY_ANGLE_RIGHT,
                                end_angle  =90 - WALL_RIGHT,
                                border_width=6)
        # left wall zone: 330° (safety) → 345° (wall)
        arcade.draw_arc_outline(CX, CY, r, r, COL_WALL_ARC,
                                start_angle=90 - WALL_LEFT,
                                end_angle  =90 - SAFETY_ANGLE_LEFT,
                                border_width=6)
 
    # ── paddle arm + ball ─────────────────────────────────────────────────────
    def _draw_arm_and_ball(self):
        bx, by = deg_to_xy(CX, CY, ARM_LEN, self.angle)
 
        # blend ball color when penetrating a wall
        pen_frac = min(1.0, abs(self.penetration) / 20.0)
        ball_col = lerp_color(COL_BALL, COL_BALL_WALL, pen_frac)
 
        arcade.draw_line(CX, CY, bx, by, COL_ARM, 4)
        arcade.draw_circle_filled(bx, by, BALL_R, ball_col)
        arcade.draw_circle_outline(bx, by, BALL_R, (255, 255, 255), 1)
 
    # ── force / restoring arrow ───────────────────────────────────────────────
    def _draw_force_arrow(self):
        if abs(self.desired_ma) < 1.0:
            return
 
        max_ma     = WALL_STIFFNESS * 20   # approx max force shown
        arrow_len  = ARM_LEN * 0.6 * min(1.0, abs(self.desired_ma) / max_ma)
 
        # force direction is opposite to wall penetration
        force_angle = self.angle + (180 if self.desired_ma < 0 else 0)
        bx, by = deg_to_xy(CX, CY, ARM_LEN, self.angle)
        fx, fy = deg_to_xy(bx, by, arrow_len, force_angle)    # tip of arrow
 
        arcade.draw_line(bx, by, fx, fy, COL_FORCE, 3)
        # arrowhead
        for side in [-25, 25]:
            hx, hy = deg_to_xy(fx, fy, 14, force_angle + 180 + side)
            arcade.draw_line(fx, fy, hx, hy, COL_FORCE, 3)
 
    # ── HUD readout ───────────────────────────────────────────────────────────
    def _draw_hud(self):
        x, y = 18, SCREEN_H - 24
        line_h = 24
 
        in_wall = abs(self.desired_ma) > 0.5
 
        arcade.draw_text(f"Angle     {self.angle:>8.2f} °",
                         x, y, COL_TEXT, 14, font_name="Courier New")
        y -= line_h
 
        arcade.draw_text(f"Force     {self.desired_ma:>+8.1f} mA",
                         x, y,
                         COL_FORCE if in_wall else COL_TEXT, 14,
                         font_name="Courier New")
        y -= line_h
 
        pen_col = COL_WALL_ARC if self.penetration > 0.1 else COL_TEXT
        arcade.draw_text(f"Pen.      {self.penetration:>8.2f} °",
                         x, y, pen_col, 14, font_name="Courier New")
        y -= line_h
 
        zone = "FREE ZONE"
        zone_col = (40, 200, 100)
        if self.penetration > 0.1:
            zone = "IN WALL"
            zone_col = COL_WALL_ARC
        arcade.draw_text(f"Zone      {zone}", x, y, zone_col, 14,
                         font_name="Courier New")
 
    # ── legend ────────────────────────────────────────────────────────────────
    def _draw_legend(self):
        items = [
            (COL_SAFETY,   "Safety stop  (ADC limit)"),
            (COL_WALL_LINE,"Haptic wall"),
            (COL_FREE_ARC, "Free zone"),
            (COL_BALL,     "Paddle position"),
            (COL_FORCE,    "Restoring force"),
        ]
        x, y = SCREEN_W - 260, 20 + len(items) * 22
        for color, label in items:
            arcade.draw_circle_filled(x, y, 6, color)
            arcade.draw_text(label, x + 14, y, COL_LABEL, 12, anchor_y="center")
            y += 22
 
    # ── safety flash overlay ──────────────────────────────────────────────────
    def _draw_safety_flash(self):
        r, g, b = COL_SAFE_FLASH
        #arcade.draw_rect_filled(SCREEN_W / 2, SCREEN_H / 2, SCREEN_W, SCREEN_H, (r, g, b, self._flash_alpha))
        arcade.draw_text("⚠  SAFETY STOP  ⚠",
                         SCREEN_W / 2, SCREEN_H / 2,
                         (255, 255, 255, min(255, self._flash_alpha * 2)),
                         28, bold=True, anchor_x="center", anchor_y="center")
 
 
# ── Entry point ───────────────────────────────────────────────────────────────
 
def auto_detect_port():
    """Try common Pico/FTDI serial port names."""
    import serial.tools.list_ports
    for p in serial.tools.list_ports.comports():
        desc = p.description.lower()
        if any(k in desc for k in ("pico", "cdc", "uart", "usb serial", "cp210", "ch340")):
            return p.device
    return None
 
 
def main():
    parser = argparse.ArgumentParser(description="Haptic Paddle Visualizer")
    parser.add_argument("--port",  default=None,  help="Serial port (e.g. /dev/ttyACM0 or COM3)")
    parser.add_argument("--baud",  default=115200, type=int)
    parser.add_argument("--demo",  action="store_true", help="Run animated demo (no hardware)")
    args = parser.parse_args()
 
    if args.demo:
        source = DemoSource()
        print("[demo mode] — no serial port used")
    else:
        port = args.port or auto_detect_port()
        if port is None:
            print("Could not auto-detect serial port. Run with --port /dev/ttyACMx or use --demo")
            sys.exit(1)
        print(f"[serial] connecting to {port} @ {args.baud} baud …")
        reader = SerialReader(port, args.baud)
        reader.start()
        source = reader
 
    window = HapticVizWindow(source)
    arcade.run()
 
 
if __name__ == "__main__":
    main()