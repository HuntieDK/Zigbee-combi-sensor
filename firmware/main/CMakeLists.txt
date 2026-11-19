idf_component_register(SRCS "main.c" "antenna.c" "zigbee_device.c" "pir.c" "pir_debounce.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nvs_flash esp_driver_gpio esp_timer)
