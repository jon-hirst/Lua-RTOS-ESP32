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

#ifndef AXP2101_H
#define AXP2101_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/driver.h>

/* ------------------------------------------------------------------ */
/* Error codes                                                         */
/* ------------------------------------------------------------------ */

#define AXP2101_ERR_CANT_INIT   (DRIVER_EXCEPTION_BASE(AXP2101_DRIVER_ID) | 0)
#define AXP2101_ERR_NOT_SETUP   (DRIVER_EXCEPTION_BASE(AXP2101_DRIVER_ID) | 1)
#define AXP2101_ERR_INVALID_ARG (DRIVER_EXCEPTION_BASE(AXP2101_DRIVER_ID) | 2)

extern const int axp2101_errors;
extern const int axp2101_error_map;

/* ------------------------------------------------------------------ */
/* Charger status values                                               */
/* ------------------------------------------------------------------ */

#define AXP2101_CHG_TRI_STATE   0   /* trickle charge  */
#define AXP2101_CHG_PRE_STATE   1   /* pre-charge      */
#define AXP2101_CHG_CC_STATE    2   /* constant current */
#define AXP2101_CHG_CV_STATE    3   /* constant voltage */
#define AXP2101_CHG_DONE_STATE  4   /* charge done     */
#define AXP2101_CHG_STOP_STATE  5   /* not charging    */

/* ------------------------------------------------------------------ */
/* Charging current register values (ICC_CHG_SET bits[4:0])           */
/* ------------------------------------------------------------------ */

#define AXP2101_CHG_CUR_100MA   4
#define AXP2101_CHG_CUR_125MA   5
#define AXP2101_CHG_CUR_150MA   6
#define AXP2101_CHG_CUR_175MA   7
#define AXP2101_CHG_CUR_200MA   8
#define AXP2101_CHG_CUR_300MA   9
#define AXP2101_CHG_CUR_400MA   10
#define AXP2101_CHG_CUR_500MA   11
#define AXP2101_CHG_CUR_600MA   12
#define AXP2101_CHG_CUR_700MA   13
#define AXP2101_CHG_CUR_800MA   14
#define AXP2101_CHG_CUR_900MA   15
#define AXP2101_CHG_CUR_1000MA  16

/* ------------------------------------------------------------------ */
/* Charge target voltage register values (CV_CHG_VOL_SET bits[2:0])   */
/* ------------------------------------------------------------------ */

#define AXP2101_CHG_VOL_4V      1
#define AXP2101_CHG_VOL_4V1     2
#define AXP2101_CHG_VOL_4V2     3
#define AXP2101_CHG_VOL_4V35    4
#define AXP2101_CHG_VOL_4V4     5

/* ------------------------------------------------------------------ */
/* VBUS voltage limit register values                                  */
/* ------------------------------------------------------------------ */

#define AXP2101_VBUS_VOL_LIM_3V88   0
#define AXP2101_VBUS_VOL_LIM_3V96   1
#define AXP2101_VBUS_VOL_LIM_4V04   2
#define AXP2101_VBUS_VOL_LIM_4V12   3
#define AXP2101_VBUS_VOL_LIM_4V20   4
#define AXP2101_VBUS_VOL_LIM_4V28   5
#define AXP2101_VBUS_VOL_LIM_4V36   6
#define AXP2101_VBUS_VOL_LIM_4V44   7
#define AXP2101_VBUS_VOL_LIM_4V52   8
#define AXP2101_VBUS_VOL_LIM_4V60   9
#define AXP2101_VBUS_VOL_LIM_4V68   10
#define AXP2101_VBUS_VOL_LIM_4V76   11
#define AXP2101_VBUS_VOL_LIM_4V84   12
#define AXP2101_VBUS_VOL_LIM_4V92   13
#define AXP2101_VBUS_VOL_LIM_5V     14
#define AXP2101_VBUS_VOL_LIM_5V08   15

/* ------------------------------------------------------------------ */
/* VBUS current limit register values                                  */
/* ------------------------------------------------------------------ */

