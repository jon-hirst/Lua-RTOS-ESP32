/*
 * lua_rtos_main.c — Lua RTOS main entry point, lock functions, and REPL.
 *
 * In Lua 5.3 (patched), these were compiled as part of lua.c via the
 * luaconf_custom.h hook mechanism (#ifdef lua_c ... #include "lua_adds.inc").
 * In Lua 5.5 (stock), that hook is gone, so we compile lua_adds.inc here as
 * a standalone translation unit, providing the static helpers it needs.
 */

#include "sdkconfig.h"
#include "luartos.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "modules.h"
#include "luaconf_custom.h"   /* LUA_MAXINPUT, lua_readline, lua_freeline, etc. */
#include "linenoise.h"
#include "shell.h"
#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/status.h>
#include <sys/debug.h>

/* _pthread_signal registers a signal handler for the calling thread */
extern sig_t _pthread_signal(int s, sig_t h);

/* Lua state used by the REPL — stored so the SIGINT handler can reach it */
static lua_State *g_L = NULL;

/* Hook installed by the SIGINT handler: clears itself then raises "interrupted!" */
static void lstop (lua_State *L, lua_Debug *ar) {
    (void)ar;
    lua_sethook(L, NULL, 0, 0);
    luaL_error(L, "interrupted!");
}

/* Signal handler called (possibly from another task context) when Ctrl-C arrives */
static void lua_sigint_handler (int sig) {
    (void)sig;
    if (g_L) {
        lua_sethook(g_L, lstop,
                    LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE | LUA_MASKCOUNT,
                    1);
    }
}

/* --------------------------------------------------------------------------
 * Static helpers expected by lua_adds.inc (were static in lua.c)
 * -------------------------------------------------------------------------- */

static const char *progname = "lua";

static void l_message (const char *pname, const char *msg) {
    if (pname) lua_writestringerror("%s: ", pname);
    lua_writestringerror("%s\n", msg);
}

static int report (lua_State *L, int status) {
    if (status != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        if (msg == NULL) msg = "(error message not a string)";
        l_message(progname, msg);
        lua_pop(L, 1);
    }
    return status;
}

static void print_version (void) {
    lua_writestring(LUA_COPYRIGHT, strlen(LUA_COPYRIGHT));
    lua_writeline();
}

