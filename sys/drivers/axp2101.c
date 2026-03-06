/*
 * Lua RTOS, AXP2101 Power Management Unit driver
 *
 * AXP2101 connected on I2C0: SDA=GPIO10, SCL=GPIO11
 *
 * Based on MicroPython driver:
 *   Copyright (c) 2022 lewis he (lewishe@outlook.com)
 *   https://github.com/lewisxhe/XPowersLib
 *
 * MIT License
 */

#include "luartos.h"

#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <sys/driver.h>
#include <drivers/i2c.h>
#include <drivers/axp2101.h>

/* ------------------------------------------------------------------ */
/* Hardware configuration                                              */
/* ------------------------------------------------------------------ */

#define AXP2101_I2C_ADDR    0x34
#define AXP2101_I2C_UNIT    CONFIG_LUA_RTOS_AXP2101_I2C_UNIT
#define AXP2101_SDA_GPIO    CONFIG_LUA_RTOS_AXP2101_SDA_GPIO
#define AXP2101_SCL_GPIO    CONFIG_LUA_RTOS_AXP2101_SCL_GPIO
#define AXP2101_I2C_SPEED   CONFIG_LUA_RTOS_AXP2101_I2C_SPEED
#define AXP2101_CHIP_ID     0x4A

/* ------------------------------------------------------------------ */
/* Register map                                                        */
/* ------------------------------------------------------------------ */

#define REG_STATUS1             0x00
#define REG_STATUS2             0x01
#define REG_IC_TYPE             0x03
#define REG_COMMON_CONFIG       0x10
#define REG_BATFET_CTRL         0x12
#define REG_DIE_TEMP_CTRL       0x13
#define REG_MIN_SYS_VOL         0x14
#define REG_INPUT_VOL_LIMIT     0x15
#define REG_INPUT_CUR_LIMIT     0x16
#define REG_RESET_FUEL_GAUGE    0x17
#define REG_CHG_GAUGE_WDT_CTRL  0x18
#define REG_WDT_CTRL            0x19
#define REG_LOW_BAT_WARN        0x1A
#define REG_PWRON_STATUS        0x20
#define REG_PWROFF_STATUS       0x21
#define REG_PWROFF_EN           0x22
#define REG_DC_OVP_UVP_CTRL     0x23
#define REG_VOFF_SET            0x24
#define REG_PWROK_SEQU_CTRL     0x25
#define REG_SLEEP_WAKEUP_CTRL   0x26
#define REG_IRQ_LEVEL_CTRL      0x27
#define REG_ADC_CH_CTRL         0x30
/* ADC result registers — each pair is (H, L) at consecutive addresses */
#define REG_ADC_BAT_H           0x34   /* battery voltage  H5L8 */
#define REG_ADC_BAT_L           0x35
#define REG_ADC_TS_H            0x36   /* TS pin voltage   H6L8 */
#define REG_ADC_TS_L            0x37
#define REG_ADC_VBUS_H          0x38   /* VBUS voltage     H6L8 */
#define REG_ADC_VBUS_L          0x39
#define REG_ADC_VSYS_H          0x3A   /* VSYS voltage     H6L8 */
#define REG_ADC_VSYS_L          0x3B
#define REG_ADC_TDIE_H          0x3C   /* die temperature  H6L8 */
#define REG_ADC_TDIE_L          0x3D
#define REG_INTEN1              0x40
#define REG_INTEN2              0x41
#define REG_INTEN3              0x42
#define REG_INTSTS1             0x48
#define REG_INTSTS2             0x49
#define REG_INTSTS3             0x4A
#define REG_IPRECHG_SET         0x61
#define REG_ICC_CHG_SET         0x62
#define REG_ITERM_CHG_SET       0x63
#define REG_CV_CHG_VOL_SET      0x64
#define REG_THERMAL_THRES       0x65
#define REG_BAT_DET_CTRL        0x68
#define REG_CHGLED_CTRL         0x69
#define REG_BTN_BAT_CHG_VOL     0x6A
#define REG_DC_ONOFF_DVM_CTRL   0x80
#define REG_DC_FORCE_PWM_CTRL   0x81
#define REG_DC_VOL0_CTRL        0x82   /* DC1 */
#define REG_DC_VOL1_CTRL        0x83   /* DC2 */
#define REG_DC_VOL2_CTRL        0x84   /* DC3 */
#define REG_DC_VOL3_CTRL        0x85   /* DC4 */
#define REG_DC_VOL4_CTRL        0x86   /* DC5 */
#define REG_LDO_ONOFF_CTRL0     0x90
#define REG_LDO_ONOFF_CTRL1     0x91
#define REG_LDO_VOL0_CTRL       0x92   /* ALDO1  */
#define REG_LDO_VOL1_CTRL       0x93   /* ALDO2  */
#define REG_LDO_VOL2_CTRL       0x94   /* ALDO3  */
#define REG_LDO_VOL3_CTRL       0x95   /* ALDO4  */
#define REG_LDO_VOL4_CTRL       0x96   /* BLDO1  */
#define REG_LDO_VOL5_CTRL       0x97   /* BLDO2  */
#define REG_LDO_VOL6_CTRL       0x98   /* CPUSLDO */
#define REG_LDO_VOL7_CTRL       0x99   /* DLDO1  */
#define REG_LDO_VOL8_CTRL       0x9A   /* DLDO2  */
#define REG_FUEL_GAUGE_CTRL     0xA2
#define REG_BAT_PERCENT         0xA4

/* ------------------------------------------------------------------ */
/* Voltage range constants (millivolts)                                */
/* ------------------------------------------------------------------ */

