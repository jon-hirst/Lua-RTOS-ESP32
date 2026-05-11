if(CONFIG_LUA_RTOS_LUA_USE_COAP)
    set(srcs
        "coap_pdu.c"
        "coap_net.c"
    )
    set(include_dirs "include")
else()
    set(srcs "")
    set(include_dirs "")
endif()

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES lwip
)
