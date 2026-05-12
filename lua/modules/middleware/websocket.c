#include "luartos.h"

#if CONFIG_LUA_RTOS_LUA_USE_WEBSOCKET

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "modules.h"
#include "error.h"
#include "sys.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

#include "websocket.h"

/* ----------------------------------------------------------------- errors */

#define LUA_WS_ERR_CANT_CREATE_SERVER (DRIVER_EXCEPTION_BASE(WS_DRIVER_ID) | 0)
#define LUA_WS_ERR_CANT_CREATE_CLIENT (DRIVER_EXCEPTION_BASE(WS_DRIVER_ID) | 1)
#define LUA_WS_ERR_SEND_FAILED        (DRIVER_EXCEPTION_BASE(WS_DRIVER_ID) | 2)
#define LUA_WS_ERR_NOT_ENOUGH_MEM     (DRIVER_EXCEPTION_BASE(WS_DRIVER_ID) | 3)

DRIVER_REGISTER_BEGIN(WS, ws, 0, NULL, NULL);
    DRIVER_REGISTER_ERROR(WS, ws, CannotCreateServer, "can't create server",
                          LUA_WS_ERR_CANT_CREATE_SERVER);
    DRIVER_REGISTER_ERROR(WS, ws, CannotCreateClient, "can't create client",
                          LUA_WS_ERR_CANT_CREATE_CLIENT);
    DRIVER_REGISTER_ERROR(WS, ws, SendFailed,         "send failed",
                          LUA_WS_ERR_SEND_FAILED);
    DRIVER_REGISTER_ERROR(WS, ws, NotEnoughMemory,    "not enough memory",
                          LUA_WS_ERR_NOT_ENOUGH_MEM);
DRIVER_REGISTER_END(WS, ws, 0, NULL, NULL);

/* ----------------------------------------------------------------- types */

/* "message", "open", or "close" → callback slot index */
#define WS_CB_MESSAGE 0
#define WS_CB_OPEN    1
#define WS_CB_CLOSE   2
#define WS_CB_COUNT   3

typedef struct {
    ws_server_handle_t handle;
    lua_callback_t    *cbs[WS_CB_COUNT];
} lws_server_t;

typedef struct {
    ws_client_handle_t handle;
    lua_callback_t    *cbs[WS_CB_COUNT];
} lws_client_t;

static const LUA_REG_TYPE lws_server_map[];
static const LUA_REG_TYPE lws_client_map[];

/* -------------------------------------------------------- event → index */

static int event_index(const char *ev) {
    if (strcmp(ev, "message") == 0) return WS_CB_MESSAGE;
    if (strcmp(ev, "open")    == 0) return WS_CB_OPEN;
    if (strcmp(ev, "close")   == 0) return WS_CB_CLOSE;
    return -1;
}

/* ======================================================= server callbacks */

static void srv_on_open(void *handle, int fd, void *user_data) {
    lws_server_t *srv = (lws_server_t *)user_data;
    if (!srv->cbs[WS_CB_OPEN]) return;
    lua_State *L = luaS_callback_state(srv->cbs[WS_CB_OPEN]);
    lua_pushinteger(L, fd);
    luaS_callback_call(srv->cbs[WS_CB_OPEN], 1);
}

static void srv_on_message(void *handle, const char *payload, size_t len,
                            void *user_data) {
    lws_server_t *srv = (lws_server_t *)user_data;
    if (!srv->cbs[WS_CB_MESSAGE]) return;
    lua_State *L = luaS_callback_state(srv->cbs[WS_CB_MESSAGE]);
    lua_pushlstring(L, payload, len);
    luaS_callback_call(srv->cbs[WS_CB_MESSAGE], 1);
}

static void srv_on_close(void *handle, int fd, void *user_data) {
    lws_server_t *srv = (lws_server_t *)user_data;
    if (!srv->cbs[WS_CB_CLOSE]) return;
    lua_State *L = luaS_callback_state(srv->cbs[WS_CB_CLOSE]);
    lua_pushinteger(L, fd);
    luaS_callback_call(srv->cbs[WS_CB_CLOSE], 1);
}

/* ======================================================= server Lua API */

