/*
 * Lua RTOS, BMA423 accelerometer driver
 *
 * BMA423 connected on I2C0: SDA=GPIO10, SCL=GPIO11
 * Interrupt signal: GPIO14 (INT1)
 *
 * Based on MicroPython driver:
 *   Copyright (C) 2024 Salvatore Sanfilippo -- All Rights Reserved
 *   MIT License
 *
 * Written reading the specification at:
 *   https://www.mouser.com/datasheet/2/783/BST-BMA423-DS000-1509600.pdf
 */

#include "luartos.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <sys/driver.h>
#include <drivers/i2c.h>
#include <drivers/gpio.h>
#include <drivers/bma423.h>

/* ------------------------------------------------------------------ */
/* Hardware configuration                                              */
/* ------------------------------------------------------------------ */

#define BMA423_I2C_UNIT    CONFIG_LUA_RTOS_BMA423_I2C_UNIT
#define BMA423_SDA_GPIO    CONFIG_LUA_RTOS_BMA423_SDA_GPIO
#define BMA423_SCL_GPIO    CONFIG_LUA_RTOS_BMA423_SCL_GPIO
#define BMA423_I2C_SPEED   CONFIG_LUA_RTOS_BMA423_I2C_SPEED
#define BMA423_INT_GPIO    CONFIG_LUA_RTOS_BMA423_INT_GPIO

#define BMA423_CHIP_ID     0x13

#define BMA423_CONFIG_CHUNK 8      /* config blob is written 8 bytes at a time */
#define BMA423_FEATURES_IN_SIZE 70 /* size of FEATURES_IN configuration area */

/* ------------------------------------------------------------------ */
/* Register map                                                        */
/* ------------------------------------------------------------------ */

#define REG_CHIP_ID         0x00
#define REG_INT_STATUS_0    0x1C
#define REG_INT_STATUS_1    0x1D
#define REG_STEP_COUNTER_0  0x1E
#define REG_TEMPERATURE     0x22
#define REG_INTERNAL_STATUS 0x2A
#define REG_ACC_DATA        0x12   /* 6 bytes: xl,xh,yl,yh,zl,zh */
#define REG_ACC_CONF        0x40
#define REG_ACC_RANGE       0x41
#define REG_INT1_IO_CTRL    0x53
#define REG_INT_LATCH       0x55
#define REG_INT1_MAP        0x56
#define REG_INT_MAP_DATA    0x58
#define REG_INIT_CTRL       0x59
#define REG_ASIC_LSB        0x5B   /* ASIC memory address LSB (bits 3:0 = word_addr[3:0]) */
#define REG_ASIC_MSB        0x5C   /* ASIC memory address MSB (bits 7:0 = word_addr[11:4]) */
#define REG_ASIC_DATA       0x5E   /* ASIC memory data port */
#define REG_PWR_CONF        0x7C
#define REG_PWR_CTL         0x7D
#define REG_CMD             0x7E

/* REG_CMD values */
#define CMD_SOFTRESET       0xB6

/* REG_ACC_CONF bits */
#define ACC_CONF_PERF_MODE  0x80   /* bit 7: continuous sampling */
#define ACC_CONF_OSR_MASK   0x70   /* bits 6:4: OSR / averaging */
#define ACC_CONF_ODR_MASK   0x0F   /* bits 3:0: output data rate */
#define ACC_CONF_ODR_100HZ  0x08   /* 100 Hz */
#define ACC_CONF_OSR_NORMAL 0x20   /* normal mode (OSR=2, bits [6:4]=2) */

/* REG_PWR_CTL bits */
#define PWR_CTL_ACC_EN      0x04
#define PWR_CTL_AUX_EN      0x01

/* REG_INT1_IO_CTRL bits */
#define INT1_IO_CTRL_OUTPUT_EN  0x08
#define INT1_IO_CTRL_ACTIVE_HI  0x02

/* REG_INT_MAP_DATA: INT1 data-ready bit */
#define INT_MAP_DATA_INT1_DRDY  0x04

