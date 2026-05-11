/*
 * Copyright (C) 2015 - 2020, IBEROXARXA SERVICIOS INTEGRALES, S.L.
 * Copyright (C) 2015 - 2020, Jaume Olive Petrus (jolive@whitecatboard.org)
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
 * Lua RTOS, ST7789 driver
 *
 */

/*
 * This driver is based on the ST7735 driver and adapted for the ST7789
 * LCD controller, which supports up to 240x240 pixels with 262K colors
 * (18-bit RGB) over SPI interface.
 */

#include "sdkconfig.h"

#if CONFIG_LUA_RTOS_LUA_USE_GDISPLAY

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#include "driver.h"
#include "syslog.h"

#include "gdisplay.h"

#include "gpio.h"
#include "spi.h"
#include "dgdisplay.h"

#include <drivers/st7789.h>

// Initialization commands for ST7789
static const uint8_t ST7789_init_cmds[] = {
  14,                             // 14 commands in list:
  ST7789_SWRESET,   DELAY,       //  1: Software reset, no args, w/delay
  150,                            //     150 ms delay
  ST7789_SLPOUT,    DELAY,       //  2: Out of sleep mode, no args, w/delay
  255,                            //     500 ms delay
  ST7789_COLMOD,  1+DELAY,       //  3: Set color mode, 1 arg + delay:
  ST7789_COLMOD_16BIT,           //     16-bit RGB565 – matches gdisplay pipeline
  10,                             //     10 ms delay
  ST7789_MADCTL,  1,             //  4: Memory access ctrl, 1 arg:
  0x00,                           //     Row addr/col addr, top to bottom refresh
  ST7789_CASET,   4,             //  5: Column addr set, 4 args:
  0x00, 0x00,                    //     XSTART = 0
  0x00, 0xEF,                    //     XEND = 239
  ST7789_RASET,   4,             //  6: Row addr set, 4 args:
  0x00, 0x00,                    //     YSTART = 0
  0x01, 0x3F,                    //     YEND = 319
  ST7789_INVON,   DELAY,         //  7: Inversion ON (ST7789 typically needs this)
  10,                             //     10 ms delay
  ST7789_FRMCTR2, 5,             //  8: Porch control, 5 args:
  0x0C, 0x0C, 0x00, 0x33, 0x33, //     Front/back porch settings
  ST7789_PWCTR1,  2,             //  9: Power control 1, 2 args:
  0xA4, 0xA1,                    //     AVDD=6.8V, AVCL=-4.8V, VDDS=2.3V
  ST7789_GMCTRP1, 14,            // 10: Positive gamma correction, 14 args:
  0xD0, 0x04, 0x0D, 0x11,
  0x13, 0x2B, 0x3F, 0x54,
  0x4C, 0x18, 0x0D, 0x0B,
  0x1F, 0x23,
  ST7789_GMCTRN1, 14,            // 11: Negative gamma correction, 14 args:
  0xD0, 0x04, 0x0C, 0x11,
  0x13, 0x2C, 0x3F, 0x44,
  0x51, 0x2F, 0x1F, 0x1F,
  0x20, 0x23,
  ST7789_NORON,     DELAY,       // 12: Normal display on, no args, w/delay
  10,                             //     10 ms delay
  ST7789_DISPON,    DELAY,       // 13: Main screen turn on, no args, w/delay
  100,                            //     100 ms delay
  ST7789_RAMWR,   0              // 14: Begin writing to RAM
};

/*
 * Helper functions
 */
static void st7789_reset(void) {
#if CONFIG_LUA_RTOS_GDISPLAY_RESET == -1
	gdisplay_ll_command(ST7789_SWRESET);
	vTaskDelay(130 / portTICK_PERIOD_MS);
#else
	(void)gpio_pin_set(CONFIG_LUA_RTOS_GDISPLAY_RESET);
	vTaskDelay(10 / portTICK_PERIOD_MS);
	(void)gpio_pin_clr(CONFIG_LUA_RTOS_GDISPLAY_RESET);
	vTaskDelay(50 / portTICK_PERIOD_MS);
	(void)gpio_pin_set(CONFIG_LUA_RTOS_GDISPLAY_RESET);
	vTaskDelay(130 / portTICK_PERIOD_MS);
#endif
}

/*
 * Operation functions
 */
