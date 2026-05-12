#include "sdkconfig.h"
#if CONFIG_LUA_RTOS_LUA_USE_WEBSOCKET

#include "websocket.h"

#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

#define TAG "ws_client"

#define WS_CLIENT_RX_BUF  4096
#define WS_CLIENT_TASK_STACK 4096
#define WS_CLIENT_TASK_PRIO  5
#define WS_CONNECT_TIMEOUT_MS 10000
#define WS_READ_TIMEOUT_MS    5000

struct ws_client_ctx_t {
    esp_transport_handle_t       tcp_t;
    esp_transport_handle_t       ws_t;
    ws_open_cb_t                 on_open;
    ws_msg_cb_t                  on_message;
    ws_close_cb_t                on_close;
    void                        *user_data;
    TaskHandle_t                 rx_task;
    volatile int                 running;
};

/* -------------------------------------------------------- URI parsing */

static int parse_ws_uri(const char *uri, char *host, size_t host_sz,
                         int *port, char *path, size_t path_sz,
                         int *use_tls) {
    const char *p = uri;

    if (strncmp(p, "wss://", 6) == 0) {
        *use_tls = 1;
        *port    = 443;
        p += 6;
    } else if (strncmp(p, "ws://", 5) == 0) {
        *use_tls = 0;
        *port    = 80;
        p += 5;
    } else {
        return -1;
    }

    /* Extract host[:port] */
    const char *host_start = p;
    const char *colon = NULL;
    const char *slash = NULL;
    for (; *p && *p != '/'; p++) {
        if (*p == ':') colon = p;
    }
    slash = (*p == '/') ? p : NULL;

    size_t host_len;
    if (colon) {
        host_len = (size_t)(colon - host_start);
        *port    = atoi(colon + 1);
    } else {
        host_len = (size_t)(p - host_start);
    }
    if (host_len >= host_sz) host_len = host_sz - 1;
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    if (slash) {
        size_t plen = strlen(slash);
        if (plen >= path_sz) plen = path_sz - 1;
        memcpy(path, slash, plen);
        path[plen] = '\0';
    } else {
        path[0] = '/';
        path[1] = '\0';
    }

    return 0;
}

/* -------------------------------------------------------- receive task */

static void ws_client_rx_task(void *arg) {
    struct ws_client_ctx_t *ctx = (struct ws_client_ctx_t *)arg;
    char *buf = (char *)malloc(WS_CLIENT_RX_BUF);
    if (!buf) {
        ESP_LOGE(TAG, "OOM in rx task");
        vTaskDelete(NULL);
        return;
    }

    while (ctx->running) {
        int n = esp_transport_read(ctx->ws_t, buf, WS_CLIENT_RX_BUF - 1,
                                    WS_READ_TIMEOUT_MS);
        if (n < 0) {
            /* Connection closed or error */
            if (ctx->running) {
                ctx->running = 0;
                if (ctx->on_close) {
                    ctx->on_close(ctx, 0, ctx->user_data);
                }
            }
            break;
        }
        if (n == 0) {
            /* Timeout — keep polling */
            continue;
        }

        ws_transport_opcodes_t opcode = esp_transport_ws_get_read_opcode(ctx->ws_t);

        if (opcode == WS_TRANSPORT_OPCODES_CLOSE) {
            ctx->running = 0;
            if (ctx->on_close) {
                ctx->on_close(ctx, 0, ctx->user_data);
            }
            break;
        }

        if ((opcode == WS_TRANSPORT_OPCODES_TEXT ||
             opcode == WS_TRANSPORT_OPCODES_BINARY) && ctx->on_message) {
            buf[n] = '\0';
            ctx->on_message(ctx, buf, (size_t)n, ctx->user_data);
        }
        /* Ping/pong/cont frames are handled by the transport layer. */
    }

    free(buf);
    vTaskDelete(NULL);
}

/* -------------------------------------------------------- public API */

int ws_client_create(const char    *uri,
                     ws_open_cb_t   on_open,
                     ws_msg_cb_t    on_message,
                     ws_close_cb_t  on_close,
                     void          *user_data,
                     ws_client_handle_t *out_handle) {
    char host[128];
    char path[256];
    int  port;
    int  use_tls = 0;

    if (parse_ws_uri(uri, host, sizeof(host), &port, path, sizeof(path),
                      &use_tls) != 0) {
        ESP_LOGE(TAG, "invalid uri: %s", uri);
        return -1;
    }

    struct ws_client_ctx_t *ctx =
        (struct ws_client_ctx_t *)calloc(1, sizeof(struct ws_client_ctx_t));
    if (!ctx) return -1;

    ctx->on_open    = on_open;
    ctx->on_message = on_message;
    ctx->on_close   = on_close;
    ctx->user_data  = user_data;

    if (use_tls) {
        ctx->tcp_t = esp_transport_ssl_init();
    } else {
        ctx->tcp_t = esp_transport_tcp_init();
    }
    if (!ctx->tcp_t) {
        free(ctx);
        return -1;
    }

    ctx->ws_t = esp_transport_ws_init(ctx->tcp_t);
    if (!ctx->ws_t) {
        esp_transport_destroy(ctx->tcp_t);
        free(ctx);
        return -1;
    }
    esp_transport_ws_set_path(ctx->ws_t, path);

    if (esp_transport_connect(ctx->ws_t, host, port, WS_CONNECT_TIMEOUT_MS) < 0) {
        ESP_LOGE(TAG, "connect failed to %s:%d%s", host, port, path);
        esp_transport_destroy(ctx->ws_t);
        esp_transport_destroy(ctx->tcp_t);
        free(ctx);
        return -1;
    }

    ctx->running = 1;

    if (on_open) {
        on_open(ctx, 0, user_data);
    }

    if (xTaskCreate(ws_client_rx_task, "ws_cli_rx", WS_CLIENT_TASK_STACK,
                    ctx, WS_CLIENT_TASK_PRIO, &ctx->rx_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to create rx task");
        ctx->running = 0;
        esp_transport_close(ctx->ws_t);
        esp_transport_destroy(ctx->ws_t);
        esp_transport_destroy(ctx->tcp_t);
        free(ctx);
        return -1;
    }

    *out_handle = ctx;
    return 0;
}

void ws_client_destroy(ws_client_handle_t handle) {
    if (!handle) return;
    handle->running = 0;
    /* Give rx task time to notice and exit. */
    vTaskDelay(pdMS_TO_TICKS(WS_READ_TIMEOUT_MS + 200));
    esp_transport_close(handle->ws_t);
    esp_transport_destroy(handle->ws_t);
    esp_transport_destroy(handle->tcp_t);
    free(handle);
}

int ws_client_send(ws_client_handle_t handle, const char *payload, size_t len) {
    if (!handle || !payload || !handle->running) return -1;
    int n = esp_transport_ws_send_raw(handle->ws_t,
                                       WS_TRANSPORT_OPCODES_TEXT,
                                       payload, (int)len,
                                       5000);
    return (n == (int)len) ? 0 : -1;
}

#endif /* CONFIG_LUA_RTOS_LUA_USE_WEBSOCKET */