/* REG_INTERNAL_STATUS: init OK value */
#define INTERNAL_STATUS_INIT_OK 0x01
#define INTERNAL_STATUS_MASK    0x1F

/* ------------------------------------------------------------------ */
/* Driver registration                                                 */
/* ------------------------------------------------------------------ */

DRIVER_REGISTER_BEGIN(BMA423, bma423, 0, NULL, NULL);
    DRIVER_REGISTER_ERROR(BMA423, bma423, CantInit,   "can't initialize",       BMA423_ERR_CANT_INIT);
    DRIVER_REGISTER_ERROR(BMA423, bma423, NotSetup,   "not setup",              BMA423_ERR_NOT_SETUP);
    DRIVER_REGISTER_ERROR(BMA423, bma423, InvalidArg, "invalid argument",        BMA423_ERR_INVALID_ARG);
    DRIVER_REGISTER_ERROR(BMA423, bma423, NoConfig,   "bma423conf.bin not found",BMA423_ERR_NO_CONFIG);
    DRIVER_REGISTER_ERROR(BMA423, bma423, ConfigFail, "features init timeout",   BMA423_ERR_CONFIG_FAIL);
DRIVER_REGISTER_END(BMA423, bma423, 0, NULL, NULL);

/* ------------------------------------------------------------------ */
/* Driver state                                                        */
/* ------------------------------------------------------------------ */

static int               i2c_device   = -1;
static uint8_t           acc_range_g  = 2;    /* current range in g */
static bma423_callback_t user_callback = NULL;
static QueueHandle_t     irq_queue    = NULL;
static TaskHandle_t      irq_task     = NULL;

/* ------------------------------------------------------------------ */
/* Low-level I2C helpers                                               */
/* ------------------------------------------------------------------ */

static driver_error_t *bma423_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_write(i2c_device, buf, 2);
}

static driver_error_t *bma423_read_reg(uint8_t reg, uint8_t *val) {
    return i2c_write_read(i2c_device, &reg, 1, val, 1);
}

static driver_error_t *bma423_read_regs(uint8_t reg, uint8_t *buf, int len) {
    return i2c_write_read(i2c_device, &reg, 1, buf, len);
}

/* ------------------------------------------------------------------ */
/* ASIC config memory helpers                                          */
/* ------------------------------------------------------------------ */

/* Set the ASIC memory address pointer to byte offset idx. */
static driver_error_t *bma423_set_asic_addr(uint16_t idx) {
    driver_error_t *error;
    uint16_t word_idx = idx / 2;
    if ((error = bma423_write_reg(REG_ASIC_LSB, (uint8_t)(word_idx & 0x0F)))) return error;
    return bma423_write_reg(REG_ASIC_MSB, (uint8_t)(word_idx >> 4));
}

/* Write one BMA423_CONFIG_CHUNK-byte block to ASIC memory at byte offset idx. */
static driver_error_t *bma423_write_config_mem(uint16_t idx, const uint8_t *data) {
    driver_error_t *error;
    uint8_t buf[1 + BMA423_CONFIG_CHUNK];

    if ((error = bma423_set_asic_addr(idx))) return error;
    buf[0] = REG_ASIC_DATA;
    memcpy(buf + 1, data, BMA423_CONFIG_CHUNK);
    return i2c_write(i2c_device, buf, sizeof(buf));
}

/* Read count bytes from ASIC memory starting at byte offset idx. */
static driver_error_t *bma423_read_config_mem(uint16_t idx, uint8_t *buf, uint16_t count) {
    driver_error_t *error;
    uint8_t reg = REG_ASIC_DATA;
    if ((error = bma423_set_asic_addr(idx))) return error;
    return i2c_write_read(i2c_device, &reg, 1, buf, count);
}

/* ------------------------------------------------------------------ */
/* Interrupt task                                                      */
/* ------------------------------------------------------------------ */

