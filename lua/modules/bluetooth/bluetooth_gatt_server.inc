/*
 * Lua RTOS, GATT server Lua bindings
 * Included directly by lua/modules/bluetooth/bluetooth.c
 */

#if CONFIG_BT_ENABLED

#include <drivers/bluetooth.h>

/* ---- per-characteristic Lua callback refs ---- */
#define GATTS_LUA_MAX_CHARS 32

typedef struct {
    uint16_t handle;
    int      read_ref;
    int      write_ref;
} gatts_lua_char_t;

static gatts_lua_char_t s_lua_chars[GATTS_LUA_MAX_CHARS];
static int              s_lua_char_cnt      = 0;
static int              s_gatts_connect_ref    = LUA_NOREF;
static int              s_gatts_disconnect_ref = LUA_NOREF;

static gatts_lua_char_t *gatts_lua_find_char(uint16_t handle) {
    for (int i = 0; i < s_lua_char_cnt; i++) {
        if (s_lua_chars[i].handle == handle)
            return &s_lua_chars[i];
    }
    return NULL;
}

static gatts_lua_char_t *gatts_lua_get_or_create_char(uint16_t handle) {
    gatts_lua_char_t *c = gatts_lua_find_char(handle);
    if (c) return c;
    if (s_lua_char_cnt >= GATTS_LUA_MAX_CHARS) return NULL;
    c = &s_lua_chars[s_lua_char_cnt++];
    c->handle    = handle;
    c->read_ref  = LUA_NOREF;
    c->write_ref = LUA_NOREF;
    return c;
}

/* ---- C callback glue: called from driver task ---- */

static void gatts_read_lua_cb(uint16_t conn_id, uint32_t trans_id,
                               uint16_t char_handle, uint16_t offset) {
    gatts_lua_char_t *c = gatts_lua_find_char(char_handle);
    if (!c || c->read_ref == LUA_NOREF) return;

    lua_State *L  = pvGetLuaState();
    lua_State *TL = lua_newthread(L);
    int fref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, c->read_ref);
    lua_xmove(L, TL, 1);

    lua_createtable(TL, 0, 4);
    lua_pushstring(TL, "conn_id");  lua_pushinteger(TL, conn_id);  lua_settable(TL, -3);
    lua_pushstring(TL, "trans_id"); lua_pushinteger(TL, (lua_Integer)trans_id); lua_settable(TL, -3);
    lua_pushstring(TL, "handle");   lua_pushinteger(TL, char_handle); lua_settable(TL, -3);
    lua_pushstring(TL, "offset");   lua_pushinteger(TL, offset);  lua_settable(TL, -3);

    lua_pcall(TL, 1, 0, 0);
    luaL_unref(TL, LUA_REGISTRYINDEX, fref);
}

static void gatts_write_lua_cb(uint16_t conn_id, uint32_t trans_id,
                                uint16_t char_handle, uint8_t *value,
                                uint16_t len, bool need_rsp) {
    gatts_lua_char_t *c = gatts_lua_find_char(char_handle);
    if (!c || c->write_ref == LUA_NOREF) return;

    char hex_buf[len * 2 + 1];
    val_to_hex_string(hex_buf, (char *)value, len, 0);

    lua_State *L  = pvGetLuaState();
    lua_State *TL = lua_newthread(L);
    int fref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, c->write_ref);
    lua_xmove(L, TL, 1);

    lua_createtable(TL, 0, 5);
    lua_pushstring(TL, "conn_id");  lua_pushinteger(TL, conn_id);  lua_settable(TL, -3);
    lua_pushstring(TL, "trans_id"); lua_pushinteger(TL, (lua_Integer)trans_id); lua_settable(TL, -3);
    lua_pushstring(TL, "handle");   lua_pushinteger(TL, char_handle); lua_settable(TL, -3);
    lua_pushstring(TL, "value");    lua_pushstring(TL, hex_buf);   lua_settable(TL, -3);
    lua_pushstring(TL, "need_rsp"); lua_pushboolean(TL, need_rsp); lua_settable(TL, -3);

    lua_pcall(TL, 1, 0, 0);
    luaL_unref(TL, LUA_REGISTRYINDEX, fref);
}

