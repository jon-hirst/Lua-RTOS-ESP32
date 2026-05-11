/*
 * Minimal CoAP implementation for Lua RTOS on ESP32 (RFC 7252 + RFC 7641 Observe).
 * Self-contained UDP-based CoAP stack; no external dependencies beyond lwIP sockets.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>

/* ------------------------------------------------------------------ types */

/* CoAP message types */
#define COAP_MESSAGE_CON  0
#define COAP_MESSAGE_NON  1
#define COAP_MESSAGE_ACK  2
#define COAP_MESSAGE_RST  3

/* CoAP request codes */
#define COAP_REQUEST_GET     1
#define COAP_REQUEST_POST    2
#define COAP_REQUEST_PUT     3
#define COAP_REQUEST_DELETE  4

/* CoAP response codes encoded as (class<<5)|detail */
#define COAP_RESPONSE_CREATED       0x41   /* 2.01 */
#define COAP_RESPONSE_DELETED       0x42   /* 2.02 */
#define COAP_RESPONSE_VALID         0x43   /* 2.03 */
#define COAP_RESPONSE_CHANGED       0x44   /* 2.04 */
#define COAP_RESPONSE_CONTENT       0x45   /* 2.05 */
#define COAP_RESPONSE_BAD_REQUEST   0x80   /* 4.00 */
#define COAP_RESPONSE_UNAUTHORIZED  0x81   /* 4.01 */
#define COAP_RESPONSE_NOT_FOUND     0x84   /* 4.04 */
#define COAP_RESPONSE_NOT_ALLOWED   0x85   /* 4.05 */
#define COAP_RESPONSE_INTERNAL_ERR  0xA0   /* 5.00 */

/* CoAP option numbers */
#define COAP_OPT_IF_MATCH       1
#define COAP_OPT_URI_HOST       3
#define COAP_OPT_ETAG           4
#define COAP_OPT_IF_NONE_MATCH  5
#define COAP_OPT_OBSERVE        6
#define COAP_OPT_URI_PORT       7
#define COAP_OPT_LOCATION_PATH  8
#define COAP_OPT_URI_PATH       11
#define COAP_OPT_CONTENT_FORMAT 12
#define COAP_OPT_MAX_AGE        14
#define COAP_OPT_URI_QUERY      15
#define COAP_OPT_ACCEPT         17
#define COAP_OPT_LOCATION_QUERY 20
#define COAP_OPT_BLOCK2         23
#define COAP_OPT_BLOCK1         27
#define COAP_OPT_SIZE2          28
#define COAP_OPT_PROXY_URI      35
#define COAP_OPT_SIZE1          60

/* Content-Format values */
#define COAP_CT_TEXT_PLAIN   0
#define COAP_CT_APP_LINK     40
#define COAP_CT_APP_XML      41
#define COAP_CT_APP_OCTET    42
#define COAP_CT_APP_EXI      47
#define COAP_CT_APP_JSON     50
#define COAP_CT_APP_CBOR     60

#define COAP_DEFAULT_PORT    5683
#define COAP_MAX_PDU_SIZE    1280
#define COAP_MAX_OPTIONS     16   /* max options per PDU */
#define COAP_MAX_OPT_VAL     256  /* total bytes for option value storage */
#define COAP_MAX_RESOURCES   16   /* max registered server resources */

/* One CoAP option as decoded/to-be-encoded */
typedef struct {
    uint16_t       number;
    uint16_t       length;
    const uint8_t *value;  /* points into pdu->opt_store or supplied buffer */
} coap_opt_t;

/* CoAP PDU – used for both incoming and outgoing messages */
typedef struct coap_pdu_t {
    uint8_t   type;                  /* COAP_MESSAGE_xxx */
    uint8_t   code;                  /* request code or response code */
    uint16_t  mid;                   /* message ID */
    uint8_t   token[8];
    uint8_t   token_len;
    coap_opt_t options[COAP_MAX_OPTIONS];
    int        num_options;
    uint8_t    opt_store[COAP_MAX_OPT_VAL]; /* storage for decoded option values */
    int        opt_store_used;
    const uint8_t *payload;          /* points inside wire[] for decoded, or caller buf */
    size_t     payload_len;
    uint8_t    wire[COAP_MAX_PDU_SIZE]; /* scratch for encode/decode */
    size_t     wire_len;
} coap_pdu_t;

