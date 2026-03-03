/*
 * Lua RTOS, Lua AXP2101 PMU module
 *
 * Usage example:
 *
 *   -- Enable battery ADC channels and set up the PMU
 *   pmu = axp2101.setup(axp2101.ADC_BAT | axp2101.ADC_VBUS | axp2101.ADC_SYS | axp2101.ADC_TEMP)
 *
 *   -- Read battery state
 *   print(pmu:bat_voltage(), "mV")
 *   print(pmu:bat_percent(), "%")
 *   print(pmu:temperature(), "°C")
 *
 *   -- Configure charging
 *   pmu:chg_current(axp2101.CUR_500MA)
 *   pmu:chg_voltage(axp2101.VOL_4V2)
 *
 *   -- Set DC3 to 3.3 V and enable it
 *   pmu:dc3_voltage(3300)
 *   pmu:dc3_enable(true)
 *
 *   -- Set charging LED to 1 Hz blink
 *   pmu:chg_led(axp2101.LED_BLINK1)
 *
 *   -- Power off
 *   pmu:shutdown()
 */

#include "sdkconfig.h"

#if CONFIG_LUA_RTOS_LUA_USE_AXP2101

#include "freertos/FreeRTOS.h"
#include "freertos/adds.h"

#include "lua.h"
#include "lauxlib.h"
#include "modules.h"
#include "error.h"
#include "sys.h"

#include <drivers/axp2101.h>

/* The driver is a singleton; userdata carries no per-instance state. */
typedef struct { int dummy; } axp2101_userdata_t;

#define CHECK_UD(L) \
    ((void)luaL_checkudata(L, 1, "axp2101.pmu"))

/* ------------------------------------------------------------------ */
/* Module-level: setup                                                 */
/* ------------------------------------------------------------------ */

/*
 * axp2101.setup([adc_channels])
 *
 * Initialises I2C0, verifies chip ID and optionally enables ADC
 * channels (OR of axp2101.ADC_* constants).  Returns a pmu instance.
 */