static void bma423_irq_task(void *arg) {
    uint8_t dummy;
    uint8_t s0, s1;

    for (;;) {
        xQueueReceive(irq_queue, &dummy, portMAX_DELAY);

        s0 = 0;
        s1 = 0;
        bma423_read_reg(REG_INT_STATUS_0, &s0);
        bma423_read_reg(REG_INT_STATUS_1, &s1);

        if (user_callback) {
            user_callback(s0, s1);
        }
    }
}

static void IRAM_ATTR bma423_isr(void *arg) {
    uint8_t dummy = 0;
    xQueueSendFromISR(irq_queue, &dummy, NULL);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

driver_error_t *bma423_setup(void) {
    driver_error_t *error;

    if (i2c_device >= 0) {
        return NULL;   /* Already initialised */
    }

    /* Configure I2C bus pins */
    if ((error = i2c_pin_map(BMA423_I2C_UNIT, BMA423_SDA_GPIO, BMA423_SCL_GPIO))) {
        return error;
    }

    /*
     * The BMA423 I2C address depends on the SDO pin:
     *   SDO = GND → 0x18
     *   SDO = VCC → 0x19
     * Probe both addresses by reading the chip-ID register.
     */
    static const uint8_t candidate_addrs[] = { 0x18, 0x19 };
    for (int a = 0; a < 2; a++) {
        int candidate;
        if (i2c_attach(BMA423_I2C_UNIT, I2C_MASTER, BMA423_I2C_SPEED,
                       0, candidate_addrs[a], &candidate)) {
            continue;
        }
        uint8_t reg = REG_CHIP_ID;
        uint8_t chip_id = 0;
        driver_error_t *probe = i2c_write_read(candidate, &reg, 1, &chip_id, 1);
        if (probe == NULL && chip_id == BMA423_CHIP_ID) {
            i2c_device = candidate;
            break;
        }
        /* probe error is a small one-time allocation; discard it */
    }
    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_CANT_INIT,
                            "device not found at 0x18 or 0x19");
    }

    /* Soft reset */
    if ((error = bma423_write_reg(REG_CMD, CMD_SOFTRESET))) return error;
    vTaskDelay(pdMS_TO_TICKS(1000));  /* Datasheet: wait after reset */

    /* Confirm chip ID survives reset */
    uint8_t chip_id;
    if ((error = bma423_read_reg(REG_CHIP_ID, &chip_id))) return error;
    if (chip_id != BMA423_CHIP_ID) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_CANT_INIT,
                            "unexpected chip ID after reset");
    }

    /* Disable advanced power save, disable FIFO self-wakeup */
    if ((error = bma423_write_reg(REG_PWR_CONF, 0x00))) return error;
    vTaskDelay(pdMS_TO_TICKS(1));

    /* Enable accelerometer, disable aux sensor */
    if ((error = bma423_write_reg(REG_PWR_CTL, PWR_CTL_ACC_EN))) return error;

    /* Performance mode on, OSR=normal, ODR=100 Hz */
    if ((error = bma423_write_reg(REG_ACC_CONF,
                                  ACC_CONF_PERF_MODE |
                                  ACC_CONF_OSR_NORMAL |
                                  ACC_CONF_ODR_100HZ))) {
        return error;
    }

    /* Default range: 2 g */
    if ((error = bma423_write_reg(REG_ACC_RANGE, 0x00))) return error;
    acc_range_g = 2;

    return NULL;
}

