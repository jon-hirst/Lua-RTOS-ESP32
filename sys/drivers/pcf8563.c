/*
 * Lua RTOS, PCF8563 real-time clock driver
 *
 * PCF8563 connected on I2C0: SDA=GPIO10, SCL=GPIO11, INT=GPIO17
 *
 * Based on MicroPython driver:
 *   Copyright (c) 2020 Sebastian Wicki
 *   Copyright (c) 2020 Mika Tuupola
 *   https://github.com/tuupola/pcf8563
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions: The above copyright notice and this
 * permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "luartos.h"

#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <sys/driver.h>

#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <drivers/pcf8563.h>

/* ------------------------------------------------------------------ */
/* Hardware configuration                                              */
/* ------------------------------------------------------------------ */

#define PCF8563_I2C_ADDR    0x51
#define PCF8563_I2C_UNIT    0
#define PCF8563_SDA_GPIO    10
#define PCF8563_SCL_GPIO    11
#define PCF8563_INT_GPIO    17
#define PCF8563_I2C_SPEED   400000  /* 400 kHz */

/* ------------------------------------------------------------------ */
/* Register map                                                        */
/* ------------------------------------------------------------------ */

#define REG_CTRL1       0x00
#define REG_CTRL2       0x01
#define REG_SECONDS     0x02
#define REG_MINUTES     0x03
#define REG_HOURS       0x04
#define REG_DAY         0x05
#define REG_WEEKDAY     0x06
#define REG_MONTH       0x07
#define REG_YEAR        0x08
#define REG_ALARM_MIN   0x09
#define REG_ALARM_HOUR  0x0A
#define REG_ALARM_DAY   0x0B
#define REG_ALARM_WDAY  0x0C
#define REG_TIMER_CTRL  0x0E
#define REG_TIMER       0x0F

/* Control/Status 1 bits */
#define CTRL1_TEST1     0x80
#define CTRL1_STOP      0x20
#define CTRL1_TESTC     0x08

/* Control/Status 2 bits */
#define CTRL2_TI_TP     0x10
#define CTRL2_AF        0x08    /* Alarm Flag */
#define CTRL2_TF        0x04    /* Timer Flag */
#define CTRL2_AIE       0x02    /* Alarm Interrupt Enable */
#define CTRL2_TIE       0x01    /* Timer Interrupt Enable */

/* Month register century bit */
#define CENTURY_BIT     0x80

/* Alarm disable bit (set in alarm registers to ignore that field) */
#define ALARM_DISABLE   0x80

/* ------------------------------------------------------------------ */
/* Driver registration                                                 */
/* ------------------------------------------------------------------ */

DRIVER_REGISTER_BEGIN(PCF8563, pcf8563, 0, NULL, NULL);
    DRIVER_REGISTER_ERROR(PCF8563, pcf8563, CantInit, "can't initialize", PCF8563_ERR_CANT_INIT);
    DRIVER_REGISTER_ERROR(PCF8563, pcf8563, NotSetup,  "not setup",        PCF8563_ERR_NOT_SETUP);
DRIVER_REGISTER_END(PCF8563, pcf8563, 0, NULL, NULL);

/* ------------------------------------------------------------------ */
/* Driver state                                                        */
/* ------------------------------------------------------------------ */

static int              i2c_device        = -1;
static TaskHandle_t     alarm_task_handle = NULL;
static pcf8563_alarm_callback_t alarm_cb  = NULL;
static void            *alarm_cb_arg      = NULL;

/* ------------------------------------------------------------------ */
/* BCD helpers                                                         */
/* ------------------------------------------------------------------ */

static inline uint8_t dec2bcd(int val) {
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

static inline int bcd2dec(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/* ------------------------------------------------------------------ */
/* Low-level I2C helpers                                               */
/* ------------------------------------------------------------------ */

/* Write a single register byte. */
static driver_error_t *pcf8563_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_write(i2c_device, buf, 2);
}

/* Read len consecutive bytes starting at reg. */
static driver_error_t *pcf8563_read_regs(uint8_t reg, uint8_t *data, int len) {
    return i2c_write_read(i2c_device, &reg, 1, data, len);
}

/* Write len consecutive bytes starting at reg (uses i2c_multiple_write to
 * avoid a heap allocation). */
static driver_error_t *pcf8563_write_regs(uint8_t reg, const uint8_t *data, int len) {
    return i2c_multiple_write(i2c_device, &reg, 1, (uint8_t *)data, len);
}

/* ------------------------------------------------------------------ */
/* Alarm interrupt handling (deferred to a FreeRTOS task)             */
/* ------------------------------------------------------------------ */

static void pcf8563_alarm_task(void *arg) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (alarm_cb) {
            alarm_cb(alarm_cb_arg);
        }
    }
}

