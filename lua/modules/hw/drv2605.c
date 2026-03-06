/*
 * Lua RTOS, Lua DRV2605 haptic motor driver module
 *
 * Usage example:
 *
 *   dev = drv2605.setup()
 *
 *   -- Play effect 1 (strong click) from default slot 0
 *   dev:play()
 *
 *   -- Load a sequence: effect 47 then a 0.5 s pause then effect 14
 *   dev:setwaveform(0, 47)
 *   dev:setpause(1, 50)
 *   dev:setwaveform(2, 14)
 *   dev:setwaveform(3, 0)   -- terminate sequence
 *   dev:play()
 *
 *   -- Real-time playback at 75% amplitude
 *   dev:mode(drv2605.MODE_REALTIME)
 *   dev:rtpvalue(96)
 *   -- ... later ...
 *   dev:rtpvalue(0)
 *   dev:mode(drv2605.MODE_INTTRIG)
 */

#include "sdkconfig.h"

#if CONFIG_LUA_RTOS_LUA_USE_DRV2605

#include "freertos/FreeRTOS.h"
#include "freertos/adds.h"

#include "lua.h"
#include "lauxlib.h"
#include "modules.h"
#include "error.h"

#include <drivers/drv2605.h>

/* Singleton driver — no per-instance state needed. */
typedef struct { int dummy; } drv2605_userdata_t;

#define CHECK_UD(L) \
    ((void)luaL_checkudata(L, 1, "drv2605.dev"))

/* ------------------------------------------------------------------ */
/* Module-level: setup                                                 */
/* ------------------------------------------------------------------ */

/*
 * drv2605.setup()
 *
 * Initialises I2C0 (SDA=GPIO10, SCL=GPIO11), verifies the device ID,
 * and configures ERM open-loop mode with internal trigger and TS2200A
 * waveform library.  Returns a drv2605 device instance.
 */
