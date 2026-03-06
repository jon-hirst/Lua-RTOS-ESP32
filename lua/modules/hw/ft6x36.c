/*
 * Lua RTOS, Lua FT6x36 capacitive touch controller module
 *
 * Usage example:
 *
 *   dev = ft6x36.setup(240, 240)
 *
 *   -- Poll for touch events
 *   local n, x1, y1, w1, a1 = dev:positions()
 *   if n > 0 then
 *       print(string.format("touch at (%d,%d) w=%d area=%d", x1, y1, w1, a1))
 *   end
 *
 *   -- Read gesture
 *   local g = dev:gesture()
 *   if g ~= ft6x36.GESTURE_NONE then
 *       print("gesture:", g)
 *   end
 *
 *   -- Read chip info
 *   print("fw:", dev:fwversion())
 *   print("vendor:", dev:vendorid())
 *
 *   -- Change rotation
 *   dev:rotation(ft6x36.LANDSCAPE)
 */

#include "sdkconfig.h"

#if CONFIG_LUA_RTOS_LUA_USE_FT6X36

#include "freertos/FreeRTOS.h"
#include "freertos/adds.h"

#include "lua.h"
#include "lauxlib.h"
#include "modules.h"
#include "error.h"

#include <drivers/ft6x36.h>

/* Singleton driver — no per-instance state. */
typedef struct { int dummy; } ft6x36_userdata_t;

#define CHECK_UD(L) \
    ((void)luaL_checkudata(L, 1, "ft6x36.dev"))

/* ------------------------------------------------------------------ */
/* Module-level: setup                                                 */
/* ------------------------------------------------------------------ */

/*
 * ft6x36.setup([width [, height]])
 *
 * Initialises I2C1 (SDA=GPIO39, SCL=GPIO40) and verifies the device
 * responds.  width and height default to 240 and are used for rotation
 * coordinate remapping.  Returns an ft6x36 device instance.
 */