driver_error_t *st7789_init(uint8_t chip, uint8_t orientation, uint8_t address) {
	driver_error_t *error;
	gdisplay_caps_t *caps = gdisplay_ll_get_caps();

	caps->addr_window = st7789_addr_window;
	caps->on = st7789_on;
	caps->off = st7789_off;
	caps->invert = st7789_invert;
	caps->orientation = st7789_set_orientation;
	caps->touch_get = NULL;
	caps->touch_cal = NULL;
	caps->bytes_per_pixel = 2;
	caps->rdepth = 6;
	caps->gdepth = 6;
	caps->bdepth = 6;
	caps->phys_width  = ST7789_HEIGHT;
	caps->phys_height = ST7789_WIDTH;
	caps->interface = GDisplaySPIInterface;

	// Init SPI bus
	if (caps->device == -1) {
		if ((error = spi_setup(CONFIG_LUA_RTOS_GDISPLAY_SPI, 1, CONFIG_LUA_RTOS_GDISPLAY_CS, 0, 40000000, SPI_FLAG_WRITE | SPI_FLAG_NO_DMA, &caps->device))) {
			return error;
		}
	}

#if CONFIG_LUA_RTOS_USE_HARDWARE_LOCKS
	driver_unit_lock_error_t *lock_error = NULL;
	if ((error = spi_lock_bus_resources(CONFIG_LUA_RTOS_GDISPLAY_SPI, DRIVER_ALL_FLAGS))) {
		return error;
	}

	if ((lock_error = driver_lock(GDISPLAY_DRIVER, 0, GPIO_DRIVER, CONFIG_LUA_RTOS_GDISPLAY_CMD, DRIVER_ALL_FLAGS, "gdisplay - ST7789"))) {
		return driver_lock_error(GDISPLAY_DRIVER, lock_error);
	}
#endif

	// setup command pin
	if ((error = gpio_pin_output(CONFIG_LUA_RTOS_GDISPLAY_CMD))) return error;

#if CONFIG_LUA_RTOS_GDISPLAY_RESET != -1
#if CONFIG_LUA_RTOS_USE_HARDWARE_LOCKS
	if ((lock_error = driver_lock(GDISPLAY_DRIVER, 0, GPIO_DRIVER, CONFIG_LUA_RTOS_GDISPLAY_RESET, DRIVER_ALL_FLAGS, "gdisplay - ST7789"))) {
		return driver_lock_error(GDISPLAY_DRIVER, lock_error);
	}
#endif

	// setup reset pin
	if ((error = gpio_pin_output(CONFIG_LUA_RTOS_GDISPLAY_RESET))) return error;
	gpio_ll_pin_clr(CONFIG_LUA_RTOS_GDISPLAY_RESET);
#endif

	// Reset display
	st7789_reset();

	// Send initialization commands
	gdisplay_ll_command_list(ST7789_init_cmds);

	st7789_set_orientation(orientation);

	// Allocate buffer
	if (!gdisplay_ll_allocate_buffer(ST7789_BUFFER)) {
		return driver_error(GDISPLAY_DRIVER, GDISPLAY_ERR_NOT_ENOUGH_MEMORY, NULL);
	}

	// Clear screen (black)
	st7789_clear(0x000000);

	return NULL;
}

void st7789_addr_window(uint8_t write, int x0, int y0, int x1, int y1) {
	gdisplay_caps_t *caps = gdisplay_ll_get_caps();
	uint32_t wd;

	if ((x0 >= caps->width) || (y0 >= caps->height) || (x1 >= caps->width) || (y1 >= caps->height)) {
		return;
	}

	x0 += caps->xstart;
	x1 += caps->xstart;

	y0 += caps->ystart;
	y1 += caps->ystart;

	wd  = (uint32_t)(x0 >> 8);
	wd |= (uint32_t)(x0 & 0xff) << 8;
	wd |= (uint32_t)(x1 >> 8) << 16;
	wd |= (uint32_t)(x1 & 0xff) << 24;

	gdisplay_ll_command(ST7789_CASET);
	gdisplay_ll_data32(wd);

	wd  = (uint32_t)(y0 >> 8);
	wd |= (uint32_t)(y0 & 0xff) << 8;
	wd |= (uint32_t)(y1 >> 8) << 16;
	wd |= (uint32_t)(y1 & 0xff) << 24;

	gdisplay_ll_command(ST7789_RASET);
	gdisplay_ll_data32(wd);

	gdisplay_ll_command((write ? ST7789_RAMWR : ST7789_RAMRD));
}

