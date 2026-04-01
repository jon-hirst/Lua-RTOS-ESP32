/*
 * liolib_adds.c — Lua RTOS io-module extensions.
 *
 * In Lua 5.3 (patched), liolib_adds.inc was compiled as part of liolib.c
 * via the luaconf_custom.h hook (#ifdef liolib_c ... #include "liolib_adds.inc").
 * In Lua 5.5 (stock), that hook is gone, so we compile it standalone here
 * and register the extra functions into the io table via luaos_register_io_adds().
 */

#include "sdkconfig.h"
#include "luartos.h"
#include "lua.h"
#include "lauxlib.h"

/* liolib_adds.inc includes all other headers it needs at the top */
#include "liolib_adds.inc"

void luaos_register_io_adds (lua_State *L) {
    static const luaL_Reg io_adds[] = {
        {"receive",    f_receive},
        {"send",       f_send},
        {"attributes", f_attributes},
        {NULL, NULL}
    };
    lua_getglobal(L, "io");
    if (lua_istable(L, -1)) {
        luaL_setfuncs(L, io_adds, 0);
    }
    lua_pop(L, 1);
}