/* DC1: fixed 100 mV steps, 1500–3400 mV */
#define DCDC1_VOL_MIN    1500
#define DCDC1_VOL_MAX    3400
#define DCDC1_VOL_STEP   100

/* DC2 / DC4: two ranges */
#define DCDC24_VOL1_MIN   500
#define DCDC24_VOL1_MAX  1200
#define DCDC24_VOL2_MIN  1220
#define DCDC24_VOL2_MAX  1840    /* DC4 stops at 1840; DC2 at 1540 — enforced per-function */
#define DCDC24_VOL_STEP1   10
#define DCDC24_VOL_STEP2   20
#define DCDC24_STEP2_BASE  71

/* DC3: three ranges */
#define DCDC3_VOL_MIN     500
#define DCDC3_VOL1_MAX   1200
#define DCDC3_VOL2_MIN   1220
#define DCDC3_VOL2_MAX   1540
#define DCDC3_VOL3_MIN   1600
#define DCDC3_VOL3_MAX   3400
#define DCDC3_VOL_STEP1    10
#define DCDC3_VOL_STEP2    20
#define DCDC3_VOL_STEP3   100
#define DCDC3_STEP2_BASE   71
#define DCDC3_STEP3_BASE   88

/* DC5: 1400–3700 mV / 100 mV steps, plus 1200 mV as special value */
#define DCDC5_VOL_1200MV 1200
#define DCDC5_VOL_VAL    0x19
#define DCDC5_VOL_MIN    1400
#define DCDC5_VOL_MAX    3700
#define DCDC5_VOL_STEP   100

/* ALDO1–4, BLDO1–2, DLDO1–2: 500–3500 mV (DLDO 3400 max), 100 mV */
#define ALDO_VOL_MIN      500
#define ALDO_VOL_MAX     3500
#define ALDO_VOL_STEP    100

#define BLDO_VOL_MIN      500
#define BLDO_VOL_MAX     3500
#define BLDO_VOL_STEP    100

#define CPUSLDO_VOL_MIN   500
#define CPUSLDO_VOL_MAX  1400
#define CPUSLDO_VOL_STEP   50

#define DLDO_VOL_MIN      500
#define DLDO_VOL_MAX     3400
#define DLDO_VOL_STEP    100

/* VSYS power-down threshold */
#define VSYS_VOL_MIN     2600
#define VSYS_VOL_MAX     3300
#define VSYS_VOL_STEP    100

/* ------------------------------------------------------------------ */
/* Driver registration                                                 */
/* ------------------------------------------------------------------ */

DRIVER_REGISTER_BEGIN(AXP2101, axp2101, 0, NULL, NULL);
    DRIVER_REGISTER_ERROR(AXP2101, axp2101, CantInit,   "can't initialize", AXP2101_ERR_CANT_INIT);
    DRIVER_REGISTER_ERROR(AXP2101, axp2101, NotSetup,   "not setup",        AXP2101_ERR_NOT_SETUP);
    DRIVER_REGISTER_ERROR(AXP2101, axp2101, InvalidArg, "invalid argument", AXP2101_ERR_INVALID_ARG);
DRIVER_REGISTER_END(AXP2101, axp2101, 0, NULL, NULL);

/* ------------------------------------------------------------------ */
/* Driver state                                                        */
/* ------------------------------------------------------------------ */

static int i2c_device = -1;

/* ------------------------------------------------------------------ */
/* Low-level I2C helpers                                               */
/* ------------------------------------------------------------------ */

static driver_error_t *reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_write(i2c_device, buf, 2);
}

static driver_error_t *reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_write_read(i2c_device, &reg, 1, val, 1);
}

/* Read len bytes starting at reg. */
static driver_error_t *reg_read_n(uint8_t reg, uint8_t *buf, int len)
{
    return i2c_write_read(i2c_device, &reg, 1, buf, len);
}

/* Read two consecutive registers and combine as (H6 << 8 | L8). */
static driver_error_t *reg_read_h6l8(uint8_t reg_h, int *out)
{
    driver_error_t *error;
    uint8_t buf[2];
    if ((error = reg_read_n(reg_h, buf, 2))) return error;
    *out = (int)((buf[0] & 0x3F) << 8) | buf[1];
    return NULL;
}

/* Read two consecutive registers and combine as (H5 << 8 | L8). */
static driver_error_t *reg_read_h5l8(uint8_t reg_h, int *out)
{
    driver_error_t *error;
    uint8_t buf[2];
    if ((error = reg_read_n(reg_h, buf, 2))) return error;
    *out = (int)((buf[0] & 0x1F) << 8) | buf[1];
    return NULL;
}

/* Set a single bit in a register (read-modify-write). */
static driver_error_t *set_bit(uint8_t reg, int bit)
{
    driver_error_t *error;
    uint8_t val;
    if ((error = reg_read(reg, &val))) return error;
    return reg_write(reg, val | (uint8_t)(1u << bit));
}

/* Clear a single bit in a register (read-modify-write). */
static driver_error_t *clr_bit(uint8_t reg, int bit)
{
    driver_error_t *error;
    uint8_t val;
    if ((error = reg_read(reg, &val))) return error;
    return reg_write(reg, val & (uint8_t)~(1u << bit));
}

/* Read a single bit from a register. */
static driver_error_t *get_bit(uint8_t reg, int bit, bool *out)
{
    driver_error_t *error;
    uint8_t val;
    if ((error = reg_read(reg, &val))) return error;
    *out = (val >> bit) & 1;
    return NULL;
}

/* Convenience: return NOT_SETUP if not yet initialised. */
#define REQUIRE_SETUP() \
    do { \
        if (i2c_device < 0) \
            return driver_error(AXP2101_DRIVER, AXP2101_ERR_NOT_SETUP, NULL); \
    } while (0)

