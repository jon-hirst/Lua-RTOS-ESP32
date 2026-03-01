/*
 * Lua RTOS, Lua PCF8563 RTC module
 *
 * Exposes the PCF8563 real-time clock driver as a Lua module.
 *
 * Usage example:
 *
 *   rtc = pcf8563.setup(true)   -- true = enable alarm interrupt
 *
 *   -- Set date/time
 *   rtc:setdatetime(2026, 2, 27, 14, 30, 0, 4)
 *
 *   -- Read date/time
 *   year,month,mday,hour,min,sec,wday = rtc:getdatetime()
 *
 *   -- Set alarm at 14:31 (minute and hour match only)
 *   rtc:setalarm(14, 31, nil, nil)
 *
 *   -- Register callback fired when alarm triggers
 *   rtc:onalarm(function() print("alarm!") end)
 *
 *   -- Check / clear alarm flag
 *   if rtc:alarmactive(true) then print("was active, now cleared") end
 */

#include "sdkconfig.h"

#if CONFIG_LUA_RTOS_LUA_USE_PCF8563

#include "freertos/FreeRTOS.h"
#include "freertos/adds.h"

#include "lua.h"
#include "lauxlib.h"
#include "modules.h"
#include "error.h"
#include "sys.h"

#include <drivers/pcf8563.h>

/* Per-object userdata stored in the Lua value */
typedef struct {
    lua_callback_t *callback;
} pcf8563_userdata_t;

/* ------------------------------------------------------------------ */
/* Alarm callback bridge                                               */
/* ------------------------------------------------------------------ */

/* Called from the driver's deferred-interrupt task; invokes the
 * registered Lua function with no arguments. */