#define AXP2101_VBUS_CUR_LIM_100MA  0
#define AXP2101_VBUS_CUR_LIM_500MA  1
#define AXP2101_VBUS_CUR_LIM_900MA  2
#define AXP2101_VBUS_CUR_LIM_1000MA 3
#define AXP2101_VBUS_CUR_LIM_1500MA 4
#define AXP2101_VBUS_CUR_LIM_2000MA 5

/* ------------------------------------------------------------------ */
/* Charging LED mode values                                            */
/* ------------------------------------------------------------------ */

#define AXP2101_CHG_LED_OFF         0
#define AXP2101_CHG_LED_BLINK_1HZ   1
#define AXP2101_CHG_LED_BLINK_4HZ   2
#define AXP2101_CHG_LED_ON          3
#define AXP2101_CHG_LED_CTRL_CHG    4   /* hardware-controlled by charger */

/* ------------------------------------------------------------------ */
/* Power-on press time values                                          */
/* ------------------------------------------------------------------ */

#define AXP2101_POWERON_128MS   0
#define AXP2101_POWERON_512MS   1
#define AXP2101_POWERON_1S      2
#define AXP2101_POWERON_2S      3

/* ------------------------------------------------------------------ */
/* Power-off press time values                                         */
/* ------------------------------------------------------------------ */

#define AXP2101_POWEROFF_4S     0
#define AXP2101_POWEROFF_6S     1
#define AXP2101_POWEROFF_8S     2
#define AXP2101_POWEROFF_10S    3

/* ------------------------------------------------------------------ */
/* ADC channel enable bits (REG_ADC_CH_CTRL 0x30)                     */
/* ------------------------------------------------------------------ */

#define AXP2101_ADC_BAT_VOL     (1 << 0)
#define AXP2101_ADC_TS_PIN      (1 << 1)
#define AXP2101_ADC_VBUS_VOL    (1 << 2)
#define AXP2101_ADC_SYS_VOL     (1 << 3)
#define AXP2101_ADC_DIE_TEMP    (1 << 4)
#define AXP2101_ADC_GPADC       (1 << 5)
#define AXP2101_ADC_TS_LOW_FREQ (1 << 7)

/* ------------------------------------------------------------------ */
/* IRQ mask bits (24-bit value spanning INTEN1/2/3 at 0x40/41/42)     */
/* ------------------------------------------------------------------ */

/* INTEN1 (0x40) — bits 0..7 of mask */
#define AXP2101_IRQ_BAT_UNDER_TEMP      (1UL << 0)
#define AXP2101_IRQ_BAT_OVER_TEMP       (1UL << 1)
#define AXP2101_IRQ_BAT_CHG_UNDER_TEMP  (1UL << 2)
#define AXP2101_IRQ_BAT_CHG_OVER_TEMP   (1UL << 3)
#define AXP2101_IRQ_GAUGE_NEW_SOC       (1UL << 4)
#define AXP2101_IRQ_WDT_TIMEOUT         (1UL << 5)
#define AXP2101_IRQ_WARNING_LEVEL1      (1UL << 6)
#define AXP2101_IRQ_WARNING_LEVEL2      (1UL << 7)
/* INTEN2 (0x41) — bits 8..15 of mask */
#define AXP2101_IRQ_PKEY_POSITIVE       (1UL << 8)
#define AXP2101_IRQ_PKEY_NEGATIVE       (1UL << 9)
#define AXP2101_IRQ_PKEY_LONG           (1UL << 10)
#define AXP2101_IRQ_PKEY_SHORT          (1UL << 11)
#define AXP2101_IRQ_BAT_REMOVE          (1UL << 12)
#define AXP2101_IRQ_BAT_INSERT          (1UL << 13)
#define AXP2101_IRQ_VBUS_REMOVE         (1UL << 14)
#define AXP2101_IRQ_VBUS_INSERT         (1UL << 15)
/* INTEN3 (0x42) — bits 16..23 of mask */
#define AXP2101_IRQ_BAT_OVER_VOL        (1UL << 16)
#define AXP2101_IRQ_CHAGER_TIMER        (1UL << 17)
#define AXP2101_IRQ_DIE_OVER_TEMP       (1UL << 18)
#define AXP2101_IRQ_BAT_CHG_START       (1UL << 19)
#define AXP2101_IRQ_BAT_CHG_DONE        (1UL << 20)
#define AXP2101_IRQ_BATFET_OVER_CURR    (1UL << 21)
#define AXP2101_IRQ_LDO_OVER_CURR       (1UL << 22)
#define AXP2101_IRQ_WDT_EXPIRE          (1UL << 23)
#define AXP2101_IRQ_ALL                 0x00FFFFFFul

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * Initialise the AXP2101.  Configures I2C0 (SDA=GPIO10, SCL=GPIO11),
 * verifies the chip ID, and enables the ADC channels selected by
 * adc_channels (OR of AXP2101_ADC_* bits).  Pass 0 to leave ADC in
 * its reset state.
 */