driver_error_t *bma423_load_config(const char *path) {
    driver_error_t *error;

    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }

    /* Save current power config, disable advanced power save */
    uint8_t saved_pwr_conf;
    if ((error = bma423_read_reg(REG_PWR_CONF, &saved_pwr_conf))) return error;
    if ((error = bma423_write_reg(REG_PWR_CONF, 0x00))) return error;
    vTaskDelay(pdMS_TO_TICKS(1));

    /* Prepare ASIC for configuration load */
    if ((error = bma423_write_reg(REG_INIT_CTRL, 0x00))) return error;

    /* Open and stream the binary config blob */
    FILE *f = fopen(path, "rb");
    if (!f) {
        bma423_write_reg(REG_PWR_CONF, saved_pwr_conf);
        return driver_error(BMA423_DRIVER, BMA423_ERR_NO_CONFIG, NULL);
    }

    uint8_t chunk[BMA423_CONFIG_CHUNK];
    uint16_t idx = 0;
    while (fread(chunk, 1, BMA423_CONFIG_CHUNK, f) == BMA423_CONFIG_CHUNK) {
        if ((error = bma423_write_config_mem(idx, chunk))) {
            fclose(f);
            bma423_write_reg(REG_PWR_CONF, saved_pwr_conf);
            return error;
        }
        idx += BMA423_CONFIG_CHUNK;
    }
    fclose(f);

    /* Enable features engine */
    if ((error = bma423_write_reg(REG_INIT_CTRL, 0x01))) return error;
    vTaskDelay(pdMS_TO_TICKS(140));

    /* Poll until ASIC reports initialisation complete */
    for (int attempts = 0; attempts < 20; attempts++) {
        uint8_t status;
        if ((error = bma423_read_reg(REG_INTERNAL_STATUS, &status))) {
            bma423_write_reg(REG_PWR_CONF, saved_pwr_conf);
            return error;
        }
        if ((status & INTERNAL_STATUS_MASK) == INTERNAL_STATUS_INIT_OK) {
            bma423_write_reg(REG_PWR_CONF, saved_pwr_conf);
            return NULL;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    bma423_write_reg(REG_PWR_CONF, saved_pwr_conf);
    return driver_error(BMA423_DRIVER, BMA423_ERR_CONFIG_FAIL, NULL);
}

driver_error_t *bma423_enable_feature(const char *feature) {
    driver_error_t *error;

    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }
    if (!feature) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_INVALID_ARG, NULL);
    }

    /* Read the 70-byte FEATURES_IN area from ASIC memory offset 0 */
    uint8_t features_in[BMA423_FEATURES_IN_SIZE];
    if ((error = bma423_read_config_mem(0, features_in, BMA423_FEATURES_IN_SIZE))) {
        return error;
    }

    if (strcmp(feature, "step-count") == 0) {
        features_in[0x3B] |= 0x10;   /* Enable step counter */
    } else {
        return driver_error(BMA423_DRIVER, BMA423_ERR_INVALID_ARG, "unknown feature");
    }

    /* Write back the modified FEATURES_IN area */
    uint8_t buf[1 + BMA423_FEATURES_IN_SIZE];
    buf[0] = REG_ASIC_DATA;
    memcpy(buf + 1, features_in, BMA423_FEATURES_IN_SIZE);
    if ((error = bma423_set_asic_addr(0))) return error;
    return i2c_write(i2c_device, buf, sizeof(buf));
}

/* Convert a raw 12-bit two's-complement value (packed in two bytes:
 * low nibble in byte[0] bits[7:4], high byte in byte[1]) to a signed
 * float in units of g, given the current range. */
static float bma423_raw_to_g(uint8_t lo, uint8_t hi) {
    int16_t raw = (int16_t)((lo >> 4) | ((uint16_t)hi << 4));
    /* Sign-extend from 12 bits */
    if (raw & 0x800) {
        raw = (int16_t)(raw | (int16_t)0xF000);
    }
    return (float)acc_range_g / 2047.0f * (float)raw;
}

driver_error_t *bma423_get_xyz(float *x, float *y, float *z) {
    driver_error_t *error;
    uint8_t raw[6];

    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }
    if ((error = bma423_read_regs(REG_ACC_DATA, raw, 6))) return error;

    *x = bma423_raw_to_g(raw[0], raw[1]);
    *y = bma423_raw_to_g(raw[2], raw[3]);
    *z = bma423_raw_to_g(raw[4], raw[5]);
    return NULL;
}

