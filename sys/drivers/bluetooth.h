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
 * Lua RTOS, BT driver
 *
 */

#include "sdkconfig.h"

#if CONFIG_BT_ENABLED

#ifndef BT_H_
#define BT_H_

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "bluetooth_hci.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"

#include <stdint.h>

#include <sys/driver.h>

// BT modes
typedef enum {
	Idle = 0,
	BLE = 1,
	Classic = 2,
	Dual = 3
} bt_mode_t;

typedef enum {
	BTAdvUnknown = 0,
	BTAdvEddystoneUID = 1,
	BTAdvEddystoneURL = 2,
} bt_adv_frame_type_t;

typedef struct {
	int     rssi;
	uint8_t raw[31];
	uint8_t len;
	uint8_t flags;

	bt_adv_frame_type_t frame_type;
	union {
		struct {
			int8_t  tx_power;
			float   distance;
			uint8_t namespace[10];
			uint8_t instance[6];
		} eddystone_uid;

		struct {
			int8_t  tx_power;
			float   distance;
			uint8_t url[100];
		} eddystone_url;
	} data;
} bt_adv_frame_t;

typedef void (*bt_scan_callback_t)(int, bt_adv_frame_t *);

typedef uint8_t bt_adress_t[6];

// BT errors
#define BT_ERR_CANT_INIT			 	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  0)
#define BT_ERR_INVALID_MODE   		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  1)
#define BT_ERR_IS_NOT_SETUP   		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  2)
#define BT_ERR_NOT_ENOUGH_MEMORY	 	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  3)
#define BT_ERR_INVALID_ARGUMENT	 	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  4)
#define BT_ERR_INVALID_BEACON	 	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  5)
#define BT_ERR_CANT_START_SCAN	 	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  6)
#define BT_ERR_CANT_STOP_SCAN	 	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  7)
#define BT_ERR_CANT_START_ADV		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  8)
#define BT_ERR_CANT_STOP_ADV			 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) |  9)
#define BT_ERR_INVALID_TX_POWER		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 10)
#define BT_ERR_ADVDATA_TOO_LONG		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 11)
#define BT_ERR_CANT_REGISTER_GATTS	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 12)
#define BT_ERR_CANT_CREATE_SERVICE	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 13)
#define BT_ERR_CANT_ADD_CHAR		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 14)
#define BT_ERR_CANT_ADD_DESCR		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 15)
#define BT_ERR_CANT_START_SERVICE	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 16)
#define BT_ERR_CANT_REGISTER_GATTC	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 17)
#define BT_ERR_CANT_CONNECT		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 18)
#define BT_ERR_CANT_SEARCH_SERVICE	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 19)
#define BT_ERR_CANT_READ_CHAR		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 20)
#define BT_ERR_CANT_WRITE_CHAR		 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 21)
#define BT_ERR_CANT_REGISTER_NOTIFY	 (DRIVER_EXCEPTION_BASE(BT_DRIVER_ID) | 22)

driver_error_t *bt_setup(bt_mode_t mode);
driver_error_t *bt_adv_start(bte_advertise_params_t params, uint8_t *adv_data, uint16_t adv_data_len);
driver_error_t *bt_adv_stop();
driver_error_t *bt_scan_start(bt_scan_callback_t cb, int cb_id);
driver_error_t *bt_scan_stop();

extern const int bt_errors;
extern const int bt_error_map;

/* GATT server callback typedefs */
typedef void (*bt_gatts_read_cb_t)(uint16_t conn_id, uint32_t trans_id, uint16_t char_handle, uint16_t offset);
typedef void (*bt_gatts_write_cb_t)(uint16_t conn_id, uint32_t trans_id, uint16_t char_handle, uint8_t *value, uint16_t len, bool need_rsp);
typedef void (*bt_gatts_connect_cb_t)(uint16_t conn_id, uint8_t *addr);
typedef void (*bt_gatts_disconnect_cb_t)(uint16_t conn_id, uint8_t *addr);

