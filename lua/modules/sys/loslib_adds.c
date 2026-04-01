/*
 * loslib_adds.c — Lua RTOS os-module extensions.
 *
 * In Lua 5.3 (patched), loslib_adds.inc was compiled as part of loslib.c
 * via the luaconf_custom.h hook (#ifdef loslib_c ... #include "loslib_adds.inc").
 * In Lua 5.5 (stock), that hook is gone, so we compile it standalone here
 * and register the extra functions into the os table via luaos_register_os_adds().
 */

#include "sdkconfig.h"
#include "luartos.h"
#include "lua.h"
#include "lauxlib.h"

/* os_settime() in loslib_adds.inc forward-declares os_time() which is static
 * in the stock loslib.c TU and therefore not accessible from here.  Provide
 * a local definition that delegates to os.time via the Lua API. */
static int os_time (lua_State *L) {
    int nargs = lua_gettop(L);
    lua_getglobal(L, "os");
    lua_getfield(L, -1, "time");
    lua_remove(L, -2);     /* drop the 'os' table */
    /* move the function below any existing arguments */
    if (nargs > 0)
        lua_insert(L, 1);
    lua_call(L, nargs, 1);
    return 1;
}

/* loslib_adds.inc includes all other headers it needs at the top */
#include "loslib_adds.inc"

void luaos_register_os_adds (lua_State *L) {
    static const luaL_Reg os_adds[] = {
        {"stdout",          os_stdout},
        {"shell",           os_shell},
        {"edit",            os_edit},
        {"sleep",           os_sleep},
        {"reset_reason",    os_reset_reason},
        {"loglevel",        os_loglevel},
        {"cat",             os_cat},
        {"more",            os_more},
        {"dmesg",           os_dmesg},
        {"cd",              os_cd},
        {"pwd",             os_pwd},
        {"mkdir",           os_mkdir},
        {"ls",              os_ls},
        {"clear",           os_clear},
        {"version",         os_version},
        {"cpu",             os_cpu},
        {"board",           os_board},
        {"logcons",         os_logcons},
        {"syslog",          os_syslog},
        {"stats",           os_stats},
        {"lua_running",     os_lua_running},
        {"lua_interpreter", os_lua_interpreter},
        {"history",         os_history},
        {"cp",              os_cp},
        {"run",             os_run},
        {"bootcount",       os_bootcount},
        {"flash_unique_id", os_flash_unique_id},
        {"exists",          os_exists},
        {"factory_reset",   os_factory_reset},
        {"partitions",      os_partitions},
        {"passwd",          os_passwd},
        {"uptime",          os_uptime},
        {"settime",         os_settime},
        {"format",          os_format},
        {"df",              os_df},
#if CONFIG_LUA_RTOS_USE_RSYSLOG
        {"setrsyslog",      os_setrsyslog},
#endif
#if CONFIG_LUA_RTOS_USE_HARDWARE_LOCKS
        {"locks",           os_locks},
#endif
        {NULL, NULL}
    };

    lua_getglobal(L, "os");
    if (lua_istable(L, -1)) {
        luaL_setfuncs(L, os_adds, 0);
    }
    lua_pop(L, 1);
}
