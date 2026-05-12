/*
 * C-level WebSocket driver API for Lua RTOS on ESP32.
 * Self-contained — no Lua headers.
 *
 * Server side uses esp_http_server with CONFIG_HTTPD_WS_SUPPORT.
 * Client side uses the tcp_transport WebSocket transport stack.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ errors */

#define WS_ERR_CANT_CREATE_SERVER  0x00
#define WS_ERR_CANT_CREATE_CLIENT  0x01
#define WS_ERR_SEND_FAILED         0x02
#define WS_ERR_NOT_ENOUGH_MEM      0x03

/* ------------------------------------------------------------------ types */

typedef struct ws_server_ctx_t *ws_server_handle_t;
typedef struct ws_client_ctx_t *ws_client_handle_t;

/* Callbacks – all invoked from a dedicated FreeRTOS task, not from httpd. */
typedef void (*ws_msg_cb_t)(void *handle, const char *payload, size_t len,
                             void *user_data);
typedef void (*ws_open_cb_t)(void *handle, int fd, void *user_data);
typedef void (*ws_close_cb_t)(void *handle, int fd, void *user_data);

/* -------------------------------------------------------------- Server API */

/*
 * Create and start a WebSocket server on the given port.
 * Registers a single URI handler at "/" that accepts WS upgrades.
 * Returns 0 on success, -1 on failure.
 */
int ws_server_create(uint16_t port,
                     ws_open_cb_t  on_open,
                     ws_msg_cb_t   on_message,
                     ws_close_cb_t on_close,
                     void         *user_data,
                     ws_server_handle_t *out_handle);

void ws_server_destroy(ws_server_handle_t handle);

/*
 * Send a text frame to the client identified by file-descriptor fd.
 * Safe to call from any task.
 */
int ws_server_send(ws_server_handle_t handle, int fd,
                   const char *payload, size_t len);

/* Broadcast to all currently connected WebSocket clients. */
int ws_server_broadcast(ws_server_handle_t handle,
                        const char *payload, size_t len);

/* -------------------------------------------------------------- Client API */

/*
 * Connect to a WebSocket server at uri (ws:// or wss://).
 * Spawns a background receive task; on_open is called once the handshake
 * succeeds.
 * Returns 0 on success, -1 on failure.
 */
int ws_client_create(const char    *uri,
                     ws_open_cb_t   on_open,
                     ws_msg_cb_t    on_message,
                     ws_close_cb_t  on_close,
                     void          *user_data,
                     ws_client_handle_t *out_handle);

void ws_client_destroy(ws_client_handle_t handle);

/* Send a text frame to the server. */
int ws_client_send(ws_client_handle_t handle, const char *payload, size_t len);
