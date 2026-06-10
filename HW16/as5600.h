#ifndef AS5600_H
#define AS5600_H

/**
 * AS5600 I2C Library for Raspberry Pi Pico
 *
 * The AS5600 is a 12-bit magnetic rotary position sensor.
 * I2C address is fixed at 0x36.
 *
 * Wiring:
 *   AS5600 SDA  -> Pico GP4 (or any SDA pin)
 *   AS5600 SCL  -> Pico GP5 (or any SCL pin)
 *   AS5600 VCC  -> 3.3V
 *   AS5600 GND  -> GND
 *   AS5600 DIR  -> GND (clockwise) or VCC (counter-clockwise)
 */

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Device address ────────────────────────────────────────────── */
#define AS5600_I2C_ADDR     0x36

/* ── Register map ──────────────────────────────────────────────── */

/* Configuration registers */
#define AS5600_REG_ZMCO     0x00   /* Number of times ZPOS/MPOS have been written (read-only) */
#define AS5600_REG_ZPOS_H   0x01   /* Zero position high byte */
#define AS5600_REG_ZPOS_L   0x02   /* Zero position low byte  */
#define AS5600_REG_MPOS_H   0x03   /* Maximum position high byte */
#define AS5600_REG_MPOS_L   0x04   /* Maximum position low byte  */
#define AS5600_REG_MANG_H   0x05   /* Maximum angle high byte */
#define AS5600_REG_MANG_L   0x06   /* Maximum angle low byte  */
#define AS5600_REG_CONF_H   0x07   /* Configuration high byte */
#define AS5600_REG_CONF_L   0x08   /* Configuration low byte  */

/* Output registers (read-only) */
#define AS5600_REG_RAW_ANGLE_H  0x0C   /* Raw angle high byte (12-bit, 0-4095) */
#define AS5600_REG_RAW_ANGLE_L  0x0D   /* Raw angle low byte  */
#define AS5600_REG_ANGLE_H      0x0E   /* Scaled/filtered angle high byte */
#define AS5600_REG_ANGLE_L      0x0F   /* Scaled/filtered angle low byte  */

/* Status registers (read-only) */
#define AS5600_REG_STATUS   0x0B   /* Magnet status */
#define AS5600_REG_AGC      0x1A   /* Automatic gain control */
#define AS5600_REG_MAGNITUDE_H  0x1B   /* CORDIC magnitude high byte */
#define AS5600_REG_MAGNITUDE_L  0x1C   /* CORDIC magnitude low byte  */

/* Burn command register */
#define AS5600_REG_BURN     0xFF

/* ── Status register bit masks ─────────────────────────────────── */
#define AS5600_STATUS_MH    (1 << 3)   /* Magnet too strong */
#define AS5600_STATUS_ML    (1 << 4)   /* Magnet too weak   */
#define AS5600_STATUS_MD    (1 << 5)   /* Magnet detected   */

/* ── CONF register bit fields ──────────────────────────────────── */

/* Power mode (CONF[1:0]) */
typedef enum {
    AS5600_PM_NOM   = 0x00,  /* Normal mode        */
    AS5600_PM_LPM1  = 0x01,  /* Low power mode 1   */
    AS5600_PM_LPM2  = 0x02,  /* Low power mode 2   */
    AS5600_PM_LPM3  = 0x03,  /* Low power mode 3   */
} as5600_power_mode_t;

/* Hysteresis (CONF[3:2]) */
typedef enum {
    AS5600_HYST_OFF  = 0x00,  /* Off       */
    AS5600_HYST_1LSB = 0x01,  /* 1 LSB     */
    AS5600_HYST_2LSB = 0x02,  /* 2 LSBs    */
    AS5600_HYST_3LSB = 0x03,  /* 3 LSBs    */
} as5600_hysteresis_t;

/* Output stage (CONF[5:4]) */
typedef enum {
    AS5600_OUTS_ANALOG_FULL  = 0x00,  /* Analog 0–100% VDD */
    AS5600_OUTS_ANALOG_RANGE = 0x01,  /* Analog 10–90% VDD */
    AS5600_OUTS_PWM          = 0x02,  /* PWM                */
} as5600_output_stage_t;

