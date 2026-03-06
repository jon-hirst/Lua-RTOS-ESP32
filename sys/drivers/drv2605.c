/*
 * Lua RTOS, DRV2605 haptic motor driver
 *
 * DRV2605 connected on I2C0: SDA=GPIO10, SCL=GPIO11
 * IN/TRIG pin not used (tied to GND); ERM open-loop mode.
 *
 * Based on Adafruit CircuitPython DRV2605 driver:
 *   Copyright (c) 2017 Tony DiCola for Adafruit Industries
 *   SPDX-License-Identifier: MIT
 */

#include "luartos.h"

#include <stdint.h>
#include <stdbool.h>

#include <sys/driver.h>
#include <drivers/i2c.h>
#include <drivers/drv2605.h>

/* ------------------------------------------------------------------ */
/* Hardware configuration                                              */
/* ------------------------------------------------------------------ */

#define DRV2605_I2C_ADDR   0x5A
#define DRV2605_I2C_UNIT   0
#define DRV2605_SDA_GPIO   10
#define DRV2605_SCL_GPIO   11
#define DRV2605_I2C_SPEED  400000   /* 400 kHz */

/* ------------------------------------------------------------------ */
/* Register map                                                        */
/* ------------------------------------------------------------------ */

#define REG_STATUS      0x00
#define REG_MODE        0x01
#define REG_RTPIN       0x02
#define REG_LIBRARY     0x03
#define REG_WAVESEQ1    0x04   /* slots 0–7 are REG_WAVESEQ1 + slot */
#define REG_GO          0x0C
#define REG_OVERDRIVE   0x0D
#define REG_SUSTAINPOS  0x0E
#define REG_SUSTAINNEG  0x0F
#define REG_BREAK       0x10
#define REG_AUDIOMAX    0x13
#define REG_FEEDBACK    0x1A
#define REG_CONTROL3    0x1D

/* STATUS register: device-ID field */
#define STATUS_DEVID_SHIFT   5
#define STATUS_DEVID_MASK    0x07

/* FEEDBACK register: N_ERM_LRA bit selects motor type */
#define FEEDBACK_N_ERM_LRA   0x80   /* 0 = ERM, 1 = LRA */

/* CONTROL3 register: ERM open-loop bit */
#define CONTROL3_ERM_OPEN_LOOP 0x20

/* Sequence slot: bit 7 flags a timed-pause entry */
#define SLOT_PAUSE_FLAG      0x80

/* ------------------------------------------------------------------ */
/* Driver registration                                                 */
/* ------------------------------------------------------------------ */

DRIVER_REGISTER_BEGIN(DRV2605, drv2605, 0, NULL, NULL);
    DRIVER_REGISTER_ERROR(DRV2605, drv2605, CantInit,   "can't initialize", DRV2605_ERR_CANT_INIT);
    DRIVER_REGISTER_ERROR(DRV2605, drv2605, NotSetup,   "not setup",        DRV2605_ERR_NOT_SETUP);
    DRIVER_REGISTER_ERROR(DRV2605, drv2605, InvalidArg, "invalid argument",  DRV2605_ERR_INVALID_ARG);
DRIVER_REGISTER_END(DRV2605, drv2605, 0, NULL, NULL);

/* ------------------------------------------------------------------ */
/* Driver state                                                        */
/* ------------------------------------------------------------------ */

static int i2c_device = -1;

/* ------------------------------------------------------------------ */
/* Low-level I2C helpers                                               */
/* ------------------------------------------------------------------ */

static driver_error_t *drv2605_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_write(i2c_device, buf, 2);
}

static driver_error_t *drv2605_read_reg(uint8_t reg, uint8_t *val) {
    return i2c_write_read(i2c_device, &reg, 1, val, 1);
}

