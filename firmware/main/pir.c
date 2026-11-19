#include "pir.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pir_debounce.h"
#include "zigbee_device.h"

static const char *TAG = "pir";

typedef struct {
    int gpio;
    uint8_t ep;
    pir_debounce_t deb;
} pir_t;

static pir_t s_pirs[] = {
    {.gpio = APP_PIR1_GPIO, .ep = APP_EP_PIR1},
    {.gpio = APP_PIR2_GPIO, .ep = APP_EP_PIR2},
};

static void pir_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        for (size_t i = 0; i < sizeof(s_pirs) / sizeof(s_pirs[0]); i++) {
            pir_t *p = &s_pirs[i];
            if (pir_debounce_update(&p->deb, gpio_get_level(p->gpio) != 0, now_ms)) {
                ESP_LOGI(TAG, "PIR on GPIO%d -> %d", p->gpio, p->deb.stable);
                zigbee_device_set_occupancy(p->ep, p->deb.stable);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(APP_PIR_POLL_MS));
    }
}

esp_err_t pir_start(void)
{
    for (size_t i = 0; i < sizeof(s_pirs) / sizeof(s_pirs[0]); i++) {
        const gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_pirs[i].gpio,
            .mode = GPIO_MODE_INPUT,
            /* Pull-down: an unconnected/floating input reads "no presence". */
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK) {
            return err;
        }
        pir_debounce_init(&s_pirs[i].deb, false, APP_PIR_DEBOUNCE_MS);
    }
    return xTaskCreate(pir_task, "pir", 3072, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}
