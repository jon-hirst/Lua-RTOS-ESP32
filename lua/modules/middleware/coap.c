/*
 * Copyright (C) 2015 - 2020, IBEROXARXA SERVICIOS INTEGRALES, S.L.
 * Copyright (C) 2015 - 2020, Jaume Olivé Petrus (jolive@whitecatboard.org)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *     * The WHITECAT logotype cannot be changed, you can remove it, but you
 *       cannot change it in any way. The WHITECAT logotype is:
 *
 *          /\       /\
 *         /  \_____/  \
 *        /_____________\
 *        W H I T E C A T
 *
 *     * Redistributions in binary form must retain all copyright notices printed
 *       to any local or remote output device. This include any reference to
 *       Lua RTOS, whitecatboard.org, Lua, and other copyright notices that may
 *       appear in the future.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Lua RTOS, Lua CoAP module
 */

#include "luartos.h"

#if CONFIG_LUA_RTOS_LUA_USE_COAP

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

#include "coap.h"

/* ----------------------------------------------------------------- errors */

#define LUA_COAP_ERR_CANT_CREATE_SERVER (DRIVER_EXCEPTION_BASE(COAP_DRIVER_ID) | 0)
#define LUA_COAP_ERR_CANT_CREATE_RES    (DRIVER_EXCEPTION_BASE(COAP_DRIVER_ID) | 1)
#define LUA_COAP_ERR_REQUEST_FAILED     (DRIVER_EXCEPTION_BASE(COAP_DRIVER_ID) | 2)
#define LUA_COAP_ERR_NOT_ENOUGH_MEM     (DRIVER_EXCEPTION_BASE(COAP_DRIVER_ID) | 3)

DRIVER_REGISTER_BEGIN(COAP, coap, 0, NULL, NULL);
    DRIVER_REGISTER_ERROR(COAP, coap, CannotCreateServer, "can't create server",
                          LUA_COAP_ERR_CANT_CREATE_SERVER);
    DRIVER_REGISTER_ERROR(COAP, coap, CannotCreateResource, "can't create resource",
                          LUA_COAP_ERR_CANT_CREATE_RES);
    DRIVER_REGISTER_ERROR(COAP, coap, RequestFailed, "request failed",
                          LUA_COAP_ERR_REQUEST_FAILED);
    DRIVER_REGISTER_ERROR(COAP, coap, NotEnoughMemory, "not enough memory",
                          LUA_COAP_ERR_NOT_ENOUGH_MEM);
DRIVER_REGISTER_END(COAP, coap, 0, NULL, NULL);

/* ----------------------------------------------------------------- types */

/* Server userdata */
typedef struct {
    coap_context_t *ctx;
    TaskHandle_t    task;
    volatile int    running;
} lcoap_server_t;

/* Per-method Lua callback entry */
typedef struct {
    lua_callback_t *cb;
} lcoap_handler_t;

/* Resource userdata */
typedef struct {
    coap_resource_t *res;
    lcoap_server_t  *server; /* kept alive via reference – do not free here */
    lcoap_handler_t  handlers[4]; /* GET, POST, PUT, DELETE */
} lcoap_resource_t;

/* Observe handle userdata */
typedef struct {
    coap_observe_handle_t *handle;
    lua_callback_t        *cb;
} lcoap_observe_t;

static const LUA_REG_TYPE lcoap_server_map[];
static const LUA_REG_TYPE lcoap_resource_map[];
static const LUA_REG_TYPE lcoap_observe_map[];

/* ---------------------------------------------------------- method → index */

static int method_index(uint8_t method) {
    switch (method) {
    case COAP_REQUEST_GET:    return 0;
    case COAP_REQUEST_POST:   return 1;
    case COAP_REQUEST_PUT:    return 2;
    case COAP_REQUEST_DELETE: return 3;
    default:                  return -1;
    }
}