/* ------------------------------------------------------------------ */
/* Setup                                                               */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_setup(uint8_t adc_channels)
{
    driver_error_t *error;

    if (i2c_device >= 0) return NULL;  /* already initialised */

    if ((error = i2c_pin_map(AXP2101_I2C_UNIT, AXP2101_SDA_GPIO, AXP2101_SCL_GPIO)))
        return error;
    if ((error = i2c_attach(AXP2101_I2C_UNIT, I2C_MASTER, AXP2101_I2C_SPEED,
                             0, AXP2101_I2C_ADDR, &i2c_device)))
        return error;

    /* Verify chip ID */
    uint8_t id;
    if ((error = reg_read(REG_IC_TYPE, &id))) return error;
    if (id != AXP2101_CHIP_ID)
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_CANT_INIT,
                            "unexpected chip ID");

    /* Enable requested ADC channels */
    if (adc_channels) {
        if ((error = reg_write(REG_ADC_CH_CTRL, adc_channels))) return error;
    }

    return NULL;
}

driver_error_t *axp2101_get_chip_id(uint8_t *id)
{
    REQUIRE_SETUP();
    return reg_read(REG_IC_TYPE, id);
}

/* ------------------------------------------------------------------ */
/* Power control                                                       */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_shutdown(void)
{
    REQUIRE_SETUP();
    return set_bit(REG_COMMON_CONFIG, 0);
}

driver_error_t *axp2101_reset(void)
{
    REQUIRE_SETUP();
    return set_bit(REG_COMMON_CONFIG, 1);
}

/* ------------------------------------------------------------------ */
/* Status                                                              */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_is_vbus_good(bool *out)
{
    REQUIRE_SETUP();
    return get_bit(REG_STATUS1, 5, out);
}

driver_error_t *axp2101_is_battery_connected(bool *out)
{
    REQUIRE_SETUP();
    return get_bit(REG_STATUS1, 3, out);
}

driver_error_t *axp2101_is_charging(bool *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_STATUS2, &val))) return error;
    *out = ((val >> 5) == 0x01);
    return NULL;
}

driver_error_t *axp2101_is_discharging(bool *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_STATUS2, &val))) return error;
    *out = ((val >> 5) == 0x02);
    return NULL;
}

driver_error_t *axp2101_get_charger_status(uint8_t *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_STATUS2, &val))) return error;
    *out = val & 0x07;
    return NULL;
}

driver_error_t *axp2101_get_poweron_source(uint8_t *out)
{
    REQUIRE_SETUP();
    return reg_read(REG_PWRON_STATUS, out);
}

driver_error_t *axp2101_get_poweroff_source(uint8_t *out)
{
    REQUIRE_SETUP();
    return reg_read(REG_PWROFF_STATUS, out);
}

/* ------------------------------------------------------------------ */
/* ADC                                                                 */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_adc_enable(uint8_t channels)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_ADC_CH_CTRL, &val))) return error;
    return reg_write(REG_ADC_CH_CTRL, val | channels);
}

driver_error_t *axp2101_adc_disable(uint8_t channels)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_ADC_CH_CTRL, &val))) return error;
    return reg_write(REG_ADC_CH_CTRL, val & ~channels);
}

driver_error_t *axp2101_get_battery_voltage(int *mv)
{
    driver_error_t *error;
    bool connected;
    REQUIRE_SETUP();
    if ((error = axp2101_is_battery_connected(&connected))) return error;
    if (!connected) { *mv = 0; return NULL; }
    return reg_read_h5l8(REG_ADC_BAT_H, mv);
}

driver_error_t *axp2101_get_battery_percent(int *pct)
{
    driver_error_t *error;
    bool connected;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = axp2101_is_battery_connected(&connected))) return error;
    if (!connected) { *pct = -1; return NULL; }
    if ((error = reg_read(REG_BAT_PERCENT, &val))) return error;
    *pct = (int)val;
    return NULL;
}

driver_error_t *axp2101_get_vbus_voltage(int *mv)
{
    REQUIRE_SETUP();
    return reg_read_h6l8(REG_ADC_VBUS_H, mv);
}

driver_error_t *axp2101_get_system_voltage(int *mv)
{
    REQUIRE_SETUP();
    return reg_read_h6l8(REG_ADC_VSYS_H, mv);
}

driver_error_t *axp2101_get_temperature(int *deciC)
{
    driver_error_t *error;
    int raw;
    REQUIRE_SETUP();
    if ((error = reg_read_h6l8(REG_ADC_TDIE_H, &raw))) return error;
    /* Formula from Python driver: temp = 22.0 + (7274 - raw) / 20.0
     * Return in units of 0.1°C: deciC = 220 + (7274 - raw) * 10 / 20 */
    *deciC = 220 + (7274 - raw) / 2;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Fuel gauge / battery detection                                      */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_fuel_gauge_control(bool write_rom, bool enable)
{
    driver_error_t *error;
    REQUIRE_SETUP();
    if (write_rom) {
        if ((error = clr_bit(REG_FUEL_GAUGE_CTRL, 4))) return error;
    } else {
        if ((error = set_bit(REG_FUEL_GAUGE_CTRL, 4))) return error;
    }
    if (enable)
        return set_bit(REG_FUEL_GAUGE_CTRL, 0);
    else
        return clr_bit(REG_FUEL_GAUGE_CTRL, 0);
}

driver_error_t *axp2101_battery_detect_enable(bool enable)
{
    REQUIRE_SETUP();
    if (enable) return set_bit(REG_BAT_DET_CTRL, 0);
    return clr_bit(REG_BAT_DET_CTRL, 0);
}

driver_error_t *axp2101_btn_bat_chg_enable(bool enable)
{
    REQUIRE_SETUP();
    if (enable) return set_bit(REG_CHG_GAUGE_WDT_CTRL, 2);
    return clr_bit(REG_CHG_GAUGE_WDT_CTRL, 2);
}

/* ------------------------------------------------------------------ */
/* Charging configuration                                              */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_set_charge_current(uint8_t opt)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if (opt < 4 || opt > 16)
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "charge current must be 4..16");
    if ((error = reg_read(REG_ICC_CHG_SET, &val))) return error;
    return reg_write(REG_ICC_CHG_SET, (val & 0xE0) | opt);
}

