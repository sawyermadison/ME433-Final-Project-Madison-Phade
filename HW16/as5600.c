#include "as5600.h"
#include <math.h>

/* ── Internal helpers ──────────────────────────────────────────── */

static bool i2c_write_reg(const as5600_t *dev, uint8_t reg,
                          const uint8_t *data, size_t len)
{
    uint8_t buf[len + 1];
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) buf[i + 1] = data[i];
    int ret = i2c_write_blocking(dev->i2c, dev->addr, buf, len + 1, false);
    return ret == (int)(len + 1);
}

static bool i2c_read_reg(const as5600_t *dev, uint8_t reg,
                         uint8_t *data, size_t len)
{
    /* Write register address, then read data */
    int ret = i2c_write_blocking(dev->i2c, dev->addr, &reg, 1, true);
    if (ret != 1) return false;
    ret = i2c_read_blocking(dev->i2c, dev->addr, data, len, false);
    return ret == (int)len;
}

static bool read_u8(const as5600_t *dev, uint8_t reg, uint8_t *out)
{
    return i2c_read_reg(dev, reg, out, 1);
}

static bool read_u16(const as5600_t *dev, uint8_t reg_h, uint16_t *out)
{
    uint8_t buf[2];
    if (!i2c_read_reg(dev, reg_h, buf, 2)) return false;
    *out = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

static bool write_u16(const as5600_t *dev, uint8_t reg_h, uint16_t val)
{
    uint8_t buf[2] = { (val >> 8) & 0x0F, val & 0xFF };
    return i2c_write_reg(dev, reg_h, buf, 2);
}

/* ── Public API ────────────────────────────────────────────────── */

bool as5600_init(as5600_t *dev, i2c_inst_t *i2c, uint8_t addr)
{
    dev->i2c  = i2c;
    dev->addr = addr;

    /* Probe: attempt to read STATUS register */
    uint8_t dummy;
    return read_u8(dev, AS5600_REG_STATUS, &dummy);
}

/* ── Angle reads ───────────────────────────────────────────────── */

bool as5600_read_raw_angle(const as5600_t *dev, uint16_t *angle)
{
    uint16_t raw;
    if (!read_u16(dev, AS5600_REG_RAW_ANGLE_H, &raw)) return false;
    *angle = raw & 0x0FFF;
    return true;
}

bool as5600_read_angle(const as5600_t *dev, uint16_t *angle)
{
    uint16_t raw;
    if (!read_u16(dev, AS5600_REG_ANGLE_H, &raw)) return false;
    *angle = raw & 0x0FFF;
    return true;
}

bool as5600_read_angle_degrees(const as5600_t *dev, float *degrees)
{
    uint16_t angle;
    if (!as5600_read_angle(dev, &angle)) return false;
    *degrees = (float)angle * 360.0f / 4096.0f;
    return true;
}

/* ── Status ────────────────────────────────────────────────────── */

bool as5600_read_status(const as5600_t *dev, uint8_t *status)
{
    return read_u8(dev, AS5600_REG_STATUS, status);
}

as5600_magnet_status_t as5600_magnet_status(const as5600_t *dev)
{
    uint8_t status;
    if (!read_u8(dev, AS5600_REG_STATUS, &status)) {
        return AS5600_MAGNET_NOT_DETECTED;
    }
    if (!(status & AS5600_STATUS_MD))  return AS5600_MAGNET_NOT_DETECTED;
    if (status & AS5600_STATUS_MH)     return AS5600_MAGNET_TOO_STRONG;
    if (status & AS5600_STATUS_ML)     return AS5600_MAGNET_TOO_WEAK;
    return AS5600_MAGNET_OK;
}

bool as5600_read_agc(const as5600_t *dev, uint8_t *agc)
{
    return read_u8(dev, AS5600_REG_AGC, agc);
}

bool as5600_read_magnitude(const as5600_t *dev, uint16_t *magnitude)
{
    uint16_t raw;
    if (!read_u16(dev, AS5600_REG_MAGNITUDE_H, &raw)) return false;
    *magnitude = raw & 0x0FFF;
    return true;
}

/* ── Configuration ─────────────────────────────────────────────── */

bool as5600_read_conf(const as5600_t *dev, uint16_t *conf)
{
    return read_u16(dev, AS5600_REG_CONF_H, conf);
}

bool as5600_write_conf(const as5600_t *dev, uint16_t conf)
{
    return write_u16(dev, AS5600_REG_CONF_H, conf);
}

/* Generic helper: read-modify-write a bit field in CONF */
static bool conf_rmw(const as5600_t *dev, uint16_t mask,
                     uint16_t shift, uint16_t value)
{
    uint16_t conf;
    if (!as5600_read_conf(dev, &conf)) return false;
    conf = (conf & ~mask) | ((value << shift) & mask);
    return as5600_write_conf(dev, conf);
}

bool as5600_set_power_mode(const as5600_t *dev, as5600_power_mode_t pm)
{
    return conf_rmw(dev, 0x0003, 0, (uint16_t)pm);
}

bool as5600_set_hysteresis(const as5600_t *dev, as5600_hysteresis_t hyst)
{
    return conf_rmw(dev, 0x000C, 2, (uint16_t)hyst);
}

bool as5600_set_output_stage(const as5600_t *dev, as5600_output_stage_t outs)
{
    return conf_rmw(dev, 0x0030, 4, (uint16_t)outs);
}

bool as5600_set_pwm_freq(const as5600_t *dev, as5600_pwm_freq_t pwmf)
{
    return conf_rmw(dev, 0x00C0, 6, (uint16_t)pwmf);
}

bool as5600_set_slow_filter(const as5600_t *dev, as5600_slow_filter_t sf)
{
    return conf_rmw(dev, 0x0300, 8, (uint16_t)sf);
}

bool as5600_set_fast_filter(const as5600_t *dev, as5600_fast_filter_t fth)
{
    return conf_rmw(dev, 0x1C00, 10, (uint16_t)fth);
}

bool as5600_set_watchdog(const as5600_t *dev, as5600_watchdog_t wd)
{
    return conf_rmw(dev, 0x4000, 14, (uint16_t)wd);
}

/* ── Zero / max position ───────────────────────────────────────── */

bool as5600_set_zero_position(const as5600_t *dev, uint16_t zpos)
{
    return write_u16(dev, AS5600_REG_ZPOS_H, zpos & 0x0FFF);
}

bool as5600_get_zero_position(const as5600_t *dev, uint16_t *zpos)
{
    uint16_t raw;
    if (!read_u16(dev, AS5600_REG_ZPOS_H, &raw)) return false;
    *zpos = raw & 0x0FFF;
    return true;
}

bool as5600_set_max_position(const as5600_t *dev, uint16_t mpos)
{
    return write_u16(dev, AS5600_REG_MPOS_H, mpos & 0x0FFF);
}

bool as5600_set_max_angle(const as5600_t *dev, uint16_t mang)
{
    return write_u16(dev, AS5600_REG_MANG_H, mang & 0x0FFF);
}

bool as5600_zero_here(const as5600_t *dev)
{
    uint16_t raw;
    if (!as5600_read_raw_angle(dev, &raw)) return false;
    return as5600_set_zero_position(dev, raw);
}

/* ── OTP burn ──────────────────────────────────────────────────── */

bool as5600_burn_angle(const as5600_t *dev)
{
    uint8_t cmd = 0x80;
    return i2c_write_reg(dev, AS5600_REG_BURN, &cmd, 1);
}

bool as5600_burn_setting(const as5600_t *dev)
{
    uint8_t cmd = 0x40;
    return i2c_write_reg(dev, AS5600_REG_BURN, &cmd, 1);
}

bool as5600_read_zmco(const as5600_t *dev, uint8_t *zmco)
{
    return read_u8(dev, AS5600_REG_ZMCO, zmco);
}