/* GPIO ISR — the PCF8563 INT pin is open-drain, active-low. */
static void IRAM_ATTR pcf8563_isr(void *arg) {
    BaseType_t higher_priority_awoken = pdFALSE;
    if (alarm_task_handle) {
        vTaskNotifyGiveFromISR(alarm_task_handle, &higher_priority_awoken);
        if (higher_priority_awoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

driver_error_t *pcf8563_setup(bool alarm_irq) {
    driver_error_t *error;

    if (i2c_device >= 0) {
        return NULL;  /* Already initialised */
    }

    /* Set I2C pin map then attach to the bus */
    if ((error = i2c_pin_map(PCF8563_I2C_UNIT, PCF8563_SDA_GPIO, PCF8563_SCL_GPIO))) {
        return error;
    }
    if ((error = i2c_attach(PCF8563_I2C_UNIT, I2C_MASTER, PCF8563_I2C_SPEED,
                             0, PCF8563_I2C_ADDR, &i2c_device))) {
        return error;
    }

    /* Clear Control/Status1 — normal operating mode, RTC running */
    if ((error = pcf8563_write_reg(REG_CTRL1, 0x00))) {
        return error;
    }

    /* Control/Status2: enable alarm interrupt if requested */
    uint8_t ctrl2 = 0x00;
    if (alarm_irq) {
        ctrl2 |= CTRL2_AIE;
    }
    if ((error = pcf8563_write_reg(REG_CTRL2, ctrl2))) {
        return error;
    }

    if (alarm_irq) {
        /* Create the deferred-interrupt task */
        BaseType_t ret = xTaskCreatePinnedToCore(
            pcf8563_alarm_task,
            "pcf8563_alm",
            CONFIG_LUA_RTOS_LUA_THREAD_STACK_SIZE,
            NULL,
            CONFIG_LUA_RTOS_LUA_THREAD_PRIORITY,
            &alarm_task_handle,
            xPortGetCoreID()
        );
        if (ret != pdPASS) {
            return driver_error(PCF8563_DRIVER, PCF8563_ERR_CANT_INIT,
                                "can't create alarm task");
        }

        /* Configure INT pin: input with pull-up (open-drain, active-low) */
        if ((error = gpio_pin_input(PCF8563_INT_GPIO)))  return error;
        if ((error = gpio_pin_pullup(PCF8563_INT_GPIO))) return error;

        /* Attach falling-edge ISR */
        if ((error = gpio_isr_attach(PCF8563_INT_GPIO, pcf8563_isr,
                                     GPIO_INTR_NEGEDGE, NULL))) {
            return error;
        }
    }

    return NULL;
}

driver_error_t *pcf8563_get_datetime(pcf8563_datetime_t *dt) {
    driver_error_t *error;
    uint8_t data[7];

    if (i2c_device < 0) {
        return driver_error(PCF8563_DRIVER, PCF8563_ERR_NOT_SETUP, NULL);
    }

    if ((error = pcf8563_read_regs(REG_SECONDS, data, 7))) {
        return error;
    }

    /* seconds  : bits 6..0  (bit 7 = VL, low-voltage flag, ignored here) */
    dt->second  = bcd2dec(data[0] & 0x7F);
    /* minutes  : bits 6..0 */
    dt->minute  = bcd2dec(data[1] & 0x7F);
    /* hours    : bits 5..0 */
    dt->hour    = bcd2dec(data[2] & 0x3F);
    /* day      : bits 5..0 */
    dt->mday    = bcd2dec(data[3] & 0x3F);
    /* weekday  : bits 2..0 */
    dt->weekday = bcd2dec(data[4] & 0x07);
    /* month    : bits 4..0; bit 7 = century flag */
    dt->month   = bcd2dec(data[5] & 0x1F);
    /* year: BCD value is years-since-century-start.
     * Century flag set  → add 100 (i.e. year is 2000-based)
     * Century flag clear → year is 1900-based
     * Final result: BCD_year + century_offset + 1900 */
    int century  = (data[5] & CENTURY_BIT) ? 100 : 0;
    dt->year     = bcd2dec(data[6] & 0xFF) + century + 1900;

    return NULL;
}

driver_error_t *pcf8563_set_datetime(const pcf8563_datetime_t *dt) {
    uint8_t data[7];

    if (i2c_device < 0) {
        return driver_error(PCF8563_DRIVER, PCF8563_ERR_NOT_SETUP, NULL);
    }

    data[0] = dec2bcd(dt->second)  & 0x7F;
    data[1] = dec2bcd(dt->minute)  & 0x7F;
    data[2] = dec2bcd(dt->hour)    & 0x3F;
    data[3] = dec2bcd(dt->mday)    & 0x3F;
    data[4] = dec2bcd(dt->weekday) & 0x07;
    data[5] = dec2bcd(dt->month)   & 0x1F;
    /* Set century bit for years 2000..2099 */
    if (dt->year >= 2000) {
        data[5] |= CENTURY_BIT;
    }
    /* Store the two-digit year within the century */
    data[6] = dec2bcd(dt->year % 100) & 0xFF;

    return pcf8563_write_regs(REG_SECONDS, data, 7);
}

driver_error_t *pcf8563_get_alarm(pcf8563_alarm_t *alarm) {
    driver_error_t *error;
    uint8_t data[4];

    if (i2c_device < 0) {
        return driver_error(PCF8563_DRIVER, PCF8563_ERR_NOT_SETUP, NULL);
    }

    if ((error = pcf8563_read_regs(REG_ALARM_MIN, data, 4))) {
        return error;
    }

    /* Bit 7 of each alarm register is the disable flag */
    alarm->minute  = (data[0] & ALARM_DISABLE) ? -1 : bcd2dec(data[0] & 0x7F);
    alarm->hour    = (data[1] & ALARM_DISABLE) ? -1 : bcd2dec(data[1] & 0x3F);
    alarm->mday    = (data[2] & ALARM_DISABLE) ? -1 : bcd2dec(data[2] & 0x3F);
    alarm->weekday = (data[3] & ALARM_DISABLE) ? -1 : bcd2dec(data[3] & 0x07);

    return NULL;
}

driver_error_t *pcf8563_set_alarm(const pcf8563_alarm_t *alarm) {
    uint8_t data[4];

    if (i2c_device < 0) {
        return driver_error(PCF8563_DRIVER, PCF8563_ERR_NOT_SETUP, NULL);
    }

    data[0] = (alarm->minute  < 0) ? ALARM_DISABLE : (dec2bcd(alarm->minute)  & 0x7F);
    data[1] = (alarm->hour    < 0) ? ALARM_DISABLE : (dec2bcd(alarm->hour)    & 0x3F);
    data[2] = (alarm->mday    < 0) ? ALARM_DISABLE : (dec2bcd(alarm->mday)    & 0x3F);
    data[3] = (alarm->weekday < 0) ? ALARM_DISABLE : (dec2bcd(alarm->weekday) & 0x07);

    return pcf8563_write_regs(REG_ALARM_MIN, data, 4);
}

driver_error_t *pcf8563_alarm_active(bool *active, bool clear) {
    driver_error_t *error;
    uint8_t ctrl2;

    if (i2c_device < 0) {
        return driver_error(PCF8563_DRIVER, PCF8563_ERR_NOT_SETUP, NULL);
    }

    if ((error = pcf8563_read_regs(REG_CTRL2, &ctrl2, 1))) {
        return error;
    }

    *active = (ctrl2 & CTRL2_AF) != 0;

    if (clear) {
        /* Clear AF; set TF to preserve the timer flag (mirrors Python driver
         * behaviour: data[0] |= TF ensures the timer flag is not accidentally
         * cleared if AF and TF happen to share the same write). */
        ctrl2 &= ~CTRL2_AF;
        ctrl2 |=  CTRL2_TF;
        if ((error = pcf8563_write_reg(REG_CTRL2, ctrl2))) {
            return error;
        }
    }

    return NULL;
}

driver_error_t *pcf8563_register_alarm_callback(pcf8563_alarm_callback_t callback,
                                                  void *arg) {
    portDISABLE_INTERRUPTS();
    alarm_cb     = callback;
    alarm_cb_arg = arg;
    portENABLE_INTERRUPTS();
    return NULL;
}
