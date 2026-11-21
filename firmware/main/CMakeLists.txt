idf_component_register(SRCS "main.c" "antenna.c" "zigbee_device.c" "pir.c" "pir_debounce.c"
                            "ld2410.c" "ld2410_proto.c" "presence_filter.c"
                            "button.c" "button_hold.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nvs_flash esp_driver_gpio esp_driver_uart esp_partition esp_system esp_timer)
