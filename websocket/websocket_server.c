#include "sdkconfig.h"
#if CONFIG_LUA_RTOS_LUA_USE_WEBSOCKET

#include "websocket.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

#define TAG "ws_server"

#define WS_MAX_CLIENTS 8
#define WS_RX_BUF_SIZE 4096

struct ws_server_ctx_t {
    httpd_handle_t  httpd;
    ws_open_cb_t    on_open;
    ws_msg_cb_t     on_message;
    ws_close_cb_t   on_close;
    void           *user_data;
    int             client_fds[WS_MAX_CLIENTS];
    int             num_clients;
};

/* -------------------------------------------------------- client FD tracking */

static void srv_add_client(struct ws_server_ctx_t *ctx, int fd) {
    for (int i = 0; i < ctx->num_clients; i++) {
        if (ctx->client_fds[i] == fd) return;
    }
    if (ctx->num_clients < WS_MAX_CLIENTS) {
        ctx->client_fds[ctx->num_clients++] = fd;
    }
}

static void srv_remove_client(struct ws_server_ctx_t *ctx, int fd) {
    for (int i = 0; i < ctx->num_clients; i++) {
        if (ctx->client_fds[i] == fd) {
            ctx->client_fds[i] = ctx->client_fds[--ctx->num_clients];
            return;
        }
    }
}

/* -------------------------------------------------------- URI handler */

static esp_err_t ws_handler(httpd_req_t *req) {
    struct ws_server_ctx_t *ctx = (struct ws_server_ctx_t *)req->user_ctx;

    if (req->method == HTTP_GET) {
        /* HTTP Upgrade handshake – httpd performs it automatically. */
        int fd = httpd_req_to_sockfd(req);
        srv_add_client(ctx, fd);
        if (ctx->on_open) {
            ctx->on_open(ctx, fd, ctx->user_data);
        }
        return ESP_OK;
    }

    /* Receive frame: first call with max_len=0 to get true length. */
    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "recv_frame size probe: %d", ret);
        return ret;
    }

    if (pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        srv_remove_client(ctx, fd);
        if (ctx->on_close) {
            ctx->on_close(ctx, fd, ctx->user_data);
        }
        /* Send close response */
        httpd_ws_frame_t close_frame = {
            .type = HTTPD_WS_TYPE_CLOSE,
            .payload = NULL,
            .len = 0,
            .final = true,
        };
        httpd_ws_send_frame(req, &close_frame);
        return ESP_OK;
    }

    if (pkt.len == 0) {
        /* Ping or zero-length text; nothing to dispatch. */
        if (pkt.type == HTTPD_WS_TYPE_PING) {
            pkt.type = HTTPD_WS_TYPE_PONG;
            httpd_ws_send_frame(req, &pkt);
        }
        return ESP_OK;
    }

    uint8_t *buf = (uint8_t *)malloc(pkt.len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "OOM for frame payload");
        return ESP_ERR_NO_MEM;
    }
    pkt.payload = buf;

    ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (ret != ESP_OK) {
        free(buf);
        ESP_LOGE(TAG, "recv_frame data: %d", ret);
        return ret;
    }
    buf[pkt.len] = '\0';

    if (ctx->on_message) {
        int fd = httpd_req_to_sockfd(req);
        ctx->on_message(ctx, (const char *)buf, pkt.len, ctx->user_data);
        (void)fd;
    }

    free(buf);
    return ESP_OK;
}

/* -------------------------------------------------------- public API */

int ws_server_create(uint16_t port,
                     ws_open_cb_t  on_open,
                     ws_msg_cb_t   on_message,
                     ws_close_cb_t on_close,
                     void         *user_data,
                     ws_server_handle_t *out_handle) {
    struct ws_server_ctx_t *ctx =
        (struct ws_server_ctx_t *)calloc(1, sizeof(struct ws_server_ctx_t));
    if (!ctx) return -1;

    ctx->on_open    = on_open;
    ctx->on_message = on_message;
    ctx->on_close   = on_close;
    ctx->user_data  = user_data;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port   = port;
    config.ctrl_port     = port + 1;   /* avoid clash with another httpd */
    config.max_open_sockets = WS_MAX_CLIENTS + 2;

    if (httpd_start(&ctx->httpd, &config) != ESP_OK) {
        free(ctx);
        return -1;
    }

    static const httpd_uri_t ws_uri = {
        .uri        = "/",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .is_websocket = true,
    };
    /* We need user_ctx per-registration; cast away const for the ptr field. */
    httpd_uri_t uri = ws_uri;
    uri.user_ctx = ctx;

    if (httpd_register_uri_handler(ctx->httpd, &uri) != ESP_OK) {
        httpd_stop(ctx->httpd);
        free(ctx);
        return -1;
    }

    *out_handle = ctx;
    return 0;
}

void ws_server_destroy(ws_server_handle_t handle) {
    if (!handle) return;
    httpd_stop(handle->httpd);
    free(handle);
}

int ws_server_send(ws_server_handle_t handle, int fd,
                   const char *payload, size_t len) {
    if (!handle || !payload) return -1;

    httpd_ws_frame_t frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)payload,
        .len     = len,
        .final   = true,
    };
    esp_err_t ret = httpd_ws_send_frame_async(handle->httpd, fd, &frame);
    return (ret == ESP_OK) ? 0 : -1;
}

int ws_server_broadcast(ws_server_handle_t handle,
                        const char *payload, size_t len) {
    if (!handle || !payload) return -1;
    int err = 0;
    for (int i = 0; i < handle->num_clients; i++) {
        if (ws_server_send(handle, handle->client_fds[i], payload, len) != 0) {
            err = -1;
        }
    }
    return err;
}

#endif /* CONFIG_LUA_RTOS_LUA_USE_WEBSOCKET */
