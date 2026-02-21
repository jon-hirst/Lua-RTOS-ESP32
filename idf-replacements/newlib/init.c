/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This file registers the CORE-stage initialization functions for the
 * Lua-RTOS newlib replacement component.
 *
 * In IDF v5.5, the standard newlib component provides:
 *   ESP_SYSTEM_INIT_FN(init_libc,      CORE, BIT(0), 102) -> esp_libc_init()
 *   ESP_SYSTEM_INIT_FN(init_libc_stdio,CORE, BIT(0), 115) -> esp_libc_init_global_stdio(...)
 *
 * Lua-RTOS replaces the newlib component and provides its own versions of
 * these functions using the older esp_newlib_* naming convention.
 *
 * init_libc_stdio opens /dev/console (priority 115, after init_vfs_console at
 * priority 114).  The Lua-RTOS tty VFS driver (vfs_tty_register) then installs
 * the USB-Serial-JTAG driver and redirects _GLOBAL_REENT stdio to /dev/tty/0.
 */

#include "sdkconfig.h"
#include "esp_newlib.h"
#include "esp_private/startup_internal.h"
#include "esp_bit_defs.h"

ESP_SYSTEM_INIT_FN(init_libc, CORE, BIT(0), 102)
{
    esp_newlib_init();
    return ESP_OK;
}

ESP_SYSTEM_INIT_FN(init_libc_stdio, CORE, BIT(0), 115)
{
#if CONFIG_VFS_SUPPORT_IO
    esp_newlib_init_global_stdio("/dev/console");
#else
    esp_newlib_init_global_stdio(NULL);
#endif
    return ESP_OK;
}

/* Linker hook — ensures the linker pulls in this translation unit */
void esp_libc_init_funcs(void)
{
}
