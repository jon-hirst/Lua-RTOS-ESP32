/*
 * Lua RTOS, FT6x36 capacitive touch controller driver
 *
 * FT6x36 connected on I2C1: SDA=GPIO39, SCL=GPIO40
 *
 * Based on CircuitPython driver:
 *   SPDX-FileCopyrightText: 2022 lbuque
 *   SPDX-License-Identifier: MIT
 */

#ifndef FT6X36_H
#define FT6X36_H

#include <stdint.h>
#include <sys/driver.h>

/* ------------------------------------------------------------------ */
/* Error codes                                                         */
/* ------------------------------------------------------------------ */

#define FT6X36_ERR_CANT_INIT   (DRIVER_EXCEPTION_BASE(FT6X36_DRIVER_ID) | 0)
#define FT6X36_ERR_NOT_SETUP   (DRIVER_EXCEPTION_BASE(FT6X36_DRIVER_ID) | 1)
#define FT6X36_ERR_INVALID_ARG (DRIVER_EXCEPTION_BASE(FT6X36_DRIVER_ID) | 2)

extern const int ft6x36_errors;
extern const int ft6x36_error_map;

/* ------------------------------------------------------------------ */
/* Rotation constants                                                  */
/* ------------------------------------------------------------------ */

#define FT6X36_PORTRAIT            0
#define FT6X36_LANDSCAPE           1
#define FT6X36_PORTRAIT_INVERTED   2
#define FT6X36_LANDSCAPE_INVERTED  3

/* ------------------------------------------------------------------ */
/* Gesture constants                                                   */
/* ------------------------------------------------------------------ */

#define FT6X36_GESTURE_NONE        0
#define FT6X36_GESTURE_MOVE_UP     1
#define FT6X36_GESTURE_MOVE_LEFT   2
#define FT6X36_GESTURE_MOVE_DOWN   3
#define FT6X36_GESTURE_MOVE_RIGHT  4
#define FT6X36_GESTURE_ZOOM_IN     5
#define FT6X36_GESTURE_ZOOM_OUT    6

/* ------------------------------------------------------------------ */
/* Interrupt mode constants                                            */
/* ------------------------------------------------------------------ */

#define FT6X36_POLLING_MODE  0x00
#define FT6X36_TRIGGER_MODE  0x01

/* ------------------------------------------------------------------ */
/* Touch point                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t x;       /* X coordinate (rotation-adjusted) */
    uint16_t y;       /* Y coordinate (rotation-adjusted) */
    uint8_t  weight;  /* Touch weight / pressure           */
    uint8_t  area;    /* Touch area (lower 4 bits)         */
} ft6x36_point_t;

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * Initialise the FT6x36 on I2C1 (SDA=GPIO39, SCL=GPIO40).
 * width and height are the screen dimensions used for rotation remapping.
 */
driver_error_t *ft6x36_setup(uint16_t width, uint16_t height);

/* Read the decoded gesture identifier (FT6X36_GESTURE_*). */
driver_error_t *ft6x36_get_gesture(uint8_t *gesture);

/*
 * Read up to two touch points.
 * *num_points is set to 0, 1, or 2.
 * points[0] and points[1] are populated for each valid touch.
 */
driver_error_t *ft6x36_get_positions(uint8_t *num_points, ft6x36_point_t points[2]);

/* Get / set screen rotation (FT6X36_PORTRAIT etc.). */
driver_error_t *ft6x36_set_rotation(uint8_t rotation);
driver_error_t *ft6x36_get_rotation(uint8_t *rotation);

/* Touch detection threshold (TH_GROUP register). */
driver_error_t *ft6x36_get_threshold(uint8_t *val);
driver_error_t *ft6x36_set_threshold(uint8_t val);

/* Time before switching from Active to Monitor mode (seconds). */
driver_error_t *ft6x36_get_monitor_time(uint8_t *val);
driver_error_t *ft6x36_set_monitor_time(uint8_t val);

/* Report rate in Active mode (ms). */
driver_error_t *ft6x36_get_active_period(uint8_t *val);
driver_error_t *ft6x36_set_active_period(uint8_t val);

/* Report rate in Monitor mode (ms). */
driver_error_t *ft6x36_get_monitor_period(uint8_t *val);
driver_error_t *ft6x36_set_monitor_period(uint8_t val);

/* 16-bit library version (read-only). */
driver_error_t *ft6x36_get_lib_version(uint16_t *version);

/* Firmware version byte (read-only). */
driver_error_t *ft6x36_get_fw_version(uint8_t *version);

/* Interrupt mode (FT6X36_POLLING_MODE or FT6X36_TRIGGER_MODE). */
driver_error_t *ft6x36_get_int_mode(uint8_t *mode);
driver_error_t *ft6x36_set_int_mode(uint8_t mode);

/* Current power mode. */
driver_error_t *ft6x36_get_power_mode(uint8_t *mode);
driver_error_t *ft6x36_set_power_mode(uint8_t mode);

/* Vendor / chip ID (CIPHER register). */
driver_error_t *ft6x36_get_vendor_id(uint8_t *id);

/* FocalTech panel ID (read-only). */
driver_error_t *ft6x36_get_panel_id(uint8_t *id);

#endif /* FT6X36_H */