/* Network peer identity */
typedef struct {
    int    sock;
    struct sockaddr_storage addr;
    socklen_t addr_len;
} coap_session_t;

typedef struct coap_resource_t coap_resource_t;
typedef struct coap_context_t  coap_context_t;

/* Server-side method handler callback */
typedef void (*coap_handler_fn)(coap_context_t   *ctx,
                                 coap_resource_t  *res,
                                 coap_session_t   *session,
                                 const coap_pdu_t *request,
                                 coap_pdu_t       *response,
                                 void             *user_data);

typedef struct {
    uint8_t        method;   /* COAP_REQUEST_xxx */
    coap_handler_fn fn;
    void           *user_data;
} coap_method_entry_t;

struct coap_resource_t {
    char               path[128];
    coap_method_entry_t methods[4]; /* one slot per method GET/POST/PUT/DELETE */
    int                 num_methods;
    coap_resource_t    *next;
};

struct coap_context_t {
    int               sock;
    uint16_t          port;
    coap_resource_t  *resources;
};

/* ---------------------------------------------------------------- PDU API */

coap_pdu_t *coap_pdu_new(void);
void        coap_pdu_free(coap_pdu_t *pdu);
void        coap_pdu_init(coap_pdu_t *pdu, uint8_t type, uint8_t code, uint16_t mid);
int         coap_add_token(coap_pdu_t *pdu, const uint8_t *token, uint8_t len);
int         coap_add_option(coap_pdu_t *pdu, uint16_t opt_num,
                             const uint8_t *val, uint16_t val_len);
int         coap_set_payload(coap_pdu_t *pdu, const uint8_t *data, size_t len);
const coap_opt_t *coap_find_option(const coap_pdu_t *pdu, uint16_t opt_num);
int         coap_encode_pdu(coap_pdu_t *pdu);           /* fills pdu->wire / wire_len */
int         coap_decode_pdu(coap_pdu_t *pdu, const uint8_t *buf, size_t len);

/* ----------------------------------------------------------- Context API */

coap_context_t *coap_context_new(uint16_t port);
void            coap_context_free(coap_context_t *ctx);
/* Process one inbound packet; returns 1 if processed, 0 on timeout, <0 on error */
int             coap_io_process(coap_context_t *ctx, uint32_t timeout_ms);

/* ---------------------------------------------------------- Resource API */

coap_resource_t *coap_resource_new(const char *path);
void             coap_resource_free(coap_resource_t *res);
int              coap_add_resource(coap_context_t *ctx, coap_resource_t *res);
void             coap_register_handler(coap_resource_t *res, uint8_t method,
                                        coap_handler_fn fn, void *user_data);

/* ------------------------------------------------------------ Client API */

typedef struct {
    uint8_t  code;
    uint8_t *payload;       /* heap-allocated; caller must free with coap_client_resp_free */
    size_t   payload_len;
    uint16_t content_format;
} coap_client_resp_t;

/*
 * Send a CoAP request and block until a response arrives or timeout expires.
 * uri format: "coap://host[:port]/path[?query]"
 * Returns 0 on success, -1 on error.
 */
int  coap_client_request(uint8_t method, const char *uri,
                          const uint8_t *payload, size_t payload_len,
                          uint16_t content_format,
                          uint32_t timeout_ms,
                          coap_client_resp_t *resp);
void coap_client_resp_free(coap_client_resp_t *resp);

/* ---------------------------------------------------------- Observe API */

typedef void (*coap_observe_cb_t)(const uint8_t *payload, size_t len,
                                   uint8_t code, void *user_data);

typedef struct coap_observe_handle_t {
    volatile int       active;
    void              *task_handle;  /* FreeRTOS TaskHandle_t cast to void* */
    char               uri[256];
    coap_observe_cb_t  cb;
    void              *user_data;
} coap_observe_handle_t;

coap_observe_handle_t *coap_observe_start(const char *uri,
                                           coap_observe_cb_t cb,
                                           void *user_data);
void coap_observe_cancel(coap_observe_handle_t *handle);