driver_error_t *axp2101_get_charge_current(uint8_t *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_ICC_CHG_SET, &val))) return error;
    *out = val & 0x1F;
    return NULL;
}

driver_error_t *axp2101_set_charge_term_current(uint8_t opt)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if (opt > 8)
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "termination current must be 0..8");
    if ((error = reg_read(REG_ITERM_CHG_SET, &val))) return error;
    return reg_write(REG_ITERM_CHG_SET, (val & 0xF0) | opt);
}

driver_error_t *axp2101_get_charge_term_current(uint8_t *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_ITERM_CHG_SET, &val))) return error;
    *out = val & 0x0F;
    return NULL;
}

driver_error_t *axp2101_set_charge_voltage(uint8_t opt)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if (opt < 1 || opt > 5)
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "charge voltage must be 1..5");
    if ((error = reg_read(REG_CV_CHG_VOL_SET, &val))) return error;
    return reg_write(REG_CV_CHG_VOL_SET, (val & 0xFC) | opt);
}

driver_error_t *axp2101_get_charge_voltage(uint8_t *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_CV_CHG_VOL_SET, &val))) return error;
    *out = val & 0x03;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* VBUS limits                                                         */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_set_vbus_voltage_limit(uint8_t opt)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_INPUT_VOL_LIMIT, &val))) return error;
    return reg_write(REG_INPUT_VOL_LIMIT, (val & 0xF0) | (opt & 0x0F));
}

driver_error_t *axp2101_get_vbus_voltage_limit(uint8_t *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_INPUT_VOL_LIMIT, &val))) return error;
    *out = val & 0x0F;
    return NULL;
}

driver_error_t *axp2101_set_vbus_current_limit(uint8_t opt)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_INPUT_CUR_LIMIT, &val))) return error;
    return reg_write(REG_INPUT_CUR_LIMIT, (val & 0xF8) | (opt & 0x07));
}

driver_error_t *axp2101_get_vbus_current_limit(uint8_t *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_INPUT_CUR_LIMIT, &val))) return error;
    *out = val & 0x07;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* System power-down voltage                                           */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_set_sys_powerdown_voltage(int mv)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if (mv < VSYS_VOL_MIN || mv > VSYS_VOL_MAX || (mv % VSYS_VOL_STEP))
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "VSYS voltage out of range or not on 100mV step");
    if ((error = reg_read(REG_VOFF_SET, &val))) return error;
    return reg_write(REG_VOFF_SET,
                     (val & 0xF8) | (uint8_t)((mv - VSYS_VOL_MIN) / VSYS_VOL_STEP));
}

driver_error_t *axp2101_get_sys_powerdown_voltage(int *mv)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_VOFF_SET, &val))) return error;
    *mv = (int)(val & 0x07) * VSYS_VOL_STEP + VSYS_VOL_MIN;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Low-battery warning thresholds                                      */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_set_low_bat_warn(uint8_t warn_pct, uint8_t shutdown_pct)
{
    uint8_t val;
    REQUIRE_SETUP();
    if (warn_pct < 5 || warn_pct > 20)
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "warn threshold must be 5..20 %");
    if (shutdown_pct > 15)
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "shutdown threshold must be 0..15 %");
    val = (uint8_t)((warn_pct - 5) << 4) | (shutdown_pct & 0x0F);
    return reg_write(REG_LOW_BAT_WARN, val);
}

driver_error_t *axp2101_get_low_bat_warn(uint8_t *warn_pct, uint8_t *shutdown_pct)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_LOW_BAT_WARN, &val))) return error;
    *warn_pct     = ((val & 0xF0) >> 4) + 5;
    *shutdown_pct = val & 0x0F;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Charging LED                                                        */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_set_charging_led(uint8_t mode)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_CHGLED_CTRL, &val))) return error;
    if (mode <= AXP2101_CHG_LED_ON) {
        /* Manual mode: bits[5:4] = mode, bit2=1 (manual ctrl), bit0=1 */
        val &= 0xC8;
        val |= 0x05;
        val |= (uint8_t)(mode << 4);
    } else {
        /* Type-A charger-controlled mode */
        val &= 0xF9;
        val |= 0x01;
    }
    return reg_write(REG_CHGLED_CTRL, val);
}

driver_error_t *axp2101_get_charging_led(uint8_t *mode)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_CHGLED_CTRL, &val))) return error;
    val >>= 1;
    if ((val & 0x02) == 0x02) {
        *mode = (val >> 4) & 0x03;
    } else {
        *mode = AXP2101_CHG_LED_CTRL_CHG;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Power key timing                                                    */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_set_pkey_press_on_time(uint8_t opt)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if (opt > 3)
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "press-on time must be 0..3");
    if ((error = reg_read(REG_IRQ_LEVEL_CTRL, &val))) return error;
    return reg_write(REG_IRQ_LEVEL_CTRL, (val & 0xFC) | opt);
}