driver_error_t *axp2101_setup(uint8_t adc_channels);

/* Read the chip ID register (expect 0x4A). */
driver_error_t *axp2101_get_chip_id(uint8_t *id);

/* Shut down all power rails — only VRTC survives. */
driver_error_t *axp2101_shutdown(void);

/* Soft-reset the PMIC (POWOFF/POWON + register reset). */
driver_error_t *axp2101_reset(void);

/* ------------------------------------------------------------------ */
/* Status                                                              */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_is_vbus_good(bool *out);
driver_error_t *axp2101_is_battery_connected(bool *out);
driver_error_t *axp2101_is_charging(bool *out);
driver_error_t *axp2101_is_discharging(bool *out);

/*
 * Get charger FSM state.
 * *out is one of AXP2101_CHG_*_STATE.
 */
driver_error_t *axp2101_get_charger_status(uint8_t *out);

/* Raw power-on / power-off source registers (0x20 / 0x21). */
driver_error_t *axp2101_get_poweron_source(uint8_t *out);
driver_error_t *axp2101_get_poweroff_source(uint8_t *out);

/* ------------------------------------------------------------------ */
/* ADC measurements                                                    */
/* ------------------------------------------------------------------ */

/* Enable/disable ADC channels; channels is OR of AXP2101_ADC_* bits. */
driver_error_t *axp2101_adc_enable(uint8_t channels);
driver_error_t *axp2101_adc_disable(uint8_t channels);

/* Battery voltage in mV (0 if battery not connected). */
driver_error_t *axp2101_get_battery_voltage(int *mv);

/* Battery state-of-charge in % (−1 if battery not connected). */
driver_error_t *axp2101_get_battery_percent(int *pct);

/* VBUS voltage in mV. */
driver_error_t *axp2101_get_vbus_voltage(int *mv);

/* System (VSYS) voltage in mV. */
driver_error_t *axp2101_get_system_voltage(int *mv);

/*
 * Die temperature in units of 0.1 °C
 * (e.g. 250 = 25.0 °C).
 */
driver_error_t *axp2101_get_temperature(int *deciC);

/* ------------------------------------------------------------------ */
/* Fuel gauge / battery detection                                      */
/* ------------------------------------------------------------------ */

/*
 * Control the fuel gauge.
 *   write_rom : false = use ROM model, true = use RAM model
 *   enable    : enable / disable the gauge
 */
driver_error_t *axp2101_fuel_gauge_control(bool write_rom, bool enable);

/* Enable / disable battery presence detection. */
driver_error_t *axp2101_battery_detect_enable(bool enable);

/* Enable / disable button battery (coin cell) charging (REG 0x18 bit 2). */
driver_error_t *axp2101_btn_bat_chg_enable(bool enable);

/* ------------------------------------------------------------------ */
/* Charging configuration                                              */
/* ------------------------------------------------------------------ */

/* Set constant-charge current; opt is AXP2101_CHG_CUR_* (4..16). */
driver_error_t *axp2101_set_charge_current(uint8_t opt);
driver_error_t *axp2101_get_charge_current(uint8_t *out);

/* Set charge termination current; opt is 0..8 (0=0mA, 1=25mA …). */
driver_error_t *axp2101_set_charge_term_current(uint8_t opt);
driver_error_t *axp2101_get_charge_term_current(uint8_t *out);

