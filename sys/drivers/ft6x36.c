/*
 * Lua RTOS, FT6x36 capacitive touch controller driver
 *
 * FT6x36 connected on I2C1: SDA=GPIO39, SCL=GPIO40
 *
 * Based on CircuitPython driver:
 *   SPDX-FileCopyrightText: 2022 lbuque
 *   SPDX-License-Identifier: MIT
 */

#include "luartos.h"

#include <stdint.h>

#include <sys/driver.h>
#include <drivers/i2c.h>
#include <drivers/ft6x36.h>

/* ------------------------------------------------------------------ */
/* Hardware configuration                                              */
/* ------------------------------------------------------------------ */

#define FT6X36_I2C_ADDR   0x38
#define FT6X36_I2C_UNIT   1
#define FT6X36_SDA_GPIO   39
#define FT6X36_SCL_GPIO   40
#define FT6X36_I2C_SPEED  400000   /* 400 kHz */

/* ------------------------------------------------------------------ */
/* Register map                                                        */
/* ------------------------------------------------------------------ */

#define REG_DEV_MODE         0x00
#define REG_GEST_ID          0x01
#define REG_TD_STATUS        0x02
#define REG_P1_XH            0x03   /* 12 bytes: P1 XH,XL,YH,YL,WEIGHT,MISC then P2 */
#define REG_TH_GROUP         0x80
#define REG_TIMEENTERMONITOR 0x87
#define REG_PERIODACTIVE     0x88
#define REG_PERIODMONITOR    0x89
#define REG_LIB_VER_H        0xA1
#define REG_CIPHER           0xA3
#define REG_G_MODE           0xA4
#define REG_PWR_MODE         0xA5
#define REG_FIRMID           0xA6
#define REG_FOCALTECH_ID     0xA8

/* Raw gesture codes from the chip */
#define GEST_RAW_MOVE_UP    0x10
#define GEST_RAW_MOVE_RIGHT 0x14
#define GEST_RAW_MOVE_DOWN  0x18
#define GEST_RAW_MOVE_LEFT  0x1C
#define GEST_RAW_ZOOM_IN    0x48
#define GEST_RAW_ZOOM_OUT   0x49

/* ------------------------------------------------------------------ */
/* Driver registration                                                 */
/* ------------------------------------------------------------------ */

DRIVER_REGISTER_BEGIN(FT6X36, ft6x36, 0, NULL, NULL);
    DRIVER_REGISTER_ERROR(FT6X36, ft6x36, CantInit,   "can't initialize", FT6X36_ERR_CANT_INIT);
    DRIVER_REGISTER_ERROR(FT6X36, ft6x36, NotSetup,   "not setup",        FT6X36_ERR_NOT_SETUP);
    DRIVER_REGISTER_ERROR(FT6X36, ft6x36, InvalidArg, "invalid argument",  FT6X36_ERR_INVALID_ARG);
DRIVER_REGISTER_END(FT6X36, ft6x36, 0, NULL, NULL);

/* ------------------------------------------------------------------ */
/* Driver state                                                        */
/* ------------------------------------------------------------------ */

static int      i2c_device  = -1;
static uint8_t  rotation    = FT6X36_PORTRAIT;
static uint16_t scr_width   = 240;
static uint16_t scr_height  = 240;

/* ------------------------------------------------------------------ */
/* Low-level I2C helpers                                               */
/* ------------------------------------------------------------------ */

static driver_error_t *ft6x36_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_write(i2c_device, buf, 2);
}

static driver_error_t *ft6x36_read_reg(uint8_t reg, uint8_t *val) {
    return i2c_write_read(i2c_device, &reg, 1, val, 1);
}

static driver_error_t *ft6x36_read_regs(uint8_t reg, uint8_t *buf, int len) {
    return i2c_write_read(i2c_device, &reg, 1, buf, len);
}

/* ------------------------------------------------------------------ */
/* Rotation coordinate transform                                       */
/* ------------------------------------------------------------------ */