static uint8_t name_to_method(const char *name) {
    if (strcmp(name, "get")    == 0) return COAP_REQUEST_GET;
    if (strcmp(name, "post")   == 0) return COAP_REQUEST_POST;
    if (strcmp(name, "put")    == 0) return COAP_REQUEST_PUT;
    if (strcmp(name, "delete") == 0) return COAP_REQUEST_DELETE;
    return 0;
}

/* ----------------------------------------------- C method handler bridge */

/*
 * Called by the CoAP library from the server task.
 * Invokes the Lua callback with (payload_string, content_format).
 * Expects callback to return (response_payload_string [, content_format]).
 */
static void coap_lua_method_handler(coap_context_t  *ctx,
                                     coap_resource_t *res,
                                     coap_session_t  *session,
                                     const coap_pdu_t *request,
                                     coap_pdu_t       *response,
                                     void             *user_data) {
    (void)ctx; (void)res; (void)session;

    lcoap_handler_t *h = (lcoap_handler_t *)user_data;
    if (!h || !h->cb) return;

    lua_State *L = luaS_callback_state(h->cb);

    /* Arg 1: payload string */
    if (request->payload_len > 0) {
        lua_pushlstring(L, (const char *)request->payload, request->payload_len);
    } else {
        lua_pushstring(L, "");
    }

    /* Arg 2: content format */
    uint16_t cf = 0;
    const coap_opt_t *cf_opt = coap_find_option(request, COAP_OPT_CONTENT_FORMAT);
    if (cf_opt) {
        if (cf_opt->length == 1) cf = cf_opt->value[0];
        else if (cf_opt->length == 2)
            cf = ((uint16_t)cf_opt->value[0] << 8) | cf_opt->value[1];
    }
    lua_pushinteger(L, cf);

    if (luaS_callback_call_return(h->cb, 2, 2) != 0) {
        response->code = COAP_RESPONSE_INTERNAL_ERR;
        return;
    }

    /* Return value 1: response payload (string or nil) */
    if (lua_type(L, -2) == LUA_TSTRING) {
        size_t      rlen;
        const char *rpay = lua_tolstring(L, -2, &rlen);
        /* Store payload in response wire buffer area */
        if (rlen > 0 && rlen < COAP_MAX_PDU_SIZE - 32) {
            /* Use a static per-call buffer – safe because we send synchronously */
            static uint8_t resp_payload_buf[COAP_MAX_PDU_SIZE];
            memcpy(resp_payload_buf, rpay, rlen);
            coap_set_payload(response, resp_payload_buf, rlen);

            /* Return value 2: response content format */
            uint16_t rcf = COAP_CT_TEXT_PLAIN;
            if (lua_type(L, -1) == LUA_TNUMBER) rcf = (uint16_t)lua_tointeger(L, -1);
            if (rcf <= 255) {
                uint8_t cf_byte = (uint8_t)rcf;
                coap_add_option(response, COAP_OPT_CONTENT_FORMAT, &cf_byte, 1);
            } else {
                uint8_t cf_bytes[2] = { (uint8_t)(rcf >> 8), (uint8_t)(rcf & 0xFF) };
                coap_add_option(response, COAP_OPT_CONTENT_FORMAT, cf_bytes, 2);
            }
        }
    }
    lua_pop(L, 2);
}

/* --------------------------------------------------------- server FreeRTOS task */

static void coap_server_task(void *arg) {
    lcoap_server_t *srv = (lcoap_server_t *)arg;
    while (srv->running) {
        coap_io_process(srv->ctx, 500);
    }
    vTaskDelete(NULL);
}

/* =========================================================== Lua server API */

