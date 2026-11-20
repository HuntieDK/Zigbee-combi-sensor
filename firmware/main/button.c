#include "button.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "button_hold.h"
#include "zigbee_device.h"

static const char *TAG = "button";

static void led_set(bool on)
{
    gpio_set_level(APP_STATUS_LED_GPIO, (on ^ APP_STATUS_LED_ACTIVE_LOW) ? 1 : 0);
}

static void button_task(void *arg)
{
    (void)arg;
    button_hold_t hold;
    button_hold_init(&hold, APP_BUTTON_WARN_MS, APP_BUTTON_HOLD_MS);
    uint32_t tick = 0;

    for (;;) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        bool pressed = gpio_get_level(APP_BUTTON_GPIO) == 0; /* active low */
        switch (button_hold_update(&hold, pressed, now_ms)) {
        case BUTTON_HOLD_WARN:
            led_set((tick++ / 2) & 1); /* ~5 Hz blink at a 50 ms poll */
            break;
        case BUTTON_HOLD_FIRE:
            ESP_LOGW(TAG, "Long press: leaving the Zigbee network and clearing pairing");
            led_set(true);
            zigbee_device_factory_reset();
            vTaskDelay(pdMS_TO_TICKS(500));
            led_set(false);
            break;
        default:
            tick = 0;
            led_set(false);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t button_start(void)
{
    const gpio_config_t btn = {
        .pin_bit_mask = 1ULL << APP_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&btn);
    if (err != ESP_OK) {
        return err;
    }
    const gpio_config_t led = {
        .pin_bit_mask = 1ULL << APP_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    err = gpio_config(&led);
    if (err != ESP_OK) {
        return err;
    }
    led_set(false);
    return xTaskCreate(button_task, "button", 3072, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}