driver_error_t *axp2101_get_pkey_press_on_time(uint8_t *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_IRQ_LEVEL_CTRL, &val))) return error;
    *out = val & 0x03;
    return NULL;
}

driver_error_t *axp2101_set_pkey_press_off_time(uint8_t opt)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if (opt > 3)
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "press-off time must be 0..3");
    if ((error = reg_read(REG_IRQ_LEVEL_CTRL, &val))) return error;
    return reg_write(REG_IRQ_LEVEL_CTRL, (val & 0xF3) | (uint8_t)(opt << 2));
}

driver_error_t *axp2101_get_pkey_press_off_time(uint8_t *out)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_IRQ_LEVEL_CTRL, &val))) return error;
    *out = (val >> 2) & 0x03;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Sleep / wakeup                                                      */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_sleep_enable(bool enable)
{
    REQUIRE_SETUP();
    if (enable) return set_bit(REG_SLEEP_WAKEUP_CTRL, 0);
    return clr_bit(REG_SLEEP_WAKEUP_CTRL, 0);
}

driver_error_t *axp2101_wakeup_enable(bool enable)
{
    REQUIRE_SETUP();
    if (enable) return set_bit(REG_SLEEP_WAKEUP_CTRL, 1);
    return clr_bit(REG_SLEEP_WAKEUP_CTRL, 1);
}

/* ------------------------------------------------------------------ */
/* DC/LDO enable helpers (read-modify-write on ONOFF registers)        */
/* ------------------------------------------------------------------ */

static driver_error_t *dcdc_enable(uint8_t bit, bool enable)
{
    if (enable) return set_bit(REG_DC_ONOFF_DVM_CTRL, bit);
    return clr_bit(REG_DC_ONOFF_DVM_CTRL, bit);
}

static driver_error_t *dcdc_is_enabled(uint8_t bit, bool *out)
{
    return get_bit(REG_DC_ONOFF_DVM_CTRL, bit, out);
}

/* LDO_ONOFF_CTRL0 covers ALDO1–4 (bits 0–3), BLDO1–2 (bits 4–5),
 * CPUSLDO (bit 6), DLDO1 (bit 7).
 * LDO_ONOFF_CTRL1 covers DLDO2 (bit 0). */
static driver_error_t *ldo0_enable(uint8_t bit, bool enable)
{
    if (enable) return set_bit(REG_LDO_ONOFF_CTRL0, bit);
    return clr_bit(REG_LDO_ONOFF_CTRL0, bit);
}
static driver_error_t *ldo0_is_enabled(uint8_t bit, bool *out)
{
    return get_bit(REG_LDO_ONOFF_CTRL0, bit, out);
}
static driver_error_t *ldo1_enable(uint8_t bit, bool enable)
{
    if (enable) return set_bit(REG_LDO_ONOFF_CTRL1, bit);
    return clr_bit(REG_LDO_ONOFF_CTRL1, bit);
}
static driver_error_t *ldo1_is_enabled(uint8_t bit, bool *out)
{
    return get_bit(REG_LDO_ONOFF_CTRL1, bit, out);
}

/* Generic LDO set/get voltage for rails with 100 mV steps using bits[4:0] */
static driver_error_t *ldo_set_voltage(uint8_t reg, int mv,
                                        int min_mv, int max_mv, int step_mv)
{
    driver_error_t *error;
    uint8_t val;
    if (mv < min_mv || mv > max_mv || (mv % step_mv))
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "LDO voltage out of range or off step");
    if ((error = reg_read(reg, &val))) return error;
    return reg_write(reg, (val & 0xE0) | (uint8_t)((mv - min_mv) / step_mv));
}

static driver_error_t *ldo_get_voltage(uint8_t reg, int *mv,
                                        int min_mv, int step_mv)
{
    driver_error_t *error;
    uint8_t val;
    if ((error = reg_read(reg, &val))) return error;
    *mv = (int)(val & 0x1F) * step_mv + min_mv;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* DCDC1                                                               */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_dc1_enable(bool enable)     { REQUIRE_SETUP(); return dcdc_enable(0, enable); }
driver_error_t *axp2101_dc1_is_enabled(bool *out)   { REQUIRE_SETUP(); return dcdc_is_enabled(0, out); }

driver_error_t *axp2101_dc1_set_voltage(int mv)
{
    REQUIRE_SETUP();
    if (mv < DCDC1_VOL_MIN || mv > DCDC1_VOL_MAX || (mv % DCDC1_VOL_STEP))
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "DC1 voltage out of range or off 100mV step");
    return reg_write(REG_DC_VOL0_CTRL,
                     (uint8_t)((mv - DCDC1_VOL_MIN) / DCDC1_VOL_STEP));
}

driver_error_t *axp2101_dc1_get_voltage(int *mv)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_DC_VOL0_CTRL, &val))) return error;
    *mv = (int)(val & 0x1F) * DCDC1_VOL_STEP + DCDC1_VOL_MIN;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* DCDC2                                                               */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_dc2_enable(bool enable)     { REQUIRE_SETUP(); return dcdc_enable(1, enable); }
driver_error_t *axp2101_dc2_is_enabled(bool *out)   { REQUIRE_SETUP(); return dcdc_is_enabled(1, out); }

