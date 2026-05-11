/*
 * Lua RTOS, BLE GATT server driver
 */

#include "sdkconfig.h"

#if CONFIG_BT_ENABLED

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bluetooth.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_gatt_defs.h"

#include <string.h>
#include <sys/driver.h>
#include <sys/syslog.h>

/* ---- limits ---- */
#define GATTS_MAX_SERVICES     8
#define GATTS_MAX_CHARS        16
#define GATTS_MAX_VALUE_LEN    64
#define GATTS_QUEUE_LEN        8

/* ---- event bits ---- */
#define evGATTS_REG_OK         (1 << 0)
#define evGATTS_REG_ERR        (1 << 1)
#define evGATTS_CREATE_OK      (1 << 2)
#define evGATTS_CREATE_ERR     (1 << 3)
#define evGATTS_CHAR_OK        (1 << 4)
#define evGATTS_CHAR_ERR       (1 << 5)
#define evGATTS_DESCR_OK       (1 << 6)
#define evGATTS_DESCR_ERR      (1 << 7)
#define evGATTS_START_OK       (1 << 8)
#define evGATTS_START_ERR      (1 << 9)
#define evGATTS_STOP_OK        (1 << 10)
#define evGATTS_STOP_ERR       (1 << 11)

/* ---- callback queue item types ---- */
#define GATTS_EV_READ          1
#define GATTS_EV_WRITE         2
#define GATTS_EV_CONNECT       3
#define GATTS_EV_DISCONNECT    4

typedef struct {
    uint8_t  type;
    uint16_t conn_id;
    uint32_t trans_id;
    uint16_t handle;
    uint16_t offset;
    uint16_t len;
    bool     need_rsp;
    uint8_t  value[GATTS_MAX_VALUE_LEN];
    uint8_t  addr[6];
} gatts_event_t;

/* ---- characteristic slot ---- */
typedef struct {
    uint16_t           uuid16;
    uint16_t           char_handle;
    uint16_t           descr_handle;
    uint8_t            value[GATTS_MAX_VALUE_LEN];
    uint16_t           value_len;
    bt_gatts_read_cb_t  read_cb;
    bt_gatts_write_cb_t write_cb;
} gatts_char_t;

/* ---- service slot ---- */
typedef struct {
    uint16_t     uuid16;
    uint16_t     service_handle;
    uint8_t      num_chars;
    gatts_char_t chars[GATTS_MAX_CHARS];
} gatts_svc_t;

/* ---- module state ---- */
static esp_gatt_if_t    s_gatts_if   = ESP_GATT_IF_NONE;
static EventGroupHandle_t s_event    = NULL;
static QueueHandle_t    s_queue      = NULL;
static TaskHandle_t     s_task       = NULL;

static gatts_svc_t s_svcs[GATTS_MAX_SERVICES];
static uint8_t     s_num_svcs = 0;

/* pending slots filled during event callbacks */
static int      s_pending_svc_idx  = -1;
static int      s_pending_char_idx = -1;

static bt_gatts_connect_cb_t    s_connect_cb    = NULL;
static bt_gatts_disconnect_cb_t s_disconnect_cb = NULL;

/* current connection */
static uint16_t s_conn_id = 0xFFFF;

/* ---- helpers ---- */
static gatts_char_t *find_char_by_handle(uint16_t handle) {
    for (int i = 0; i < s_num_svcs; i++) {
        for (int j = 0; j < s_svcs[i].num_chars; j++) {
            if (s_svcs[i].chars[j].char_handle == handle)
                return &s_svcs[i].chars[j];
        }
    }
    return NULL;
}

