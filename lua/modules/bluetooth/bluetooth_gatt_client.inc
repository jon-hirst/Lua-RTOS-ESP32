/*
 * Lua RTOS, GATT client Lua bindings
 * Included directly by lua/modules/bluetooth/bluetooth.c
 */

#if CONFIG_BT_ENABLED

#include <drivers/bluetooth.h>

/* ---- Lua callback refs ---- */
static int s_gattc_connect_ref    = LUA_NOREF;
static int s_gattc_disconnect_ref = LUA_NOREF;
static int s_gattc_notify_ref     = LUA_NOREF;
static int s_gattc_read_ref       = LUA_NOREF;

/* ---- C callback glue ---- */

static void gattc_connect_lua_cb(uint16_t conn_id, uint8_t *addr) {
    if (s_gattc_connect_ref == LUA_NOREF) return;

    char addr_str[13];
    val_to_hex_string(addr_str, (char *)addr, 6, 0);

    lua_State *L  = pvGetLuaState();
    lua_State *TL = lua_newthread(L);
    int fref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, s_gattc_connect_ref);
    lua_xmove(L, TL, 1);

    lua_createtable(TL, 0, 2);
    lua_pushstring(TL, "conn_id"); lua_pushinteger(TL, conn_id);  lua_settable(TL, -3);
    lua_pushstring(TL, "addr");    lua_pushstring(TL, addr_str);  lua_settable(TL, -3);

    lua_pcall(TL, 1, 0, 0);
    luaL_unref(TL, LUA_REGISTRYINDEX, fref);
}

static void gattc_disconnect_lua_cb(uint16_t conn_id, uint8_t *addr) {
    if (s_gattc_disconnect_ref == LUA_NOREF) return;

    char addr_str[13];
    val_to_hex_string(addr_str, (char *)addr, 6, 0);

    lua_State *L  = pvGetLuaState();
    lua_State *TL = lua_newthread(L);
    int fref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, s_gattc_disconnect_ref);
    lua_xmove(L, TL, 1);

    lua_createtable(TL, 0, 2);
    lua_pushstring(TL, "conn_id"); lua_pushinteger(TL, conn_id);  lua_settable(TL, -3);
    lua_pushstring(TL, "addr");    lua_pushstring(TL, addr_str);  lua_settable(TL, -3);

    lua_pcall(TL, 1, 0, 0);
    luaL_unref(TL, LUA_REGISTRYINDEX, fref);
}

static void gattc_notify_lua_cb(uint16_t conn_id, uint16_t char_handle,
                                 uint8_t *value, uint16_t len) {
    if (s_gattc_notify_ref == LUA_NOREF) return;

    char hex_buf[len * 2 + 1];
    val_to_hex_string(hex_buf, (char *)value, len, 0);

    lua_State *L  = pvGetLuaState();
    lua_State *TL = lua_newthread(L);
    int fref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, s_gattc_notify_ref);
    lua_xmove(L, TL, 1);

    lua_createtable(TL, 0, 3);
    lua_pushstring(TL, "conn_id"); lua_pushinteger(TL, conn_id);     lua_settable(TL, -3);
    lua_pushstring(TL, "handle");  lua_pushinteger(TL, char_handle); lua_settable(TL, -3);
    lua_pushstring(TL, "value");   lua_pushstring(TL, hex_buf);      lua_settable(TL, -3);

    lua_pcall(TL, 1, 0, 0);
    luaL_unref(TL, LUA_REGISTRYINDEX, fref);
}

