#include "app_config.h"
#include "antenna.h"
#include "button.h"
#include "ld2410.h"
#include "pir.h"
#include "zigbee_device.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static void on_switch(uint8_t ep, bool on)
{
    ld2410_request_switch(ep, on);
}

#if APP_DEMO_MODE
static void demo_task(void *arg)
{
    (void)arg;
    bool state = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(APP_DEMO_PERIOD_S * 1000));
        state = !state;
        zigbee_device_set_occupancy(APP_EP_LD2410, state);
    }
}
#endif

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(antenna_init());
    ESP_ERROR_CHECK(zigbee_device_start(on_switch));
    ESP_ERROR_CHECK(pir_start());
    ESP_ERROR_CHECK(ld2410_start());
    ESP_ERROR_CHECK(button_start());
#if APP_DEMO_MODE
    xTaskCreate(demo_task, "demo", 3072, NULL, 4, NULL);
#endif
}
