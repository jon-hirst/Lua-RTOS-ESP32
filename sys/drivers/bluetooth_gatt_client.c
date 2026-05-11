/*
 * Lua RTOS, BLE GATT client driver
 */

#include "sdkconfig.h"

#if CONFIG_BT_ENABLED

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bluetooth.h"
#include "esp_gattc_api.h"
#include "esp_bt_defs.h"
#include "esp_gatt_defs.h"

#include <string.h>
#include <stdio.h>
#include <sys/driver.h>
#include <sys/syslog.h>

/* ---- limits ---- */
#define GATTC_MAX_CONNS     4
#define GATTC_MAX_SVCS      8
#define GATTC_QUEUE_LEN     8
#define GATTC_MAX_VALUE_LEN 64
#define GATTC_UUID_STR_LEN  40

/* ---- event bits ---- */
#define evGATTC_REG_OK         (1 << 0)
#define evGATTC_REG_ERR        (1 << 1)
#define evGATTC_OPEN_OK        (1 << 2)
#define evGATTC_OPEN_ERR       (1 << 3)
#define evGATTC_SEARCH_DONE    (1 << 4)
#define evGATTC_SEARCH_ERR     (1 << 5)
#define evGATTC_NOTIFY_REG_OK  (1 << 6)
#define evGATTC_NOTIFY_REG_ERR (1 << 7)

/* ---- callback queue item types ---- */
#define GATTC_EV_CONNECT      1
#define GATTC_EV_DISCONNECT   2
#define GATTC_EV_READ         3
#define GATTC_EV_WRITE        4
#define GATTC_EV_NOTIFY       5

typedef struct {
    uint8_t            type;
    uint16_t           conn_id;
    uint16_t           handle;
    uint16_t           len;
    uint8_t            value[GATTC_MAX_VALUE_LEN];
    uint8_t            addr[6];
    esp_gatt_status_t  status;
    bool               is_notify;
} gattc_event_t;

/* ---- discovered service record ---- */
typedef struct {
    char     uuid_str[GATTC_UUID_STR_LEN];
    uint16_t start_handle;
    uint16_t end_handle;
} gattc_svc_rec_t;

/* ---- connection slot ---- */
typedef struct {
    bool     active;
    uint16_t conn_id;
    uint8_t  addr[6];
} gattc_conn_t;

/* ---- module state ---- */
static esp_gatt_if_t      s_gattc_if  = ESP_GATT_IF_NONE;
static EventGroupHandle_t s_event     = NULL;
static QueueHandle_t      s_queue     = NULL;
static TaskHandle_t       s_task      = NULL;

static gattc_conn_t s_conns[GATTC_MAX_CONNS];

/* pending search results */
static gattc_svc_rec_t   s_search_recs[GATTC_MAX_SVCS];
static int               s_search_cnt = 0;
static bt_gattc_search_cb_t s_search_cb  = NULL;

/* per-operation callbacks (single-shot, cleared after use) */
static bt_gattc_read_cb_t  s_read_cb  = NULL;
static bt_gattc_write_cb_t s_write_cb = NULL;

/* persistent callbacks */
static bt_gattc_connect_cb_t    s_connect_cb    = NULL;
static bt_gattc_disconnect_cb_t s_disconnect_cb = NULL;
static bt_gattc_notify_cb_t     s_notify_cb     = NULL;

/* ---- uuid to string ---- */
static void uuid_to_str(esp_bt_uuid_t *uuid, char *buf, size_t buflen) {
    if (uuid->len == ESP_UUID_LEN_16) {
        snprintf(buf, buflen, "%04X", uuid->uuid.uuid16);
    } else if (uuid->len == ESP_UUID_LEN_32) {
        snprintf(buf, buflen, "%08X", (unsigned)uuid->uuid.uuid32);
    } else {
        const uint8_t *u = uuid->uuid.uuid128;
        snprintf(buf, buflen,
            "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            u[15],u[14],u[13],u[12],u[11],u[10],u[9],u[8],
            u[7],u[6],u[5],u[4],u[3],u[2],u[1],u[0]);
    }
}