/* ws.server(port) → server_ud */
static int lws_server_new(lua_State *L) {
    uint16_t port = (uint16_t)luaL_optinteger(L, 1, 8080);

    lws_server_t *srv = (lws_server_t *)lua_newuserdata(L, sizeof(lws_server_t));
    memset(srv, 0, sizeof(*srv));

    if (ws_server_create(port, srv_on_open, srv_on_message, srv_on_close,
                          srv, &srv->handle) != 0) {
        return luaL_exception(L, LUA_WS_ERR_CANT_CREATE_SERVER);
    }

    luaL_newmetarotable(L, "ws.srv", (void *)lws_server_map);
    lua_setmetatable(L, -2);
    return 1;
}

/* server:on(event, callback) */
static int lws_server_on(lua_State *L) {
    lws_server_t *srv  = (lws_server_t *)luaL_checkudata(L, 1, "ws.srv");
    const char   *ev   = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    int idx = event_index(ev);
    if (idx < 0) return luaL_error(L, "unknown event '%s'", ev);

    if (srv->cbs[idx]) luaS_callback_destroy(srv->cbs[idx]);
    srv->cbs[idx] = luaS_callback_create(L, 3);
    if (!srv->cbs[idx]) return luaL_exception(L, LUA_WS_ERR_NOT_ENOUGH_MEM);
    return 0;
}

/* server:send(fd, payload) */
static int lws_server_send(lua_State *L) {
    lws_server_t *srv = (lws_server_t *)luaL_checkudata(L, 1, "ws.srv");
    int           fd  = (int)luaL_checkinteger(L, 2);
    size_t        len;
    const char   *pay = luaL_checklstring(L, 3, &len);

    if (ws_server_send(srv->handle, fd, pay, len) != 0) {
        return luaL_exception(L, LUA_WS_ERR_SEND_FAILED);
    }
    return 0;
}

/* server:broadcast(payload) */
static int lws_server_broadcast(lua_State *L) {
    lws_server_t *srv = (lws_server_t *)luaL_checkudata(L, 1, "ws.srv");
    size_t        len;
    const char   *pay = luaL_checklstring(L, 2, &len);

    if (ws_server_broadcast(srv->handle, pay, len) != 0) {
        return luaL_exception(L, LUA_WS_ERR_SEND_FAILED);
    }
    return 0;
}

/* server:close() */
static int lws_server_close(lua_State *L) {
    lws_server_t *srv = (lws_server_t *)luaL_checkudata(L, 1, "ws.srv");
    if (srv->handle) {
        ws_server_destroy(srv->handle);
        srv->handle = NULL;
    }
    return 0;
}

static int lws_server_gc(lua_State *L) {
    lws_server_t *srv = (lws_server_t *)luaL_checkudata(L, 1, "ws.srv");
    if (srv->handle) {
        ws_server_destroy(srv->handle);
        srv->handle = NULL;
    }
    for (int i = 0; i < WS_CB_COUNT; i++) {
        if (srv->cbs[i]) {
            luaS_callback_destroy(srv->cbs[i]);
            srv->cbs[i] = NULL;
        }
    }
    return 0;
}

/* ======================================================= client callbacks */

static void cli_on_open(void *handle, int fd, void *user_data) {
    lws_client_t *cli = (lws_client_t *)user_data;
    if (!cli->cbs[WS_CB_OPEN]) return;
    lua_State *L = luaS_callback_state(cli->cbs[WS_CB_OPEN]);
    luaS_callback_call(cli->cbs[WS_CB_OPEN], 0);
}

static void cli_on_message(void *handle, const char *payload, size_t len,
                            void *user_data) {
    lws_client_t *cli = (lws_client_t *)user_data;
    if (!cli->cbs[WS_CB_MESSAGE]) return;
    lua_State *L = luaS_callback_state(cli->cbs[WS_CB_MESSAGE]);
    lua_pushlstring(L, payload, len);
    luaS_callback_call(cli->cbs[WS_CB_MESSAGE], 1);
}

static void cli_on_close(void *handle, int fd, void *user_data) {
    lws_client_t *cli = (lws_client_t *)user_data;
    if (!cli->cbs[WS_CB_CLOSE]) return;
    lua_State *L = luaS_callback_state(cli->cbs[WS_CB_CLOSE]);
    luaS_callback_call(cli->cbs[WS_CB_CLOSE], 0);
}

/* ======================================================= client Lua API */

