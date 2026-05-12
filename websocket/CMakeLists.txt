if(CONFIG_LUA_RTOS_LUA_USE_WEBSOCKET)
    set(srcs
        "websocket_server.c"
        "websocket_client.c"
    )
    set(include_dirs "include")
else()
    set(srcs "")
    set(include_dirs "")
endif()

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES esp_http_server tcp_transport sys
)