/* ---- GATTC callback ---- */
static void gattc_event_handler(esp_gattc_cb_event_t event,
                                esp_gatt_if_t gattc_if,
                                esp_ble_gattc_cb_param_t *param) {
    gattc_event_t ev;
    memset(&ev, 0, sizeof(ev));

    switch (event) {
        case ESP_GATTC_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                s_gattc_if = gattc_if;
                xEventGroupSetBits(s_event, evGATTC_REG_OK);
            } else {
                xEventGroupSetBits(s_event, evGATTC_REG_ERR);
            }
            break;

        case ESP_GATTC_CONNECT_EVT:
            /* physical link up — request MTU */
            esp_ble_gattc_send_mtu_req(gattc_if, param->connect.conn_id);
            break;

        case ESP_GATTC_CFG_MTU_EVT:
            /* MTU negotiated — virtual connection open */
            xEventGroupSetBits(s_event, evGATTC_OPEN_OK);
            break;

        case ESP_GATTC_OPEN_EVT:
            if (param->open.status != ESP_GATT_OK) {
                xEventGroupSetBits(s_event, evGATTC_OPEN_ERR);
            } else {
                for (int i = 0; i < GATTC_MAX_CONNS; i++) {
                    if (!s_conns[i].active) {
                        s_conns[i].active  = true;
                        s_conns[i].conn_id = param->open.conn_id;
                        memcpy(s_conns[i].addr, param->open.remote_bda, 6);
                        break;
                    }
                }
                /* OPEN_EVT fires before CFG_MTU; signal is sent from CFG_MTU */
            }
            break;

        case ESP_GATTC_CLOSE_EVT:
            for (int i = 0; i < GATTC_MAX_CONNS; i++) {
                if (s_conns[i].active && s_conns[i].conn_id == param->close.conn_id) {
                    ev.type    = GATTC_EV_DISCONNECT;
                    ev.conn_id = param->close.conn_id;
                    memcpy(ev.addr, s_conns[i].addr, 6);
                    s_conns[i].active = false;
                    xQueueSend(s_queue, &ev, 0);
                    break;
                }
            }
            break;

        case ESP_GATTC_SEARCH_RES_EVT:
            if (s_search_cnt < GATTC_MAX_SVCS) {
                gattc_svc_rec_t *r = &s_search_recs[s_search_cnt++];
                uuid_to_str(&param->search_res.srvc_id.uuid, r->uuid_str, sizeof(r->uuid_str));
                r->start_handle = param->search_res.start_handle;
                r->end_handle   = param->search_res.end_handle;
            }
            break;

        case ESP_GATTC_SEARCH_CMPL_EVT:
            if (param->search_cmpl.status == ESP_GATT_OK) {
                xEventGroupSetBits(s_event, evGATTC_SEARCH_DONE);
            } else {
                xEventGroupSetBits(s_event, evGATTC_SEARCH_ERR);
            }
            break;

        case ESP_GATTC_READ_CHAR_EVT: {
            bt_gattc_read_cb_t cb = s_read_cb;
            s_read_cb = NULL;
            if (cb) {
                uint16_t copy = param->read.value_len < GATTC_MAX_VALUE_LEN ?
                                param->read.value_len : GATTC_MAX_VALUE_LEN;
                cb(param->read.conn_id, param->read.handle,
                   param->read.value, copy, param->read.status);
            }
            break;
        }

        case ESP_GATTC_WRITE_CHAR_EVT: {
            bt_gattc_write_cb_t cb = s_write_cb;
            s_write_cb = NULL;
            if (cb)
                cb(param->write.conn_id, param->write.handle, param->write.status);
            break;
        }

        case ESP_GATTC_NOTIFY_EVT: {
            if (s_notify_cb) {
                ev.type    = GATTC_EV_NOTIFY;
                ev.conn_id = param->notify.conn_id;
                ev.handle  = param->notify.handle;
                ev.len     = param->notify.value_len < GATTC_MAX_VALUE_LEN ?
                             param->notify.value_len : GATTC_MAX_VALUE_LEN;
                memcpy(ev.value, param->notify.value, ev.len);
                ev.is_notify = param->notify.is_notify;
                xQueueSend(s_queue, &ev, 0);
            }
            break;
        }

        case ESP_GATTC_REG_FOR_NOTIFY_EVT:
            xEventGroupSetBits(s_event,
                param->reg_for_notify.status == ESP_GATT_OK ?
                evGATTC_NOTIFY_REG_OK : evGATTC_NOTIFY_REG_ERR);
            break;

        case ESP_GATTC_DISCONNECT_EVT:
            /* physical link down — handled via CLOSE_EVT above */
            break;

        default:
            break;
    }
}