static void gatts_connect_lua_cb(uint16_t conn_id, uint8_t *addr) {
    if (s_gatts_connect_ref == LUA_NOREF) return;

    char addr_str[13];
    val_to_hex_string(addr_str, (char *)addr, 6, 0);

    lua_State *L  = pvGetLuaState();
    lua_State *TL = lua_newthread(L);
    int fref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, s_gatts_connect_ref);
    lua_xmove(L, TL, 1);

    lua_createtable(TL, 0, 2);
    lua_pushstring(TL, "conn_id"); lua_pushinteger(TL, conn_id); lua_settable(TL, -3);
    lua_pushstring(TL, "addr");    lua_pushstring(TL, addr_str); lua_settable(TL, -3);

    lua_pcall(TL, 1, 0, 0);
    luaL_unref(TL, LUA_REGISTRYINDEX, fref);
}

static void gatts_disconnect_lua_cb(uint16_t conn_id, uint8_t *addr) {
    if (s_gatts_disconnect_ref == LUA_NOREF) return;

    char addr_str[13];
    val_to_hex_string(addr_str, (char *)addr, 6, 0);

    lua_State *L  = pvGetLuaState();
    lua_State *TL = lua_newthread(L);
    int fref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, s_gatts_disconnect_ref);
    lua_xmove(L, TL, 1);

    lua_createtable(TL, 0, 2);
    lua_pushstring(TL, "conn_id"); lua_pushinteger(TL, conn_id); lua_settable(TL, -3);
    lua_pushstring(TL, "addr");    lua_pushstring(TL, addr_str); lua_settable(TL, -3);

    lua_pcall(TL, 1, 0, 0);
    luaL_unref(TL, LUA_REGISTRYINDEX, fref);
}

/* ---- Lua-callable functions ---- */

