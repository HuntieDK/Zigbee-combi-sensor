#include "ld2410.h"

#include <string.h>

#include "app_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ld2410_proto.h"
#include "presence_filter.h"
#include "zigbee_device.h"

static const char *TAG = "ld2410";

#define LD2410_UART UART_NUM_1

typedef struct {
    uint8_t ep;
    bool on;
} switch_req_t;

static QueueHandle_t s_ack_queue; /* ld2410_event_t, ACKs from the rx task to the command task */
static QueueHandle_t s_req_queue; /* switch_req_t */

static bool s_eng_valid;
static bool s_eng;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------------------ */
/* RX task: parse frames, report presence, track engineering mode           */
/* ------------------------------------------------------------------------ */

static void rx_task(void *arg)
{
    (void)arg;
    ld2410_parser_t parser;
    ld2410_parser_init(&parser);
    presence_filter_t filter;
    presence_filter_init(&filter, APP_LD2410_OFF_DELAY_MS);

    uint8_t buf[128];
    uint8_t target_state = 0;
    uint32_t last_frame_ms = 0;
    bool have_data = false;
    bool timeout_logged = false;

    for (;;) {
        int n = uart_read_bytes(LD2410_UART, buf, sizeof(buf), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            ld2410_event_t ev;
            if (!ld2410_parser_feed(&parser, buf[i], &ev)) {
                continue;
            }
            if (ev.type == LD2410_EV_ACK) {
                xQueueSend(s_ack_queue, &ev, 0);
                continue;
            }
            if (!have_data) {
                ESP_LOGI(TAG, "Receiving data from the LD2410C");
            }
            have_data = true;
            timeout_logged = false;
            last_frame_ms = now_ms();
            target_state = ev.target_state;
            if (!s_eng_valid || s_eng != ev.engineering) {
                s_eng_valid = true;
                s_eng = ev.engineering;
                ESP_LOGI(TAG, "Engineering mode is %s", s_eng ? "on" : "off");
                zigbee_device_set_switch(APP_EP_LD2410_ENG, s_eng);
            }
        }

        uint32_t t = now_ms();
        bool fresh = have_data && (uint32_t)(t - last_frame_ms) < APP_LD2410_DATA_TIMEOUT_MS;
        if (have_data && !fresh && !timeout_logged) {
            ESP_LOGW(TAG, "No data from the LD2410C for %d s; reporting no presence", APP_LD2410_DATA_TIMEOUT_MS / 1000);
            timeout_logged = true;
        }
        if (presence_filter_update(&filter, fresh && target_state != 0, t)) {
            ESP_LOGI(TAG, "Presence -> %d", filter.present);
            zigbee_device_set_occupancy(APP_EP_LD2410, filter.present);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Command task                                                             */
/* ------------------------------------------------------------------------ */

/* Send a command and wait for its acknowledgement. Optionally returns the ack event. */
static bool send_wait(uint8_t cmd, const uint8_t *value, size_t value_len, ld2410_event_t *ack_out)
{
    uint8_t frame[LD2410_MAX_FRAME];
    size_t n = ld2410_build_command(cmd, value, value_len, frame, sizeof(frame));
    if (n == 0) {
        return false;
    }
    xQueueReset(s_ack_queue);
    uart_write_bytes(LD2410_UART, frame, n);

    uint32_t start = now_ms();
    for (;;) {
        uint32_t elapsed = now_ms() - start;
        if (elapsed >= APP_LD2410_ACK_TIMEOUT_MS) {
            ESP_LOGW(TAG, "No ack for command 0x%02X", cmd);
            return false;
        }
        ld2410_event_t ev;
        if (xQueueReceive(s_ack_queue, &ev, pdMS_TO_TICKS(APP_LD2410_ACK_TIMEOUT_MS - elapsed)) == pdTRUE &&
            ev.cmd == cmd) {
            if (!ev.ok) {
                ESP_LOGW(TAG, "Command 0x%02X refused", cmd);
                return false;
            }
            if (ack_out) {
                *ack_out = ev;
            }
            return true;
        }
    }
}

static bool config_begin(void)
{
    static const uint8_t v[2] = {0x01, 0x00};
    for (int attempt = 0; attempt < 3; attempt++) {
        if (send_wait(LD2410_CMD_ENABLE_CONFIG, v, sizeof(v), NULL)) {
            return true;
        }
    }
    ESP_LOGE(TAG, "LD2410C does not answer (check wiring and power)");
    return false;
}

static void config_end(void)
{
    send_wait(LD2410_CMD_DISABLE_CONFIG, NULL, 0, NULL);
}

/* Read Bluetooth state from the module and publish it. */
static void query_bluetooth(void)
{
    static const uint8_t v[2] = {0x01, 0x00};
    if (!config_begin()) {
        return;
    }
    ld2410_event_t ack;
    if (send_wait(LD2410_CMD_QUERY_MAC, v, sizeof(v), &ack) && ack.payload_len >= 6) {
        bool bt = ld2410_bluetooth_enabled_from_mac(ack.payload);
        ESP_LOGI(TAG, "Bluetooth is %s", bt ? "on" : "off");
        zigbee_device_set_switch(APP_EP_LD2410_BT, bt);
    }
    config_end();
}

static void set_engineering(bool on)
{
    if (config_begin()) {
        send_wait(on ? LD2410_CMD_ENABLE_ENG : LD2410_CMD_DISABLE_ENG, NULL, 0, NULL);
        config_end();
    }
    /* Let the module switch modes, then publish what it is really doing. */
    vTaskDelay(pdMS_TO_TICKS(500));
    if (s_eng_valid) {
        zigbee_device_set_switch(APP_EP_LD2410_ENG, s_eng);
    }
}

static void set_bluetooth(bool on)
{
    if (config_begin()) {
        const uint8_t v[2] = {on ? 0x01 : 0x00, 0x00};
        if (send_wait(LD2410_CMD_BLUETOOTH, v, sizeof(v), NULL)) {
            /* The change only takes effect after a restart. */
            send_wait(LD2410_CMD_RESTART, NULL, 0, NULL);
            vTaskDelay(pdMS_TO_TICKS(1500));
        } else {
            config_end();
        }
    }
    query_bluetooth(); /* publish the real state, whatever happened */
}

static void cmd_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2000)); /* let the module and the stack settle */
    query_bluetooth();

    for (;;) {
        switch_req_t req;
        if (xQueueReceive(s_req_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "Switch request: EP%u -> %s", req.ep, req.on ? "on" : "off");
        if (req.ep == APP_EP_LD2410_ENG) {
            set_engineering(req.on);
        } else if (req.ep == APP_EP_LD2410_BT) {
            set_bluetooth(req.on);
        }
    }
}

/* ------------------------------------------------------------------------ */

void ld2410_request_switch(uint8_t ep, bool on)
{
    switch_req_t req = {.ep = ep, .on = on};
    if (xQueueSend(s_req_queue, &req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Request queue full, dropping switch request");
    }
}

esp_err_t ld2410_start(void)
{
    const uart_config_t cfg = {
        .baud_rate = APP_LD2410_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(LD2410_UART, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(LD2410_UART, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(LD2410_UART, APP_LD2410_TX_GPIO, APP_LD2410_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    s_ack_queue = xQueueCreate(4, sizeof(ld2410_event_t));
    s_req_queue = xQueueCreate(4, sizeof(switch_req_t));
    if (!s_ack_queue || !s_req_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(rx_task, "ld2410_rx", 4096, NULL, 6, NULL) != pdPASS ||
        xTaskCreate(cmd_task, "ld2410_cmd", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
