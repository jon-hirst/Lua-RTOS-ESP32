idf_build_get_property(idf_path IDF_PATH)
idf_build_get_property(idf_target IDF_TARGET)

idf_component_register(SRC_DIRS src
                                src/comdrivers
                                src/devdrivers
                                src/dispdrivers
                                src/emudevs
                                src/network
                       INCLUDE_DIRS src
                                src/fonts
                                src/images
                                src/comdrivers
                                src/devdrivers
                                src/dispdrivers
                                src/emudevs
                                src/network
                                ${idf_path}/components/spiffs/include
                                ${idf_path}/components/ulp/ulp_common/include
                                ${idf_path}/components/ulp/ulp_fsm/include
                                ${idf_path}/components/ulp/ulp_fsm/include/${idf_target}
                       REQUIRES fatfs
                                spiffs
                                esp_adc
                                ulp
                                nvs_flash
                                soc
                                esp_wifi
                                esp_timer
                                driver
                                spi_flash
                                esp_rom
                                esp_http_client)
