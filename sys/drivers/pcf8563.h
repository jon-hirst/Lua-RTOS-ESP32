/*
 * Lua RTOS, PCF8563 real-time clock driver
 *
 * PCF8563 connected on I2C0: SDA=GPIO10, SCL=GPIO11, INT=GPIO17
 *
 * Based on MicroPython driver by Sebastian Wicki and Mika Tuupola.
 */

#ifndef PCF8563_H
#define PCF8563_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/driver.h>

// Error codes
#define PCF8563_ERR_CANT_INIT  (DRIVER_EXCEPTION_BASE(PCF8563_DRIVER_ID) | 0)
#define PCF8563_ERR_NOT_SETUP  (DRIVER_EXCEPTION_BASE(PCF8563_DRIVER_ID) | 1)

extern const int pcf8563_errors;
extern const int pcf8563_error_map;

// Alarm interrupt callback type
typedef void (*pcf8563_alarm_callback_t)(void *arg);

/*
 * Date/time structure.
 *   year    : 1900..2099
 *   month   : 1..12
 *   mday    : 1..31
 *   hour    : 0..23
 *   minute  : 0..59
 *   second  : 0..59
 *   weekday : 0..6
 */
typedef struct {
    int year;
    int month;
    int mday;
    int hour;
    int minute;
    int second;
    int weekday;
} pcf8563_datetime_t;

/*
 * Alarm structure.
 * Set any field to -1 to disable that match condition.
 *   hour    : 0..23 or -1
 *   minute  : 0..59 or -1
 *   mday    : 1..31 or -1
 *   weekday : 0..6  or -1
 */
typedef struct {
    int hour;
    int minute;
    int mday;
    int weekday;
} pcf8563_alarm_t;

/*
 * Initialise the PCF8563. Configures I2C0 (SDA=GPIO10, SCL=GPIO11),
 * clears the control registers, and optionally enables the alarm
 * interrupt on GPIO17.
 */
driver_error_t *pcf8563_setup(bool alarm_irq);

/* Get the current date and time. */
driver_error_t *pcf8563_get_datetime(pcf8563_datetime_t *dt);

/* Set the date and time. */
driver_error_t *pcf8563_set_datetime(const pcf8563_datetime_t *dt);

/* Get the currently programmed alarm. */
driver_error_t *pcf8563_get_alarm(pcf8563_alarm_t *alarm);

/* Program an alarm. */
driver_error_t *pcf8563_set_alarm(const pcf8563_alarm_t *alarm);

/*
 * Check whether the alarm flag (AF) is set.
 * If clear is true, the AF bit is cleared afterwards (timer flag is
 * preserved, matching the behaviour of the reference Python driver).
 */
driver_error_t *pcf8563_alarm_active(bool *active, bool clear);

/*
 * Register a callback that is invoked (from a FreeRTOS task) each time
 * the INT line is asserted. Pass NULL to deregister.
 */
driver_error_t *pcf8563_register_alarm_callback(pcf8563_alarm_callback_t callback, void *arg);

#endif /* PCF8563_H */