static int ldrv2605_setup(lua_State *L) {
    driver_error_t *error;

    if ((error = drv2605_setup()))
        return luaL_driver_error(L, error);

    drv2605_userdata_t *ud = (drv2605_userdata_t *)
        lua_newuserdata(L, sizeof(drv2605_userdata_t));
    ud->dummy = 0;

    luaL_getmetatable(L, "drv2605.dev");
    lua_setmetatable(L, -2);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Playback control                                                    */
/* ------------------------------------------------------------------ */

/* dev:play()  — write 1 to the GO register, starting the sequence */
static int ldrv2605_play(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if ((error = drv2605_play()))
        return luaL_driver_error(L, error);
    return 0;
}

/* dev:stop()  — write 0 to the GO register, stopping the motor */
static int ldrv2605_stop(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if ((error = drv2605_stop()))
        return luaL_driver_error(L, error);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Operating mode                                                      */
/* ------------------------------------------------------------------ */

/*
 * dev:mode([mode])
 *
 * Called with no argument: returns the current mode (integer).
 * Called with a drv2605.MODE_* constant: sets the mode.
 */
static int ldrv2605_mode(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = drv2605_set_mode((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t val;
    if ((error = drv2605_get_mode(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Waveform library                                                    */
/* ------------------------------------------------------------------ */

/*
 * dev:library([lib])
 *
 * Called with no argument: returns the current library (integer).
 * Called with a drv2605.LIBRARY_* constant: selects the library.
 */
static int ldrv2605_library(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        if ((error = drv2605_set_library((uint8_t)luaL_checkinteger(L, 2))))
            return luaL_driver_error(L, error);
        return 0;
    }
    uint8_t val;
    if ((error = drv2605_get_library(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Waveform sequence slots                                             */
/* ------------------------------------------------------------------ */

/*
 * dev:setwaveform(slot, effect_id)
 *
 * Load an effect into a sequence slot.
 *   slot      : 0–7
 *   effect_id : 0–123  (0 terminates the sequence at this slot)
 */
static int ldrv2605_setwaveform(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    uint8_t slot      = (uint8_t)luaL_checkinteger(L, 2);
    uint8_t effect_id = (uint8_t)luaL_checkinteger(L, 3);
    if ((error = drv2605_set_waveform(slot, effect_id)))
        return luaL_driver_error(L, error);
    return 0;
}

/*
 * dev:setpause(slot, duration_cs)
 *
 * Insert a timed pause into a sequence slot.
 *   slot        : 0–7
 *   duration_cs : pause length in centiseconds (1–127, i.e. 0.01–1.27 s)
 */
static int ldrv2605_setpause(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    uint8_t slot        = (uint8_t)luaL_checkinteger(L, 2);
    uint8_t duration_cs = (uint8_t)luaL_checkinteger(L, 3);
    if ((error = drv2605_set_pause(slot, duration_cs)))
        return luaL_driver_error(L, error);
    return 0;
}

/*
 * dev:getslot(slot) → integer
 *
 * Read the raw byte from a sequence slot.
 * If bit 7 is clear it is an effect ID; if set it is a pause entry.
 */
static int ldrv2605_getslot(lua_State *L) {
    driver_error_t *error;
    uint8_t val;
    CHECK_UD(L);
    uint8_t slot = (uint8_t)luaL_checkinteger(L, 2);
    if ((error = drv2605_get_slot(slot, &val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Real-time playback                                                  */
/* ------------------------------------------------------------------ */

/*
 * dev:rtpvalue([val])
 *
 * Called with no argument: returns the current RTP register value.
 * Called with a value (−127..127): sets the RTP output level.
 * The motor runs continuously at this amplitude while in MODE_REALTIME.
 */
static int ldrv2605_rtpvalue(lua_State *L) {
    driver_error_t *error;
    CHECK_UD(L);
    if (lua_gettop(L) >= 2) {
        int8_t val = (int8_t)luaL_checkinteger(L, 2);
        if ((error = drv2605_set_realtime_value(val)))
            return luaL_driver_error(L, error);
        return 0;
    }
    int8_t val;
    if ((error = drv2605_get_realtime_value(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Status                                                              */
/* ------------------------------------------------------------------ */

/* dev:status() → integer  — raw STATUS register; bits[7:5] = device ID */
static int ldrv2605_status(lua_State *L) {
    driver_error_t *error;
    uint8_t val;
    CHECK_UD(L);
    if ((error = drv2605_get_status(&val)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, val);
    return 1;
}

/* ------------------------------------------------------------------ */
/* GC                                                                  */
/* ------------------------------------------------------------------ */

static int ldrv2605_gc(lua_State *L) {
    (void)L;   /* singleton driver — nothing to release */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Registration tables                                                 */
/* ------------------------------------------------------------------ */

static const LUA_REG_TYPE drv2605_inst_map[] = {
    { LSTRKEY("play"),        LFUNCVAL(ldrv2605_play)        },
    { LSTRKEY("stop"),        LFUNCVAL(ldrv2605_stop)        },
    { LSTRKEY("mode"),        LFUNCVAL(ldrv2605_mode)        },
    { LSTRKEY("library"),     LFUNCVAL(ldrv2605_library)     },
    { LSTRKEY("setwaveform"), LFUNCVAL(ldrv2605_setwaveform) },
    { LSTRKEY("setpause"),    LFUNCVAL(ldrv2605_setpause)    },
    { LSTRKEY("getslot"),     LFUNCVAL(ldrv2605_getslot)     },
    { LSTRKEY("rtpvalue"),    LFUNCVAL(ldrv2605_rtpvalue)    },
    { LSTRKEY("status"),      LFUNCVAL(ldrv2605_status)      },
    { LSTRKEY("__metatable"), LROVAL  (drv2605_inst_map)     },
    { LSTRKEY("__index"),     LROVAL  (drv2605_inst_map)     },
    { LSTRKEY("__gc"),        LFUNCVAL(ldrv2605_gc)          },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE drv2605_map[] = {
    { LSTRKEY("setup"),           LFUNCVAL(ldrv2605_setup)              },
    /* Operating mode constants */
    { LSTRKEY("MODE_INTTRIG"),    LINTVAL(DRV2605_MODE_INTTRIG)         },
    { LSTRKEY("MODE_EXTTRIGEDGE"),LINTVAL(DRV2605_MODE_EXTTRIGEDGE)     },
    { LSTRKEY("MODE_EXTTRIGLVL"), LINTVAL(DRV2605_MODE_EXTTRIGLVL)      },
    { LSTRKEY("MODE_PWMANALOG"),  LINTVAL(DRV2605_MODE_PWMANALOG)       },
    { LSTRKEY("MODE_AUDIOVIBE"),  LINTVAL(DRV2605_MODE_AUDIOVIBE)       },
    { LSTRKEY("MODE_REALTIME"),   LINTVAL(DRV2605_MODE_REALTIME)        },
    { LSTRKEY("MODE_DIAGNOS"),    LINTVAL(DRV2605_MODE_DIAGNOS)         },
    { LSTRKEY("MODE_AUTOCAL"),    LINTVAL(DRV2605_MODE_AUTOCAL)         },
    /* Waveform library constants */
    { LSTRKEY("LIBRARY_EMPTY"),   LINTVAL(DRV2605_LIBRARY_EMPTY)        },
    { LSTRKEY("LIBRARY_TS2200A"), LINTVAL(DRV2605_LIBRARY_TS2200A)      },
    { LSTRKEY("LIBRARY_TS2200B"), LINTVAL(DRV2605_LIBRARY_TS2200B)      },
    { LSTRKEY("LIBRARY_TS2200C"), LINTVAL(DRV2605_LIBRARY_TS2200C)      },
    { LSTRKEY("LIBRARY_TS2200D"), LINTVAL(DRV2605_LIBRARY_TS2200D)      },
    { LSTRKEY("LIBRARY_TS2200E"), LINTVAL(DRV2605_LIBRARY_TS2200E)      },
    { LSTRKEY("LIBRARY_LRA"),     LINTVAL(DRV2605_LIBRARY_LRA)          },
    DRIVER_REGISTER_LUA_ERRORS(drv2605)
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_drv2605(lua_State *L) {
    luaL_newmetarotable(L, "drv2605.dev", (void *)drv2605_inst_map);
    return 0;
}

MODULE_REGISTER_ROM(DRV2605, drv2605, drv2605_map, luaopen_drv2605, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_DRV2605 */