static int lgatts_register(lua_State *L) {
    driver_error_t *error;
    uint16_t app_id = (uint16_t)luaL_checkinteger(L, 1);
    if ((error = bt_gatts_register(app_id)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgatts_create_service(lua_State *L) {
    driver_error_t *error;
    uint16_t uuid16   = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t handles  = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t svc_hdl  = 0;
    if ((error = bt_gatts_create_service(uuid16, handles, &svc_hdl)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, svc_hdl);
    return 1;
}

static int lgatts_add_characteristic(lua_State *L) {
    driver_error_t *error;
    uint16_t srv_hdl = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t uuid16  = (uint16_t)luaL_checkinteger(L, 2);
    uint8_t  props   = (uint8_t) luaL_checkinteger(L, 3);
    uint16_t perms   = (uint16_t)luaL_checkinteger(L, 4);

    uint8_t  *val     = NULL;
    uint16_t  val_len = 0;
    if (!lua_isnoneornil(L, 5)) {
        const char *hex = luaL_checkstring(L, 5);
        if (!lcheck_hex_str(hex))
            return luaL_exception_extended(L, BT_ERR_INVALID_ARGUMENT, "value must be hex string");
        val_len = (uint16_t)(strlen(hex) / 2);
        val = (uint8_t *)alloca(val_len);
        hex_string_to_val((char *)hex, (char *)val, val_len, 0);
    }

    uint16_t char_hdl = 0;
    if ((error = bt_gatts_add_characteristic(srv_hdl, uuid16, props, perms, val, val_len, &char_hdl)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, char_hdl);
    return 1;
}

static int lgatts_add_descriptor(lua_State *L) {
    driver_error_t *error;
    uint16_t srv_hdl  = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t char_hdl = (uint16_t)luaL_checkinteger(L, 2);
    uint16_t uuid16   = (uint16_t)luaL_checkinteger(L, 3);
    uint16_t perms    = (uint16_t)luaL_checkinteger(L, 4);
    uint16_t descr_hdl = 0;
    if ((error = bt_gatts_add_descriptor(srv_hdl, char_hdl, uuid16, perms, &descr_hdl)))
        return luaL_driver_error(L, error);
    lua_pushinteger(L, descr_hdl);
    return 1;
}

static int lgatts_start_service(lua_State *L) {
    driver_error_t *error;
    uint16_t srv_hdl = (uint16_t)luaL_checkinteger(L, 1);
    if ((error = bt_gatts_start_service(srv_hdl)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgatts_stop_service(lua_State *L) {
    driver_error_t *error;
    uint16_t srv_hdl = (uint16_t)luaL_checkinteger(L, 1);
    if ((error = bt_gatts_stop_service(srv_hdl)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgatts_send_response(lua_State *L) {
    driver_error_t *error;
    uint16_t conn_id  = (uint16_t)luaL_checkinteger(L, 1);
    uint32_t trans_id = (uint32_t)luaL_checkinteger(L, 2);
    uint16_t char_hdl = (uint16_t)luaL_checkinteger(L, 3);
    const char *hex   = luaL_checkstring(L, 4);

    if (!lcheck_hex_str(hex))
        return luaL_exception_extended(L, BT_ERR_INVALID_ARGUMENT, "value must be hex string");

    uint16_t len = (uint16_t)(strlen(hex) / 2);
    uint8_t *val = (uint8_t *)alloca(len);
    hex_string_to_val((char *)hex, (char *)val, len, 0);

    if ((error = bt_gatts_send_response(conn_id, trans_id, char_hdl, val, len)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgatts_notify(lua_State *L) {
    driver_error_t *error;
    uint16_t conn_id  = (uint16_t)luaL_checkinteger(L, 1);
    uint16_t char_hdl = (uint16_t)luaL_checkinteger(L, 2);
    const char *hex   = luaL_checkstring(L, 3);
    bool need_confirm = lua_toboolean(L, 4);

    if (!lcheck_hex_str(hex))
        return luaL_exception_extended(L, BT_ERR_INVALID_ARGUMENT, "value must be hex string");

    uint16_t len = (uint16_t)(strlen(hex) / 2);
    uint8_t *val = (uint8_t *)alloca(len);
    hex_string_to_val((char *)hex, (char *)val, len, 0);

    if ((error = bt_gatts_send_notify(conn_id, char_hdl, val, len, need_confirm)))
        return luaL_driver_error(L, error);
    return 0;
}

static int lgatts_on_read(lua_State *L) {
    uint16_t char_hdl = (uint16_t)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    gatts_lua_char_t *c = gatts_lua_get_or_create_char(char_hdl);
    if (!c) return luaL_exception(L, BT_ERR_NOT_ENOUGH_MEMORY);

    if (c->read_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, c->read_ref);
    c->read_ref = ref;

    bt_gatts_on_read(char_hdl, gatts_read_lua_cb);
    return 0;
}

static int lgatts_on_write(lua_State *L) {
    uint16_t char_hdl = (uint16_t)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    gatts_lua_char_t *c = gatts_lua_get_or_create_char(char_hdl);
    if (!c) return luaL_exception(L, BT_ERR_NOT_ENOUGH_MEMORY);

    if (c->write_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, c->write_ref);
    c->write_ref = ref;

    bt_gatts_on_write(char_hdl, gatts_write_lua_cb);
    return 0;
}

static int lgatts_on_connect(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (s_gatts_connect_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, s_gatts_connect_ref);
    s_gatts_connect_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    bt_gatts_on_connect(gatts_connect_lua_cb);
    return 0;
}

static int lgatts_on_disconnect(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (s_gatts_disconnect_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, s_gatts_disconnect_ref);
    s_gatts_disconnect_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    bt_gatts_on_disconnect(gatts_disconnect_lua_cb);
    return 0;
}

static const LUA_REG_TYPE lbt_gatts_map[] = {
    { LSTRKEY("register"),        LFUNCVAL(lgatts_register)        },
    { LSTRKEY("create_service"),  LFUNCVAL(lgatts_create_service)  },
    { LSTRKEY("add_char"),        LFUNCVAL(lgatts_add_characteristic) },
    { LSTRKEY("add_descriptor"),  LFUNCVAL(lgatts_add_descriptor)  },
    { LSTRKEY("start_service"),   LFUNCVAL(lgatts_start_service)   },
    { LSTRKEY("stop_service"),    LFUNCVAL(lgatts_stop_service)    },
    { LSTRKEY("send_response"),   LFUNCVAL(lgatts_send_response)   },
    { LSTRKEY("notify"),          LFUNCVAL(lgatts_notify)          },
    { LSTRKEY("on_read"),         LFUNCVAL(lgatts_on_read)         },
    { LSTRKEY("on_write"),        LFUNCVAL(lgatts_on_write)        },
    { LSTRKEY("on_connect"),      LFUNCVAL(lgatts_on_connect)      },
    { LSTRKEY("on_disconnect"),   LFUNCVAL(lgatts_on_disconnect)   },
    { LNILKEY, LNILVAL }
};

#endif /* CONFIG_BT_ENABLED */
