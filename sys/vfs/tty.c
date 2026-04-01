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
 * Lua RTOS tty vfs
 *
 */

#include "sdkconfig.h"
#include "vfs.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>

#include <sys/stat.h>

#ifdef CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include <sys/status.h>
#include <sys/_signal.h>
#include <signal.h>
#else
#include <drivers/uart.h>
#endif

extern FILE *lua_stdout_file;

// Local storage for file descriptors
static vfs_fd_local_storage_t *local_storage;

#ifdef CONSOLE_USB_SERIAL_JTAG
static QueueHandle_t usj_rx_queue = NULL;
static pthread_mutex_t usj_mtx = PTHREAD_MUTEX_INITIALIZER;

extern pthread_t lua_thread;
extern uint8_t console_raw;

static void usj_reader_task(void *arg) {
	uint8_t buf[64];

	for (;;) {
		int len = usb_serial_jtag_read_bytes(buf, sizeof(buf), portMAX_DELAY);
		for (int i = 0; i < len; i++) {
			uint8_t byte = buf[i];

			if ((byte == 0x04) && (!console_raw)) {
				status_set(STATUS_LUA_ABORT_BOOT_SCRIPTS, 0x00000000);
				continue;
			} else if ((byte == 0x03) && (!console_raw)) {
				if (status_get(STATUS_LUA_RUNNING)) {
					pthread_kill(lua_thread, SIGINT);
				}
				continue;
			}

			if (status_get(STATUS_LUA_RUNNING) || console_raw) {
				xQueueSend(usj_rx_queue, &byte, portMAX_DELAY);
			}
		}
	}
}

static int has_bytes(int fd, int to) {
	char c;

	if (to != portMAX_DELAY) {
		to = to / portTICK_PERIOD_MS;
	}

	return (xQueuePeek(usj_rx_queue, &c, (BaseType_t)to) == pdTRUE);
}

static int get(int fd, char *c) {
	if (xQueueReceive(usj_rx_queue, c, portMAX_DELAY) == pdTRUE) {
		return 1;
	}
	return 0;
}

static void put(int fd, char *c) {
	usb_serial_jtag_write_bytes((const uint8_t *)c, 1, portMAX_DELAY);
	if (lua_stdout_file) {
		fwrite(c, 1, 1, lua_stdout_file);
	}
}

#ifndef CONFIG_IDF_TARGET_ESP32S3
static int tty_has_bytes(int fd, int to) {
	char c;

	if (to != portMAX_DELAY) {
		to = to / portTICK_PERIOD_MS;
	}

	return (xQueuePeek(usj_rx_queue, &c, (BaseType_t)to) == pdTRUE);
}

static int tty_free(int fd) {
	return uxQueueSpacesAvailable(usj_rx_queue);
}
#endif

#else /* UART mode */

static int has_bytes(int fd, int to) {
	char c;

	if (to != portMAX_DELAY) {
		to = to / portTICK_PERIOD_MS;
	}

	return (xQueuePeek(uart_get_queue(fd), &c, (BaseType_t)to) == pdTRUE);
}

static int get(int fd, char *c) {
	return uart_read(fd, c, portMAX_DELAY);
}

static void put(int fd, char *c) {
    uart_write(fd, *c);
    if (lua_stdout_file) {
    	fwrite(c, 1, 1, lua_stdout_file);
    }
}

#ifndef CONFIG_IDF_TARGET_ESP32S3
static int tty_has_bytes(int fd, int to) {
    char c;

    if (to != portMAX_DELAY) {
        to = to / portTICK_PERIOD_MS;
    }

    return (xQueuePeek(uart_get_queue(fd), &c, (BaseType_t)to) == pdTRUE);
}

static int tty_free(int fd) {
    return uxQueueSpacesAvailable(uart_get_queue(fd));
}
#endif

#endif /* CONSOLE_USB_SERIAL_JTAG */

void console_io_lock(void) {
#ifdef CONSOLE_USB_SERIAL_JTAG
    pthread_mutex_lock(&usj_mtx);
#else
    uart_ll_lock(CONSOLE_UART);
#endif
}

void console_io_unlock(void) {
#ifdef CONSOLE_USB_SERIAL_JTAG
    pthread_mutex_unlock(&usj_mtx);
#else
    uart_ll_unlock(CONSOLE_UART);
#endif
}

void console_io_drain(void) {
#ifdef CONSOLE_USB_SERIAL_JTAG
    uint8_t byte;
    while (xQueueReceive(usj_rx_queue, &byte, 0) == pdTRUE) {}
#else
    uart_consume(CONSOLE_UART);
#endif
}

int console_io_read(char *c, uint32_t timeout_ms) {
#ifdef CONSOLE_USB_SERIAL_JTAG
    TickType_t ticks = (timeout_ms == portMAX_DELAY)
        ? portMAX_DELAY : (TickType_t)(timeout_ms / portTICK_PERIOD_MS);
    return (xQueueReceive(usj_rx_queue, c, ticks) == pdTRUE) ? 1 : 0;
#else
    return uart_read(CONSOLE_UART, c, timeout_ms);
#endif
}

void console_io_write(char c) {
#ifdef CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_write_bytes((const uint8_t *)&c, 1, portMAX_DELAY);
#else
    uart_write(CONSOLE_UART, c);
#endif
}

#ifndef CONFIG_IDF_TARGET_ESP32S3
static int __attribute__((unused)) vfs_tty_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout) {
    return vfs_generic_select(local_storage, tty_has_bytes, tty_free, maxfdp1, readset, writeset, exceptset, timeout);
}
#endif