static void gattc_read_lua_cb(uint16_t conn_id, uint16_t char_handle,
                               uint8_t *value, uint16_t len,
                               esp_gatt_status_t status) {
    if (s_gattc_read_ref == LUA_NOREF) return;
    int ref = s_gattc_read_ref;
    s_gattc_read_ref = LUA_NOREF;

    char hex_buf[len * 2 + 1];
    val_to_hex_string(hex_buf, (char *)value, len, 0);

    lua_State *L  = pvGetLuaState();
    lua_State *TL = lua_newthread(L);
    int fref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_xmove(L, TL, 1);
    luaL_unref(L, LUA_REGISTRYINDEX, ref);

    lua_createtable(TL, 0, 4);
    lua_pushstring(TL, "conn_id"); lua_pushinteger(TL, conn_id);     lua_settable(TL, -3);
    lua_pushstring(TL, "handle");  lua_pushinteger(TL, char_handle); lua_settable(TL, -3);
    lua_pushstring(TL, "value");   lua_pushstring(TL, hex_buf);      lua_settable(TL, -3);
    lua_pushstring(TL, "status");  lua_pushinteger(TL, status);      lua_settable(TL, -3);

    lua_pcall(TL, 1, 0, 0);
    luaL_unref(TL, LUA_REGISTRYINDEX, fref);
}

/* ---- search result accumulator ---- */
#define GATTC_LUA_MAX_SVCS 8
typedef struct { char uuid[40]; uint16_t start; uint16_t end; } gattc_lua_svc_t;
static gattc_lua_svc_t s_lua_svcs[GATTC_LUA_MAX_SVCS];
static int             s_lua_svc_cnt = 0;

static void gattc_search_lua_cb(uint16_t conn_id, const char *uuid_str,
                                 uint16_t start_handle, uint16_t end_handle) {
    if (s_lua_svc_cnt < GATTC_LUA_MAX_SVCS) {
        gattc_lua_svc_t *r = &s_lua_svcs[s_lua_svc_cnt++];
        strncpy(r->uuid, uuid_str, sizeof(r->uuid) - 1);
        r->uuid[sizeof(r->uuid)-1] = '\0';
        r->start = start_handle;
        r->end   = end_handle;
    }
}

/* ---- Lua-callable functions ---- */

