/*
 * CoAP network layer: context management, server dispatch, client requests,
 * and Observe (RFC 7641).
 */
#include "coap.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---------------------------------------------------------------- helpers */

static uint16_t s_next_mid = 1;

static uint16_t next_mid(void) {
    return s_next_mid++;
}

/* Build the canonical resource path from Uri-Path options in the PDU.
   Result written to 'out' (size 'outsz'), always NUL-terminated.          */
static void build_path(const coap_pdu_t *pdu, char *out, size_t outsz) {
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < pdu->num_options; i++) {
        if (pdu->options[i].number != COAP_OPT_URI_PATH) continue;
        size_t seg_len = pdu->options[i].length;
        const uint8_t *seg = pdu->options[i].value;
        if (used + 1 + seg_len + 1 > outsz) break;
        out[used++] = '/';
        memcpy(out + used, seg, seg_len);
        used += seg_len;
        out[used] = '\0';
    }
    if (used == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
}

/* Find a registered resource whose path matches 'path' */
static coap_resource_t *find_resource(coap_context_t *ctx, const char *path) {
    for (coap_resource_t *r = ctx->resources; r; r = r->next) {
        if (strcmp(r->path, path) == 0) return r;
    }
    return NULL;
}

/* Find a method handler in a resource */
static coap_method_entry_t *find_handler(coap_resource_t *res, uint8_t method) {
    for (int i = 0; i < res->num_methods; i++) {
        if (res->methods[i].method == method) return &res->methods[i];
    }
    return NULL;
}

/* Send a PDU back to the remote peer described by 'session' */
static int send_response(coap_session_t *session, coap_pdu_t *resp) {
    int n = coap_encode_pdu(resp);
    if (n < 0) return -1;
    ssize_t sent = sendto(session->sock, resp->wire, (size_t)n, 0,
                          (struct sockaddr *)&session->addr, session->addr_len);
    return (int)sent;
}

/* Dispatch one incoming UDP packet */
static void dispatch(coap_context_t *ctx, const uint8_t *buf, size_t len,
                     struct sockaddr_storage *peer, socklen_t peer_len) {
    coap_pdu_t req, resp;
    if (coap_decode_pdu(&req, buf, len) < 0) return;

    /* We only handle confirmable and non-confirmable requests here */
    if (req.type != COAP_MESSAGE_CON && req.type != COAP_MESSAGE_NON) return;

    /* Only method codes (class 0) */
    if ((req.code >> 5) != 0) return;

    char path[128];
    build_path(&req, path, sizeof(path));

    coap_session_t session = {
        .sock      = ctx->sock,
        .addr      = *peer,
        .addr_len  = peer_len,
    };

    coap_resource_t *res = find_resource(ctx, path);
    if (!res) {
        coap_pdu_init(&resp, COAP_MESSAGE_ACK, COAP_RESPONSE_NOT_FOUND, req.mid);
        coap_add_token(&resp, req.token, req.token_len);
        send_response(&session, &resp);
        return;
    }

    coap_method_entry_t *me = find_handler(res, req.code);
    if (!me) {
        coap_pdu_init(&resp, COAP_MESSAGE_ACK, COAP_RESPONSE_NOT_ALLOWED, req.mid);
        coap_add_token(&resp, req.token, req.token_len);
        send_response(&session, &resp);
        return;
    }

    /* Prepare default response */
    coap_pdu_init(&resp, COAP_MESSAGE_ACK,
                  (req.code == COAP_REQUEST_GET)    ? COAP_RESPONSE_CONTENT :
                  (req.code == COAP_REQUEST_POST)   ? COAP_RESPONSE_CREATED :
                  (req.code == COAP_REQUEST_PUT)    ? COAP_RESPONSE_CHANGED :
                                                      COAP_RESPONSE_DELETED,
                  req.mid);
    coap_add_token(&resp, req.token, req.token_len);

    me->fn(ctx, res, &session, &req, &resp, me->user_data);

    send_response(&session, &resp);
}

/* ----------------------------------------------------------- Context API */