static int  vfs_tty_open(const char *path, int flags, int mode) {
	int unit = 0;

	// Get unit
    if (strcmp(path, "/0") == 0) {
    	unit = 0;
    } else if (strcmp(path, "/1") == 0) {
    	unit = 1;
    } else if (strcmp(path, "/2") == 0) {
    	unit = 2;
    } else {
		errno = ENOENT;
    	return -1;
	}

#ifdef CONSOLE_USB_SERIAL_JTAG
    if (unit == 0) {
        usb_serial_jtag_driver_config_t cfg = {
            .tx_buffer_size = CONSOLE_BUFFER_LEN,
            .rx_buffer_size = CONSOLE_BUFFER_LEN,
        };
        usb_serial_jtag_driver_install(&cfg);
        usj_rx_queue = xQueueCreate(CONSOLE_BUFFER_LEN, sizeof(uint8_t));
        xTaskCreatePinnedToCore(usj_reader_task, "usj_rd", 4096, NULL, 21, NULL, 0);
    }
#else
    // Init uart unit
    uart_init(unit, CONSOLE_BR, 8, 0, 1, UART_FLAG_READ | UART_FLAG_WRITE, CONSOLE_BUFFER_LEN);
    uart_setup_interrupts(unit);
#endif

    // Store flags
    local_storage[unit].flags = flags;

    return unit;
}

static ssize_t vfs_tty_write(int fd, const void *data, size_t size) {
	int ret;

#ifdef CONSOLE_USB_SERIAL_JTAG
    pthread_mutex_lock(&usj_mtx);
	ret = vfs_generic_write(local_storage, put, fd, data, size);
    pthread_mutex_unlock(&usj_mtx);
#else
    uart_ll_lock(fd);
	ret = vfs_generic_write(local_storage, put, fd, data, size);
    uart_ll_unlock(fd);
#endif

    return ret;
}

static ssize_t vfs_tty_read(int fd, void * dst, size_t size) {
	return vfs_generic_read(local_storage, has_bytes, get, fd, dst, size);
}

static int vfs_tty_fstat(int fd, struct stat * st) {
    st->st_mode = S_IFCHR;
    return 0;
}

static int vfs_tty_close(int fd) {
	return 0;
}

#ifndef CONFIG_IDF_TARGET_ESP32S3
static ssize_t __attribute__((unused)) vfs_tty_writev(int fd, const struct iovec *iov, int iovcnt) {
	int ret;

#ifdef CONSOLE_USB_SERIAL_JTAG
    pthread_mutex_lock(&usj_mtx);
	ret = vfs_generic_writev(local_storage, put, fd, iov, iovcnt);
    pthread_mutex_unlock(&usj_mtx);
#else
    uart_ll_lock(fd);
	ret = vfs_generic_writev(local_storage, put, fd, iov, iovcnt);
    uart_ll_unlock(fd);
#endif

    return ret;
}
#endif

static int vfs_tty_fcntl(int fd, int cmd, int arg) {
	return vfs_generic_fcntl(local_storage, fd, cmd, arg);
}

void vfs_tty_register() {
    esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .write = &vfs_tty_write,
        .open = &vfs_tty_open,
        .fstat = &vfs_tty_fstat,
        .close = &vfs_tty_close,
        .read = &vfs_tty_read,
        .lseek = NULL,
        .stat = NULL,
        .link = NULL,
        .unlink = NULL,
        .rename = NULL,
		.fcntl = &vfs_tty_fcntl,
		//.writev = &vfs_tty_writev,
		//.select = &vfs_tty_select,
    };

    ESP_ERROR_CHECK(esp_vfs_register("/dev/tty", &vfs, NULL));

	local_storage = vfs_create_fd_local_storage(3);
	assert(local_storage != NULL);

	// Open standard streams, using defined tty for console
#ifdef CONSOLE_USB_SERIAL_JTAG
	_GLOBAL_REENT->_stdin  = fopen("/dev/tty/0", "r");
	_GLOBAL_REENT->_stdout = fopen("/dev/tty/0", "w");
	_GLOBAL_REENT->_stderr = fopen("/dev/tty/0", "w");
#else
	if (CONSOLE_UART == 0) {
		_GLOBAL_REENT->_stdin  = fopen("/dev/tty/0", "r");
		_GLOBAL_REENT->_stdout = fopen("/dev/tty/0", "w");
		_GLOBAL_REENT->_stderr = fopen("/dev/tty/0", "w");
	}

	if (CONSOLE_UART == 1) {
		_GLOBAL_REENT->_stdin  = fopen("/dev/tty/1", "r");
		_GLOBAL_REENT->_stdout = fopen("/dev/tty/1", "w");
		_GLOBAL_REENT->_stderr = fopen("/dev/tty/1", "w");
	}

	if (CONSOLE_UART == 2) {
		_GLOBAL_REENT->_stdin  = fopen("/dev/tty/2", "r");
		_GLOBAL_REENT->_stdout = fopen("/dev/tty/2", "w");
		_GLOBAL_REENT->_stderr = fopen("/dev/tty/2", "w");
	}
#endif

	// Work-around newlib is not compiled with HAVE_BLKSIZE flag
	setvbuf(_GLOBAL_REENT->_stdin , NULL, _IONBF, 0);
	setvbuf(_GLOBAL_REENT->_stdout, NULL, _IONBF, 0);
	setvbuf(_GLOBAL_REENT->_stderr, NULL, _IONBF, 0);

    // As vfs_tty_register can be called from a thread, update the
    // standard thread streams
    __getreent()->_stdin  = _GLOBAL_REENT->_stdin;
    __getreent()->_stdout = _GLOBAL_REENT->_stdout;
    __getreent()->_stderr = _GLOBAL_REENT->_stderr;
}
