/*
 * Lua RTOS, Lua BMA423 accelerometer module
 *
 * Usage example:
 *
 *   dev = bma423.setup()
 *
 *   -- Load features config blob and enable step counting
 *   dev:loadconfig()
 *   dev:enablefeatures("step-count")
 *
 *   -- Read acceleration, temperature, steps
 *   x, y, z = dev:xyz()
 *   temp     = dev:temperature()
 *   steps    = dev:steps()
 *
 *   -- Interrupt callback (fires on data-ready from INT1/GPIO14)
 *   dev:enableinterrupt(function(s0, s1)
 *       print(string.format("INT s0=0x%02X s1=0x%02X", s0, s1))
 *   end)
 *
 *   -- Disable interrupt when done
 *   dev:disableinterrupt()
 */

#include "sdkconfig.h"

#if CONFIG_LUA_RTOS_LUA_USE_BMA423

#include "freertos/FreeRTOS.h"
#include "freertos/adds.h"

#include "lua.h"
#include "lauxlib.h"
#include "modules.h"
#include "error.h"
#include "sys.h"

#include <drivers/bma423.h>

/* Singleton driver — no per-instance state. */
typedef struct { int dummy; } bma423_userdata_t;

#define CHECK_UD(L) \
    ((void)luaL_checkudata(L, 1, "bma423.dev"))

/* ------------------------------------------------------------------ */
/* Interrupt callback bridge (C driver → Lua)                         */
/* ------------------------------------------------------------------ */

static lua_callback_t *bma423_lua_cb = NULL;

static void bma423_callback_bridge(uint8_t status0, uint8_t status1) {
    if (bma423_lua_cb == NULL) return;
    lua_State *state = luaS_callback_state(bma423_lua_cb);
    if (state == NULL) return;
    lua_pushinteger(state, status0);
    lua_pushinteger(state, status1);
    luaS_callback_call(bma423_lua_cb, 2);
}

/* ------------------------------------------------------------------ */
/* Module-level: setup                                                 */
/* ------------------------------------------------------------------ */

/*
 * bma423.setup()
 *
 * Initialises I2C0 (SDA=GPIO10, SCL=GPIO11), soft-resets the BMA423,
 * verifies chip ID, and configures 100 Hz / 2 g / performance mode.
 * Returns a bma423 device instance.
 */