/* coap.server([port]) → server_ud */
static int lcoap_server_new(lua_State *L) {
    uint16_t port = (uint16_t)luaL_optinteger(L, 1, COAP_DEFAULT_PORT);

    lcoap_server_t *srv = (lcoap_server_t *)lua_newuserdata(L, sizeof(lcoap_server_t));
    memset(srv, 0, sizeof(*srv));

    srv->ctx = coap_context_new(port);
    if (!srv->ctx) {
        return luaL_exception(L, LUA_COAP_ERR_CANT_CREATE_SERVER);
    }
    srv->running = 1;

    if (xTaskCreate(coap_server_task, "coap_srv", 4096, srv, 5, &srv->task) != pdPASS) {
        coap_context_free(srv->ctx);
        srv->ctx = NULL;
        return luaL_exception(L, LUA_COAP_ERR_CANT_CREATE_SERVER);
    }

    luaL_newmetarotable(L, "coap.srv", (void *)lcoap_server_map);
    lua_setmetatable(L, -2);
    return 1;
}

/* server:resource(path [, mediatype]) → resource_ud */
static int lcoap_server_resource(lua_State *L) {
    lcoap_server_t *srv = (lcoap_server_t *)luaL_checkudata(L, 1, "coap.srv");
    const char     *path = luaL_checkstring(L, 2);

    lcoap_resource_t *lres = (lcoap_resource_t *)lua_newuserdata(L, sizeof(lcoap_resource_t));
    memset(lres, 0, sizeof(*lres));
    lres->server = srv;

    lres->res = coap_resource_new(path);
    if (!lres->res) {
        return luaL_exception(L, LUA_COAP_ERR_CANT_CREATE_RES);
    }

    coap_add_resource(srv->ctx, lres->res);

    luaL_newmetarotable(L, "coap.res", (void *)lcoap_resource_map);
    lua_setmetatable(L, -2);
    return 1;
}

/* server:stop() */
static int lcoap_server_stop(lua_State *L) {
    lcoap_server_t *srv = (lcoap_server_t *)luaL_checkudata(L, 1, "coap.srv");
    srv->running = 0;
    /* Give the task a moment to exit before freeing the context */
    vTaskDelay(pdMS_TO_TICKS(600));
    if (srv->ctx) {
        coap_context_free(srv->ctx);
        srv->ctx = NULL;
    }
    return 0;
}

static int lcoap_server_gc(lua_State *L) {
    return lcoap_server_stop(L);
}

/* =========================================================== Lua resource API */

/* resource:on(method, callback) */
static int lcoap_resource_on(lua_State *L) {
    lcoap_resource_t *lres = (lcoap_resource_t *)luaL_checkudata(L, 1, "coap.res");
    const char       *mname = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    uint8_t method = name_to_method(mname);
    if (method == 0) {
        return luaL_error(L, "unknown method '%s'", mname);
    }
    int idx = method_index(method);

    if (lres->handlers[idx].cb) {
        luaS_callback_destroy(lres->handlers[idx].cb);
    }
    lres->handlers[idx].cb = luaS_callback_create(L, 3);
    if (!lres->handlers[idx].cb) {
        return luaL_exception(L, LUA_COAP_ERR_NOT_ENOUGH_MEM);
    }

    coap_register_handler(lres->res, method,
                           coap_lua_method_handler,
                           &lres->handlers[idx]);
    return 0;
}

static int lcoap_resource_gc(lua_State *L) {
    lcoap_resource_t *lres = (lcoap_resource_t *)luaL_checkudata(L, 1, "coap.res");
    for (int i = 0; i < 4; i++) {
        if (lres->handlers[i].cb) {
            luaS_callback_destroy(lres->handlers[i].cb);
            lres->handlers[i].cb = NULL;
        }
    }
    /* lres->res is owned by the context; do not free here */
    return 0;
}

/* =========================================================== Lua client API */

