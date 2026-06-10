# I installed pygame and arcade and ran python 3.12.0 in my .venv
import arcade
import math
import random

SCREEN_WIDTH = 800
SCREEN_HEIGHT = 600

TERRAIN_LENGTH = 3000
POINT_SPACING = 20
BIKER_FORCE = 400
SLOPE_FACTOR = 250
FRICTION = 0.97
SUSPENSION_STIFFNESS = 0.15
WHEEL_BASE = 30


class Biker:
    def __init__(self):
        self.x = 100
        self.y = 0
        self.vx = 0
        self.angle = 0


class HillClimbGame(arcade.Window):
    def __init__(self):
        super().__init__(SCREEN_WIDTH, SCREEN_HEIGHT, "Hill Climb – Arcade 3.x Safe")
        arcade.set_background_color(arcade.color.SKY_BLUE)

        self.terrain = []
        self.generate_terrain()

        self.biker = Biker()
        self.camera_x = 0
        self.pressed = set()

    # ---------------- TERRAIN ----------------
    def generate_terrain(self):
        x = 0
        y = 200
        self.terrain.append((x, y))

        while x < TERRAIN_LENGTH:
            x += POINT_SPACING
            y += random.randint(-50, 50)
            y = max(100, min(400, y))
            self.terrain.append((x, y))

    def get_height(self, x):
        if x <= 0:
            return self.terrain[0][1]
        if x >= self.terrain[-1][0]:
            return self.terrain[-1][1]

        i = int(x // POINT_SPACING)
        x1, y1 = self.terrain[i]
        x2, y2 = self.terrain[i + 1]

        t = (x - x1) / (x2 - x1)
        return (1 - t) * y1 + t * y2

    def get_slope(self, x):
        eps = 1
        y1 = self.get_height(x - eps)
        y2 = self.get_height(x + eps)
        return (y2 - y1) / (2 * eps)

    # ---------------- UPDATE ----------------
    def on_update(self, dt):
        force = 0
        if arcade.key.RIGHT in self.pressed:
            force += 1
        if arcade.key.LEFT in self.pressed:
            force -= 1

        slope = self.get_slope(self.biker.x)
        slope_resistance = -slope * SLOPE_FACTOR

        accel = force * BIKER_FORCE + slope_resistance
        self.biker.vx += accel * dt
        self.biker.vx *= FRICTION # tune friction
        self.biker.x += self.biker.vx * dt


        self.biker.x = max(0, min(TERRAIN_LENGTH, self.biker.x))
        self.biker.y = self.get_height(self.biker.x)

        front_y = self.get_height(self.biker.x + WHEEL_BASE)
        back_y = self.get_height(self.biker.x - WHEEL_BASE)
        target_angle = math.atan2(front_y - back_y, WHEEL_BASE * 2)
        self.biker.angle += (target_angle - self.biker.angle) * SUSPENSION_STIFFNESS

        target_cam = self.biker.x - SCREEN_WIDTH / 2
        self.camera_x += (target_cam - self.camera_x) * 0.1

    # ---------------- DRAW ----------------
    def on_draw(self):
        # Arcade 3.x uses clear(), not start_render()
        self.clear()

        # Terrain
        for i in range(len(self.terrain) - 1):
            x1, y1 = self.terrain[i]
            x2, y2 = self.terrain[i + 1]
            arcade.draw_line(
                x1 - self.camera_x, y1,
                x2 - self.camera_x, y2,
                arcade.color.DARK_GREEN, 3
            )

        # Wheels
        front_x = self.biker.x + WHEEL_BASE
        back_x = self.biker.x - WHEEL_BASE
        front_y = self.get_height(front_x)
        back_y = self.get_height(back_x)

        arcade.draw_circle_filled(front_x - self.camera_x, front_y + 10, 12, arcade.color.BLACK)
        arcade.draw_circle_filled(back_x - self.camera_x, back_y + 10, 12, arcade.color.BLACK)

        # Body bar
        arcade.draw_line(
            back_x - self.camera_x, back_y + 10,
            front_x - self.camera_x, front_y + 10,
            arcade.color.GRAY, 4
        )

        # Rider
        arcade.draw_circle_filled(
            self.biker.x - self.camera_x,
            self.biker.y + 30,
            10,
            arcade.color.RED
        )

    # ---------------- INPUT ----------------
    def on_key_press(self, key, modifiers):
        self.pressed.add(key)

    def on_key_release(self, key, modifiers):
        if key in self.pressed:
            self.pressed.remove(key)


def main():
    game = HillClimbGame()
    arcade.run()


if __name__ == "__main__":
    main()