static int laxp2101_setup(lua_State *L) {
    driver_error_t *error;
    uint8_t adc_ch = (uint8_t)luaL_optinteger(L, 1, 0);

    if ((error = axp2101_setup(adc_ch)))
        return luaL_driver_error(L, error);

    axp2101_userdata_t *ud = (axp2101_userdata_t *)
        lua_newuserdata(L, sizeof(axp2101_userdata_t));
    ud->dummy = 0;
    luaL_getmetatable(L, "axp2101.pmu");
    lua_setmetatable(L, -2);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Info / control                                                      */
/* ------------------------------------------------------------------ */

/* pmu:chipid() → integer (expect 0x4A) */
static int laxp2101_chipid(lua_State *L) {
    driver_error_t *error;
    uint8_t id;
    CHECK_UD(L);
    if ((error = axp2101_get_chip_id(&id)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, id);
    return 1;
}

/* pmu:shutdown() — power off all rails (only VRTC survives) */
static int laxp2101_shutdown(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if ((error = axp2101_shutdown()))
        return luaL_driver_error(L, error);
    return 0;
}

/* pmu:reset() — soft-reset the PMIC */
static int laxp2101_reset(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if ((error = axp2101_reset()))
        return luaL_driver_error(L, error);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Status                                                              */
/* ------------------------------------------------------------------ */

static int laxp2101_vbus_good(lua_State *L) {
    driver_error_t *error;
    bool val;
    CHECK_UD(L);
    if ((error = axp2101_is_vbus_good(&val)))
        return luaL_driver_error(L, error);
    lua_pushboolean(L, val);
    return 1;
}

static int laxp2101_bat_connected(lua_State *L) {
    driver_error_t *error;
    bool val;
    CHECK_UD(L);
    if ((error = axp2101_is_battery_connected(&val)))
        return luaL_driver_error(L, error);
    lua_pushboolean(L, val);
    return 1;
}

static int laxp2101_charging(lua_State *L) {
    driver_error_t *error;
    bool val;
    CHECK_UD(L);
    if ((error = axp2101_is_charging(&val)))
        return luaL_driver_error(L, error);
    lua_pushboolean(L, val);
    return 1;
}

static int laxp2101_discharging(lua_State *L) {
    driver_error_t *error;
    bool val;
    CHECK_UD(L);
    if ((error = axp2101_is_discharging(&val)))
        return luaL_driver_error(L, error);
    lua_pushboolean(L, val);
    return 1;
}

/* pmu:chg_status() → integer (axp2101.CHG_* constant) */
static int laxp2101_chg_status(lua_State *L) {
    driver_error_t *error;
    uint8_t val;
    CHECK_UD(L);
    if ((error = axp2101_get_charger_status(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* pmu:poweron_src() → raw byte from register 0x20 */
static int laxp2101_poweron_src(lua_State *L) {
    driver_error_t *error;
    uint8_t val;
    CHECK_UD(L);
    if ((error = axp2101_get_poweron_source(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* pmu:poweroff_src() → raw byte from register 0x21 */
static int laxp2101_poweroff_src(lua_State *L) {
    driver_error_t *error;
    uint8_t val;
    CHECK_UD(L);
    if ((error = axp2101_get_poweroff_source(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* ------------------------------------------------------------------ */
/* ADC measurements                                                    */
/* ------------------------------------------------------------------ */

/* pmu:bat_voltage() → integer mV (0 if battery disconnected) */
static int laxp2101_bat_voltage(lua_State *L) {
    driver_error_t *error;
    int mv;
    CHECK_UD(L);
    if ((error = axp2101_get_battery_voltage(&mv)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, mv);
    return 1;
}

/* pmu:bat_percent() → integer % (−1 if battery disconnected) */
static int laxp2101_bat_percent(lua_State *L) {
    driver_error_t *error;
    int pct;
    CHECK_UD(L);
    if ((error = axp2101_get_battery_percent(&pct)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, pct);
    return 1;
}

/* pmu:vbus_voltage() → integer mV */
static int laxp2101_vbus_voltage(lua_State *L) {
    driver_error_t *error;
    int mv;
    CHECK_UD(L);
    if ((error = axp2101_get_vbus_voltage(&mv)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, mv);
    return 1;
}

/* pmu:sys_voltage() → integer mV */
static int laxp2101_sys_voltage(lua_State *L) {
    driver_error_t *error;
    int mv;
    CHECK_UD(L);
    if ((error = axp2101_get_system_voltage(&mv)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, mv);
    return 1;
}

/* pmu:temperature() → number in °C (e.g. 25.5) */
static int laxp2101_temperature(lua_State *L) {
    driver_error_t *error;
    int deciC;
    CHECK_UD(L);
    if ((error = axp2101_get_temperature(&deciC)))
        return luaL_driver_error(L, error);
    lua_pushnumber(L, (lua_Number)deciC / 10.0);
    return 1;
}

/* ------------------------------------------------------------------ */
/* ADC channel enable / disable                                        */
/* ------------------------------------------------------------------ */

/* pmu:adc_enable(channels) — channels is OR of axp2101.ADC_* bits */
static int laxp2101_adc_enable(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    uint8_t ch = (uint8_t)luaL_checkinteger(L, 2);
    if ((error = axp2101_adc_enable(ch)))
        return luaL_driver_error(L, error);
    return 0;
}

/* pmu:adc_disable(channels) */
static int laxp2101_adc_disable(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    uint8_t ch = (uint8_t)luaL_checkinteger(L, 2);
    if ((error = axp2101_adc_disable(ch)))
        return luaL_driver_error(L, error);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Fuel gauge / battery detection                                      */
/* ------------------------------------------------------------------ */

/* pmu:fuel_gauge(write_rom, enable) */
static int laxp2101_fuel_gauge(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    bool write_rom = lua_toboolean(L, 2);
    bool enable    = lua_toboolean(L, 3);
    if ((error = axp2101_fuel_gauge_control(write_rom, enable)))
        return luaL_driver_error(L, error);
    return 0;
}

/* pmu:bat_detect(enable) */
static int laxp2101_bat_detect(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    bool enable = lua_toboolean(L, 2);
    if ((error = axp2101_battery_detect_enable(enable)))
        return luaL_driver_error(L, error);
    return 0;
}

/* pmu:btn_bat_chg(enable) — enable/disable button battery (coin cell) charging */
static int laxp2101_btn_bat_chg(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    bool enable = lua_toboolean(L, 2);
    if ((error = axp2101_btn_bat_chg_enable(enable)))
        return luaL_driver_error(L, error);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Charging configuration — get/set pattern                           */
/* Called with no arg: returns current value.                         */
/* Called with arg: sets value, returns nothing.                      */
/* ------------------------------------------------------------------ */

/* pmu:chg_current([opt]) — opt is axp2101.CUR_* constant */
static int laxp2101_chg_current(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = axp2101_set_charge_current((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t val;
    if ((error = axp2101_get_charge_current(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* pmu:chg_term_current([opt]) — 0..8 */
static int laxp2101_chg_term_current(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = axp2101_set_charge_term_current((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t val;
    if ((error = axp2101_get_charge_term_current(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* pmu:chg_voltage([opt]) — opt is axp2101.VOL_* constant */
static int laxp2101_chg_voltage(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = axp2101_set_charge_voltage((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t val;
    if ((error = axp2101_get_charge_voltage(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* ------------------------------------------------------------------ */
/* VBUS limits                                                         */
/* ------------------------------------------------------------------ */

/* pmu:vbus_vlimit([opt]) — opt is axp2101.VBUS_VOL_LIM_* */
static int laxp2101_vbus_vlimit(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = axp2101_set_vbus_voltage_limit((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t val;
    if ((error = axp2101_get_vbus_voltage_limit(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* pmu:vbus_climit([opt]) — opt is axp2101.VBUS_CUR_LIM_* */
static int laxp2101_vbus_climit(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = axp2101_set_vbus_current_limit((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t val;
    if ((error = axp2101_get_vbus_current_limit(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* ------------------------------------------------------------------ */
/* System power-down voltage                                           */
/* ------------------------------------------------------------------ */

/* pmu:sys_pdvoltage([mv]) — 2600..3300 mV, 100 mV steps */
static int laxp2101_sys_pdvoltage(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = axp2101_set_sys_powerdown_voltage((int)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    int mv;
    if ((error = axp2101_get_sys_powerdown_voltage(&mv)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, mv);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Low-battery warning thresholds                                      */
/* ------------------------------------------------------------------ */

/*
 * pmu:low_bat_warn([warn_pct, shutdown_pct])
 * Set: pass warn_pct (5..20) and shutdown_pct (0..15).
 * Get: returns warn_pct, shutdown_pct.
 */
static int laxp2101_low_bat_warn(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 3) {
        uint8_t w = (uint8_t)luaL_checkinteger(L, 2);
        uint8_t s = (uint8_t)luaL_checkinteger(L, 3);
        if ((error = axp2101_set_low_bat_warn(w, s)))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t w, s;
    if ((error = axp2101_get_low_bat_warn(&w, &s)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, w);
    lua_pushinteger(L, s);
    return 2;
}

/* ------------------------------------------------------------------ */
/* Charging LED                                                        */
/* ------------------------------------------------------------------ */

/* pmu:chg_led([mode]) — mode is axp2101.LED_* */
static int laxp2101_chg_led(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = axp2101_set_charging_led((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t mode;
    if ((error = axp2101_get_charging_led(&mode)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, mode);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Power key timing                                                    */
/* ------------------------------------------------------------------ */

/* pmu:pkey_on_time([opt]) — opt is axp2101.POWERON_* */
static int laxp2101_pkey_on_time(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = axp2101_set_pkey_press_on_time((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t val;
    if ((error = axp2101_get_pkey_press_on_time(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* pmu:pkey_off_time([opt]) — opt is axp2101.POWEROFF_* */
static int laxp2101_pkey_off_time(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = axp2101_set_pkey_press_off_time((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t val;
    if ((error = axp2101_get_pkey_press_off_time(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Sleep / wakeup                                                      */
/* ------------------------------------------------------------------ */

/* pmu:sleep(enable) */
static int laxp2101_sleep(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if ((error = axp2101_sleep_enable(lua_toboolean(L, 2))))
        return luaL_driver_error(L, error);
    return 0;
}

/* pmu:wakeup(enable) */
static int laxp2101_wakeup(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if ((error = axp2101_wakeup_enable(lua_toboolean(L, 2))))
        return luaL_driver_error(L, error);
    return 0;
}

/* ------------------------------------------------------------------ */
/* DC / LDO regulator helpers — macros eliminate the repetition       */
/* ------------------------------------------------------------------ */

/*
 * pmu:dcN_enable(bool)   — enable (true) or disable (false) the rail
 * pmu:dcN_isenabled()    — returns boolean
 * pmu:dcN_voltage([mv])  — get (no arg) or set (integer mV) voltage
 */

#define REG_ENABLE_FUNC(lua_name, c_fn) \
static int laxp2101_##lua_name(lua_State *L) { \
    driver_error_t *error; \
    CHECK_UD(L); \
    if ((error = c_fn(lua_toboolean(L, 2)))) \
        return luaL_driver_error(L, error); \
    return 0; \
}

#define REG_ISENABLED_FUNC(lua_name, c_fn) \
static int laxp2101_##lua_name(lua_State *L) { \
    driver_error_t *error; \
    bool val; \
    CHECK_UD(L); \
    if ((error = c_fn(&val))) \
        return luaL_driver_error(L, error); \
    lua_pushboolean(L, val); \
    return 1; \
}

#define REG_VOLTAGE_FUNC(lua_name, c_set, c_get) \
static int laxp2101_##lua_name(lua_State *L) { \
    driver_error_t *error; \
    CHECK_UD(L); \
    if (lua_gettop(L) >= 2) { \
        if ((error = c_set((int)luaL_checkinteger(L, 2)))) \
            return luaL_driver_error(L, error); \
        return 0; \
    } \
    int mv; \
    if ((error = c_get(&mv))) \
        return luaL_driver_error(L, error); \
    lua_pushinteger(L, mv); \
    return 1; \
}

/* DC1–5 */
REG_ENABLE_FUNC   (dc1_enable,    axp2101_dc1_enable)
REG_ISENABLED_FUNC(dc1_isenabled, axp2101_dc1_is_enabled)
REG_VOLTAGE_FUNC  (dc1_voltage,   axp2101_dc1_set_voltage, axp2101_dc1_get_voltage)

REG_ENABLE_FUNC   (dc2_enable,    axp2101_dc2_enable)
REG_ISENABLED_FUNC(dc2_isenabled, axp2101_dc2_is_enabled)
REG_VOLTAGE_FUNC  (dc2_voltage,   axp2101_dc2_set_voltage, axp2101_dc2_get_voltage)

REG_ENABLE_FUNC   (dc3_enable,    axp2101_dc3_enable)
REG_ISENABLED_FUNC(dc3_isenabled, axp2101_dc3_is_enabled)
REG_VOLTAGE_FUNC  (dc3_voltage,   axp2101_dc3_set_voltage, axp2101_dc3_get_voltage)

REG_ENABLE_FUNC   (dc4_enable,    axp2101_dc4_enable)
REG_ISENABLED_FUNC(dc4_isenabled, axp2101_dc4_is_enabled)
REG_VOLTAGE_FUNC  (dc4_voltage,   axp2101_dc4_set_voltage, axp2101_dc4_get_voltage)

REG_ENABLE_FUNC   (dc5_enable,    axp2101_dc5_enable)
REG_ISENABLED_FUNC(dc5_isenabled, axp2101_dc5_is_enabled)
REG_VOLTAGE_FUNC  (dc5_voltage,   axp2101_dc5_set_voltage, axp2101_dc5_get_voltage)

/* ALDO1–4 */
REG_ENABLE_FUNC   (aldo1_enable,    axp2101_aldo1_enable)
REG_ISENABLED_FUNC(aldo1_isenabled, axp2101_aldo1_is_enabled)
REG_VOLTAGE_FUNC  (aldo1_voltage,   axp2101_aldo1_set_voltage, axp2101_aldo1_get_voltage)

REG_ENABLE_FUNC   (aldo2_enable,    axp2101_aldo2_enable)
REG_ISENABLED_FUNC(aldo2_isenabled, axp2101_aldo2_is_enabled)
REG_VOLTAGE_FUNC  (aldo2_voltage,   axp2101_aldo2_set_voltage, axp2101_aldo2_get_voltage)

REG_ENABLE_FUNC   (aldo3_enable,    axp2101_aldo3_enable)
REG_ISENABLED_FUNC(aldo3_isenabled, axp2101_aldo3_is_enabled)
REG_VOLTAGE_FUNC  (aldo3_voltage,   axp2101_aldo3_set_voltage, axp2101_aldo3_get_voltage)

REG_ENABLE_FUNC   (aldo4_enable,    axp2101_aldo4_enable)
REG_ISENABLED_FUNC(aldo4_isenabled, axp2101_aldo4_is_enabled)
REG_VOLTAGE_FUNC  (aldo4_voltage,   axp2101_aldo4_set_voltage, axp2101_aldo4_get_voltage)

/* BLDO1–2 */
REG_ENABLE_FUNC   (bldo1_enable,    axp2101_bldo1_enable)
REG_ISENABLED_FUNC(bldo1_isenabled, axp2101_bldo1_is_enabled)
REG_VOLTAGE_FUNC  (bldo1_voltage,   axp2101_bldo1_set_voltage, axp2101_bldo1_get_voltage)

REG_ENABLE_FUNC   (bldo2_enable,    axp2101_bldo2_enable)
REG_ISENABLED_FUNC(bldo2_isenabled, axp2101_bldo2_is_enabled)
REG_VOLTAGE_FUNC  (bldo2_voltage,   axp2101_bldo2_set_voltage, axp2101_bldo2_get_voltage)

/* CPUSLDO */
REG_ENABLE_FUNC   (cpusldo_enable,    axp2101_cpusldo_enable)
REG_ISENABLED_FUNC(cpusldo_isenabled, axp2101_cpusldo_is_enabled)
REG_VOLTAGE_FUNC  (cpusldo_voltage,   axp2101_cpusldo_set_voltage, axp2101_cpusldo_get_voltage)

/* DLDO1–2 */
REG_ENABLE_FUNC   (dldo1_enable,    axp2101_dldo1_enable)
REG_ISENABLED_FUNC(dldo1_isenabled, axp2101_dldo1_is_enabled)
REG_VOLTAGE_FUNC  (dldo1_voltage,   axp2101_dldo1_set_voltage, axp2101_dldo1_get_voltage)

REG_ENABLE_FUNC   (dldo2_enable,    axp2101_dldo2_enable)
REG_ISENABLED_FUNC(dldo2_isenabled, axp2101_dldo2_is_enabled)
REG_VOLTAGE_FUNC  (dldo2_voltage,   axp2101_dldo2_set_voltage, axp2101_dldo2_get_voltage)

/* ------------------------------------------------------------------ */
/* IRQ                                                                 */
/* ------------------------------------------------------------------ */

/* pmu:irq_enable(mask) — mask is OR of axp2101.IRQ_* constants */
static int laxp2101_irq_enable(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    uint32_t mask = (uint32_t)luaL_checkinteger(L, 2);
    if ((error = axp2101_irq_enable(mask)))
        return luaL_driver_error(L, error);
    return 0;
}

/* pmu:irq_disable(mask) */
static int laxp2101_irq_disable(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    uint32_t mask = (uint32_t)luaL_checkinteger(L, 2);
    if ((error = axp2101_irq_disable(mask)))
        return luaL_driver_error(L, error);
    return 0;
}

/* pmu:irq_status() → 24-bit integer (OR of axp2101.IRQ_* bits) */
static int laxp2101_irq_status(lua_State *L) {
    driver_error_t *error;
    uint32_t status;
    CHECK_UD(L);
    if ((error = axp2101_irq_get_status(&status)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, (lua_Integer)status);
    return 1;
}

/* pmu:irq_clear() — clear all latched interrupt status flags */
static int laxp2101_irq_clear(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if ((error = axp2101_irq_clear_status()))
        return luaL_driver_error(L, error);
    return 0;
}

/* ------------------------------------------------------------------ */
/* GC                                                                  */
/* ------------------------------------------------------------------ */

static int laxp2101_gc(lua_State *L) {
    (void)L;  /* singleton driver, nothing to release */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Registration tables                                                 */
/* ------------------------------------------------------------------ */

static const LUA_REG_TYPE axp2101_inst_map[] = {
    /* Info / control */
    { LSTRKEY("chipid"),            LFUNCVAL(laxp2101_chipid)           },
    { LSTRKEY("shutdown"),          LFUNCVAL(laxp2101_shutdown)         },
    { LSTRKEY("reset"),             LFUNCVAL(laxp2101_reset)            },
    /* Status */
    { LSTRKEY("vbus_good"),         LFUNCVAL(laxp2101_vbus_good)        },
    { LSTRKEY("bat_connected"),     LFUNCVAL(laxp2101_bat_connected)    },
    { LSTRKEY("charging"),          LFUNCVAL(laxp2101_charging)         },
    { LSTRKEY("discharging"),       LFUNCVAL(laxp2101_discharging)      },
    { LSTRKEY("chg_status"),        LFUNCVAL(laxp2101_chg_status)       },
    { LSTRKEY("poweron_src"),       LFUNCVAL(laxp2101_poweron_src)      },
    { LSTRKEY("poweroff_src"),      LFUNCVAL(laxp2101_poweroff_src)     },
    /* Measurements */
    { LSTRKEY("bat_voltage"),       LFUNCVAL(laxp2101_bat_voltage)      },
    { LSTRKEY("bat_percent"),       LFUNCVAL(laxp2101_bat_percent)      },
    { LSTRKEY("vbus_voltage"),      LFUNCVAL(laxp2101_vbus_voltage)     },
    { LSTRKEY("sys_voltage"),       LFUNCVAL(laxp2101_sys_voltage)      },
    { LSTRKEY("temperature"),       LFUNCVAL(laxp2101_temperature)      },
    /* ADC channel control */
    { LSTRKEY("adc_enable"),        LFUNCVAL(laxp2101_adc_enable)       },
    { LSTRKEY("adc_disable"),       LFUNCVAL(laxp2101_adc_disable)      },
    /* Fuel gauge / battery detect */
    { LSTRKEY("fuel_gauge"),        LFUNCVAL(laxp2101_fuel_gauge)       },
    { LSTRKEY("bat_detect"),        LFUNCVAL(laxp2101_bat_detect)       },
    { LSTRKEY("btn_bat_chg"),       LFUNCVAL(laxp2101_btn_bat_chg)      },
    /* Charging */
    { LSTRKEY("chg_current"),       LFUNCVAL(laxp2101_chg_current)      },
    { LSTRKEY("chg_term_current"),  LFUNCVAL(laxp2101_chg_term_current) },
    { LSTRKEY("chg_voltage"),       LFUNCVAL(laxp2101_chg_voltage)      },
    /* VBUS limits */
    { LSTRKEY("vbus_vlimit"),       LFUNCVAL(laxp2101_vbus_vlimit)      },
    { LSTRKEY("vbus_climit"),       LFUNCVAL(laxp2101_vbus_climit)      },
    /* System power-down voltage */
    { LSTRKEY("sys_pdvoltage"),     LFUNCVAL(laxp2101_sys_pdvoltage)    },
    { LSTRKEY("low_bat_warn"),      LFUNCVAL(laxp2101_low_bat_warn)     },
    /* Charging LED */
    { LSTRKEY("chg_led"),           LFUNCVAL(laxp2101_chg_led)          },
    /* Power key timing */
    { LSTRKEY("pkey_on_time"),      LFUNCVAL(laxp2101_pkey_on_time)     },
    { LSTRKEY("pkey_off_time"),     LFUNCVAL(laxp2101_pkey_off_time)    },
    /* Sleep / wakeup */
    { LSTRKEY("sleep"),             LFUNCVAL(laxp2101_sleep)            },
    { LSTRKEY("wakeup"),            LFUNCVAL(laxp2101_wakeup)           },
    /* DC regulators */
    { LSTRKEY("dc1_enable"),        LFUNCVAL(laxp2101_dc1_enable)       },
    { LSTRKEY("dc1_isenabled"),     LFUNCVAL(laxp2101_dc1_isenabled)    },
    { LSTRKEY("dc1_voltage"),       LFUNCVAL(laxp2101_dc1_voltage)      },
    { LSTRKEY("dc2_enable"),        LFUNCVAL(laxp2101_dc2_enable)       },
    { LSTRKEY("dc2_isenabled"),     LFUNCVAL(laxp2101_dc2_isenabled)    },
    { LSTRKEY("dc2_voltage"),       LFUNCVAL(laxp2101_dc2_voltage)      },
    { LSTRKEY("dc3_enable"),        LFUNCVAL(laxp2101_dc3_enable)       },
    { LSTRKEY("dc3_isenabled"),     LFUNCVAL(laxp2101_dc3_isenabled)    },
    { LSTRKEY("dc3_voltage"),       LFUNCVAL(laxp2101_dc3_voltage)      },
    { LSTRKEY("dc4_enable"),        LFUNCVAL(laxp2101_dc4_enable)       },
    { LSTRKEY("dc4_isenabled"),     LFUNCVAL(laxp2101_dc4_isenabled)    },
    { LSTRKEY("dc4_voltage"),       LFUNCVAL(laxp2101_dc4_voltage)      },
    { LSTRKEY("dc5_enable"),        LFUNCVAL(laxp2101_dc5_enable)       },
    { LSTRKEY("dc5_isenabled"),     LFUNCVAL(laxp2101_dc5_isenabled)    },
    { LSTRKEY("dc5_voltage"),       LFUNCVAL(laxp2101_dc5_voltage)      },
    /* ALDO regulators */
    { LSTRKEY("aldo1_enable"),      LFUNCVAL(laxp2101_aldo1_enable)     },
    { LSTRKEY("aldo1_isenabled"),   LFUNCVAL(laxp2101_aldo1_isenabled)  },
    { LSTRKEY("aldo1_voltage"),     LFUNCVAL(laxp2101_aldo1_voltage)    },
    { LSTRKEY("aldo2_enable"),      LFUNCVAL(laxp2101_aldo2_enable)     },
    { LSTRKEY("aldo2_isenabled"),   LFUNCVAL(laxp2101_aldo2_isenabled)  },
    { LSTRKEY("aldo2_voltage"),     LFUNCVAL(laxp2101_aldo2_voltage)    },
    { LSTRKEY("aldo3_enable"),      LFUNCVAL(laxp2101_aldo3_enable)     },
    { LSTRKEY("aldo3_isenabled"),   LFUNCVAL(laxp2101_aldo3_isenabled)  },
    { LSTRKEY("aldo3_voltage"),     LFUNCVAL(laxp2101_aldo3_voltage)    },
    { LSTRKEY("aldo4_enable"),      LFUNCVAL(laxp2101_aldo4_enable)     },
    { LSTRKEY("aldo4_isenabled"),   LFUNCVAL(laxp2101_aldo4_isenabled)  },
    { LSTRKEY("aldo4_voltage"),     LFUNCVAL(laxp2101_aldo4_voltage)    },
    /* BLDO regulators */
    { LSTRKEY("bldo1_enable"),      LFUNCVAL(laxp2101_bldo1_enable)     },
    { LSTRKEY("bldo1_isenabled"),   LFUNCVAL(laxp2101_bldo1_isenabled)  },
    { LSTRKEY("bldo1_voltage"),     LFUNCVAL(laxp2101_bldo1_voltage)    },
    { LSTRKEY("bldo2_enable"),      LFUNCVAL(laxp2101_bldo2_enable)     },
    { LSTRKEY("bldo2_isenabled"),   LFUNCVAL(laxp2101_bldo2_isenabled)  },
    { LSTRKEY("bldo2_voltage"),     LFUNCVAL(laxp2101_bldo2_voltage)    },
    /* CPUSLDO */
    { LSTRKEY("cpusldo_enable"),    LFUNCVAL(laxp2101_cpusldo_enable)   },
    { LSTRKEY("cpusldo_isenabled"), LFUNCVAL(laxp2101_cpusldo_isenabled)},
    { LSTRKEY("cpusldo_voltage"),   LFUNCVAL(laxp2101_cpusldo_voltage)  },
    /* DLDO regulators */
    { LSTRKEY("dldo1_enable"),      LFUNCVAL(laxp2101_dldo1_enable)     },
    { LSTRKEY("dldo1_isenabled"),   LFUNCVAL(laxp2101_dldo1_isenabled)  },
    { LSTRKEY("dldo1_voltage"),     LFUNCVAL(laxp2101_dldo1_voltage)    },
    { LSTRKEY("dldo2_enable"),      LFUNCVAL(laxp2101_dldo2_enable)     },
    { LSTRKEY("dldo2_isenabled"),   LFUNCVAL(laxp2101_dldo2_isenabled)  },
    { LSTRKEY("dldo2_voltage"),     LFUNCVAL(laxp2101_dldo2_voltage)    },
    /* IRQ */
    { LSTRKEY("irq_enable"),        LFUNCVAL(laxp2101_irq_enable)       },
    { LSTRKEY("irq_disable"),       LFUNCVAL(laxp2101_irq_disable)      },
    { LSTRKEY("irq_status"),        LFUNCVAL(laxp2101_irq_status)       },
    { LSTRKEY("irq_clear"),         LFUNCVAL(laxp2101_irq_clear)        },
    /* Metatable */
    { LSTRKEY("__metatable"),       LROVAL(axp2101_inst_map)            },
    { LSTRKEY("__index"),           LROVAL(axp2101_inst_map)            },
    { LSTRKEY("__gc"),              LFUNCVAL(laxp2101_gc)               },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE axp2101_map[] = {
    { LSTRKEY("setup"),             LFUNCVAL(laxp2101_setup)            },
    /* Charger FSM status constants */
    { LSTRKEY("CHG_TRI"),           LINTVAL(AXP2101_CHG_TRI_STATE)     },
    { LSTRKEY("CHG_PRE"),           LINTVAL(AXP2101_CHG_PRE_STATE)     },
    { LSTRKEY("CHG_CC"),            LINTVAL(AXP2101_CHG_CC_STATE)      },
    { LSTRKEY("CHG_CV"),            LINTVAL(AXP2101_CHG_CV_STATE)      },
    { LSTRKEY("CHG_DONE"),          LINTVAL(AXP2101_CHG_DONE_STATE)    },
    { LSTRKEY("CHG_STOP"),          LINTVAL(AXP2101_CHG_STOP_STATE)    },
    /* Charge current constants */
    { LSTRKEY("CUR_100MA"),         LINTVAL(AXP2101_CHG_CUR_100MA)     },
    { LSTRKEY("CUR_125MA"),         LINTVAL(AXP2101_CHG_CUR_125MA)     },
    { LSTRKEY("CUR_150MA"),         LINTVAL(AXP2101_CHG_CUR_150MA)     },
    { LSTRKEY("CUR_175MA"),         LINTVAL(AXP2101_CHG_CUR_175MA)     },
    { LSTRKEY("CUR_200MA"),         LINTVAL(AXP2101_CHG_CUR_200MA)     },
    { LSTRKEY("CUR_300MA"),         LINTVAL(AXP2101_CHG_CUR_300MA)     },
    { LSTRKEY("CUR_400MA"),         LINTVAL(AXP2101_CHG_CUR_400MA)     },
    { LSTRKEY("CUR_500MA"),         LINTVAL(AXP2101_CHG_CUR_500MA)     },
    { LSTRKEY("CUR_600MA"),         LINTVAL(AXP2101_CHG_CUR_600MA)     },
    { LSTRKEY("CUR_700MA"),         LINTVAL(AXP2101_CHG_CUR_700MA)     },
    { LSTRKEY("CUR_800MA"),         LINTVAL(AXP2101_CHG_CUR_800MA)     },
    { LSTRKEY("CUR_900MA"),         LINTVAL(AXP2101_CHG_CUR_900MA)     },
    { LSTRKEY("CUR_1000MA"),        LINTVAL(AXP2101_CHG_CUR_1000MA)    },
    /* Charge target voltage constants */
    { LSTRKEY("VOL_4V"),            LINTVAL(AXP2101_CHG_VOL_4V)        },
    { LSTRKEY("VOL_4V1"),           LINTVAL(AXP2101_CHG_VOL_4V1)       },
    { LSTRKEY("VOL_4V2"),           LINTVAL(AXP2101_CHG_VOL_4V2)       },
    { LSTRKEY("VOL_4V35"),          LINTVAL(AXP2101_CHG_VOL_4V35)      },
    { LSTRKEY("VOL_4V4"),           LINTVAL(AXP2101_CHG_VOL_4V4)       },
    /* Charging LED mode constants */
    { LSTRKEY("LED_OFF"),           LINTVAL(AXP2101_CHG_LED_OFF)       },
    { LSTRKEY("LED_BLINK1"),        LINTVAL(AXP2101_CHG_LED_BLINK_1HZ) },
    { LSTRKEY("LED_BLINK4"),        LINTVAL(AXP2101_CHG_LED_BLINK_4HZ) },
    { LSTRKEY("LED_ON"),            LINTVAL(AXP2101_CHG_LED_ON)        },
    { LSTRKEY("LED_CHG"),           LINTVAL(AXP2101_CHG_LED_CTRL_CHG)  },
    /* Power key press-on time constants */
    { LSTRKEY("POWERON_128MS"),     LINTVAL(AXP2101_POWERON_128MS)     },
    { LSTRKEY("POWERON_512MS"),     LINTVAL(AXP2101_POWERON_512MS)     },
    { LSTRKEY("POWERON_1S"),        LINTVAL(AXP2101_POWERON_1S)        },
    { LSTRKEY("POWERON_2S"),        LINTVAL(AXP2101_POWERON_2S)        },
    /* Power key press-off time constants */
    { LSTRKEY("POWEROFF_4S"),       LINTVAL(AXP2101_POWEROFF_4S)       },
    { LSTRKEY("POWEROFF_6S"),       LINTVAL(AXP2101_POWEROFF_6S)       },
    { LSTRKEY("POWEROFF_8S"),       LINTVAL(AXP2101_POWEROFF_8S)       },
    { LSTRKEY("POWEROFF_10S"),      LINTVAL(AXP2101_POWEROFF_10S)      },
    /* ADC channel enable bits */
    { LSTRKEY("ADC_BAT"),           LINTVAL(AXP2101_ADC_BAT_VOL)       },
    { LSTRKEY("ADC_TS"),            LINTVAL(AXP2101_ADC_TS_PIN)        },
    { LSTRKEY("ADC_VBUS"),          LINTVAL(AXP2101_ADC_VBUS_VOL)      },
    { LSTRKEY("ADC_SYS"),           LINTVAL(AXP2101_ADC_SYS_VOL)       },
    { LSTRKEY("ADC_TEMP"),          LINTVAL(AXP2101_ADC_DIE_TEMP)      },
    /* IRQ mask constants (key subset) */
    { LSTRKEY("IRQ_BAT_UTEMP"),     LINTVAL(AXP2101_IRQ_BAT_UNDER_TEMP)   },
    { LSTRKEY("IRQ_BAT_OTEMP"),     LINTVAL(AXP2101_IRQ_BAT_OVER_TEMP)    },
    { LSTRKEY("IRQ_VBUS_IN"),       LINTVAL(AXP2101_IRQ_VBUS_INSERT)      },
    { LSTRKEY("IRQ_VBUS_OUT"),      LINTVAL(AXP2101_IRQ_VBUS_REMOVE)      },
    { LSTRKEY("IRQ_BAT_IN"),        LINTVAL(AXP2101_IRQ_BAT_INSERT)       },
    { LSTRKEY("IRQ_BAT_OUT"),       LINTVAL(AXP2101_IRQ_BAT_REMOVE)       },
    { LSTRKEY("IRQ_PKEY_POS"),      LINTVAL(AXP2101_IRQ_PKEY_POSITIVE)    },
    { LSTRKEY("IRQ_PKEY_NEG"),      LINTVAL(AXP2101_IRQ_PKEY_NEGATIVE)    },
    { LSTRKEY("IRQ_PKEY_LONG"),     LINTVAL(AXP2101_IRQ_PKEY_LONG)        },
    { LSTRKEY("IRQ_PKEY_SHORT"),    LINTVAL(AXP2101_IRQ_PKEY_SHORT)       },
    { LSTRKEY("IRQ_CHG_DONE"),      LINTVAL(AXP2101_IRQ_BAT_CHG_DONE)     },
    { LSTRKEY("IRQ_CHG_START"),     LINTVAL(AXP2101_IRQ_BAT_CHG_START)    },
    { LSTRKEY("IRQ_BAT_OVP"),       LINTVAL(AXP2101_IRQ_BAT_OVER_VOL)     },
    { LSTRKEY("IRQ_WDT"),           LINTVAL(AXP2101_IRQ_WDT_EXPIRE)       },
    { LSTRKEY("IRQ_ALL"),           LINTVAL(AXP2101_IRQ_ALL)              },
    DRIVER_REGISTER_LUA_ERRORS(axp2101)
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_axp2101(lua_State *L) {
    luaL_newmetarotable(L, "axp2101.pmu", (void *)axp2101_inst_map);
    return 0;
}

MODULE_REGISTER_ROM(AXP2101, axp2101, axp2101_map, luaopen_axp2101, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_AXP2101 */