static int lgattc_register(lua_State *L) {
    driver_error_t *error;
    uint16_t app_id = (uint16_t)luaL_checkinteger(L, 1);
    if ((error = bt_gattc_register(app_id)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgattc_connect(lua_State *L) {
    driver_error_t *error;
    const char *addr_hex = luaL_checkstring(L, 1);
    int addr_type = luaL_optinteger(L, 2, BLE_ADDR_TYPE_PUBLIC);

    if (!lcheck_hex_str(addr_hex))
        return luaL_exception_extended(L, BT_ERR_INVALID_ARGUMENT, "address must be hex string");

    uint8_t addr[6];
    hex_string_to_val((char *)addr_hex, (char *)addr, 6, 0);

    uint16_t conn_id = 0;
    if ((error = bt_gattc_open(addr, (esp_ble_addr_type_t)addr_type, &conn_id)))
        return luaL_driver_error(L, error);

    lua_pushinteger(L, conn_id);
    return 1;
}

static int lgattc_disconnect(lua_State *L) {
    driver_error_t *error;
    uint16_t conn_id = (uint16_t)luaL_checkinteger(L, 1);
    if ((error = bt_gattc_close(conn_id)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgattc_search_service(lua_State *L) {
    driver_error_t *error;
    uint16_t conn_id = (uint16_t)luaL_checkinteger(L, 1);

    esp_bt_uuid_t filter;
    esp_bt_uuid_t *pfilter = NULL;
    if (!lua_isnoneornil(L, 2)) {
        filter.len = ESP_UUID_LEN_16;
        filter.uuid.uuid16 = (uint16_t)luaL_checkinteger(L, 2);
        pfilter = &filter;
    }

    s_lua_svc_cnt = 0;
    if ((error = bt_gattc_search_service(conn_id, pfilter, gattc_search_lua_cb)))
        return luaL_driver_error(L, error);

    /* return array of tables */
    lua_createtable(L, s_lua_svc_cnt, 0);
    for (int i = 0; i < s_lua_svc_cnt; i++) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, "uuid");         lua_pushstring(L,  s_lua_svcs[i].uuid);  lua_settable(L, -3);
        lua_pushstring(L, "start_handle"); lua_pushinteger(L, s_lua_svcs[i].start); lua_settable(L, -3);
        lua_pushstring(L, "end_handle");   lua_pushinteger(L, s_lua_svcs[i].end);   lua_settable(L, -3);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int lgattc_get_char(lua_State *L) {
    driver_error_t *error;
    uint16_t conn_id   = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t start_hdl = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t end_hdl   = (uint16_t)luaL_checkinteger(L, 3);
    uint16_t uuid16    = (uint16_t)luaL_checkinteger(L, 4);

    esp_bt_uuid_t uuid;
    uuid.len = ESP_UUID_LEN_16;
    uuid.uuid.uuid16 = uuid16;

    uint16_t char_hdl = 0;
    if ((error = bt_gattc_get_char_by_uuid(conn_id, start_hdl, end_hdl, uuid, &char_hdl)))
        return luaL_driver_error(L, error);

    lua_pushinteger(L, char_hdl);
    return 1;
}

static int lgattc_read_char(lua_State *L) {
    driver_error_t *error;
    uint16_t conn_id  = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t char_hdl = (uint16_t)luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_pushvalue(L, 3);

    if (s_gattc_read_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, s_gattc_read_ref);
    s_gattc_read_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if ((error = bt_gattc_read_char(conn_id, char_hdl, gattc_read_lua_cb)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgattc_write_char(lua_State *L) {
    driver_error_t *error;
    uint16_t conn_id   = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t char_hdl  = (uint16_t)luaL_checkinteger(L, 2);
    const char *hex    = luaL_checkstring(L, 3);
    int write_type     = luaL_optinteger(L, 4, ESP_GATT_WRITE_TYPE_RSP);

    if (!lcheck_hex_str(hex))
        return luaL_exception_extended(L, BT_ERR_INVALID_ARGUMENT, "value must be hex string");

    uint16_t len = (uint16_t)(strlen(hex) / 2);
    uint8_t *val = (uint8_t *)alloca(len);
    hex_string_to_val((char *)hex, (char *)val, len, 0);

    if ((error = bt_gattc_write_char(conn_id, char_hdl, val, len,
                                      (esp_gatt_write_type_t)write_type, NULL)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgattc_register_notify(lua_State *L) {
    driver_error_t *error;
    uint16_t conn_id  = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t char_hdl = (uint16_t)luaL_checkinteger(L, 2);
    if ((error = bt_gattc_register_for_notify(conn_id, char_hdl)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgattc_on_connect(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (s_gattc_connect_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, s_gattc_connect_ref);
    s_gattc_connect_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    bt_gattc_on_connect(gattc_connect_lua_cb);
    return 0;
}

static int lgattc_on_disconnect(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (s_gattc_disconnect_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, s_gattc_disconnect_ref);
    s_gattc_disconnect_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    bt_gattc_on_disconnect(gattc_disconnect_lua_cb);
    return 0;
}

static int lgattc_on_notify(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (s_gattc_notify_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, s_gattc_notify_ref);
    s_gattc_notify_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    bt_gattc_on_notify(gattc_notify_lua_cb);
    return 0;
}

static const LUA_REG_TYPE lbt_gattc_map[] = {
    { LSTRKEY("register"),        LFUNCVAL(lgattc_register)        },
    { LSTRKEY("connect"),         LFUNCVAL(lgattc_connect)         },
    { LSTRKEY("disconnect"),      LFUNCVAL(lgattc_disconnect)      },
    { LSTRKEY("search_service"),  LFUNCVAL(lgattc_search_service)  },
    { LSTRKEY("get_char"),        LFUNCVAL(lgattc_get_char)        },
    { LSTRKEY("read_char"),       LFUNCVAL(lgattc_read_char)       },
    { LSTRKEY("write_char"),      LFUNCVAL(lgattc_write_char)      },
    { LSTRKEY("register_notify"), LFUNCVAL(lgattc_register_notify) },
    { LSTRKEY("on_connect"),      LFUNCVAL(lgattc_on_connect)      },
    { LSTRKEY("on_disconnect"),   LFUNCVAL(lgattc_on_disconnect)   },
    { LSTRKEY("on_notify"),       LFUNCVAL(lgattc_on_notify)       },
    { LNILKEY, LNILVAL }
};

#endif /* CONFIG_BT_ENABLED */
