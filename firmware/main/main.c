#include "app_config.h"
#include "antenna.h"
#include "pir.h"
#include "zigbee_device.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "combi";

static void on_switch(uint8_t ep, bool on)
{
    /* TODO: forward to the LD2410C driver (Bluetooth / engineering mode). */
    ESP_LOGI(TAG, "switch EP%u requested %s", ep, on ? "on" : "off");
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
#if APP_DEMO_MODE
    xTaskCreate(demo_task, "demo", 3072, NULL, 4, NULL);
#endif
}