driver_error_t *axp2101_dc2_set_voltage(int mv)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_DC_VOL1_CTRL, &val))) return error;
    val &= 0x80;
    if (mv >= DCDC24_VOL1_MIN && mv <= DCDC24_VOL1_MAX) {
        if (mv % DCDC24_VOL_STEP1)
            return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                                "DC2 range1 step is 10mV");
        return reg_write(REG_DC_VOL1_CTRL,
                         val | (uint8_t)((mv - DCDC24_VOL1_MIN) / DCDC24_VOL_STEP1));
    } else if (mv >= DCDC24_VOL2_MIN && mv <= 1540) {
        if (mv % DCDC24_VOL_STEP2)
            return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                                "DC2 range2 step is 20mV");
        return reg_write(REG_DC_VOL1_CTRL,
                         val | (uint8_t)(((mv - DCDC24_VOL2_MIN) / DCDC24_VOL_STEP2)
                                        + DCDC24_STEP2_BASE));
    }
    return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                        "DC2 voltage out of range");
}

driver_error_t *axp2101_dc2_get_voltage(int *mv)
{
    driver_error_t *error;
    uint8_t raw;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_DC_VOL1_CTRL, &raw))) return error;
    raw &= 0x7F;
    if (raw < DCDC24_STEP2_BASE)
        *mv = (int)raw * DCDC24_VOL_STEP1 + DCDC24_VOL1_MIN;
    else
        *mv = (int)raw * DCDC24_VOL_STEP2 - 200;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* DCDC3                                                               */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_dc3_enable(bool enable)     { REQUIRE_SETUP(); return dcdc_enable(2, enable); }
driver_error_t *axp2101_dc3_is_enabled(bool *out)   { REQUIRE_SETUP(); return dcdc_is_enabled(2, out); }

driver_error_t *axp2101_dc3_set_voltage(int mv)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_DC_VOL2_CTRL, &val))) return error;
    val &= 0x80;
    if (mv >= DCDC3_VOL_MIN && mv <= DCDC3_VOL1_MAX) {
        if (mv % DCDC3_VOL_STEP1)
            return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                                "DC3 range1 step is 10mV");
        return reg_write(REG_DC_VOL2_CTRL,
                         val | (uint8_t)((mv - DCDC3_VOL_MIN) / DCDC3_VOL_STEP1));
    } else if (mv >= DCDC3_VOL2_MIN && mv <= DCDC3_VOL2_MAX) {
        if (mv % DCDC3_VOL_STEP2)
            return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                                "DC3 range2 step is 20mV");
        return reg_write(REG_DC_VOL2_CTRL,
                         val | (uint8_t)(((mv - DCDC3_VOL2_MIN) / DCDC3_VOL_STEP2)
                                        + DCDC3_STEP2_BASE));
    } else if (mv >= DCDC3_VOL3_MIN && mv <= DCDC3_VOL3_MAX) {
        if (mv % DCDC3_VOL_STEP3)
            return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                                "DC3 range3 step is 100mV");
        return reg_write(REG_DC_VOL2_CTRL,
                         val | (uint8_t)(((mv - DCDC3_VOL3_MIN) / DCDC3_VOL_STEP3)
                                        + DCDC3_STEP3_BASE));
    }
    return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                        "DC3 voltage out of range");
}

driver_error_t *axp2101_dc3_get_voltage(int *mv)
{
    driver_error_t *error;
    uint8_t raw;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_DC_VOL2_CTRL, &raw))) return error;
    raw &= 0x7F;
    if (raw < DCDC3_STEP2_BASE)
        *mv = (int)raw * DCDC3_VOL_STEP1 + DCDC3_VOL_MIN;
    else if (raw < DCDC3_STEP3_BASE)
        *mv = (int)raw * DCDC3_VOL_STEP2 - 200;
    else
        *mv = (int)raw * DCDC3_VOL_STEP3 - 7200;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* DCDC4                                                               */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_dc4_enable(bool enable)     { REQUIRE_SETUP(); return dcdc_enable(3, enable); }
driver_error_t *axp2101_dc4_is_enabled(bool *out)   { REQUIRE_SETUP(); return dcdc_is_enabled(3, out); }

driver_error_t *axp2101_dc4_set_voltage(int mv)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_DC_VOL3_CTRL, &val))) return error;
    val &= 0x80;
    if (mv >= DCDC24_VOL1_MIN && mv <= DCDC24_VOL1_MAX) {
        if (mv % DCDC24_VOL_STEP1)
            return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                                "DC4 range1 step is 10mV");
        return reg_write(REG_DC_VOL3_CTRL,
                         val | (uint8_t)((mv - DCDC24_VOL1_MIN) / DCDC24_VOL_STEP1));
    } else if (mv >= DCDC24_VOL2_MIN && mv <= DCDC24_VOL2_MAX) {
        if (mv % DCDC24_VOL_STEP2)
            return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                                "DC4 range2 step is 20mV");
        return reg_write(REG_DC_VOL3_CTRL,
                         val | (uint8_t)(((mv - DCDC24_VOL2_MIN) / DCDC24_VOL_STEP2)
                                        + DCDC24_STEP2_BASE));
    }
    return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                        "DC4 voltage out of range");
}

driver_error_t *axp2101_dc4_get_voltage(int *mv)
{
    driver_error_t *error;
    uint8_t raw;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_DC_VOL3_CTRL, &raw))) return error;
    raw &= 0x7F;
    if (raw < DCDC24_STEP2_BASE)
        *mv = (int)raw * DCDC24_VOL_STEP1 + DCDC24_VOL1_MIN;
    else
        *mv = (int)raw * DCDC24_VOL_STEP2 - 200;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* DCDC5                                                               */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_dc5_enable(bool enable)     { REQUIRE_SETUP(); return dcdc_enable(4, enable); }
driver_error_t *axp2101_dc5_is_enabled(bool *out)   { REQUIRE_SETUP(); return dcdc_is_enabled(4, out); }

