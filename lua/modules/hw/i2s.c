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
 * Lua RTOS, Lua I2S module
 *
 */

#include "luartos.h"

#if CONFIG_LUA_RTOS_LUA_USE_I2S

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "error.h"
#include "modules.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "driver/i2s_std.h"
#include "driver/i2s_types.h"
#include "hal/gpio_types.h"
#include "freertos/FreeRTOS.h"

/* Mode constants exposed to Lua */
#define LI2S_MODE_TX   0
#define LI2S_MODE_RX   1

/* Format constants exposed to Lua */
#define LI2S_FORMAT_MONO 0

typedef struct {
    i2s_chan_handle_t handle;
    int mode;
} i2s_userdata;

/*
 * i2s.attach(sck, ws, sd, mode, bits, format, rate, buflen)
 *
 * sck    - GPIO for bit clock (BCLK)
 * ws     - GPIO for word select (LRCLK)
 * sd     - GPIO for serial data (DOUT for TX, DIN for RX)
 * mode   - i2s.TX or i2s.RX
 * bits   - sample width: 8, 16, 24, or 32
 * format - i2s.MONO
 * rate   - sample rate in Hz
 * buflen - DMA buffer size in bytes per descriptor
 *
 * Returns an i2s device instance.
 */
static int li2s_attach(lua_State *L) {
    int sck    = luaL_checkinteger(L, 1);
    int ws     = luaL_checkinteger(L, 2);
    int sd     = luaL_checkinteger(L, 3);
    int mode   = luaL_checkinteger(L, 4);
    int bits   = luaL_checkinteger(L, 5);
    /* format arg (6) reserved for future stereo support */
    int rate   = luaL_checkinteger(L, 7);
    int buflen = luaL_checkinteger(L, 8);

    if (mode != LI2S_MODE_TX && mode != LI2S_MODE_RX) {
        return luaL_error(L, "i2s: invalid mode, use i2s.TX or i2s.RX");
    }

    i2s_data_bit_width_t bit_width;
    switch (bits) {
        case  8: bit_width = I2S_DATA_BIT_WIDTH_8BIT;  break;
        case 16: bit_width = I2S_DATA_BIT_WIDTH_16BIT; break;
        case 24: bit_width = I2S_DATA_BIT_WIDTH_24BIT; break;
        case 32: bit_width = I2S_DATA_BIT_WIDTH_32BIT; break;
        default:
            return luaL_error(L, "i2s: invalid bits %d, use 8, 16, 24 or 32", bits);
    }

    /* Convert buflen (bytes) to frames for the DMA descriptor */
    int bytes_per_sample = bits / 8;
    int dma_frame_num = buflen / bytes_per_sample;
    if (dma_frame_num < 8) dma_frame_num = 8;

    i2s_userdata *i2s = (i2s_userdata *)lua_newuserdata(L, sizeof(i2s_userdata));
    i2s->handle = NULL;
    i2s->mode   = mode;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear    = true;
    chan_cfg.dma_desc_num  = 4;
    chan_cfg.dma_frame_num = (uint32_t)dma_frame_num;

    esp_err_t err;
    if (mode == LI2S_MODE_TX) {
        err = i2s_new_channel(&chan_cfg, &i2s->handle, NULL);
    } else {
        err = i2s_new_channel(&chan_cfg, NULL, &i2s->handle);
    }
    if (err != ESP_OK) {
        return luaL_error(L, "i2s: failed to create channel (%d)", err);
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(bit_width, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)sck,
            .ws   = (gpio_num_t)ws,
            .dout = (mode == LI2S_MODE_TX) ? (gpio_num_t)sd : I2S_GPIO_UNUSED,
            .din  = (mode == LI2S_MODE_RX) ? (gpio_num_t)sd : I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(i2s->handle, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(i2s->handle);
        i2s->handle = NULL;
        return luaL_error(L, "i2s: failed to init channel (%d)", err);
    }

    err = i2s_channel_enable(i2s->handle);
    if (err != ESP_OK) {
        i2s_del_channel(i2s->handle);
        i2s->handle = NULL;
        return luaL_error(L, "i2s: failed to enable channel (%d)", err);
    }

    luaL_getmetatable(L, "i2s.dev");
    lua_setmetatable(L, -2);

    return 1;
}

/*
 * dev:detach()  — disable and release the I2S channel.
 */
static int li2s_detach(lua_State *L) {
    i2s_userdata *i2s = (i2s_userdata *)luaL_checkudata(L, 1, "i2s.dev");
    luaL_argcheck(L, i2s, 1, "i2s expected");

    if (i2s->handle) {
        i2s_channel_disable(i2s->handle);
        i2s_del_channel(i2s->handle);
        i2s->handle = NULL;
    }

    return 0;
}

/*
 * dev:write(buf)  — transmit a Lua string as raw I2S sample data.
 *
 * buf  - a Lua string containing the raw PCM bytes to send.
 *
 * Returns the number of bytes actually written.
 */
static int li2s_write(lua_State *L) {
    i2s_userdata *i2s = (i2s_userdata *)luaL_checkudata(L, 1, "i2s.dev");
    luaL_argcheck(L, i2s, 1, "i2s expected");

    if (!i2s->handle) {
        return luaL_error(L, "i2s: device is not open");
    }

    size_t len;
    const char *buf = luaL_checklstring(L, 2, &len);

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(i2s->handle, buf, len, &bytes_written, portMAX_DELAY);
    if (err != ESP_OK) {
        return luaL_error(L, "i2s: write failed (%d)", err);
    }

    lua_pushinteger(L, (lua_Integer)bytes_written);
    return 1;
}

/* Garbage collector — clean up if Lua forgets to call detach() */
static int li2s_gc(lua_State *L) {
    i2s_userdata *i2s = (i2s_userdata *)luaL_testudata(L, 1, "i2s.dev");
    if (i2s && i2s->handle) {
        i2s_channel_disable(i2s->handle);
        i2s_del_channel(i2s->handle);
        i2s->handle = NULL;
    }
    return 0;
}

/* Module-level function table (i2s.attach, i2s.TX, ...) */
static const LUA_REG_TYPE li2s_map[] = {
    { LSTRKEY("attach"), LFUNCVAL(li2s_attach)      },
    { LSTRKEY("TX"),     LINTVAL(LI2S_MODE_TX)      },
    { LSTRKEY("RX"),     LINTVAL(LI2S_MODE_RX)      },
    { LSTRKEY("MONO"),   LINTVAL(LI2S_FORMAT_MONO)  },
    { LNILKEY, LNILVAL }
};

/* Instance method table (dev:write, dev:detach) */
static const LUA_REG_TYPE li2s_dev_map[] = {
    { LSTRKEY("write"),       LFUNCVAL(li2s_write)   },
    { LSTRKEY("detach"),      LFUNCVAL(li2s_detach)  },
    { LSTRKEY("__metatable"), LROVAL(li2s_dev_map)   },
    { LSTRKEY("__index"),     LROVAL(li2s_dev_map)   },
    { LSTRKEY("__gc"),        LFUNCVAL(li2s_gc)      },
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_i2s(lua_State *L) {
    luaL_newmetarotable(L, "i2s.dev", (void *)li2s_dev_map);
    return 0;
}

MODULE_REGISTER_ROM(I2S, i2s, li2s_map, luaopen_i2s, 1);

#endif /* CONFIG_LUA_RTOS_LUA_USE_I2S */