/* GATT client callback typedefs */
typedef void (*bt_gattc_search_cb_t)(uint16_t conn_id, const char *uuid_str, uint16_t start_handle, uint16_t end_handle);
typedef void (*bt_gattc_read_cb_t)(uint16_t conn_id, uint16_t char_handle, uint8_t *value, uint16_t len, esp_gatt_status_t status);
typedef void (*bt_gattc_write_cb_t)(uint16_t conn_id, uint16_t char_handle, esp_gatt_status_t status);
typedef void (*bt_gattc_notify_cb_t)(uint16_t conn_id, uint16_t char_handle, uint8_t *value, uint16_t len);
typedef void (*bt_gattc_connect_cb_t)(uint16_t conn_id, uint8_t *addr);
typedef void (*bt_gattc_disconnect_cb_t)(uint16_t conn_id, uint8_t *addr);

/* GATT server public API */
driver_error_t *bt_gatts_register(uint16_t app_id);
driver_error_t *bt_gatts_create_service(uint16_t uuid16, uint16_t num_handles, uint16_t *service_handle);
driver_error_t *bt_gatts_add_characteristic(uint16_t service_handle, uint16_t uuid16, esp_gatt_char_prop_t props, esp_gatt_perm_t perms, uint8_t *value, uint16_t value_len, uint16_t *char_handle);
driver_error_t *bt_gatts_add_descriptor(uint16_t service_handle, uint16_t char_handle, uint16_t uuid16, esp_gatt_perm_t perms, uint16_t *descr_handle);
driver_error_t *bt_gatts_start_service(uint16_t service_handle);
driver_error_t *bt_gatts_stop_service(uint16_t service_handle);
driver_error_t *bt_gatts_send_response(uint16_t conn_id, uint32_t trans_id, uint16_t char_handle, uint8_t *value, uint16_t len);
driver_error_t *bt_gatts_send_notify(uint16_t conn_id, uint16_t char_handle, uint8_t *value, uint16_t len, bool need_confirm);
void bt_gatts_on_read(uint16_t char_handle, bt_gatts_read_cb_t cb);
void bt_gatts_on_write(uint16_t char_handle, bt_gatts_write_cb_t cb);
void bt_gatts_on_connect(bt_gatts_connect_cb_t cb);
void bt_gatts_on_disconnect(bt_gatts_disconnect_cb_t cb);

/* GATT client public API */
driver_error_t *bt_gattc_register(uint16_t app_id);
driver_error_t *bt_gattc_open(uint8_t *addr, esp_ble_addr_type_t addr_type, uint16_t *conn_id);
driver_error_t *bt_gattc_close(uint16_t conn_id);
driver_error_t *bt_gattc_search_service(uint16_t conn_id, esp_bt_uuid_t *filter_uuid, bt_gattc_search_cb_t cb);
driver_error_t *bt_gattc_get_char_by_uuid(uint16_t conn_id, uint16_t start_handle, uint16_t end_handle, esp_bt_uuid_t char_uuid, uint16_t *char_handle);
driver_error_t *bt_gattc_read_char(uint16_t conn_id, uint16_t char_handle, bt_gattc_read_cb_t cb);
driver_error_t *bt_gattc_write_char(uint16_t conn_id, uint16_t char_handle, uint8_t *value, uint16_t len, esp_gatt_write_type_t write_type, bt_gattc_write_cb_t cb);
driver_error_t *bt_gattc_register_for_notify(uint16_t conn_id, uint16_t char_handle);
void bt_gattc_on_connect(bt_gattc_connect_cb_t cb);
void bt_gattc_on_disconnect(bt_gattc_disconnect_cb_t cb);
void bt_gattc_on_notify(bt_gattc_notify_cb_t cb);

#include "bluetooth_eddystone.h"

#endif /* BT_H_ */

#endif