static int do_client_request(lua_State *L, uint8_t method) {
    const char *uri     = luaL_checkstring(L, 1);
    const char *payload = NULL;
    size_t      plen    = 0;
    uint16_t    cf      = COAP_CT_TEXT_PLAIN;
    uint32_t    timeout = 10000;

    if (method == COAP_REQUEST_PUT || method == COAP_REQUEST_POST) {
        payload = luaL_optlstring(L, 2, "", &plen);
        cf      = (uint16_t)luaL_optinteger(L, 3, COAP_CT_TEXT_PLAIN);
        timeout = (uint32_t)luaL_optinteger(L, 4, 10000);
    } else {
        /* GET / DELETE: optional options table (ignored for now), timeout */
        timeout = (uint32_t)luaL_optinteger(L, 2, 10000);
    }

    coap_client_resp_t resp;
    memset(&resp, 0, sizeof(resp));

    int rc = coap_client_request(method, uri,
                                  (const uint8_t *)payload, plen,
                                  cf, timeout, &resp);
    if (rc < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "request failed");
        return 2;
    }

    /* Return payload string (or empty string), response code, content format */
    if (resp.payload_len > 0) {
        lua_pushlstring(L, (const char *)resp.payload, resp.payload_len);
    } else {
        lua_pushstring(L, "");
    }
    lua_pushinteger(L, resp.code);
    lua_pushinteger(L, resp.content_format);

    coap_client_resp_free(&resp);
    return 3;
}

/* coap.get(uri [, timeout_ms]) → payload, code, content_format  |  nil, errmsg */
static int lcoap_get(lua_State *L) {
    return do_client_request(L, COAP_REQUEST_GET);
}

/* coap.put(uri, payload [, content_format [, timeout_ms]]) */
static int lcoap_put(lua_State *L) {
    return do_client_request(L, COAP_REQUEST_PUT);
}

/* coap.post(uri, payload [, content_format [, timeout_ms]]) */
static int lcoap_post(lua_State *L) {
    return do_client_request(L, COAP_REQUEST_POST);
}

/* coap.delete(uri [, timeout_ms]) */
static int lcoap_delete(lua_State *L) {
    return do_client_request(L, COAP_REQUEST_DELETE);
}

/* =========================================================== Lua observe API */

static void observe_lua_cb(const uint8_t *payload, size_t len,
                             uint8_t code, void *user_data) {
    lcoap_observe_t *lo = (lcoap_observe_t *)user_data;
    if (!lo || !lo->cb) return;

    lua_State *L = luaS_callback_state(lo->cb);
    if (len > 0) {
        lua_pushlstring(L, (const char *)payload, len);
    } else {
        lua_pushstring(L, "");
    }
    lua_pushinteger(L, code);
    luaS_callback_call(lo->cb, 2);
}

/* coap.observe(uri, callback [, timeout_ms]) → observe_handle */
static int lcoap_observe(lua_State *L) {
    const char *uri = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lcoap_observe_t *lo = (lcoap_observe_t *)lua_newuserdata(L, sizeof(lcoap_observe_t));
    memset(lo, 0, sizeof(*lo));

    lo->cb = luaS_callback_create(L, 2);
    if (!lo->cb) {
        return luaL_exception(L, LUA_COAP_ERR_NOT_ENOUGH_MEM);
    }

    lo->handle = coap_observe_start(uri, observe_lua_cb, lo);
    if (!lo->handle) {
        luaS_callback_destroy(lo->cb);
        return luaL_exception(L, LUA_COAP_ERR_REQUEST_FAILED);
    }

    luaL_newmetarotable(L, "coap.obs", (void *)lcoap_observe_map);
    lua_setmetatable(L, -2);
    return 1;
}

/* observe_handle:cancel() */
static int lcoap_observe_cancel(lua_State *L) {
    lcoap_observe_t *lo = (lcoap_observe_t *)luaL_checkudata(L, 1, "coap.obs");
    if (lo->handle) {
        coap_observe_cancel(lo->handle);
        lo->handle = NULL;
    }
    return 0;
}

static int lcoap_observe_gc(lua_State *L) {
    lcoap_observe_t *lo = (lcoap_observe_t *)luaL_checkudata(L, 1, "coap.obs");
    if (lo->handle) {
        coap_observe_cancel(lo->handle);
        lo->handle = NULL;
    }
    if (lo->cb) {
        luaS_callback_destroy(lo->cb);
        lo->cb = NULL;
    }
    return 0;
}

/* =========================================================== registration */

