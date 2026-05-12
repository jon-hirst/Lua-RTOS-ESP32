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
 * Lua RTOS, Lua SD card module
 *
 */

#include "sdkconfig.h"

#if (CONFIG_SD_CARD_MMC || CONFIG_SD_CARD_SPI) && CONFIG_LUA_RTOS_USE_FAT

#include "lua.h"
#include "lauxlib.h"
#include "modules.h"

#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_defs.h"

#include <vfs/vfs.h>

#define SD_DEFAULT_PATH "/sd"

/* sd.mount([path]) */
static int lsd_mount(lua_State *L) {
    const char *path = luaL_optstring(L, 1, SD_DEFAULT_PATH);
    if (vfs_fat_mount(path) != 0) {
        return luaL_error(L, "sd: mount failed on %s", path);
    }
    return 0;
}

/* sd.umount([path]) */
static int lsd_umount(lua_State *L) {
    const char *path = luaL_optstring(L, 1, SD_DEFAULT_PATH);
    if (vfs_fat_umount(path) != 0) {
        return luaL_error(L, "sd: umount failed on %s", path);
    }
    return 0;
}

/* sd.info() -> table {name, type, size_mb} */
static int lsd_info(lua_State *L) {
    sdmmc_card_t *card = (sdmmc_card_t *)vfs_fat_card();
    if (!card) {
        return luaL_error(L, "sd: no card mounted");
    }

    lua_newtable(L);

    lua_pushstring(L, card->cid.name);
    lua_setfield(L, -2, "name");

    lua_pushstring(L, (card->ocr & SD_OCR_SDHC_CAP) ? "SDHC/SDXC" : "SDSC");
    lua_setfield(L, -2, "type");

    uint32_t size_mb = (uint32_t)(
        ((uint64_t)card->csd.capacity * card->csd.sector_size) / (1024 * 1024));
    lua_pushinteger(L, (lua_Integer)size_mb);
    lua_setfield(L, -2, "size_mb");

    return 1;
}

/* sd.format([path]) */
static int lsd_format(lua_State *L) {
    const char *path = luaL_optstring(L, 1, SD_DEFAULT_PATH);
    if (vfs_fat_format(path) != 0) {
        return luaL_error(L, "sd: format failed on %s", path);
    }
    return 0;
}

static const LUA_REG_TYPE sd_map[] = {
    { LSTRKEY("mount"),  LFUNCVAL(lsd_mount)  },
    { LSTRKEY("umount"), LFUNCVAL(lsd_umount) },
    { LSTRKEY("info"),   LFUNCVAL(lsd_info)   },
    { LSTRKEY("format"), LFUNCVAL(lsd_format) },
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_sd(lua_State *L) {
    LNEWLIB(L, sd_map);
}

MODULE_REGISTER_ROM(SD, sd, sd_map, luaopen_sd, 1);

#endif
