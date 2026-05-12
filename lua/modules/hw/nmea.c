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
 * Lua RTOS, NMEA Lua module
 *
 */

#include "luartos.h"

#if CONFIG_LUA_RTOS_LUA_USE_NMEA

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "modules.h"
#include "error.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

#include <drivers/uart.h>
#include "nmea0183.h"

/* ----------------------------------------------------------------- errors */

#define LUA_NMEA_ERR_CANT_CREATE_TASK (DRIVER_EXCEPTION_BASE(NMEA_DRIVER_ID) | 0)

DRIVER_REGISTER_BEGIN(NMEA, nmea, 0, NULL, NULL);
    DRIVER_REGISTER_ERROR(NMEA, nmea, CannotCreateTask, "can't create task",
                          LUA_NMEA_ERR_CANT_CREATE_TASK);
DRIVER_REGISTER_END(NMEA, nmea, 0, NULL, NULL);

/* ------------------------------------------------------------------ types */

typedef struct {
    int          uart_id;
    TaskHandle_t task;
    int          running;
} lnmea_t;

static const LUA_REG_TYPE lnmea_inst_map[];

/* ------------------------------------------------------------------ task  */

static void nmea_reader_task(void *arg) {
    lnmea_t *inst = (lnmea_t *)arg;
    char line[84];

    while (inst->running) {
        if (uart_reads(inst->uart_id, line, sizeof(line) - 1, 1, 500)) {
            nmea_parse(line);
        }
    }
    vTaskDelete(NULL);
}

/* ----------------------------------------------------------------- Lua API */

/* nmea.setup(uart_id, baud) → instance */
static int lnmea_setup(lua_State *L) {
    driver_error_t *error;

    int uart_id = luaL_checkinteger(L, 1);
    int baud    = luaL_checkinteger(L, 2);

    error = uart_init(uart_id, baud, 8, 0, 1,
                      UART_FLAG_READ, 1024);
    if (error) {
        return luaL_driver_error(L, error);
    }

    error = uart_setup_interrupts(uart_id);
    if (error) {
        return luaL_driver_error(L, error);
    }

    lnmea_t *inst = (lnmea_t *)lua_newuserdata(L, sizeof(lnmea_t));
    memset(inst, 0, sizeof(*inst));
    inst->uart_id = uart_id;
    inst->running = 1;

    if (xTaskCreate(nmea_reader_task, "nmea_reader", 2048, inst, 5,
                    &inst->task) != pdPASS) {
        uart_stop(uart_id);
        return luaL_exception(L, LUA_NMEA_ERR_CANT_CREATE_TASK);
    }

    luaL_newmetarotable(L, "nmea.inst", (void *)lnmea_inst_map);
    lua_setmetatable(L, -2);
    return 1;
}

/* instance:read() → {lat, lon, height, sats, valid} */
static int lnmea_read(lua_State *L) {
    (void)luaL_checkudata(L, 1, "nmea.inst");

    int valid = nmea_new_pos();

    lua_createtable(L, 0, 5);

    lua_pushnumber(L, nmea_lat());
    lua_setfield(L, -2, "lat");

    lua_pushnumber(L, nmea_lon());
    lua_setfield(L, -2, "lon");

    lua_pushnumber(L, nmea_height());
    lua_setfield(L, -2, "height");

    lua_pushinteger(L, nmea_sats());
    lua_setfield(L, -2, "sats");

    lua_pushboolean(L, valid);
    lua_setfield(L, -2, "valid");

    return 1;
}

/* instance:close() */
static int lnmea_close(lua_State *L) {
    lnmea_t *inst = (lnmea_t *)luaL_checkudata(L, 1, "nmea.inst");

    if (inst->running) {
        inst->running = 0;
        /* Give the task time to exit before stopping the UART */
        vTaskDelay(pdMS_TO_TICKS(600));
        uart_stop(inst->uart_id);
    }
    return 0;
}

/* ---------------------------------------------------------------- tables */

static const LUA_REG_TYPE lnmea_inst_map[] = {
    { LSTRKEY("read"),  LFUNCVAL(lnmea_read)  },
    { LSTRKEY("close"), LFUNCVAL(lnmea_close) },
    { LSTRKEY("__index"), LROVAL(lnmea_inst_map) },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lnmea_map[] = {
    { LSTRKEY("setup"), LFUNCVAL(lnmea_setup) },
    DRIVER_REGISTER_LUA_ERRORS(nmea)
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_nmea(lua_State *L) {
    luaL_newmetarotable(L, "nmea.inst", (void *)lnmea_inst_map);
    LNEWLIB(L, nmea);
}

MODULE_REGISTER_ROM(NMEA, nmea, lnmea_map, luaopen_nmea, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_NMEA */