static int msghandler (lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    if (msg == NULL) {
        if (luaL_callmeta(L, 1, "__tostring") &&
            lua_type(L, -1) == LUA_TSTRING)
            return 1;
        msg = lua_pushfstring(L, "(error object is a %s value)",
                              luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}

static int docall (lua_State *L, int narg, int nres) {
    int base = lua_gettop(L) - narg;
    lua_pushcfunction(L, msghandler);
    lua_insert(L, base);
    int status = lua_pcall(L, narg, nres, base);
    lua_remove(L, base);
    return status;
}

static int dochunk (lua_State *L, int status) {
    if (status == LUA_OK) status = docall(L, 0, 0);
    return report(L, status);
}

/* Forward-declared static in lua_adds.inc line 58 — defined here */
static int dofile (lua_State *L, const char *name) {
    return dochunk(L, luaL_loadfile(L, name));
}

/* Stubs for rarely-used helpers that lua_adds.inc may reference */
static int runargs (lua_State *L, char **argv, int n) {
    (void)L; (void)argv; (void)n;
    return LUA_OK;
}
static void print_usage (const char *badoption) { (void)badoption; }
static int collectargs (char **argv, int *first) {
    (void)argv; (void)first;
    return 0;
}
static void createargtable (lua_State *L, char **argv,
                             int argc, int script) {
    (void)L; (void)argv; (void)argc; (void)script;
}

/* --------------------------------------------------------------------------
 * doREPL — forward declaration so luaos_pmain (inside lua_adds.inc) can
 * call it.  When LUA_USE_ROTABLE is set, luaconf_custom.h declares it as
 * non-static: void doREPL(lua_State *L).
 * -------------------------------------------------------------------------- */
void doREPL (lua_State *L);

/* --------------------------------------------------------------------------
 * Pull in Lua-RTOS additions: LuaLock/Unlock, luaos_main, luaos_pmain, …
 * -------------------------------------------------------------------------- */
#include "lua_adds.inc"

/* --------------------------------------------------------------------------
 * luaos_open_modules — iterate the .lua_libs1 linker section, call each
 * luaopen_xxx() to register metatables, then materialise the corresponding
 * rotable as a regular Lua table and install it as a global.
 *
 * The stock Lua 5.5 VM has no rotable integration, so without this step
 * every Lua RTOS module global (thread, tmr, uart, …) would be nil.
 * -------------------------------------------------------------------------- */
void luaos_open_modules (lua_State *L) {
    /* lua_libs1 is the start of the .lua_libs1 linker section; it is a
     * null-name-terminated array of luaL_Reg_adv structs. */
    extern luaL_Reg_adv lua_libs1[];
    luaL_Reg_adv *lib;

    for (lib = lua_libs1; lib->name != NULL; lib++) {
        /* Call luaopen_xxx to register metatables and other setup.
         * Use pcall so one bad module doesn't kill startup. */
        lua_pushcfunction(L, lib->func);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            lua_writestringerror("luaos_open_modules: open failed: %s\n",
                                 lua_tostring(L, -1));
            lua_pop(L, 1);
        }

        /* Find the matching rotable in lua_rotable[] and push it as a
         * regular Lua table so it is accessible as _G[lib->name]. */
        const TValue *tv = luaR_findglobal(lib->name);
        if (tv != NULL && ttisrotable(tv)) {
            luaR_push_as_table(L, (const luaR_entry *)rvalue(tv));
            lua_setglobal(L, lib->name);
        }
    }
}

/* --------------------------------------------------------------------------
 * doREPL — defined after lua_adds.inc so we can call luaos_pushline()
 * which is a static function defined in that include.
 * -------------------------------------------------------------------------- */

/* Returns 1 if the syntax error in status indicates an incomplete chunk */
static int repl_incomplete (lua_State *L, int status) {
    if (status == LUA_ERRSYNTAX) {
        size_t lmsg;
        const char *msg = lua_tolstring(L, -1, &lmsg);
        if (lmsg >= 5 && strcmp(msg + lmsg - 5, "<eof>") == 0) {
            lua_pop(L, 1);
            return 1;
        }
    }
    return 0;
}

void doREPL (lua_State *L) {
    int status;

    while (luaos_pushline(L, 1)) {
        /* Stack: [line] at index 1.
         *
         * First try "return <line>;" so bare expressions are evaluated and
         * their results printed (standard Lua REPL behaviour). */
        const char *line = lua_tostring(L, 1);
        lua_pushfstring(L, "return %s;", line);         /* Stack: [line, retline] */
        status = luaL_loadbuffer(L, lua_tostring(L, 2),
                                  lua_rawlen(L, 2), "=stdin");
        if (status == LUA_OK) {
            lua_remove(L, 2);                           /* Stack: [line, chunk] */
        } else {
            lua_pop(L, 2);                              /* pop error + retline */
            /* Stack: [line] — try as a statement, with multiline support. */
            size_t len;
            line = lua_tolstring(L, 1, &len);
            status = luaL_loadbuffer(L, line, len, "=stdin");

            while (repl_incomplete(L, status)) {
                if (!luaos_pushline(L, 0))
                    break;
                lua_pushliteral(L, "\n");
                lua_insert(L, -2);
                lua_concat(L, 3);                       /* join prev+"\n"+new */
                line = lua_tolstring(L, 1, &len);
                status = luaL_loadbuffer(L, line, len, "=stdin");
            }
        }

        lua_remove(L, 1);   /* remove line string; chunk (or error) at top */

        if (status == LUA_OK)
            status = docall(L, 0, LUA_MULTRET);

        if (status == LUA_OK && lua_gettop(L) > 0) {
            luaL_checkstack(L, LUA_MINSTACK, "too many results to print");
            lua_getglobal(L, "print");
            lua_insert(L, 1);
            if (lua_pcall(L, lua_gettop(L) - 1, 0, 0) != LUA_OK)
                lua_writestringerror("error calling 'print': %s\n",
                                     lua_tostring(L, -1));
        } else if (status != LUA_OK) {
            report(L, status);
        }

        lua_settop(L, 0);
    }

    lua_settop(L, 0);
    lua_writeline();
}