/* Read-modify-write: set bits in mask to the values in bits. */
static driver_error_t *drv2605_rmw_reg(uint8_t reg, uint8_t mask, uint8_t bits) {
    driver_error_t *error;
    uint8_t val;
    if ((error = drv2605_read_reg(reg, &val))) return error;
    val = (val & ~mask) | (bits & mask);
    return drv2605_write_reg(reg, val);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

driver_error_t *drv2605_setup(void) {
    driver_error_t *error;

    if (i2c_device >= 0) {
        return NULL;   /* Already initialised */
    }

    /* Configure I2C bus pins and attach as master */
    if ((error = i2c_pin_map(DRV2605_I2C_UNIT, DRV2605_SDA_GPIO, DRV2605_SCL_GPIO))) {
        return error;
    }
    if ((error = i2c_attach(DRV2605_I2C_UNIT, I2C_MASTER, DRV2605_I2C_SPEED,
                            0, DRV2605_I2C_ADDR, &i2c_device))) {
        return error;
    }

    /* Verify device ID: bits [7:5] of STATUS must be 3 (DRV2605) or 7 (DRV2605L) */
    uint8_t status;
    if ((error = drv2605_read_reg(REG_STATUS, &status))) {
        return error;
    }
    uint8_t device_id = (status >> STATUS_DEVID_SHIFT) & STATUS_DEVID_MASK;
    if (device_id != 3 && device_id != 7) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_CANT_INIT,
                            "unexpected device ID");
    }

    /* Take the device out of standby (MODE = 0x00) */
    if ((error = drv2605_write_reg(REG_MODE, 0x00))) return error;

    /* Clear the RTP register — no real-time-playback signal */
    if ((error = drv2605_write_reg(REG_RTPIN, 0x00))) return error;

    /* Default sequence: effect 1 (strong click) in slot 0, terminator in slot 1 */
    if ((error = drv2605_write_reg(REG_WAVESEQ1,     1))) return error;
    if ((error = drv2605_write_reg(REG_WAVESEQ1 + 1, 0))) return error;

    /* Clear timing override registers */
    if ((error = drv2605_write_reg(REG_OVERDRIVE,  0))) return error;
    if ((error = drv2605_write_reg(REG_SUSTAINPOS, 0))) return error;
    if ((error = drv2605_write_reg(REG_SUSTAINNEG, 0))) return error;
    if ((error = drv2605_write_reg(REG_BREAK,      0))) return error;

    /* Set audio-to-vibe max input level (mirrors Adafruit init) */
    if ((error = drv2605_write_reg(REG_AUDIOMAX, 0x64))) return error;

    /* Select ERM motor type: clear the N_ERM_LRA bit in FEEDBACK */
    if ((error = drv2605_rmw_reg(REG_FEEDBACK, FEEDBACK_N_ERM_LRA, 0x00))) {
        return error;
    }

    /* Enable ERM open-loop mode: set ERM_OPEN_LOOP in CONTROL3 */
    if ((error = drv2605_rmw_reg(REG_CONTROL3, CONTROL3_ERM_OPEN_LOOP,
                                  CONTROL3_ERM_OPEN_LOOP))) {
        return error;
    }

    /* Default to internal trigger mode */
    if ((error = drv2605_write_reg(REG_MODE, DRV2605_MODE_INTTRIG))) return error;

    /* Default library: TS2200 A (best choice for ERM) */
    if ((error = drv2605_write_reg(REG_LIBRARY, DRV2605_LIBRARY_TS2200A))) return error;

    return NULL;
}

driver_error_t *drv2605_play(void) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    return drv2605_write_reg(REG_GO, 1);
}

driver_error_t *drv2605_stop(void) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    return drv2605_write_reg(REG_GO, 0);
}

driver_error_t *drv2605_set_mode(uint8_t mode) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    if (mode > 7) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_INVALID_ARG, "mode 0-7");
    }
    return drv2605_write_reg(REG_MODE, mode);
}

driver_error_t *drv2605_get_mode(uint8_t *mode) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    return drv2605_read_reg(REG_MODE, mode);
}

driver_error_t *drv2605_set_library(uint8_t library) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    if (library > 6) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_INVALID_ARG, "library 0-6");
    }
    return drv2605_write_reg(REG_LIBRARY, library);
}

driver_error_t *drv2605_get_library(uint8_t *library) {
    driver_error_t *error;
    uint8_t val;

    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    if ((error = drv2605_read_reg(REG_LIBRARY, &val))) return error;
    *library = val & 0x07;
    return NULL;
}

driver_error_t *drv2605_set_waveform(uint8_t slot, uint8_t effect_id) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    if (slot > 7) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_INVALID_ARG, "slot 0-7");
    }
    if (effect_id > 123) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_INVALID_ARG, "effect_id 0-123");
    }
    return drv2605_write_reg(REG_WAVESEQ1 + slot, effect_id);
}

driver_error_t *drv2605_set_pause(uint8_t slot, uint8_t duration_cs) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    if (slot > 7) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_INVALID_ARG, "slot 0-7");
    }
    if (duration_cs == 0 || duration_cs > 127) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_INVALID_ARG, "duration_cs 1-127");
    }
    /* Bit 7 set signals a timed-pause entry to the sequencer */
    return drv2605_write_reg(REG_WAVESEQ1 + slot, SLOT_PAUSE_FLAG | duration_cs);
}

driver_error_t *drv2605_get_slot(uint8_t slot, uint8_t *value) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    if (slot > 7) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_INVALID_ARG, "slot 0-7");
    }
    return drv2605_read_reg(REG_WAVESEQ1 + slot, value);
}

driver_error_t *drv2605_set_realtime_value(int8_t val) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    return drv2605_write_reg(REG_RTPIN, (uint8_t)val);
}

driver_error_t *drv2605_get_realtime_value(int8_t *val) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    return drv2605_read_reg(REG_RTPIN, (uint8_t *)val);
}

driver_error_t *drv2605_get_status(uint8_t *status) {
    if (i2c_device < 0) {
        return driver_error(DRV2605_DRIVER, DRV2605_ERR_NOT_SETUP, NULL);
    }
    return drv2605_read_reg(REG_STATUS, status);
}