/* ws.client(uri) → client_ud */
static int lws_client_new(lua_State *L) {
    const char *uri = luaL_checkstring(L, 1);

    lws_client_t *cli = (lws_client_t *)lua_newuserdata(L, sizeof(lws_client_t));
    memset(cli, 0, sizeof(*cli));

    if (ws_client_create(uri, cli_on_open, cli_on_message, cli_on_close,
                          cli, &cli->handle) != 0) {
        return luaL_exception(L, LUA_WS_ERR_CANT_CREATE_CLIENT);
    }

    luaL_newmetarotable(L, "ws.cli", (void *)lws_client_map);
    lua_setmetatable(L, -2);
    return 1;
}

/* client:on(event, callback) */
static int lws_client_on(lua_State *L) {
    lws_client_t *cli = (lws_client_t *)luaL_checkudata(L, 1, "ws.cli");
    const char   *ev  = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    int idx = event_index(ev);
    if (idx < 0) return luaL_error(L, "unknown event '%s'", ev);

    if (cli->cbs[idx]) luaS_callback_destroy(cli->cbs[idx]);
    cli->cbs[idx] = luaS_callback_create(L, 3);
    if (!cli->cbs[idx]) return luaL_exception(L, LUA_WS_ERR_NOT_ENOUGH_MEM);
    return 0;
}

/* client:send(payload) */
static int lws_client_send(lua_State *L) {
    lws_client_t *cli = (lws_client_t *)luaL_checkudata(L, 1, "ws.cli");
    size_t        len;
    const char   *pay = luaL_checklstring(L, 2, &len);

    if (ws_client_send(cli->handle, pay, len) != 0) {
        return luaL_exception(L, LUA_WS_ERR_SEND_FAILED);
    }
    return 0;
}

/* client:close() */
static int lws_client_close(lua_State *L) {
    lws_client_t *cli = (lws_client_t *)luaL_checkudata(L, 1, "ws.cli");
    if (cli->handle) {
        ws_client_destroy(cli->handle);
        cli->handle = NULL;
    }
    return 0;
}

static int lws_client_gc(lua_State *L) {
    lws_client_t *cli = (lws_client_t *)luaL_checkudata(L, 1, "ws.cli");
    if (cli->handle) {
        ws_client_destroy(cli->handle);
        cli->handle = NULL;
    }
    for (int i = 0; i < WS_CB_COUNT; i++) {
        if (cli->cbs[i]) {
            luaS_callback_destroy(cli->cbs[i]);
            cli->cbs[i] = NULL;
        }
    }
    return 0;
}

/* ======================================================= registration */

static const LUA_REG_TYPE lws_server_map[] = {
    { LSTRKEY("on"),          LFUNCVAL(lws_server_on)        },
    { LSTRKEY("send"),        LFUNCVAL(lws_server_send)      },
    { LSTRKEY("broadcast"),   LFUNCVAL(lws_server_broadcast) },
    { LSTRKEY("close"),       LFUNCVAL(lws_server_close)     },
    { LSTRKEY("__metatable"), LROVAL(lws_server_map)         },
    { LSTRKEY("__index"),     LROVAL(lws_server_map)         },
    { LSTRKEY("__gc"),        LFUNCVAL(lws_server_gc)        },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lws_client_map[] = {
    { LSTRKEY("on"),          LFUNCVAL(lws_client_on)    },
    { LSTRKEY("send"),        LFUNCVAL(lws_client_send)  },
    { LSTRKEY("close"),       LFUNCVAL(lws_client_close) },
    { LSTRKEY("__metatable"), LROVAL(lws_client_map)     },
    { LSTRKEY("__index"),     LROVAL(lws_client_map)     },
    { LSTRKEY("__gc"),        LFUNCVAL(lws_client_gc)    },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lws_map[] = {
    { LSTRKEY("server"),  LFUNCVAL(lws_server_new) },
    { LSTRKEY("client"),  LFUNCVAL(lws_client_new) },
    DRIVER_REGISTER_LUA_ERRORS(ws)
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_ws(lua_State *L) {
    luaL_newmetarotable(L, "ws.srv", (void *)lws_server_map);
    luaL_newmetarotable(L, "ws.cli", (void *)lws_client_map);
    LNEWLIB(L, ws);
}

MODULE_REGISTER_ROM(WS, ws, lws_map, luaopen_ws, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_WEBSOCKET */