static const LUA_REG_TYPE lcoap_server_map[] = {
    { LSTRKEY("resource"),    LFUNCVAL(lcoap_server_resource) },
    { LSTRKEY("stop"),        LFUNCVAL(lcoap_server_stop)     },
    { LSTRKEY("__metatable"), LROVAL(lcoap_server_map)        },
    { LSTRKEY("__index"),     LROVAL(lcoap_server_map)        },
    { LSTRKEY("__gc"),        LFUNCVAL(lcoap_server_gc)       },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lcoap_resource_map[] = {
    { LSTRKEY("on"),          LFUNCVAL(lcoap_resource_on)  },
    { LSTRKEY("__metatable"), LROVAL(lcoap_resource_map)   },
    { LSTRKEY("__index"),     LROVAL(lcoap_resource_map)   },
    { LSTRKEY("__gc"),        LFUNCVAL(lcoap_resource_gc)  },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lcoap_observe_map[] = {
    { LSTRKEY("cancel"),      LFUNCVAL(lcoap_observe_cancel) },
    { LSTRKEY("__metatable"), LROVAL(lcoap_observe_map)      },
    { LSTRKEY("__index"),     LROVAL(lcoap_observe_map)      },
    { LSTRKEY("__gc"),        LFUNCVAL(lcoap_observe_gc)     },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lcoap_map[] = {
    { LSTRKEY("server"),      LFUNCVAL(lcoap_server_new) },
    { LSTRKEY("get"),         LFUNCVAL(lcoap_get)        },
    { LSTRKEY("put"),         LFUNCVAL(lcoap_put)        },
    { LSTRKEY("post"),        LFUNCVAL(lcoap_post)       },
    { LSTRKEY("delete"),      LFUNCVAL(lcoap_delete)     },
    { LSTRKEY("observe"),     LFUNCVAL(lcoap_observe)    },

    /* Content-format constants */
    { LSTRKEY("TEXT_PLAIN"),  LINTVAL(COAP_CT_TEXT_PLAIN)  },
    { LSTRKEY("APP_LINK"),    LINTVAL(COAP_CT_APP_LINK)    },
    { LSTRKEY("APP_XML"),     LINTVAL(COAP_CT_APP_XML)     },
    { LSTRKEY("APP_OCTET"),   LINTVAL(COAP_CT_APP_OCTET)   },
    { LSTRKEY("APP_JSON"),    LINTVAL(COAP_CT_APP_JSON)    },
    { LSTRKEY("APP_CBOR"),    LINTVAL(COAP_CT_APP_CBOR)    },

    /* Response code constants */
    { LSTRKEY("CREATED"),     LINTVAL(COAP_RESPONSE_CREATED)      },
    { LSTRKEY("DELETED"),     LINTVAL(COAP_RESPONSE_DELETED)      },
    { LSTRKEY("VALID"),       LINTVAL(COAP_RESPONSE_VALID)        },
    { LSTRKEY("CHANGED"),     LINTVAL(COAP_RESPONSE_CHANGED)      },
    { LSTRKEY("CONTENT"),     LINTVAL(COAP_RESPONSE_CONTENT)      },
    { LSTRKEY("NOT_FOUND"),   LINTVAL(COAP_RESPONSE_NOT_FOUND)    },
    { LSTRKEY("NOT_ALLOWED"), LINTVAL(COAP_RESPONSE_NOT_ALLOWED)  },
    { LSTRKEY("SERVER_ERR"),  LINTVAL(COAP_RESPONSE_INTERNAL_ERR) },

    /* Error entries */
    DRIVER_REGISTER_LUA_ERRORS(coap)
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_coap(lua_State *L) {
    luaL_newmetarotable(L, "coap.srv", (void *)lcoap_server_map);
    luaL_newmetarotable(L, "coap.res", (void *)lcoap_resource_map);
    luaL_newmetarotable(L, "coap.obs", (void *)lcoap_observe_map);
    LNEWLIB(L, coap);
}

MODULE_REGISTER_ROM(COAP, coap, lcoap_map, luaopen_coap, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_COAP */