/* ---- Lua callback task ---- */
static void gattc_task(void *arg) {
    gattc_event_t ev;
    for (;;) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) == pdTRUE) {
            switch (ev.type) {
                case GATTC_EV_CONNECT:
                    if (s_connect_cb)
                        s_connect_cb(ev.conn_id, ev.addr);
                    break;
                case GATTC_EV_DISCONNECT:
                    if (s_disconnect_cb)
                        s_disconnect_cb(ev.conn_id, ev.addr);
                    break;
                case GATTC_EV_NOTIFY:
                    if (s_notify_cb)
                        s_notify_cb(ev.conn_id, ev.handle, ev.value, ev.len);
                    break;
                default:
                    break;
            }
        }
    }
}

/* ---- public API ---- */

driver_error_t *bt_gattc_register(uint16_t app_id) {
    if (!s_event) {
        s_event = xEventGroupCreate();
        if (!s_event)
            return driver_error(BT_DRIVER, BT_ERR_NOT_ENOUGH_MEMORY, NULL);
    }
    if (!s_queue) {
        s_queue = xQueueCreate(GATTC_QUEUE_LEN, sizeof(gattc_event_t));
        if (!s_queue)
            return driver_error(BT_DRIVER, BT_ERR_NOT_ENOUGH_MEMORY, NULL);
    }
    if (!s_task) {
        if (xTaskCreatePinnedToCore(gattc_task, "gattc", CONFIG_LUA_RTOS_LUA_THREAD_STACK_SIZE,
                                    NULL, CONFIG_LUA_RTOS_LUA_THREAD_PRIORITY, &s_task,
                                    xPortGetCoreID()) != pdPASS)
            return driver_error(BT_DRIVER, BT_ERR_NOT_ENOUGH_MEMORY, NULL);
    }

    esp_ble_gattc_register_callback(gattc_event_handler);
    if (esp_ble_gattc_app_register(app_id) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_REGISTER_GATTC, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTC_REG_OK | evGATTC_REG_ERR, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & evGATTC_REG_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_REGISTER_GATTC, NULL);
    return NULL;
}

driver_error_t *bt_gattc_open(uint8_t *addr, esp_ble_addr_type_t addr_type, uint16_t *conn_id) {
    if (esp_ble_gattc_open(s_gattc_if, addr, addr_type, true) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_CONNECT, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTC_OPEN_OK | evGATTC_OPEN_ERR, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & evGATTC_OPEN_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_CONNECT, NULL);

    /* find the connection slot populated by the OPEN_EVT handler */
    for (int i = 0; i < GATTC_MAX_CONNS; i++) {
        if (s_conns[i].active && memcmp(s_conns[i].addr, addr, 6) == 0) {
            *conn_id = s_conns[i].conn_id;
            return NULL;
        }
    }
    return driver_error(BT_DRIVER, BT_ERR_CANT_CONNECT, NULL);
}

driver_error_t *bt_gattc_close(uint16_t conn_id) {
    esp_ble_gattc_close(s_gattc_if, conn_id);
    return NULL;
}