static int lft6x36_setup(lua_State *L) {
    driver_error_t *error;

    uint16_t width  = (uint16_t)luaL_optinteger(L, 1, 240);
    uint16_t height = (uint16_t)luaL_optinteger(L, 2, 240);

    if ((error = ft6x36_setup(width, height)))
        return luaL_driver_error(L, error);

    ft6x36_userdata_t *ud = (ft6x36_userdata_t *)
        lua_newuserdata(L, sizeof(ft6x36_userdata_t));
    ud->dummy = 0;

    luaL_getmetatable(L, "ft6x36.dev");
    lua_setmetatable(L, -2);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Touch data                                                          */
/* ------------------------------------------------------------------ */

/*
 * dev:positions()  →  count [, x1, y1, weight1, area1 [, x2, y2, weight2, area2]]
 *
 * Returns the number of active touch points followed by x, y, weight
 * and area for each point (rotation-adjusted coordinates).
 */
static int lft6x36_positions(lua_State *L) {
    driver_error_t *error;
    ft6x36_point_t points[2];
    uint8_t num = 0;

    CHECK_UD(L);
    if ((error = ft6x36_get_positions(&num, points)))
        return luaL_driver_error(L, error);

    lua_pushinteger(L, num);
    for (int i = 0; i < num; i++) {
        lua_pushinteger(L, points[i].x);
        lua_pushinteger(L, points[i].y);
        lua_pushinteger(L, points[i].weight);
        lua_pushinteger(L, points[i].area);
    }
    return 1 + num * 4;
}

/*
 * dev:gesture()  →  integer (ft6x36.GESTURE_*)
 *
 * Returns the current decoded gesture code.
 */
static int lft6x36_gesture(lua_State *L) {
    driver_error_t *error;
    uint8_t g;
    CHECK_UD(L);
    if ((error = ft6x36_get_gesture(&g)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, g);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Rotation                                                            */
/* ------------------------------------------------------------------ */

/*
 * dev:rotation([r])
 *
 * Called with no argument: returns the current rotation (0-3).
 * Called with r = ft6x36.PORTRAIT | LANDSCAPE | ...: sets the rotation.
 */
static int lft6x36_rotation(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        uint8_t r = (uint8_t)luaL_checkinteger(L, 2);
        if ((error = ft6x36_set_rotation(r)))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t r;
    if ((error = ft6x36_get_rotation(&r)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, r);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Configuration registers (get/set pattern)                          */
/* ------------------------------------------------------------------ */

/*
 * dev:threshold([v])  — touch detection threshold (TH_GROUP)
 */
static int lft6x36_threshold(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = ft6x36_set_threshold((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t v;
    if ((error = ft6x36_get_threshold(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/*
 * dev:monitortime([v])  — seconds before entering monitor mode
 */
static int lft6x36_monitortime(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = ft6x36_set_monitor_time((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t v;
    if ((error = ft6x36_get_monitor_time(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/*
 * dev:activeperiod([v])  — report rate in active mode (ms)
 */
static int lft6x36_activeperiod(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = ft6x36_set_active_period((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t v;
    if ((error = ft6x36_get_active_period(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/*
 * dev:monitorperiod([v])  — report rate in monitor mode (ms)
 */
static int lft6x36_monitorperiod(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = ft6x36_set_monitor_period((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t v;
    if ((error = ft6x36_get_monitor_period(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/*
 * dev:intmode([v])  — interrupt mode (ft6x36.POLLING_MODE / TRIGGER_MODE)
 */
static int lft6x36_intmode(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = ft6x36_set_int_mode((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t v;
    if ((error = ft6x36_get_int_mode(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/*
 * dev:powermode([v])  — power mode register
 */
static int lft6x36_powermode(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = ft6x36_set_power_mode((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t v;
    if ((error = ft6x36_get_power_mode(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Read-only chip information                                          */
/* ------------------------------------------------------------------ */

/* dev:libversion()  →  integer (16-bit) */
static int lft6x36_libversion(lua_State *L) {
    driver_error_t *error;
    uint16_t v;
    CHECK_UD(L);
    if ((error = ft6x36_get_lib_version(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/* dev:fwversion()  →  integer (8-bit) */
static int lft6x36_fwversion(lua_State *L) {
    driver_error_t *error;
    uint8_t v;
    CHECK_UD(L);
    if ((error = ft6x36_get_fw_version(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/* dev:vendorid()  →  integer */
static int lft6x36_vendorid(lua_State *L) {
    driver_error_t *error;
    uint8_t v;
    CHECK_UD(L);
    if ((error = ft6x36_get_vendor_id(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/* dev:panelid()  →  integer */
static int lft6x36_panelid(lua_State *L) {
    driver_error_t *error;
    uint8_t v;
    CHECK_UD(L);
    if ((error = ft6x36_get_panel_id(&v))) return luaL_driver_error(L, error);
    lua_pushinteger(L, v);
    return 1;
}

/* ------------------------------------------------------------------ */
/* GC                                                                  */
/* ------------------------------------------------------------------ */

static int lft6x36_gc(lua_State *L) {
    (void)L;   /* singleton driver — nothing to release */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Registration tables                                                 */
/* ------------------------------------------------------------------ */

static const LUA_REG_TYPE ft6x36_inst_map[] = {
    { LSTRKEY("positions"),    LFUNCVAL(lft6x36_positions)    },
    { LSTRKEY("gesture"),      LFUNCVAL(lft6x36_gesture)      },
    { LSTRKEY("rotation"),     LFUNCVAL(lft6x36_rotation)     },
    { LSTRKEY("threshold"),    LFUNCVAL(lft6x36_threshold)    },
    { LSTRKEY("monitortime"),  LFUNCVAL(lft6x36_monitortime)  },
    { LSTRKEY("activeperiod"), LFUNCVAL(lft6x36_activeperiod) },
    { LSTRKEY("monitorperiod"),LFUNCVAL(lft6x36_monitorperiod)},
    { LSTRKEY("intmode"),      LFUNCVAL(lft6x36_intmode)      },
    { LSTRKEY("powermode"),    LFUNCVAL(lft6x36_powermode)    },
    { LSTRKEY("libversion"),   LFUNCVAL(lft6x36_libversion)   },
    { LSTRKEY("fwversion"),    LFUNCVAL(lft6x36_fwversion)    },
    { LSTRKEY("vendorid"),     LFUNCVAL(lft6x36_vendorid)     },
    { LSTRKEY("panelid"),      LFUNCVAL(lft6x36_panelid)      },
    { LSTRKEY("__metatable"),  LROVAL  (ft6x36_inst_map)      },
    { LSTRKEY("__index"),      LROVAL  (ft6x36_inst_map)      },
    { LSTRKEY("__gc"),         LFUNCVAL(lft6x36_gc)           },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE ft6x36_map[] = {
    { LSTRKEY("setup"),                   LFUNCVAL(lft6x36_setup)               },
    /* Rotation constants */
    { LSTRKEY("PORTRAIT"),                LINTVAL(FT6X36_PORTRAIT)              },
    { LSTRKEY("LANDSCAPE"),               LINTVAL(FT6X36_LANDSCAPE)             },
    { LSTRKEY("PORTRAIT_INVERTED"),       LINTVAL(FT6X36_PORTRAIT_INVERTED)     },
    { LSTRKEY("LANDSCAPE_INVERTED"),      LINTVAL(FT6X36_LANDSCAPE_INVERTED)    },
    /* Gesture constants */
    { LSTRKEY("GESTURE_NONE"),            LINTVAL(FT6X36_GESTURE_NONE)          },
    { LSTRKEY("GESTURE_MOVE_UP"),         LINTVAL(FT6X36_GESTURE_MOVE_UP)       },
    { LSTRKEY("GESTURE_MOVE_LEFT"),       LINTVAL(FT6X36_GESTURE_MOVE_LEFT)     },
    { LSTRKEY("GESTURE_MOVE_DOWN"),       LINTVAL(FT6X36_GESTURE_MOVE_DOWN)     },
    { LSTRKEY("GESTURE_MOVE_RIGHT"),      LINTVAL(FT6X36_GESTURE_MOVE_RIGHT)    },
    { LSTRKEY("GESTURE_ZOOM_IN"),         LINTVAL(FT6X36_GESTURE_ZOOM_IN)       },
    { LSTRKEY("GESTURE_ZOOM_OUT"),        LINTVAL(FT6X36_GESTURE_ZOOM_OUT)      },
    /* Interrupt mode constants */
    { LSTRKEY("POLLING_MODE"),            LINTVAL(FT6X36_POLLING_MODE)          },
    { LSTRKEY("TRIGGER_MODE"),            LINTVAL(FT6X36_TRIGGER_MODE)          },
    DRIVER_REGISTER_LUA_ERRORS(ft6x36)
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_ft6x36(lua_State *L) {
    luaL_newmetarotable(L, "ft6x36.dev", (void *)ft6x36_inst_map);
    return 0;
}

MODULE_REGISTER_ROM(FT6X36, ft6x36, ft6x36_map, luaopen_ft6x36, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_FT6X36 */