driver_error_t *axp2101_dc5_set_voltage(int mv)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_DC_VOL4_CTRL, &val))) return error;
    val &= 0xE0;
    if (mv == DCDC5_VOL_1200MV) {
        return reg_write(REG_DC_VOL4_CTRL, val | DCDC5_VOL_VAL);
    }
    if (mv < DCDC5_VOL_MIN || mv > DCDC5_VOL_MAX || (mv % DCDC5_VOL_STEP))
        return driver_error(AXP2101_DRIVER, AXP2101_ERR_INVALID_ARG,
                            "DC5 voltage out of range or off 100mV step");
    return reg_write(REG_DC_VOL4_CTRL,
                     val | (uint8_t)((mv - DCDC5_VOL_MIN) / DCDC5_VOL_STEP));
}

driver_error_t *axp2101_dc5_get_voltage(int *mv)
{
    driver_error_t *error;
    uint8_t val;
    REQUIRE_SETUP();
    if ((error = reg_read(REG_DC_VOL4_CTRL, &val))) return error;
    val &= 0x1F;
    if (val == DCDC5_VOL_VAL)
        *mv = DCDC5_VOL_1200MV;
    else
        *mv = (int)val * DCDC5_VOL_STEP + DCDC5_VOL_MIN;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* ALDO1–4                                                             */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_aldo1_enable(bool enable)     { REQUIRE_SETUP(); return ldo0_enable(0, enable); }
driver_error_t *axp2101_aldo1_is_enabled(bool *out)   { REQUIRE_SETUP(); return ldo0_is_enabled(0, out); }
driver_error_t *axp2101_aldo1_set_voltage(int mv)     { REQUIRE_SETUP(); return ldo_set_voltage(REG_LDO_VOL0_CTRL, mv, ALDO_VOL_MIN, ALDO_VOL_MAX, ALDO_VOL_STEP); }
driver_error_t *axp2101_aldo1_get_voltage(int *mv)    { REQUIRE_SETUP(); return ldo_get_voltage(REG_LDO_VOL0_CTRL, mv, ALDO_VOL_MIN, ALDO_VOL_STEP); }

driver_error_t *axp2101_aldo2_enable(bool enable)     { REQUIRE_SETUP(); return ldo0_enable(1, enable); }
driver_error_t *axp2101_aldo2_is_enabled(bool *out)   { REQUIRE_SETUP(); return ldo0_is_enabled(1, out); }
driver_error_t *axp2101_aldo2_set_voltage(int mv)     { REQUIRE_SETUP(); return ldo_set_voltage(REG_LDO_VOL1_CTRL, mv, ALDO_VOL_MIN, ALDO_VOL_MAX, ALDO_VOL_STEP); }
driver_error_t *axp2101_aldo2_get_voltage(int *mv)    { REQUIRE_SETUP(); return ldo_get_voltage(REG_LDO_VOL1_CTRL, mv, ALDO_VOL_MIN, ALDO_VOL_STEP); }

driver_error_t *axp2101_aldo3_enable(bool enable)     { REQUIRE_SETUP(); return ldo0_enable(2, enable); }
driver_error_t *axp2101_aldo3_is_enabled(bool *out)   { REQUIRE_SETUP(); return ldo0_is_enabled(2, out); }
driver_error_t *axp2101_aldo3_set_voltage(int mv)     { REQUIRE_SETUP(); return ldo_set_voltage(REG_LDO_VOL2_CTRL, mv, ALDO_VOL_MIN, ALDO_VOL_MAX, ALDO_VOL_STEP); }
driver_error_t *axp2101_aldo3_get_voltage(int *mv)    { REQUIRE_SETUP(); return ldo_get_voltage(REG_LDO_VOL2_CTRL, mv, ALDO_VOL_MIN, ALDO_VOL_STEP); }

driver_error_t *axp2101_aldo4_enable(bool enable)     { REQUIRE_SETUP(); return ldo0_enable(3, enable); }
driver_error_t *axp2101_aldo4_is_enabled(bool *out)   { REQUIRE_SETUP(); return ldo0_is_enabled(3, out); }
driver_error_t *axp2101_aldo4_set_voltage(int mv)     { REQUIRE_SETUP(); return ldo_set_voltage(REG_LDO_VOL3_CTRL, mv, ALDO_VOL_MIN, ALDO_VOL_MAX, ALDO_VOL_STEP); }
driver_error_t *axp2101_aldo4_get_voltage(int *mv)    { REQUIRE_SETUP(); return ldo_get_voltage(REG_LDO_VOL3_CTRL, mv, ALDO_VOL_MIN, ALDO_VOL_STEP); }

/* ------------------------------------------------------------------ */
/* BLDO1–2                                                             */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_bldo1_enable(bool enable)     { REQUIRE_SETUP(); return ldo0_enable(4, enable); }
driver_error_t *axp2101_bldo1_is_enabled(bool *out)   { REQUIRE_SETUP(); return ldo0_is_enabled(4, out); }
driver_error_t *axp2101_bldo1_set_voltage(int mv)     { REQUIRE_SETUP(); return ldo_set_voltage(REG_LDO_VOL4_CTRL, mv, BLDO_VOL_MIN, BLDO_VOL_MAX, BLDO_VOL_STEP); }
driver_error_t *axp2101_bldo1_get_voltage(int *mv)    { REQUIRE_SETUP(); return ldo_get_voltage(REG_LDO_VOL4_CTRL, mv, BLDO_VOL_MIN, BLDO_VOL_STEP); }

driver_error_t *axp2101_bldo2_enable(bool enable)     { REQUIRE_SETUP(); return ldo0_enable(5, enable); }
driver_error_t *axp2101_bldo2_is_enabled(bool *out)   { REQUIRE_SETUP(); return ldo0_is_enabled(5, out); }
driver_error_t *axp2101_bldo2_set_voltage(int mv)     { REQUIRE_SETUP(); return ldo_set_voltage(REG_LDO_VOL5_CTRL, mv, BLDO_VOL_MIN, BLDO_VOL_MAX, BLDO_VOL_STEP); }
driver_error_t *axp2101_bldo2_get_voltage(int *mv)    { REQUIRE_SETUP(); return ldo_get_voltage(REG_LDO_VOL5_CTRL, mv, BLDO_VOL_MIN, BLDO_VOL_STEP); }

/* ------------------------------------------------------------------ */
/* CPUSLDO                                                             */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_cpusldo_enable(bool enable)   { REQUIRE_SETUP(); return ldo0_enable(6, enable); }
driver_error_t *axp2101_cpusldo_is_enabled(bool *out) { REQUIRE_SETUP(); return ldo0_is_enabled(6, out); }
driver_error_t *axp2101_cpusldo_set_voltage(int mv)   { REQUIRE_SETUP(); return ldo_set_voltage(REG_LDO_VOL6_CTRL, mv, CPUSLDO_VOL_MIN, CPUSLDO_VOL_MAX, CPUSLDO_VOL_STEP); }
driver_error_t *axp2101_cpusldo_get_voltage(int *mv)  { REQUIRE_SETUP(); return ldo_get_voltage(REG_LDO_VOL6_CTRL, mv, CPUSLDO_VOL_MIN, CPUSLDO_VOL_STEP); }

/* ------------------------------------------------------------------ */
/* DLDO1–2                                                             */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_dldo1_enable(bool enable)     { REQUIRE_SETUP(); return ldo0_enable(7, enable); }
driver_error_t *axp2101_dldo1_is_enabled(bool *out)   { REQUIRE_SETUP(); return ldo0_is_enabled(7, out); }
driver_error_t *axp2101_dldo1_set_voltage(int mv)     { REQUIRE_SETUP(); return ldo_set_voltage(REG_LDO_VOL7_CTRL, mv, DLDO_VOL_MIN, DLDO_VOL_MAX, DLDO_VOL_STEP); }
driver_error_t *axp2101_dldo1_get_voltage(int *mv)    { REQUIRE_SETUP(); return ldo_get_voltage(REG_LDO_VOL7_CTRL, mv, DLDO_VOL_MIN, DLDO_VOL_STEP); }

driver_error_t *axp2101_dldo2_enable(bool enable)     { REQUIRE_SETUP(); return ldo1_enable(0, enable); }
driver_error_t *axp2101_dldo2_is_enabled(bool *out)   { REQUIRE_SETUP(); return ldo1_is_enabled(0, out); }
driver_error_t *axp2101_dldo2_set_voltage(int mv)     { REQUIRE_SETUP(); return ldo_set_voltage(REG_LDO_VOL8_CTRL, mv, DLDO_VOL_MIN, DLDO_VOL_MAX, DLDO_VOL_STEP); }
driver_error_t *axp2101_dldo2_get_voltage(int *mv)    { REQUIRE_SETUP(); return ldo_get_voltage(REG_LDO_VOL8_CTRL, mv, DLDO_VOL_MIN, DLDO_VOL_STEP); }

/* ------------------------------------------------------------------ */
/* IRQ control                                                         */
/* ------------------------------------------------------------------ */

/*
 * Write enable/disable bits across three IRQ enable registers.
 * mask is a 24-bit value: bits[7:0] -> INTEN1, bits[15:8] -> INTEN2,
 * bits[23:16] -> INTEN3.
 */
static driver_error_t *irq_set(uint32_t mask, bool enable)
{
    driver_error_t *error;
    uint8_t val;

    if (mask & 0x0000FF) {
        uint8_t m = mask & 0xFF;
        if ((error = reg_read(REG_INTEN1, &val))) return error;
        val = enable ? (val | m) : (val & ~m);
        if ((error = reg_write(REG_INTEN1, val))) return error;
    }
    if (mask & 0x00FF00) {
        uint8_t m = (mask >> 8) & 0xFF;
        if ((error = reg_read(REG_INTEN2, &val))) return error;
        val = enable ? (val | m) : (val & ~m);
        if ((error = reg_write(REG_INTEN2, val))) return error;
    }
    if (mask & 0xFF0000) {
        uint8_t m = (mask >> 16) & 0xFF;
        if ((error = reg_read(REG_INTEN3, &val))) return error;
        val = enable ? (val | m) : (val & ~m);
        if ((error = reg_write(REG_INTEN3, val))) return error;
    }
    return NULL;
}

driver_error_t *axp2101_irq_enable(uint32_t mask)
{
    REQUIRE_SETUP();
    return irq_set(mask, true);
}

driver_error_t *axp2101_irq_disable(uint32_t mask)
{
    REQUIRE_SETUP();
    return irq_set(mask, false);
}

driver_error_t *axp2101_irq_get_status(uint32_t *status)
{
    driver_error_t *error;
    uint8_t buf[3];
    REQUIRE_SETUP();
    if ((error = reg_read_n(REG_INTSTS1, buf, 3))) return error;
    *status = ((uint32_t)buf[2] << 16) | ((uint32_t)buf[1] << 8) | buf[0];
    return NULL;
}

driver_error_t *axp2101_irq_clear_status(void)
{
    driver_error_t *error;
    REQUIRE_SETUP();
    if ((error = reg_write(REG_INTSTS1, 0xFF))) return error;
    if ((error = reg_write(REG_INTSTS2, 0xFF))) return error;
    return reg_write(REG_INTSTS3, 0xFF);
}