/* Set charge target voltage; opt is AXP2101_CHG_VOL_*. */
driver_error_t *axp2101_set_charge_voltage(uint8_t opt);
driver_error_t *axp2101_get_charge_voltage(uint8_t *out);

/* ------------------------------------------------------------------ */
/* VBUS limits                                                         */
/* ------------------------------------------------------------------ */

/* opt is AXP2101_VBUS_VOL_LIM_*. */
driver_error_t *axp2101_set_vbus_voltage_limit(uint8_t opt);
driver_error_t *axp2101_get_vbus_voltage_limit(uint8_t *out);

/* opt is AXP2101_VBUS_CUR_LIM_*. */
driver_error_t *axp2101_set_vbus_current_limit(uint8_t opt);
driver_error_t *axp2101_get_vbus_current_limit(uint8_t *out);

/* ------------------------------------------------------------------ */
/* System power-down voltage (2600..3300 mV, 100 mV steps)            */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_set_sys_powerdown_voltage(int mv);
driver_error_t *axp2101_get_sys_powerdown_voltage(int *mv);

/* ------------------------------------------------------------------ */
/* Low-battery warning thresholds                                      */
/* ------------------------------------------------------------------ */

/* warn_pct : 5..20 %   shutdown_pct : 0..15 % */
driver_error_t *axp2101_set_low_bat_warn(uint8_t warn_pct, uint8_t shutdown_pct);
driver_error_t *axp2101_get_low_bat_warn(uint8_t *warn_pct, uint8_t *shutdown_pct);

/* ------------------------------------------------------------------ */
/* Charging LED                                                        */
/* ------------------------------------------------------------------ */

/* mode is AXP2101_CHG_LED_*. */
driver_error_t *axp2101_set_charging_led(uint8_t mode);
driver_error_t *axp2101_get_charging_led(uint8_t *mode);

/* ------------------------------------------------------------------ */
/* Power key timing                                                    */
/* ------------------------------------------------------------------ */

/* opt is AXP2101_POWERON_*. */
driver_error_t *axp2101_set_pkey_press_on_time(uint8_t opt);
driver_error_t *axp2101_get_pkey_press_on_time(uint8_t *out);

/* opt is AXP2101_POWEROFF_*. */
driver_error_t *axp2101_set_pkey_press_off_time(uint8_t opt);
driver_error_t *axp2101_get_pkey_press_off_time(uint8_t *out);

/* ------------------------------------------------------------------ */
/* Sleep / wakeup                                                      */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_sleep_enable(bool enable);
driver_error_t *axp2101_wakeup_enable(bool enable);

/* ------------------------------------------------------------------ */
/* DCDC regulators                                                     */
/* ------------------------------------------------------------------ */

/*
 * DC1: 1500..3400 mV, 100 mV steps
 */
driver_error_t *axp2101_dc1_enable(bool enable);
driver_error_t *axp2101_dc1_is_enabled(bool *out);
driver_error_t *axp2101_dc1_set_voltage(int mv);
driver_error_t *axp2101_dc1_get_voltage(int *mv);

/*
 * DC2: 500..1200 mV (10 mV steps) / 1220..1540 mV (20 mV steps)
 */
driver_error_t *axp2101_dc2_enable(bool enable);
driver_error_t *axp2101_dc2_is_enabled(bool *out);
driver_error_t *axp2101_dc2_set_voltage(int mv);
driver_error_t *axp2101_dc2_get_voltage(int *mv);

/*
 * DC3: 500..1200 mV (10 mV) / 1220..1540 mV (20 mV) / 1600..3400 mV (100 mV)
 */
driver_error_t *axp2101_dc3_enable(bool enable);
driver_error_t *axp2101_dc3_is_enabled(bool *out);
driver_error_t *axp2101_dc3_set_voltage(int mv);
driver_error_t *axp2101_dc3_get_voltage(int *mv);

/*
 * DC4: 500..1200 mV (10 mV) / 1220..1840 mV (20 mV)
 */
driver_error_t *axp2101_dc4_enable(bool enable);
driver_error_t *axp2101_dc4_is_enabled(bool *out);
driver_error_t *axp2101_dc4_set_voltage(int mv);
driver_error_t *axp2101_dc4_get_voltage(int *mv);