driver_error_t *bma423_get_temperature(int *celsius, bool *valid) {
    driver_error_t *error;
    uint8_t raw;

    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }
    if ((error = bma423_read_reg(REG_TEMPERATURE, &raw))) return error;

    if (raw == 0x80) {
        *valid = false;
        *celsius = 0;
    } else {
        *valid = true;
        /* Raw is a signed 8-bit value; 0 = 23 °C */
        int8_t signed_raw = (int8_t)raw;
        *celsius = 23 + (int)signed_raw;
    }
    return NULL;
}

driver_error_t *bma423_get_steps(uint32_t *steps) {
    driver_error_t *error;
    uint8_t raw[4];

    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }
    if ((error = bma423_read_regs(REG_STEP_COUNTER_0, raw, 4))) return error;

    *steps = (uint32_t)raw[0]        |
             (uint32_t)raw[1] <<  8  |
             (uint32_t)raw[2] << 16  |
             (uint32_t)raw[3] << 24;
    return NULL;
}

driver_error_t *bma423_set_range(uint8_t range_g) {
    static const uint8_t range_table[] = { 0xFF, 0xFF, 0, 0xFF, 1, 0xFF, 0xFF, 0xFF, 2,
                                           0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 3 };
    driver_error_t *error;

    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }
    if (range_g > 16 || range_table[range_g] == 0xFF) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_INVALID_ARG, "range: 2,4,8,16");
    }
    if ((error = bma423_write_reg(REG_ACC_RANGE, range_table[range_g]))) return error;
    acc_range_g = range_g;
    return NULL;
}

driver_error_t *bma423_get_range(uint8_t *range_g) {
    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }
    *range_g = acc_range_g;
    return NULL;
}

driver_error_t *bma423_enable_interrupt(bma423_callback_t cb) {
    driver_error_t *error;

    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }

    user_callback = cb;

    /* Create the deferred-interrupt queue and task if not already running */
    if (irq_queue == NULL) {
        irq_queue = xQueueCreate(4, sizeof(uint8_t));
    }
    if (irq_task == NULL) {
        xTaskCreate(bma423_irq_task, "bma423_irq", 2048, NULL,
                    configMAX_PRIORITIES - 1, &irq_task);
    }

    /* Latch mode: interrupts stay asserted until status is read */
    if ((error = bma423_write_reg(REG_INT_LATCH, 0x01))) return error;

    /* Map data-ready to INT1 */
    if ((error = bma423_write_reg(REG_INT_MAP_DATA, INT_MAP_DATA_INT1_DRDY))) return error;

    /* Configure INT1 pin: output enable, active high, push-pull */
    if ((error = bma423_write_reg(REG_INT1_IO_CTRL,
                                  INT1_IO_CTRL_OUTPUT_EN | INT1_IO_CTRL_ACTIVE_HI))) {
        return error;
    }

    /* Attach rising-edge ISR on GPIO14 */
    if ((error = gpio_pin_input(BMA423_INT_GPIO))) return error;
    return gpio_isr_attach(BMA423_INT_GPIO, bma423_isr, GPIO_INTR_POSEDGE, NULL);
}

driver_error_t *bma423_disable_interrupt(void) {
    driver_error_t *error;

    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }

    if ((error = gpio_isr_detach(BMA423_INT_GPIO))) return error;

    if (irq_task != NULL) {
        vTaskDelete(irq_task);
        irq_task = NULL;
    }
    if (irq_queue != NULL) {
        vQueueDelete(irq_queue);
        irq_queue = NULL;
    }

    user_callback = NULL;
    return NULL;
}

driver_error_t *bma423_get_int_status(uint8_t *status0, uint8_t *status1) {
    driver_error_t *error;

    if (i2c_device < 0) {
        return driver_error(BMA423_DRIVER, BMA423_ERR_NOT_SETUP, NULL);
    }
    if ((error = bma423_read_reg(REG_INT_STATUS_0, status0))) return error;
    return bma423_read_reg(REG_INT_STATUS_1, status1);
}