/* ---- GATTS callback ---- */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {
    gatts_event_t ev;
    memset(&ev, 0, sizeof(ev));

    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                s_gatts_if = gatts_if;
                xEventGroupSetBits(s_event, evGATTS_REG_OK);
            } else {
                xEventGroupSetBits(s_event, evGATTS_REG_ERR);
            }
            break;

        case ESP_GATTS_CREATE_EVT:
            if (param->create.status == ESP_GATT_OK && s_pending_svc_idx >= 0) {
                s_svcs[s_pending_svc_idx].service_handle = param->create.service_handle;
                xEventGroupSetBits(s_event, evGATTS_CREATE_OK);
            } else {
                xEventGroupSetBits(s_event, evGATTS_CREATE_ERR);
            }
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            if (param->add_char.status == ESP_GATT_OK && s_pending_svc_idx >= 0 && s_pending_char_idx >= 0) {
                s_svcs[s_pending_svc_idx].chars[s_pending_char_idx].char_handle = param->add_char.attr_handle;
                xEventGroupSetBits(s_event, evGATTS_CHAR_OK);
            } else {
                xEventGroupSetBits(s_event, evGATTS_CHAR_ERR);
            }
            break;

        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            if (param->add_char_descr.status == ESP_GATT_OK && s_pending_svc_idx >= 0 && s_pending_char_idx >= 0) {
                s_svcs[s_pending_svc_idx].chars[s_pending_char_idx].descr_handle = param->add_char_descr.attr_handle;
                xEventGroupSetBits(s_event, evGATTS_DESCR_OK);
            } else {
                xEventGroupSetBits(s_event, evGATTS_DESCR_ERR);
            }
            break;

        case ESP_GATTS_START_EVT:
            xEventGroupSetBits(s_event, param->start.status == ESP_GATT_OK ? evGATTS_START_OK : evGATTS_START_ERR);
            break;

        case ESP_GATTS_STOP_EVT:
            xEventGroupSetBits(s_event, param->stop.status == ESP_GATT_OK ? evGATTS_STOP_OK : evGATTS_STOP_ERR);
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_conn_id = param->connect.conn_id;
            if (s_connect_cb || s_disconnect_cb) {
                ev.type    = GATTS_EV_CONNECT;
                ev.conn_id = param->connect.conn_id;
                memcpy(ev.addr, param->connect.remote_bda, 6);
                xQueueSend(s_queue, &ev, 0);
            }
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            if (s_connect_cb || s_disconnect_cb) {
                ev.type    = GATTS_EV_DISCONNECT;
                ev.conn_id = param->disconnect.conn_id;
                memcpy(ev.addr, param->disconnect.remote_bda, 6);
                xQueueSend(s_queue, &ev, 0);
            }
            s_conn_id = 0xFFFF;
            break;

        case ESP_GATTS_READ_EVT: {
            gatts_char_t *ch = find_char_by_handle(param->read.handle);
            if (ch && ch->read_cb) {
                ev.type     = GATTS_EV_READ;
                ev.conn_id  = param->read.conn_id;
                ev.trans_id = param->read.trans_id;
                ev.handle   = param->read.handle;
                ev.offset   = param->read.offset;
                xQueueSend(s_queue, &ev, 0);
            } else {
                /* auto-respond with stored value */
                esp_gatt_rsp_t rsp;
                memset(&rsp, 0, sizeof(rsp));
                rsp.attr_value.handle = param->read.handle;
                if (ch) {
                    rsp.attr_value.len = ch->value_len;
                    memcpy(rsp.attr_value.value, ch->value, ch->value_len);
                }
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                            param->read.trans_id, ESP_GATT_OK, &rsp);
            }
            break;
        }

        case ESP_GATTS_WRITE_EVT: {
            gatts_char_t *ch = find_char_by_handle(param->write.handle);
            if (ch) {
                /* update stored value */
                uint16_t copy_len = param->write.len < GATTS_MAX_VALUE_LEN ? param->write.len : GATTS_MAX_VALUE_LEN;
                memcpy(ch->value, param->write.value, copy_len);
                ch->value_len = copy_len;
            }
            if (ch && ch->write_cb) {
                ev.type     = GATTS_EV_WRITE;
                ev.conn_id  = param->write.conn_id;
                ev.trans_id = param->write.trans_id;
                ev.handle   = param->write.handle;
                ev.need_rsp = param->write.need_rsp;
                ev.len      = param->write.len < GATTS_MAX_VALUE_LEN ? param->write.len : GATTS_MAX_VALUE_LEN;
                memcpy(ev.value, param->write.value, ev.len);
                xQueueSend(s_queue, &ev, 0);
            } else if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, ESP_GATT_OK, NULL);
            }
            break;
        }

        default:
            break;
    }
}

/* ---- Lua callback task ---- */
static void gatts_task(void *arg) {
    gatts_event_t ev;
    for (;;) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) == pdTRUE) {
            gatts_char_t *ch = NULL;
            if (ev.type == GATTS_EV_READ || ev.type == GATTS_EV_WRITE)
                ch = find_char_by_handle(ev.handle);

            switch (ev.type) {
                case GATTS_EV_READ:
                    if (ch && ch->read_cb)
                        ch->read_cb(ev.conn_id, ev.trans_id, ev.handle, ev.offset);
                    break;
                case GATTS_EV_WRITE:
                    if (ch && ch->write_cb)
                        ch->write_cb(ev.conn_id, ev.trans_id, ev.handle, ev.value, ev.len, ev.need_rsp);
                    break;
                case GATTS_EV_CONNECT:
                    if (s_connect_cb)
                        s_connect_cb(ev.conn_id, ev.addr);
                    break;
                case GATTS_EV_DISCONNECT:
                    if (s_disconnect_cb)
                        s_disconnect_cb(ev.conn_id, ev.addr);
                    break;
                default:
                    break;
            }
        }
    }
}