coap_context_t *coap_context_new(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return NULL;

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return NULL;
    }

    coap_context_t *ctx = calloc(1, sizeof(coap_context_t));
    if (!ctx) { close(sock); return NULL; }
    ctx->sock = sock;
    ctx->port = port;
    return ctx;
}

void coap_context_free(coap_context_t *ctx) {
    if (!ctx) return;
    close(ctx->sock);
    /* free resource list */
    coap_resource_t *r = ctx->resources;
    while (r) {
        coap_resource_t *next = r->next;
        coap_resource_free(r);
        r = next;
    }
    free(ctx);
}

int coap_io_process(coap_context_t *ctx, uint32_t timeout_ms) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(ctx->sock, &rfds);

    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };

    int rc = select(ctx->sock + 1, &rfds, NULL, NULL, &tv);
    if (rc < 0)  return -1;
    if (rc == 0) return 0;   /* timeout */

    uint8_t buf[COAP_MAX_PDU_SIZE];
    struct sockaddr_storage peer;
    socklen_t peer_len = sizeof(peer);
    ssize_t n = recvfrom(ctx->sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&peer, &peer_len);
    if (n < 0) return -1;

    dispatch(ctx, buf, (size_t)n, &peer, peer_len);
    return 1;
}

/* ---------------------------------------------------------- Resource API */

coap_resource_t *coap_resource_new(const char *path) {
    coap_resource_t *res = calloc(1, sizeof(coap_resource_t));
    if (!res) return NULL;
    /* Ensure path starts with '/' */
    if (path[0] != '/') {
        res->path[0] = '/';
        strlcpy(res->path + 1, path, sizeof(res->path) - 1);
    } else {
        strlcpy(res->path, path, sizeof(res->path));
    }
    return res;
}

void coap_resource_free(coap_resource_t *res) {
    free(res);
}

int coap_add_resource(coap_context_t *ctx, coap_resource_t *res) {
    res->next      = ctx->resources;
    ctx->resources = res;
    return 0;
}

void coap_register_handler(coap_resource_t *res, uint8_t method,
                             coap_handler_fn fn, void *user_data) {
    if (res->num_methods >= 4) return;
    res->methods[res->num_methods].method    = method;
    res->methods[res->num_methods].fn        = fn;
    res->methods[res->num_methods].user_data = user_data;
    res->num_methods++;
}

/* ------------------------------------------------------------ Client API */

/* Parse "coap://host[:port]/path[?query]" */
static int parse_uri(const char *uri, char *host, size_t host_sz,
                      uint16_t *port, char *path, size_t path_sz) {
    *port = COAP_DEFAULT_PORT;
    if (strncmp(uri, "coap://", 7) != 0) return -1;
    const char *p = uri + 7;

    /* host (may be IPv6 in brackets) */
    const char *host_end;
    if (*p == '[') {
        p++;
        host_end = strchr(p, ']');
        if (!host_end) return -1;
        size_t hlen = (size_t)(host_end - p);
        if (hlen >= host_sz) return -1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        p = host_end + 1;
    } else {
        host_end = p;
        while (*host_end && *host_end != ':' && *host_end != '/') host_end++;
        size_t hlen = (size_t)(host_end - p);
        if (hlen >= host_sz) return -1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        p = host_end;
    }

    if (*p == ':') {
        p++;
        *port = (uint16_t)atoi(p);
        while (*p && *p != '/') p++;
    }

    /* path */
    if (*p == '/') {
        const char *qmark = strchr(p, '?');
        size_t plen = qmark ? (size_t)(qmark - p) : strlen(p);
        if (plen >= path_sz) plen = path_sz - 1;
        memcpy(path, p, plen);
        path[plen] = '\0';
    } else {
        path[0] = '/';
        path[1] = '\0';
    }
    return 0;
}

/* Split "/seg1/seg2/..." into Uri-Path options added to pdu */
static void add_path_options(coap_pdu_t *pdu, const char *path) {
    const char *p = path;
    if (*p == '/') p++;  /* skip leading slash */
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t seg_len = slash ? (size_t)(slash - p) : strlen(p);
        if (seg_len > 0)
            coap_add_option(pdu, COAP_OPT_URI_PATH, (const uint8_t *)p, (uint16_t)seg_len);
        if (!slash) break;
        p = slash + 1;
    }
}