/*
 * DC5: 1200 mV (special) or 1400..3700 mV, 100 mV steps
 */
driver_error_t *axp2101_dc5_enable(bool enable);
driver_error_t *axp2101_dc5_is_enabled(bool *out);
driver_error_t *axp2101_dc5_set_voltage(int mv);
driver_error_t *axp2101_dc5_get_voltage(int *mv);

/* ------------------------------------------------------------------ */
/* ALDO regulators  500..3500 mV, 100 mV steps                        */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_aldo1_enable(bool enable);
driver_error_t *axp2101_aldo1_is_enabled(bool *out);
driver_error_t *axp2101_aldo1_set_voltage(int mv);
driver_error_t *axp2101_aldo1_get_voltage(int *mv);

driver_error_t *axp2101_aldo2_enable(bool enable);
driver_error_t *axp2101_aldo2_is_enabled(bool *out);
driver_error_t *axp2101_aldo2_set_voltage(int mv);
driver_error_t *axp2101_aldo2_get_voltage(int *mv);

driver_error_t *axp2101_aldo3_enable(bool enable);
driver_error_t *axp2101_aldo3_is_enabled(bool *out);
driver_error_t *axp2101_aldo3_set_voltage(int mv);
driver_error_t *axp2101_aldo3_get_voltage(int *mv);

driver_error_t *axp2101_aldo4_enable(bool enable);
driver_error_t *axp2101_aldo4_is_enabled(bool *out);
driver_error_t *axp2101_aldo4_set_voltage(int mv);
driver_error_t *axp2101_aldo4_get_voltage(int *mv);

/* ------------------------------------------------------------------ */
/* BLDO regulators  500..3500 mV, 100 mV steps                        */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_bldo1_enable(bool enable);
driver_error_t *axp2101_bldo1_is_enabled(bool *out);
driver_error_t *axp2101_bldo1_set_voltage(int mv);
driver_error_t *axp2101_bldo1_get_voltage(int *mv);

driver_error_t *axp2101_bldo2_enable(bool enable);
driver_error_t *axp2101_bldo2_is_enabled(bool *out);
driver_error_t *axp2101_bldo2_set_voltage(int mv);
driver_error_t *axp2101_bldo2_get_voltage(int *mv);

/* ------------------------------------------------------------------ */
/* CPUSLDO  500..1400 mV, 50 mV steps                                 */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_cpusldo_enable(bool enable);
driver_error_t *axp2101_cpusldo_is_enabled(bool *out);
driver_error_t *axp2101_cpusldo_set_voltage(int mv);
driver_error_t *axp2101_cpusldo_get_voltage(int *mv);

/* ------------------------------------------------------------------ */
/* DLDO regulators  500..3400 mV, 100 mV steps                        */
/* ------------------------------------------------------------------ */

driver_error_t *axp2101_dldo1_enable(bool enable);
driver_error_t *axp2101_dldo1_is_enabled(bool *out);
driver_error_t *axp2101_dldo1_set_voltage(int mv);
driver_error_t *axp2101_dldo1_get_voltage(int *mv);

driver_error_t *axp2101_dldo2_enable(bool enable);
driver_error_t *axp2101_dldo2_is_enabled(bool *out);
driver_error_t *axp2101_dldo2_set_voltage(int mv);
driver_error_t *axp2101_dldo2_get_voltage(int *mv);

/* ------------------------------------------------------------------ */
/* IRQ control                                                         */
/* ------------------------------------------------------------------ */

/*
 * Enable / disable IRQ sources.
 * mask is an OR of AXP2101_IRQ_* bits (24-bit value spanning 3 registers).
 */
driver_error_t *axp2101_irq_enable(uint32_t mask);
driver_error_t *axp2101_irq_disable(uint32_t mask);

/*
 * Read the latched interrupt status into *status.
 * Bits correspond to the same AXP2101_IRQ_* mask definitions.
 */
driver_error_t *axp2101_irq_get_status(uint32_t *status);

/* Clear all interrupt status flags (write 0xFF to each status register). */
driver_error_t *axp2101_irq_clear_status(void);

#endif /* AXP2101_H */