void st7789_set_orientation(uint8_t m) {
	gdisplay_caps_t *caps = gdisplay_ll_get_caps();
	uint8_t orientation = m & 3;
	uint8_t madctl = 0;

	switch (orientation) {
	  case PORTRAIT:
		// 0°: MY=0 → gate 1..240 map to GRAM rows 0..239, no offset needed.
		// MX compensates for physical column reversal on this panel.
		madctl = (ST7789_MADCTL_MX | ST7789_MADCTL_RGB);
		caps->width  = ST7789_WIDTH;
		caps->height = ST7789_HEIGHT;
		caps->xstart = 0;
		caps->ystart = 0;
		break;
	  case LANDSCAPE:
		// 90° CW: axis exchange (MV), MY=0 → no row-axis offset needed.
		madctl = (ST7789_MADCTL_MV | ST7789_MADCTL_RGB);
		caps->width  = ST7789_HEIGHT;
		caps->height = ST7789_WIDTH;
		caps->xstart = 0;
		caps->ystart = 0;
		break;
	  case PORTRAIT_FLIP:
		// 180°: MY=1 reverses gate scan → gate 1 maps to GRAM row 319,
		// gate 240 to row 80; the 240-pixel panel covers rows 80..319.
		madctl = (ST7789_MADCTL_MY | ST7789_MADCTL_RGB);
		caps->width  = ST7789_WIDTH;
		caps->height = ST7789_HEIGHT;
		caps->xstart = 0;
		caps->ystart = ST7789_ROW_OFFSET;
		break;
	  case LANDSCAPE_FLIP:
		// 270° CW: axis exchange (MV) + MY=1 → same row-axis reversal
		// applies horizontally; 240 pixels cover GRAM rows 80..319.
		madctl = (ST7789_MADCTL_MX | ST7789_MADCTL_MY | ST7789_MADCTL_MV | ST7789_MADCTL_RGB);
		caps->width  = ST7789_HEIGHT;
		caps->height = ST7789_WIDTH;
		caps->xstart = ST7789_ROW_OFFSET;
		caps->ystart = 0;
		break;
	}

	gdisplay_ll_command(ST7789_MADCTL);
	gdisplay_ll_data(&madctl, 1);
}

void st7789_on() {
	gdisplay_ll_command(ST7789_DISPON);
}

void st7789_off() {
	gdisplay_ll_command(ST7789_DISPOFF);
}

void st7789_invert(uint8_t on) {
	if (on) {
		gdisplay_ll_command(ST7789_INVON);
	} else {
		gdisplay_ll_command(ST7789_INVOFF);
	}
}

void st7789_color(uint16_t *color, uint32_t len) {
	gdisplay_caps_t *caps = gdisplay_ll_get_caps();
	uint8_t *buff = (uint8_t *)gdisplay_ll_get_buffer();
	uint32_t buff_size = gdisplay_ll_get_buffer_size();
	uint8_t *buffer;

	// Convert 18-bit packed input (bits [17:12]=R6,[11:6]=G6,[5:0]=B6) to
	// 16-bit RGB565 sent as two big-endian bytes (MSB first) to the display.
	uint32_t c;
	memcpy(&c, color, sizeof(c));
	uint8_t r5 = (c >> 13) & 0x1F;
	uint8_t g6 = (c >> 6)  & 0x3F;
	uint8_t b5 = (c >> 1)  & 0x1F;
	uint16_t rgb565 = ((uint16_t)r5 << 11) | ((uint16_t)g6 << 5) | b5;
	uint8_t hi = (rgb565 >> 8) & 0xFF;
	uint8_t lo =  rgb565       & 0xFF;

	int i;

	if (len > 0) {
		buffer = buff;
		// Fill buffer with 2-byte RGB565 pixel values
		uint32_t pixels_in_buf = buff_size / 2;
		for (i = 0; i < pixels_in_buf; i++) {
			buffer[i * 2]     = hi;
			buffer[i * 2 + 1] = lo;
		}
	} else {
		// Single pixel write
		buff[0] = hi;
		buff[1] = lo;
		buffer = buff;
	}

	// Set DC to 1 (data mode)
	gpio_ll_pin_set(CONFIG_LUA_RTOS_GDISPLAY_CMD);
	spi_ll_select(caps->device);
	if (len > 0) {
		uint32_t pixels_in_buf = buff_size / 2;
		uint32_t clen;
		while (len) {
			clen = (len > pixels_in_buf ? pixels_in_buf : len);
			spi_ll_bulk_write(caps->device, clen * 2, buffer);
			len = len - clen;
		}
	} else {
		spi_ll_bulk_write(caps->device, 2, buffer);
	}
	spi_ll_deselect(caps->device);

	gdisplay_ll_invalidate_buffer();
}

void st7789_clear(uint32_t color) {
	gdisplay_caps_t *caps = gdisplay_ll_get_caps();

	st7789_addr_window(1, 0, 0, caps->width - 1, caps->height - 1);
	st7789_color((uint16_t *)&color, caps->width * caps->height);
}

#endif