int coap_client_request(uint8_t method, const char *uri,
                          const uint8_t *payload, size_t payload_len,
                          uint16_t content_format,
                          uint32_t timeout_ms,
                          coap_client_resp_t *resp) {
    char     host[128];
    uint16_t port;
    char     path[256];

    if (parse_uri(uri, host, sizeof(host), &port, path, sizeof(path)) < 0)
        return -1;

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo *ai = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);
    if (getaddrinfo(host, port_str, &hints, &ai) != 0 || !ai) return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { freeaddrinfo(ai); return -1; }

    /* Build request PDU */
    coap_pdu_t req;
    memset(&req, 0, sizeof(req));
    coap_pdu_init(&req, COAP_MESSAGE_CON, method, next_mid());

    uint8_t token[4];
    uint32_t t = (uint32_t)(uintptr_t)&req ^ (uint32_t)xTaskGetTickCount();
    memcpy(token, &t, 4);
    coap_add_token(&req, token, 4);

    add_path_options(&req, path);

    if (payload_len > 0) {
        uint8_t cf_buf[2];
        cf_buf[0] = (uint8_t)(content_format >> 8);
        cf_buf[1] = (uint8_t)(content_format & 0xFF);
        if (content_format <= 255) {
            coap_add_option(&req, COAP_OPT_CONTENT_FORMAT, cf_buf + 1, 1);
        } else {
            coap_add_option(&req, COAP_OPT_CONTENT_FORMAT, cf_buf, 2);
        }
        coap_set_payload(&req, payload, payload_len);
    }

    int enc = coap_encode_pdu(&req);
    if (enc < 0) { close(sock); freeaddrinfo(ai); return -1; }

    if (sendto(sock, req.wire, (size_t)enc, 0, ai->ai_addr, ai->ai_addrlen) < 0) {
        close(sock); freeaddrinfo(ai); return -1;
    }
    freeaddrinfo(ai);

    /* Wait for response */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    int rc = select(sock + 1, &rfds, NULL, NULL, &tv);
    if (rc <= 0) { close(sock); return -1; }

    uint8_t rbuf[COAP_MAX_PDU_SIZE];
    ssize_t n = recv(sock, rbuf, sizeof(rbuf), 0);
    close(sock);
    if (n < 0) return -1;

    coap_pdu_t rsp;
    memset(&rsp, 0, sizeof(rsp));
    if (coap_decode_pdu(&rsp, rbuf, (size_t)n) < 0) return -1;

    if (resp) {
        resp->code         = rsp.code;
        resp->content_format = 0;
        const coap_opt_t *cf_opt = coap_find_option(&rsp, COAP_OPT_CONTENT_FORMAT);
        if (cf_opt) {
            if (cf_opt->length == 1) resp->content_format = cf_opt->value[0];
            else if (cf_opt->length == 2)
                resp->content_format = ((uint16_t)cf_opt->value[0] << 8) | cf_opt->value[1];
        }
        resp->payload     = NULL;
        resp->payload_len = 0;
        if (rsp.payload_len > 0) {
            resp->payload = malloc(rsp.payload_len);
            if (resp->payload) {
                memcpy(resp->payload, rsp.payload, rsp.payload_len);
                resp->payload_len = rsp.payload_len;
            }
        }
    }
    return 0;
}

void coap_client_resp_free(coap_client_resp_t *resp) {
    if (resp && resp->payload) {
        free(resp->payload);
        resp->payload     = NULL;
        resp->payload_len = 0;
    }
}

/* ---------------------------------------------------------- Observe API */

typedef struct {
    coap_observe_handle_t *handle;
} observe_task_arg_t;

