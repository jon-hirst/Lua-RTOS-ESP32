/*
 * Lua RTOS, BMA423 accelerometer driver
 *
 * BMA423 connected on I2C0: SDA=GPIO10, SCL=GPIO11
 * Interrupt signal: GPIO14 (INT1)
 *
 * Based on MicroPython driver:
 *   Copyright (C) 2024 Salvatore Sanfilippo -- All Rights Reserved
 *   MIT License
 */

#ifndef BMA423_H
#define BMA423_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/driver.h>

/* ------------------------------------------------------------------ */
/* Error codes                                                         */
/* ------------------------------------------------------------------ */

#define BMA423_ERR_CANT_INIT   (DRIVER_EXCEPTION_BASE(BMA423_DRIVER_ID) | 0)
#define BMA423_ERR_NOT_SETUP   (DRIVER_EXCEPTION_BASE(BMA423_DRIVER_ID) | 1)
#define BMA423_ERR_INVALID_ARG (DRIVER_EXCEPTION_BASE(BMA423_DRIVER_ID) | 2)
#define BMA423_ERR_NO_CONFIG   (DRIVER_EXCEPTION_BASE(BMA423_DRIVER_ID) | 3)
#define BMA423_ERR_CONFIG_FAIL (DRIVER_EXCEPTION_BASE(BMA423_DRIVER_ID) | 4)

extern const int bma423_errors;
extern const int bma423_error_map;

/* ------------------------------------------------------------------ */
/* Range constants (g)                                                 */
/* ------------------------------------------------------------------ */

#define BMA423_RANGE_2G    2
#define BMA423_RANGE_4G    4
#define BMA423_RANGE_8G    8
#define BMA423_RANGE_16G  16

/* ------------------------------------------------------------------ */
/* Interrupt status bit masks (INT_STATUS_0)                          */
/* ------------------------------------------------------------------ */

#define BMA423_INT_STEP       0x02  /* Step counter interrupt */
#define BMA423_INT_ACTIVITY   0x04  /* Activity recognition   */
#define BMA423_INT_TILT       0x08  /* Wrist tilt             */
#define BMA423_INT_ANY_NONE   0x40  /* Any/no motion          */

/* ------------------------------------------------------------------ */
/* Interrupt callback                                                  */
/* ------------------------------------------------------------------ */

typedef void (*bma423_callback_t)(uint8_t status0, uint8_t status1);

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * Initialise BMA423 on I2C0 (SDA=GPIO10, SCL=GPIO11).
 * Performs soft reset, verifies chip ID (0x13), and configures the
 * accelerometer for 100 Hz normal-mode 2 g operation.
 */
driver_error_t *bma423_setup(void);

/*
 * Upload the binary features configuration from /spiffs/bma423conf.bin
 * into the BMA423 ASIC memory and enable the features engine.
 * Must be called after bma423_setup() before using step counting or
 * other feature-detection functions.
 */
driver_error_t *bma423_load_config(const char *path);

/*
 * Enable a named feature in the FEATURES_IN configuration area.
 * Currently supported feature names:
 *   "step-count"  — enables the pedometer.
 * bma423_load_config() must be called first.
 */
driver_error_t *bma423_enable_feature(const char *feature);

/*
 * Read the acceleration vector.
 * Values are in units of g, scaled to the current range setting.
 */
driver_error_t *bma423_get_xyz(float *x, float *y, float *z);

/*
 * Read the die temperature in degrees Celsius.
 * Sets *valid = false and *celsius = 0 if the sensor reports an
 * invalid reading (raw byte 0x80).
 */
driver_error_t *bma423_get_temperature(int *celsius, bool *valid);

/*
 * Read the step counter (4-byte little-endian from REG_STEP_COUNTER_0).
 * Returns the cumulative step count since last reset.
 */
driver_error_t *bma423_get_steps(uint32_t *steps);

/*
 * Set the accelerometer measurement range.
 * range_g must be one of BMA423_RANGE_2G / 4G / 8G / 16G.
 */
driver_error_t *bma423_set_range(uint8_t range_g);

/*
 * Get the current measurement range in g (2, 4, 8, or 16).
 */
driver_error_t *bma423_get_range(uint8_t *range_g);

/*
 * Attach a rising-edge interrupt on GPIO14 (BMA423 INT1 pin).
 * Configures INT1 for data-ready output (active-high, push-pull) and
 * latch mode.  When the interrupt fires, cb is called with the raw
 * contents of INT_STATUS_0 and INT_STATUS_1.
 */
driver_error_t *bma423_enable_interrupt(bma423_callback_t cb);

/*
 * Detach the GPIO14 interrupt and stop the deferred callback task.
 */
driver_error_t *bma423_disable_interrupt(void);

/*
 * Read both interrupt status registers.
 * Clears latched interrupts when latch mode is active.
 */
driver_error_t *bma423_get_int_status(uint8_t *status0, uint8_t *status1);

#endif /* BMA423_H */