/* ---- public API ---- */

driver_error_t *bt_gatts_register(uint16_t app_id) {
    if (!s_event) {
        s_event = xEventGroupCreate();
        if (!s_event)
            return driver_error(BT_DRIVER, BT_ERR_NOT_ENOUGH_MEMORY, NULL);
    }
    if (!s_queue) {
        s_queue = xQueueCreate(GATTS_QUEUE_LEN, sizeof(gatts_event_t));
        if (!s_queue)
            return driver_error(BT_DRIVER, BT_ERR_NOT_ENOUGH_MEMORY, NULL);
    }
    if (!s_task) {
        if (xTaskCreatePinnedToCore(gatts_task, "gatts", CONFIG_LUA_RTOS_LUA_THREAD_STACK_SIZE,
                                    NULL, CONFIG_LUA_RTOS_LUA_THREAD_PRIORITY, &s_task,
                                    xPortGetCoreID()) != pdPASS)
            return driver_error(BT_DRIVER, BT_ERR_NOT_ENOUGH_MEMORY, NULL);
    }

    esp_ble_gatts_register_callback(gatts_event_handler);
    if (esp_ble_gatts_app_register(app_id) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_REGISTER_GATTS, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTS_REG_OK | evGATTS_REG_ERR, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & evGATTS_REG_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_REGISTER_GATTS, NULL);
    return NULL;
}

driver_error_t *bt_gatts_create_service(uint16_t uuid16, uint16_t num_handles, uint16_t *service_handle) {
    if (s_num_svcs >= GATTS_MAX_SERVICES)
        return driver_error(BT_DRIVER, BT_ERR_NOT_ENOUGH_MEMORY, NULL);

    int idx = s_num_svcs;
    memset(&s_svcs[idx], 0, sizeof(gatts_svc_t));
    s_svcs[idx].uuid16 = uuid16;
    s_pending_svc_idx  = idx;
    s_pending_char_idx = -1;

    esp_gatt_srvc_id_t srvc_id;
    memset(&srvc_id, 0, sizeof(srvc_id));
    srvc_id.is_primary       = true;
    srvc_id.id.inst_id       = 0;
    srvc_id.id.uuid.len      = ESP_UUID_LEN_16;
    srvc_id.id.uuid.uuid.uuid16 = uuid16;

    if (esp_ble_gatts_create_service(s_gatts_if, &srvc_id, num_handles) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_CREATE_SERVICE, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTS_CREATE_OK | evGATTS_CREATE_ERR, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & evGATTS_CREATE_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_CREATE_SERVICE, NULL);

    *service_handle = s_svcs[idx].service_handle;
    s_num_svcs++;
    return NULL;
}

driver_error_t *bt_gatts_add_characteristic(uint16_t service_handle, uint16_t uuid16,
                                             esp_gatt_char_prop_t props, esp_gatt_perm_t perms,
                                             uint8_t *value, uint16_t value_len,
                                             uint16_t *char_handle) {
    /* find service slot */
    int svc_idx = -1;
    for (int i = 0; i < s_num_svcs; i++) {
        if (s_svcs[i].service_handle == service_handle) { svc_idx = i; break; }
    }
    if (svc_idx < 0 || s_svcs[svc_idx].num_chars >= GATTS_MAX_CHARS)
        return driver_error(BT_DRIVER, BT_ERR_CANT_ADD_CHAR, NULL);

    int char_idx = s_svcs[svc_idx].num_chars;
    memset(&s_svcs[svc_idx].chars[char_idx], 0, sizeof(gatts_char_t));
    s_svcs[svc_idx].chars[char_idx].uuid16 = uuid16;
    if (value && value_len) {
        uint16_t copy = value_len < GATTS_MAX_VALUE_LEN ? value_len : GATTS_MAX_VALUE_LEN;
        memcpy(s_svcs[svc_idx].chars[char_idx].value, value, copy);
        s_svcs[svc_idx].chars[char_idx].value_len = copy;
    }

    s_pending_svc_idx  = svc_idx;
    s_pending_char_idx = char_idx;

    esp_bt_uuid_t uuid;
    uuid.len = ESP_UUID_LEN_16;
    uuid.uuid.uuid16 = uuid16;

    esp_attr_value_t attr_val;
    memset(&attr_val, 0, sizeof(attr_val));
    attr_val.attr_max_len = GATTS_MAX_VALUE_LEN;
    attr_val.attr_len     = s_svcs[svc_idx].chars[char_idx].value_len;
    attr_val.attr_value   = s_svcs[svc_idx].chars[char_idx].value;

    if (esp_ble_gatts_add_char(service_handle, &uuid, perms, props, &attr_val, NULL) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_ADD_CHAR, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTS_CHAR_OK | evGATTS_CHAR_ERR, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & evGATTS_CHAR_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_ADD_CHAR, NULL);

    *char_handle = s_svcs[svc_idx].chars[char_idx].char_handle;
    s_svcs[svc_idx].num_chars++;
    return NULL;
}