static void alarm_cb(void *arg) {
    lua_callback_t *cb = (lua_callback_t *)arg;
    if (cb != NULL) {
        lua_State *state = luaS_callback_state(cb);
        if (state != NULL) {
            luaS_callback_call(cb, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Module-level functions                                              */
/* ------------------------------------------------------------------ */

/*
 * pcf8563.setup([alarm_irq])
 *
 * Initialises the RTC.  Pass true to enable the alarm interrupt on
 * GPIO17 (default: false).  Returns a pcf8563 instance.
 */
static int lpcf8563_setup(lua_State *L) {
    driver_error_t *error;

    bool alarm_irq = lua_toboolean(L, 1);

    if ((error = pcf8563_setup(alarm_irq))) {
        return luaL_driver_error(L, error);
    }

    pcf8563_userdata_t *ud = (pcf8563_userdata_t *)
        lua_newuserdata(L, sizeof(pcf8563_userdata_t));
    if (!ud) {
        return luaL_exception(L, PCF8563_ERR_CANT_INIT);
    }

    ud->callback = NULL;

    luaL_getmetatable(L, "pcf8563.rtc");
    lua_setmetatable(L, -2);

    return 1;
}

/* ------------------------------------------------------------------ */
/* Instance methods                                                    */
/* ------------------------------------------------------------------ */

/*
 * rtc:getdatetime()
 *
 * Returns seven values: year, month, mday, hour, minute, second, weekday
 *   year    1900..2099
 *   month   1..12
 *   mday    1..31
 *   hour    0..23
 *   minute  0..59
 *   second  0..59
 *   weekday 0 (Sunday) .. 6 (Saturday)
 */
static int lpcf8563_getdatetime(lua_State *L) {
    luaL_checkudata(L, 1, "pcf8563.rtc");

    driver_error_t *error;
    pcf8563_datetime_t dt;

    if ((error = pcf8563_get_datetime(&dt))) {
        return luaL_driver_error(L, error);
    }

    lua_pushinteger(L, dt.year);
    lua_pushinteger(L, dt.month);
    lua_pushinteger(L, dt.mday);
    lua_pushinteger(L, dt.hour);
    lua_pushinteger(L, dt.minute);
    lua_pushinteger(L, dt.second);
    lua_pushinteger(L, dt.weekday);

    return 7;
}

/*
 * rtc:setdatetime(year, month, mday, hour, minute, second, weekday)
 */
static int lpcf8563_setdatetime(lua_State *L) {
    luaL_checkudata(L, 1, "pcf8563.rtc");

    driver_error_t *error;
    pcf8563_datetime_t dt;

    dt.year    = luaL_checkinteger(L, 2);
    dt.month   = luaL_checkinteger(L, 3);
    dt.mday    = luaL_checkinteger(L, 4);
    dt.hour    = luaL_checkinteger(L, 5);
    dt.minute  = luaL_checkinteger(L, 6);
    dt.second  = luaL_checkinteger(L, 7);
    dt.weekday = luaL_checkinteger(L, 8);

    if ((error = pcf8563_set_datetime(&dt))) {
        return luaL_driver_error(L, error);
    }

    return 0;
}

/*
 * rtc:getalarm()
 *
 * Returns four values: hour, minute, mday, weekday.
 * A disabled field is returned as nil.
 */
static int lpcf8563_getalarm(lua_State *L) {
    luaL_checkudata(L, 1, "pcf8563.rtc");

    driver_error_t *error;
    pcf8563_alarm_t alarm;

    if ((error = pcf8563_get_alarm(&alarm))) {
        return luaL_driver_error(L, error);
    }

    if (alarm.hour    < 0) lua_pushnil(L); else lua_pushinteger(L, alarm.hour);
    if (alarm.minute  < 0) lua_pushnil(L); else lua_pushinteger(L, alarm.minute);
    if (alarm.mday    < 0) lua_pushnil(L); else lua_pushinteger(L, alarm.mday);
    if (alarm.weekday < 0) lua_pushnil(L); else lua_pushinteger(L, alarm.weekday);

    return 4;
}

/*
 * rtc:setalarm(hour, minute, mday, weekday)
 *
 * Pass nil for any field to disable that match condition.
 * Pass all nil to disable the alarm entirely.
 *
 * Example – fire every day at 07:00:
 *   rtc:setalarm(7, 0, nil, nil)
 */
static int lpcf8563_setalarm(lua_State *L) {
    luaL_checkudata(L, 1, "pcf8563.rtc");

    driver_error_t *error;
    pcf8563_alarm_t alarm;

    alarm.hour    = lua_isnil(L, 2) ? -1 : (int)luaL_checkinteger(L, 2);
    alarm.minute  = lua_isnil(L, 3) ? -1 : (int)luaL_checkinteger(L, 3);
    alarm.mday    = lua_isnil(L, 4) ? -1 : (int)luaL_checkinteger(L, 4);
    alarm.weekday = lua_isnil(L, 5) ? -1 : (int)luaL_checkinteger(L, 5);

    if ((error = pcf8563_set_alarm(&alarm))) {
        return luaL_driver_error(L, error);
    }

    return 0;
}

/*
 * rtc:alarmactive([clear])
 *
 * Returns true if the alarm flag (AF) is currently set.
 * If clear is true the flag is cleared before returning.
 */
static int lpcf8563_alarmactive(lua_State *L) {
    luaL_checkudata(L, 1, "pcf8563.rtc");

    driver_error_t *error;
    bool active;
    bool clear = lua_toboolean(L, 2);

    if ((error = pcf8563_alarm_active(&active, clear))) {
        return luaL_driver_error(L, error);
    }

    lua_pushboolean(L, active ? 1 : 0);
    return 1;
}

/*
 * rtc:onalarm(function | nil)
 *
 * Register a Lua function to be called when the alarm interrupt fires.
 * Pass nil to deregister.  The callback receives no arguments.
 *
 * Note: pcf8563.setup(true) must have been called for interrupts to work.
 */
static int lpcf8563_onalarm(lua_State *L) {
    pcf8563_userdata_t *ud = (pcf8563_userdata_t *)luaL_checkudata(L, 1, "pcf8563.rtc");
    driver_error_t *error;

    /* Destroy any previously registered callback */
    if (ud->callback != NULL) {
        luaS_callback_destroy(ud->callback);
        ud->callback = NULL;
    }

    /* Register the new one (or clear if nil) */
    if (lua_isfunction(L, 2)) {
        luaL_checktype(L, 2, LUA_TFUNCTION);
        ud->callback = luaS_callback_create(L, 2);
    }

    if ((error = pcf8563_register_alarm_callback(
                     ud->callback ? alarm_cb : NULL,
                     (void *)ud->callback))) {
        return luaL_driver_error(L, error);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* GC / destructor                                                     */
/* ------------------------------------------------------------------ */

static int lpcf8563_gc(lua_State *L) {
    pcf8563_userdata_t *ud = (pcf8563_userdata_t *)luaL_checkudata(L, 1, "pcf8563.rtc");

    if (ud->callback != NULL) {
        pcf8563_register_alarm_callback(NULL, NULL);
        luaS_callback_destroy(ud->callback);
        ud->callback = NULL;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Registration tables                                                 */
/* ------------------------------------------------------------------ */

static const LUA_REG_TYPE pcf8563_inst_map[] = {
    { LSTRKEY("getdatetime"),  LFUNCVAL(lpcf8563_getdatetime) },
    { LSTRKEY("setdatetime"),  LFUNCVAL(lpcf8563_setdatetime) },
    { LSTRKEY("getalarm"),     LFUNCVAL(lpcf8563_getalarm)    },
    { LSTRKEY("setalarm"),     LFUNCVAL(lpcf8563_setalarm)    },
    { LSTRKEY("alarmactive"),  LFUNCVAL(lpcf8563_alarmactive) },
    { LSTRKEY("onalarm"),      LFUNCVAL(lpcf8563_onalarm)     },
    { LSTRKEY("__metatable"),  LROVAL  (pcf8563_inst_map)     },
    { LSTRKEY("__index"),      LROVAL  (pcf8563_inst_map)     },
    { LSTRKEY("__gc"),         LFUNCVAL(lpcf8563_gc)          },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE pcf8563_map[] = {
    { LSTRKEY("setup"),        LFUNCVAL(lpcf8563_setup)  },
    DRIVER_REGISTER_LUA_ERRORS(pcf8563)
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_pcf8563(lua_State *L) {
    luaL_newmetarotable(L, "pcf8563.rtc", (void *)pcf8563_inst_map);
    return 0;
}

MODULE_REGISTER_ROM(PCF8563, pcf8563, pcf8563_map, luaopen_pcf8563, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_PCF8563 */