/* PWM frequency (CONF[7:6]) */
typedef enum {
    AS5600_PWMF_115HZ = 0x00,
    AS5600_PWMF_230HZ = 0x01,
    AS5600_PWMF_460HZ = 0x02,
    AS5600_PWMF_920HZ = 0x03,
} as5600_pwm_freq_t;

/* Slow filter (CONF[10:8]) */
typedef enum {
    AS5600_SF_16X = 0x00,
    AS5600_SF_8X  = 0x01,
    AS5600_SF_4X  = 0x02,
    AS5600_SF_2X  = 0x03,
} as5600_slow_filter_t;

/* Fast filter threshold (CONF[13:11]) */
typedef enum {
    AS5600_FTH_SLOW_ONLY = 0x00,
    AS5600_FTH_6LSB      = 0x01,
    AS5600_FTH_7LSB      = 0x02,
    AS5600_FTH_9LSB      = 0x03,
    AS5600_FTH_18LSB     = 0x04,
    AS5600_FTH_21LSB     = 0x05,
    AS5600_FTH_24LSB     = 0x06,
    AS5600_FTH_10LSB     = 0x07,
} as5600_fast_filter_t;

/* Watchdog (CONF[14]) */
typedef enum {
    AS5600_WD_OFF = 0x00,
    AS5600_WD_ON  = 0x01,
} as5600_watchdog_t;

/* ── Magnet status ─────────────────────────────────────────────── */
typedef enum {
    AS5600_MAGNET_NOT_DETECTED = 0,
    AS5600_MAGNET_OK,
    AS5600_MAGNET_TOO_WEAK,
    AS5600_MAGNET_TOO_STRONG,
} as5600_magnet_status_t;

/* ── Device handle ─────────────────────────────────────────────── */
typedef struct {
    i2c_inst_t *i2c;       /* Pico i2c0 or i2c1 instance */
    uint8_t     addr;      /* I2C address (default 0x36)  */
} as5600_t;

/* ── Initialization ────────────────────────────────────────────── */

/**
 * Initialize an AS5600 handle and verify the device is reachable.
 *
 * @param dev     Pointer to an as5600_t handle to initialize.
 * @param i2c     Pico I2C instance (i2c0 or i2c1). Must already be
 *                initialized with i2c_init() and gpio_set_function()
 *                before calling this.
 * @param addr    Device address (normally AS5600_I2C_ADDR = 0x36).
 * @return        true on success, false if device did not respond.
 */
bool as5600_init(as5600_t *dev, i2c_inst_t *i2c, uint8_t addr);

/* ── Angle reads ───────────────────────────────────────────────── */

/**
 * Read the raw 12-bit angle (0–4095, no zero-position or filtering applied).
 *
 * @param dev     Initialized device handle.
 * @param angle   Output: raw angle value 0–4095.
 * @return        true on success.
 */
bool as5600_read_raw_angle(const as5600_t *dev, uint16_t *angle);

/**
 * Read the processed 12-bit angle (0–4095) with zero-position offset,
 * max-angle scaling, and slow-filter applied.
 *
 * @param dev     Initialized device handle.
 * @param angle   Output: angle value 0–4095.
 * @return        true on success.
 */
bool as5600_read_angle(const as5600_t *dev, uint16_t *angle);

/**
 * Read the angle in degrees (0.0–359.9°), derived from the processed angle.
 *
 * @param dev     Initialized device handle.
 * @param degrees Output: angle in degrees.
 * @return        true on success.
 */
bool as5600_read_angle_degrees(const as5600_t *dev, float *degrees);

/* ── Status ────────────────────────────────────────────────────── */

/**
 * Read the raw status register byte.
 *
 * @param dev     Initialized device handle.
 * @param status  Output: raw STATUS register value.
 * @return        true on success.
 */
bool as5600_read_status(const as5600_t *dev, uint8_t *status);

/**
 * Check the magnet detection/strength status.
 *
 * @param dev     Initialized device handle.
 * @return        One of the as5600_magnet_status_t values.
 */
as5600_magnet_status_t as5600_magnet_status(const as5600_t *dev);