static void observe_task(void *arg) {
    observe_task_arg_t *ta = (observe_task_arg_t *)arg;
    coap_observe_handle_t *h = ta->handle;
    free(ta);

    char     host[128];
    uint16_t port;
    char     path[256];

    if (parse_uri(h->uri, host, sizeof(host), &port, path, sizeof(path)) < 0) {
        h->active = 0;
        vTaskDelete(NULL);
        return;
    }

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo *ai = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);
    if (getaddrinfo(host, port_str, &hints, &ai) != 0 || !ai) {
        h->active = 0;
        vTaskDelete(NULL);
        return;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { freeaddrinfo(ai); h->active = 0; vTaskDelete(NULL); return; }

    /* Save server address before freeing ai, for ACK and de-register use */
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len = (socklen_t)ai->ai_addrlen;
    memcpy(&server_addr, ai->ai_addr, ai->ai_addrlen);

    /* Send GET with Observe=0 to register */
    coap_pdu_t req;
    memset(&req, 0, sizeof(req));
    coap_pdu_init(&req, COAP_MESSAGE_CON, COAP_REQUEST_GET, next_mid());
    uint8_t token[4];
    uint32_t t = (uint32_t)(uintptr_t)h;
    memcpy(token, &t, 4);
    coap_add_token(&req, token, 4);

    uint8_t obs_val = 0;
    coap_add_option(&req, COAP_OPT_OBSERVE, &obs_val, 1);
    add_path_options(&req, path);

    int enc = coap_encode_pdu(&req);
    if (enc < 0) { close(sock); freeaddrinfo(ai); h->active = 0; vTaskDelete(NULL); return; }
    sendto(sock, req.wire, (size_t)enc, 0, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(ai);

    /* Receive notifications */
    while (h->active) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int rc = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0) continue;

        uint8_t rbuf[COAP_MAX_PDU_SIZE];
        ssize_t n = recv(sock, rbuf, sizeof(rbuf), 0);
        if (n < 0) break;

        coap_pdu_t rsp;
        memset(&rsp, 0, sizeof(rsp));
        if (coap_decode_pdu(&rsp, rbuf, (size_t)n) < 0) continue;

        if (h->cb) {
            h->cb(rsp.payload, rsp.payload_len, rsp.code, h->user_data);
        }

        /* Send ACK if the server sent a CON notification */
        if (rsp.type == COAP_MESSAGE_CON) {
            coap_pdu_t ack;
            coap_pdu_init(&ack, COAP_MESSAGE_ACK, 0x00, rsp.mid);
            coap_encode_pdu(&ack);
            sendto(sock, ack.wire, ack.wire_len, 0,
                   (struct sockaddr *)&server_addr, server_addr_len);
        }
    }

    /* De-register: best-effort GET with Observe=1 */
    memset(&req, 0, sizeof(req));
    coap_pdu_init(&req, COAP_MESSAGE_NON, COAP_REQUEST_GET, next_mid());
    coap_add_token(&req, token, 4);
    obs_val = 1;
    coap_add_option(&req, COAP_OPT_OBSERVE, &obs_val, 1);
    add_path_options(&req, path);
    enc = coap_encode_pdu(&req);
    if (enc > 0) {
        sendto(sock, req.wire, (size_t)enc, 0,
               (struct sockaddr *)&server_addr, server_addr_len);
    }

    close(sock);
    h->active = 0;
    vTaskDelete(NULL);
}

coap_observe_handle_t *coap_observe_start(const char *uri,
                                            coap_observe_cb_t cb,
                                            void *user_data) {
    coap_observe_handle_t *h = calloc(1, sizeof(coap_observe_handle_t));
    if (!h) return NULL;
    h->active    = 1;
    h->cb        = cb;
    h->user_data = user_data;
    strlcpy(h->uri, uri, sizeof(h->uri));

    observe_task_arg_t *ta = malloc(sizeof(*ta));
    if (!ta) { free(h); return NULL; }
    ta->handle = h;

    TaskHandle_t task;
    if (xTaskCreate(observe_task, "coap_obs", 4096, ta, 5, &task) != pdPASS) {
        free(ta); free(h); return NULL;
    }
    h->task_handle = (void *)task;
    return h;
}

void coap_observe_cancel(coap_observe_handle_t *handle) {
    if (!handle) return;
    handle->active = 0;
    /* Task will exit on next 1-second poll */
}