driver_error_t *bt_gattc_search_service(uint16_t conn_id, esp_bt_uuid_t *filter_uuid,
                                         bt_gattc_search_cb_t cb) {
    s_search_cnt = 0;
    s_search_cb  = cb;

    if (esp_ble_gattc_search_service(s_gattc_if, conn_id, filter_uuid) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_SEARCH_SERVICE, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTC_SEARCH_DONE | evGATTC_SEARCH_ERR, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & evGATTC_SEARCH_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_SEARCH_SERVICE, NULL);

    /* deliver results synchronously */
    if (cb) {
        for (int i = 0; i < s_search_cnt; i++)
            cb(conn_id, s_search_recs[i].uuid_str,
               s_search_recs[i].start_handle, s_search_recs[i].end_handle);
    }
    return NULL;
}

driver_error_t *bt_gattc_get_char_by_uuid(uint16_t conn_id, uint16_t start_handle,
                                           uint16_t end_handle, esp_bt_uuid_t char_uuid,
                                           uint16_t *char_handle) {
    esp_gattc_char_elem_t result;
    uint16_t count = 1;
    esp_gatt_status_t status = esp_ble_gattc_get_char_by_uuid(s_gattc_if, conn_id,
                                    start_handle, end_handle, char_uuid, &result, &count);
    if (status != ESP_GATT_OK || count == 0)
        return driver_error(BT_DRIVER, BT_ERR_CANT_READ_CHAR, NULL);
    *char_handle = result.char_handle;
    return NULL;
}

driver_error_t *bt_gattc_read_char(uint16_t conn_id, uint16_t char_handle,
                                    bt_gattc_read_cb_t cb) {
    s_read_cb = cb;
    if (esp_ble_gattc_read_char(s_gattc_if, conn_id, char_handle,
                                 ESP_GATT_AUTH_REQ_NONE) != ESP_OK) {
        s_read_cb = NULL;
        return driver_error(BT_DRIVER, BT_ERR_CANT_READ_CHAR, NULL);
    }
    return NULL;
}

driver_error_t *bt_gattc_write_char(uint16_t conn_id, uint16_t char_handle,
                                     uint8_t *value, uint16_t len,
                                     esp_gatt_write_type_t write_type,
                                     bt_gattc_write_cb_t cb) {
    s_write_cb = cb;
    if (esp_ble_gattc_write_char(s_gattc_if, conn_id, char_handle, len, value,
                                  write_type, ESP_GATT_AUTH_REQ_NONE) != ESP_OK) {
        s_write_cb = NULL;
        return driver_error(BT_DRIVER, BT_ERR_CANT_WRITE_CHAR, NULL);
    }
    return NULL;
}

driver_error_t *bt_gattc_register_for_notify(uint16_t conn_id, uint16_t char_handle) {
    /* esp_ble_gattc_register_for_notify needs the remote address */
    uint8_t *addr = NULL;
    for (int i = 0; i < GATTC_MAX_CONNS; i++) {
        if (s_conns[i].active && s_conns[i].conn_id == conn_id) {
            addr = s_conns[i].addr;
            break;
        }
    }
    if (!addr)
        return driver_error(BT_DRIVER, BT_ERR_CANT_REGISTER_NOTIFY, NULL);

    if (esp_ble_gattc_register_for_notify(s_gattc_if, addr, char_handle) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_REGISTER_NOTIFY, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTC_NOTIFY_REG_OK | evGATTC_NOTIFY_REG_ERR, pdTRUE, pdFALSE,
                        portMAX_DELAY);
    if (bits & evGATTC_NOTIFY_REG_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_REGISTER_NOTIFY, NULL);
    return NULL;
}

void bt_gattc_on_connect(bt_gattc_connect_cb_t cb) {
    s_connect_cb = cb;
}

void bt_gattc_on_disconnect(bt_gattc_disconnect_cb_t cb) {
    s_disconnect_cb = cb;
}

void bt_gattc_on_notify(bt_gattc_notify_cb_t cb) {
    s_notify_cb = cb;
}

#endif /* CONFIG_BT_ENABLED */
