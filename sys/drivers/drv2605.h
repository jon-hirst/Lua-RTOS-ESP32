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

#ifndef DRV2605_H
#define DRV2605_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/driver.h>

/* ------------------------------------------------------------------ */
/* Error codes                                                         */
/* ------------------------------------------------------------------ */

#define DRV2605_ERR_CANT_INIT   (DRIVER_EXCEPTION_BASE(DRV2605_DRIVER_ID) | 0)
#define DRV2605_ERR_NOT_SETUP   (DRIVER_EXCEPTION_BASE(DRV2605_DRIVER_ID) | 1)
#define DRV2605_ERR_INVALID_ARG (DRIVER_EXCEPTION_BASE(DRV2605_DRIVER_ID) | 2)

extern const int drv2605_errors;
extern const int drv2605_error_map;

/* ------------------------------------------------------------------ */
/* Operating mode constants (REG_MODE bits[2:0])                      */
/* ------------------------------------------------------------------ */

#define DRV2605_MODE_INTTRIG     0x00  /* Internal trigger (play() starts) */
#define DRV2605_MODE_EXTTRIGEDGE 0x01  /* External edge trigger             */
#define DRV2605_MODE_EXTTRIGLVL  0x02  /* External level trigger            */
#define DRV2605_MODE_PWMANALOG   0x03  /* PWM / analog input                */
#define DRV2605_MODE_AUDIOVIBE   0x04  /* Audio-to-vibration                */
#define DRV2605_MODE_REALTIME    0x05  /* Real-time playback (RTP)          */
#define DRV2605_MODE_DIAGNOS     0x06  /* Diagnostics                       */
#define DRV2605_MODE_AUTOCAL     0x07  /* Auto-calibration                  */

/* ------------------------------------------------------------------ */
/* Waveform library constants (REG_LIBRARY bits[2:0])                 */
/* ------------------------------------------------------------------ */

#define DRV2605_LIBRARY_EMPTY   0x00
#define DRV2605_LIBRARY_TS2200A 0x01  /* Default for ERM */
#define DRV2605_LIBRARY_TS2200B 0x02
#define DRV2605_LIBRARY_TS2200C 0x03
#define DRV2605_LIBRARY_TS2200D 0x04
#define DRV2605_LIBRARY_TS2200E 0x05
#define DRV2605_LIBRARY_LRA     0x06

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * Initialise the DRV2605 on I2C0 (SDA=GPIO10, SCL=GPIO11).
 * Configures ERM open-loop mode with internal trigger and TS2200A library.
 * Verifies the device ID (expects 3 or 7).
 */
driver_error_t *drv2605_setup(void);

/*
 * Start playback of the waveform sequence loaded into the slots.
 * Writes 1 to the GO register.
 */
driver_error_t *drv2605_play(void);

/*
 * Stop motor vibration immediately.
 * Writes 0 to the GO register.
 */
driver_error_t *drv2605_stop(void);

/*
 * Set the operating mode.
 * mode must be one of DRV2605_MODE_*.
 */
driver_error_t *drv2605_set_mode(uint8_t mode);

/*
 * Get the current operating mode.
 */
driver_error_t *drv2605_get_mode(uint8_t *mode);

/*
 * Select the waveform library.
 * library must be one of DRV2605_LIBRARY_* (0–6).
 */
driver_error_t *drv2605_set_library(uint8_t library);

/*
 * Get the current waveform library selection.
 */
driver_error_t *drv2605_get_library(uint8_t *library);

/*
 * Load an effect ID into a sequence slot.
 *
 * effect_id : 0–123  (0 = stop sequence at this slot)
 *             Bit 7 set means the slot encodes a pause (see set_pause_slot).
 * slot      : 0–7
 */
driver_error_t *drv2605_set_waveform(uint8_t slot, uint8_t effect_id);

/*
 * Load a timed pause into a sequence slot.
 *
 * slot          : 0–7
 * duration_cs   : pause duration in centiseconds (1–127, max 1.27 s)
 *                 Bit 7 is set automatically to flag it as a delay slot.
 */
driver_error_t *drv2605_set_pause(uint8_t slot, uint8_t duration_cs);

/*
 * Read the raw byte stored in a sequence slot.
 * If bit 7 is clear it is an effect ID; if set it is a pause.
 */
driver_error_t *drv2605_get_slot(uint8_t slot, uint8_t *value);

/*
 * Set the Real-Time Playback (RTP) output value.
 * val : signed 8-bit in [-127, 127] or unsigned [0, 255].
 * The motor is driven continuously at this level while in MODE_REALTIME.
 */
driver_error_t *drv2605_set_realtime_value(int8_t val);

/*
 * Get the current RTP register value.
 */
driver_error_t *drv2605_get_realtime_value(int8_t *val);

/*
 * Read the raw STATUS register (0x00).
 * Bits [7:5] contain the device ID (3 = DRV2605, 7 = DRV2605L).
 */
driver_error_t *drv2605_get_status(uint8_t *status);

#endif /* DRV2605_H */