static int lbma423_setup(lua_State *L) {
    driver_error_t *error;

    if ((error = bma423_setup()))
        return luaL_driver_error(L, error);

    bma423_userdata_t *ud = (bma423_userdata_t *)
        lua_newuserdata(L, sizeof(bma423_userdata_t));
    ud->dummy = 0;

    luaL_getmetatable(L, "bma423.dev");
    lua_setmetatable(L, -2);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Features config                                                     */
/* ------------------------------------------------------------------ */

/*
 * dev:loadconfig(path)
 *
 * Uploads the binary features configuration at the given filesystem
 * path into the BMA423 ASIC memory and enables the features engine.
 * Required before step counting or other feature-detection functions.
 * Example: dev:loadconfig("/spiffs/bma423conf.bin")
 */
static int lbma423_loadconfig(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    const char *path = luaL_checkstring(L, 2);
    if ((error = bma423_load_config(path)))
        return luaL_driver_error(L, error);
    return 0;
}

/*
 * dev:enablefeatures(name [, name ...])
 *
 * Enable one or more named features.  Supported names:
 *   "step-count"  — pedometer
 * loadconfig() must be called first.
 */
static int lbma423_enablefeatures(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    int n = lua_gettop(L);
    for (int i = 2; i <= n; i++) {
        const char *name = luaL_checkstring(L, i);
        if ((error = bma423_enable_feature(name)))
            return luaL_driver_error(L, error);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Sensor readings                                                     */
/* ------------------------------------------------------------------ */

/*
 * dev:xyz()  →  x, y, z
 *
 * Returns three floats: acceleration in g along each axis.
 */
static int lbma423_xyz(lua_State *L) {
    driver_error_t *error;
    float x, y, z;
    CHECK_UD(L);
    if ((error = bma423_get_xyz(&x, &y, &z)))
        return luaL_driver_error(L, error);
    lua_pushnumber(L, (lua_Number)x);
    lua_pushnumber(L, (lua_Number)y);
    lua_pushnumber(L, (lua_Number)z);
    return 3;
}

/*
 * dev:temperature()  →  integer | nil
 *
 * Returns die temperature in degrees Celsius, or nil if the sensor
 * reports an invalid reading.
 */
static int lbma423_temperature(lua_State *L) {
    driver_error_t *error;
    int celsius;
    bool valid;
    CHECK_UD(L);
    if ((error = bma423_get_temperature(&celsius, &valid)))
        return luaL_driver_error(L, error);
    if (!valid) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, celsius);
    }
    return 1;
}

/*
 * dev:steps()  →  integer
 *
 * Returns the cumulative step count since the last reset.
 */
static int lbma423_steps(lua_State *L) {
    driver_error_t *error;
    uint32_t steps;
    CHECK_UD(L);
    if ((error = bma423_get_steps(&steps)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, (lua_Integer)steps);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Range                                                               */
/* ------------------------------------------------------------------ */

/*
 * dev:range([r])
 *
 * Called with no argument: returns the current range (2, 4, 8, or 16).
 * Called with r = 2 | 4 | 8 | 16: sets the measurement range.
 */
static int lbma423_range(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        uint8_t r = (uint8_t)luaL_checkinteger(L, 2);
        if ((error = bma423_set_range(r)))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t r;
    if ((error = bma423_get_range(&r)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, r);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Interrupt                                                           */
/* ------------------------------------------------------------------ */

/*
 * dev:enableinterrupt(callback)
 *
 * Attach a rising-edge interrupt on GPIO14 (BMA423 INT1).
 * callback(status0, status1) is called from a task when the interrupt
 * fires.  status0 and status1 are the raw INT_STATUS_0 / INT_STATUS_1
 * register bytes.
 */
static int lbma423_enableinterrupt(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    if (bma423_lua_cb != NULL) {
        luaS_callback_destroy(bma423_lua_cb);
        bma423_lua_cb = NULL;
    }
    bma423_lua_cb = luaS_callback_create(L, 2);

    if ((error = bma423_enable_interrupt(bma423_callback_bridge)))
        return luaL_driver_error(L, error);
    return 0;
}

/*
 * dev:disableinterrupt()
 *
 * Detach the GPIO14 ISR and stop the deferred callback task.
 */
static int lbma423_disableinterrupt(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if ((error = bma423_disable_interrupt()))
        return luaL_driver_error(L, error);
    if (bma423_lua_cb != NULL) {
        luaS_callback_destroy(bma423_lua_cb);
        bma423_lua_cb = NULL;
    }
    return 0;
}

/*
 * dev:intstatus()  →  status0, status1
 *
 * Read both interrupt status registers.  Clears latched interrupts.
 */
static int lbma423_intstatus(lua_State *L) {
    driver_error_t *error;
    uint8_t s0, s1;
    CHECK_UD(L);
    if ((error = bma423_get_int_status(&s0, &s1)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, s0);
    lua_pushinteger(L, s1);
    return 2;
}

/* ------------------------------------------------------------------ */
/* GC                                                                  */
/* ------------------------------------------------------------------ */

static int lbma423_gc(lua_State *L) {
    (void)L;   /* singleton driver — nothing to release */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Registration tables                                                 */
/* ------------------------------------------------------------------ */

static const LUA_REG_TYPE bma423_inst_map[] = {
    { LSTRKEY("loadconfig"),      LFUNCVAL(lbma423_loadconfig)      },
    { LSTRKEY("enablefeatures"),  LFUNCVAL(lbma423_enablefeatures)  },
    { LSTRKEY("xyz"),             LFUNCVAL(lbma423_xyz)             },
    { LSTRKEY("temperature"),     LFUNCVAL(lbma423_temperature)     },
    { LSTRKEY("steps"),           LFUNCVAL(lbma423_steps)           },
    { LSTRKEY("range"),           LFUNCVAL(lbma423_range)           },
    { LSTRKEY("enableinterrupt"), LFUNCVAL(lbma423_enableinterrupt) },
    { LSTRKEY("disableinterrupt"),LFUNCVAL(lbma423_disableinterrupt)},
    { LSTRKEY("intstatus"),       LFUNCVAL(lbma423_intstatus)       },
    { LSTRKEY("__metatable"),     LROVAL  (bma423_inst_map)         },
    { LSTRKEY("__index"),         LROVAL  (bma423_inst_map)         },
    { LSTRKEY("__gc"),            LFUNCVAL(lbma423_gc)              },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE bma423_map[] = {
    { LSTRKEY("setup"),        LFUNCVAL(lbma423_setup)             },
    /* Range constants */
    { LSTRKEY("RANGE_2G"),     LINTVAL(BMA423_RANGE_2G)            },
    { LSTRKEY("RANGE_4G"),     LINTVAL(BMA423_RANGE_4G)            },
    { LSTRKEY("RANGE_8G"),     LINTVAL(BMA423_RANGE_8G)            },
    { LSTRKEY("RANGE_16G"),    LINTVAL(BMA423_RANGE_16G)           },
    /* Interrupt status bit masks (INT_STATUS_0) */
    { LSTRKEY("INT_STEP"),     LINTVAL(BMA423_INT_STEP)            },
    { LSTRKEY("INT_ACTIVITY"), LINTVAL(BMA423_INT_ACTIVITY)        },
    { LSTRKEY("INT_TILT"),     LINTVAL(BMA423_INT_TILT)            },
    { LSTRKEY("INT_ANY_NONE"), LINTVAL(BMA423_INT_ANY_NONE)        },
    DRIVER_REGISTER_LUA_ERRORS(bma423)
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_bma423(lua_State *L) {
    luaL_newmetarotable(L, "bma423.dev", (void *)bma423_inst_map);
    return 0;
}

MODULE_REGISTER_ROM(BMA423, bma423, bma423_map, luaopen_bma423, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_BMA423 */
