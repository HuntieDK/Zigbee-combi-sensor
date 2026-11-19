#include "antenna.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "antenna";

esp_err_t antenna_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << APP_RF_SWITCH_ENABLE_GPIO) | (1ULL << APP_RF_SWITCH_SELECT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    /* Power the switch first, then select the path. */
    gpio_set_level(APP_RF_SWITCH_ENABLE_GPIO, 0);
    gpio_set_level(APP_RF_SWITCH_SELECT_GPIO, APP_USE_EXTERNAL_ANTENNA ? 1 : 0);
    ESP_LOGI(TAG, "Using %s antenna", APP_USE_EXTERNAL_ANTENNA ? "external" : "on-board");
    return ESP_OK;
}