static void apply_rotation(uint16_t raw_x, uint16_t raw_y,
                            uint16_t *out_x, uint16_t *out_y) {
    switch (rotation) {
        case FT6X36_LANDSCAPE:
            *out_x = raw_y;
            *out_y = (raw_x <= scr_width) ? (scr_width - raw_x) : 0;
            break;
        case FT6X36_PORTRAIT_INVERTED:
            *out_x = (raw_x <= scr_width)  ? (scr_width  - raw_x) : 0;
            *out_y = (raw_y <= scr_height) ? (scr_height - raw_y) : 0;
            break;
        case FT6X36_LANDSCAPE_INVERTED:
            *out_x = (raw_y <= scr_height) ? (scr_height - raw_y) : 0;
            *out_y = raw_x;
            break;
        default: /* FT6X36_PORTRAIT */
            *out_x = raw_x;
            *out_y = raw_y;
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

driver_error_t *ft6x36_setup(uint16_t width, uint16_t height) {
    driver_error_t *error;

    if (i2c_device >= 0) {
        return NULL;   /* Already initialised */
    }

    scr_width  = width;
    scr_height = height;

    if ((error = i2c_pin_map(FT6X36_I2C_UNIT, FT6X36_SDA_GPIO, FT6X36_SCL_GPIO))) {
        return error;
    }
    if ((error = i2c_attach(FT6X36_I2C_UNIT, I2C_MASTER, FT6X36_I2C_SPEED,
                            0, FT6X36_I2C_ADDR, &i2c_device))) {
        return error;
    }

    /* Verify the chip responds by reading the FocalTech panel ID (should be 0x11) */
    uint8_t panel_id;
    if ((error = ft6x36_read_reg(REG_FOCALTECH_ID, &panel_id))) {
        i2c_device = -1;
        return driver_error(FT6X36_DRIVER, FT6X36_ERR_CANT_INIT,
                            "device not responding");
    }

    return NULL;
}

driver_error_t *ft6x36_get_gesture(uint8_t *gesture) {
    driver_error_t *error;
    uint8_t raw;

    if (i2c_device < 0) {
        return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    }
    if ((error = ft6x36_read_reg(REG_GEST_ID, &raw))) return error;

    switch (raw) {
        case GEST_RAW_MOVE_UP:    *gesture = FT6X36_GESTURE_MOVE_UP;    break;
        case GEST_RAW_MOVE_RIGHT: *gesture = FT6X36_GESTURE_MOVE_RIGHT; break;
        case GEST_RAW_MOVE_DOWN:  *gesture = FT6X36_GESTURE_MOVE_DOWN;  break;
        case GEST_RAW_MOVE_LEFT:  *gesture = FT6X36_GESTURE_MOVE_LEFT;  break;
        case GEST_RAW_ZOOM_IN:    *gesture = FT6X36_GESTURE_ZOOM_IN;    break;
        case GEST_RAW_ZOOM_OUT:   *gesture = FT6X36_GESTURE_ZOOM_OUT;   break;
        default:                  *gesture = FT6X36_GESTURE_NONE;       break;
    }
    return NULL;
}

driver_error_t *ft6x36_get_positions(uint8_t *num_points, ft6x36_point_t points[2]) {
    driver_error_t *error;
    uint8_t status;
    uint8_t buf[12];

    if (i2c_device < 0) {
        return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    }

    if ((error = ft6x36_read_reg(REG_TD_STATUS, &status))) return error;
    *num_points = status & 0x0F;
    if (*num_points > 2) *num_points = 2;

    if (*num_points == 0) return NULL;

    /* Read 12 bytes starting at P1_XH: 6 bytes per touch point */
    if ((error = ft6x36_read_regs(REG_P1_XH, buf, 12))) return error;

    for (int i = 0; i < 2; i++) {
        int off = i * 6;
        uint16_t raw_x = ((uint16_t)(buf[off + 0] & 0x0F) << 8) | buf[off + 1];
        uint16_t raw_y = ((uint16_t)(buf[off + 2] & 0x0F) << 8) | buf[off + 3];
        apply_rotation(raw_x, raw_y, &points[i].x, &points[i].y);
        points[i].weight = buf[off + 4];
        points[i].area   = buf[off + 5] & 0x0F;
    }
    return NULL;
}

driver_error_t *ft6x36_set_rotation(uint8_t rot) {
    if (i2c_device < 0) {
        return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    }
    if (rot > FT6X36_LANDSCAPE_INVERTED) {
        return driver_error(FT6X36_DRIVER, FT6X36_ERR_INVALID_ARG, "rotation 0-3");
    }
    rotation = rot;
    return NULL;
}

driver_error_t *ft6x36_get_rotation(uint8_t *rot) {
    if (i2c_device < 0) {
        return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    }
    *rot = rotation;
    return NULL;
}

driver_error_t *ft6x36_get_threshold(uint8_t *val) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_read_reg(REG_TH_GROUP, val);
}

driver_error_t *ft6x36_set_threshold(uint8_t val) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_write_reg(REG_TH_GROUP, val);
}

driver_error_t *ft6x36_get_monitor_time(uint8_t *val) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_read_reg(REG_TIMEENTERMONITOR, val);
}

driver_error_t *ft6x36_set_monitor_time(uint8_t val) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_write_reg(REG_TIMEENTERMONITOR, val);
}

driver_error_t *ft6x36_get_active_period(uint8_t *val) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_read_reg(REG_PERIODACTIVE, val);
}

driver_error_t *ft6x36_set_active_period(uint8_t val) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_write_reg(REG_PERIODACTIVE, val);
}

driver_error_t *ft6x36_get_monitor_period(uint8_t *val) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_read_reg(REG_PERIODMONITOR, val);
}

driver_error_t *ft6x36_set_monitor_period(uint8_t val) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_write_reg(REG_PERIODMONITOR, val);
}

driver_error_t *ft6x36_get_lib_version(uint16_t *version) {
    driver_error_t *error;
    uint8_t buf[2];
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    if ((error = ft6x36_read_regs(REG_LIB_VER_H, buf, 2))) return error;
    *version = ((uint16_t)buf[0] << 8) | buf[1];
    return NULL;
}

driver_error_t *ft6x36_get_fw_version(uint8_t *version) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_read_reg(REG_FIRMID, version);
}

driver_error_t *ft6x36_get_int_mode(uint8_t *mode) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_read_reg(REG_G_MODE, mode);
}

driver_error_t *ft6x36_set_int_mode(uint8_t mode) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    if (mode > FT6X36_TRIGGER_MODE) {
        return driver_error(FT6X36_DRIVER, FT6X36_ERR_INVALID_ARG, "mode 0 or 1");
    }
    return ft6x36_write_reg(REG_G_MODE, mode);
}

driver_error_t *ft6x36_get_power_mode(uint8_t *mode) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_read_reg(REG_PWR_MODE, mode);
}

driver_error_t *ft6x36_set_power_mode(uint8_t mode) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_write_reg(REG_PWR_MODE, mode);
}

driver_error_t *ft6x36_get_vendor_id(uint8_t *id) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_read_reg(REG_CIPHER, id);
}

driver_error_t *ft6x36_get_panel_id(uint8_t *id) {
    if (i2c_device < 0) return driver_error(FT6X36_DRIVER, FT6X36_ERR_NOT_SETUP, NULL);
    return ft6x36_read_reg(REG_FOCALTECH_ID, id);
}