driver_error_t *bt_gatts_add_descriptor(uint16_t service_handle, uint16_t char_handle,
                                         uint16_t uuid16, esp_gatt_perm_t perms,
                                         uint16_t *descr_handle) {
    int svc_idx  = -1;
    int char_idx = -1;
    for (int i = 0; i < s_num_svcs; i++) {
        if (s_svcs[i].service_handle == service_handle) {
            for (int j = 0; j < s_svcs[i].num_chars; j++) {
                if (s_svcs[i].chars[j].char_handle == char_handle) {
                    svc_idx = i; char_idx = j; break;
                }
            }
            break;
        }
    }
    if (svc_idx < 0)
        return driver_error(BT_DRIVER, BT_ERR_CANT_ADD_DESCR, NULL);

    s_pending_svc_idx  = svc_idx;
    s_pending_char_idx = char_idx;

    esp_bt_uuid_t uuid;
    uuid.len = ESP_UUID_LEN_16;
    uuid.uuid.uuid16 = uuid16;

    if (esp_ble_gatts_add_char_descr(service_handle, &uuid, perms, NULL, NULL) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_ADD_DESCR, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTS_DESCR_OK | evGATTS_DESCR_ERR, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & evGATTS_DESCR_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_ADD_DESCR, NULL);

    *descr_handle = s_svcs[svc_idx].chars[char_idx].descr_handle;
    return NULL;
}

driver_error_t *bt_gatts_start_service(uint16_t service_handle) {
    if (esp_ble_gatts_start_service(service_handle) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_START_SERVICE, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTS_START_OK | evGATTS_START_ERR, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & evGATTS_START_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_START_SERVICE, NULL);
    return NULL;
}

driver_error_t *bt_gatts_stop_service(uint16_t service_handle) {
    if (esp_ble_gatts_stop_service(service_handle) != ESP_OK)
        return driver_error(BT_DRIVER, BT_ERR_CANT_START_SERVICE, NULL);

    EventBits_t bits = xEventGroupWaitBits(s_event,
                        evGATTS_STOP_OK | evGATTS_STOP_ERR, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & evGATTS_STOP_ERR)
        return driver_error(BT_DRIVER, BT_ERR_CANT_START_SERVICE, NULL);
    return NULL;
}

driver_error_t *bt_gatts_send_response(uint16_t conn_id, uint32_t trans_id,
                                        uint16_t char_handle,
                                        uint8_t *value, uint16_t len) {
    esp_gatt_rsp_t rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.attr_value.handle = char_handle;
    rsp.attr_value.len    = len;
    if (value && len)
        memcpy(rsp.attr_value.value, value, len);

    esp_ble_gatts_send_response(s_gatts_if, conn_id, trans_id, ESP_GATT_OK, &rsp);
    return NULL;
}

driver_error_t *bt_gatts_send_notify(uint16_t conn_id, uint16_t char_handle,
                                      uint8_t *value, uint16_t len, bool need_confirm) {
    esp_ble_gatts_send_indicate(s_gatts_if, conn_id, char_handle, len, value, need_confirm);
    return NULL;
}

void bt_gatts_on_read(uint16_t char_handle, bt_gatts_read_cb_t cb) {
    gatts_char_t *ch = find_char_by_handle(char_handle);
    if (ch) ch->read_cb = cb;
}

void bt_gatts_on_write(uint16_t char_handle, bt_gatts_write_cb_t cb) {
    gatts_char_t *ch = find_char_by_handle(char_handle);
    if (ch) ch->write_cb = cb;
}

void bt_gatts_on_connect(bt_gatts_connect_cb_t cb) {
    s_connect_cb = cb;
}

void bt_gatts_on_disconnect(bt_gatts_disconnect_cb_t cb) {
    s_disconnect_cb = cb;
}

#endif /* CONFIG_BT_ENABLED */