/**
 * Read the automatic gain control register (0–255).
 * Values near 0 indicate a strong magnet; values near 255 a weak magnet.
 *
 * @param dev   Initialized device handle.
 * @param agc   Output: AGC byte.
 * @return      true on success.
 */
bool as5600_read_agc(const as5600_t *dev, uint8_t *agc);

/**
 * Read the CORDIC magnitude register (0–4095).
 *
 * @param dev       Initialized device handle.
 * @param magnitude Output: magnitude value.
 * @return          true on success.
 */
bool as5600_read_magnitude(const as5600_t *dev, uint16_t *magnitude);

/* ── Configuration ─────────────────────────────────────────────── */

/**
 * Read the full 16-bit CONF register.
 *
 * @param dev   Initialized device handle.
 * @param conf  Output: 16-bit CONF value.
 * @return      true on success.
 */
bool as5600_read_conf(const as5600_t *dev, uint16_t *conf);

/**
 * Write the full 16-bit CONF register.
 * Use the as5600_set_* helpers below for individual fields.
 *
 * @param dev   Initialized device handle.
 * @param conf  16-bit CONF value to write.
 * @return      true on success.
 */
bool as5600_write_conf(const as5600_t *dev, uint16_t conf);

bool as5600_set_power_mode   (const as5600_t *dev, as5600_power_mode_t   pm);
bool as5600_set_hysteresis   (const as5600_t *dev, as5600_hysteresis_t   hyst);
bool as5600_set_output_stage (const as5600_t *dev, as5600_output_stage_t outs);
bool as5600_set_pwm_freq     (const as5600_t *dev, as5600_pwm_freq_t     pwmf);
bool as5600_set_slow_filter  (const as5600_t *dev, as5600_slow_filter_t  sf);
bool as5600_set_fast_filter  (const as5600_t *dev, as5600_fast_filter_t  fth);
bool as5600_set_watchdog     (const as5600_t *dev, as5600_watchdog_t     wd);

/* ── Zero / max position programming ──────────────────────────── */

/**
 * Set the zero-position register (ZPOS).
 * The angle output will read 0 when the magnet is at this raw position.
 *
 * @param dev   Initialized device handle.
 * @param zpos  12-bit zero position (0–4095).
 * @return      true on success.
 */
bool as5600_set_zero_position(const as5600_t *dev, uint16_t zpos);

/**
 * Read the current ZPOS register value.
 */
bool as5600_get_zero_position(const as5600_t *dev, uint16_t *zpos);

/**
 * Set the maximum position register (MPOS).
 *
 * @param dev   Initialized device handle.
 * @param mpos  12-bit maximum position (0–4095).
 * @return      true on success.
 */
bool as5600_set_max_position(const as5600_t *dev, uint16_t mpos);

/**
 * Set the maximum angle register (MANG, 0–4095 maps to 0–360°).
 *
 * @param dev   Initialized device handle.
 * @param mang  12-bit maximum angle (0–4095).
 * @return      true on success.
 */
bool as5600_set_max_angle(const as5600_t *dev, uint16_t mang);

/**
 * Convenience: set the zero position to the current magnet angle.
 * Reads the raw angle and writes it to ZPOS.
 *
 * @param dev   Initialized device handle.
 * @return      true on success.
 */
bool as5600_zero_here(const as5600_t *dev);

/* ── OTP burn (one-time, use with caution) ─────────────────────── */

/**
 * Burn ZPOS and MPOS to OTP memory (can only be done 3 times total;
 * check ZMCO first). Sends 0x80 to the BURN register.
 *
 * @param dev   Initialized device handle.
 * @return      true on success.
 */
bool as5600_burn_angle(const as5600_t *dev);

/**
 * Burn MANG and CONF to OTP memory (can only be done once).
 * Sends 0x40 to the BURN register.
 *
 * @param dev   Initialized device handle.
 * @return      true on success.
 */
bool as5600_burn_setting(const as5600_t *dev);

/**
 * Read the ZMCO register (how many times angle has been burned, 0–3).
 *
 * @param dev   Initialized device handle.
 * @param zmco  Output: burn count.
 * @return      true on success.
 */
bool as5600_read_zmco(const as5600_t *dev, uint8_t *zmco);

#ifdef __cplusplus
}
#endif

#endif /* AS5600_H */
