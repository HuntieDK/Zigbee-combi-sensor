#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Start the LD2410C UART driver: presence reporting on APP_EP_LD2410, and the Bluetooth /
 * engineering-mode switches. The module is the source of truth for both switch states:
 * engineering mode is read from every data frame, Bluetooth from the MAC query. */
esp_err_t ld2410_start(void);

/* Ask for a switch change (APP_EP_LD2410_BT or APP_EP_LD2410_ENG). Non-blocking; safe to call
 * from the Zigbee task. The real resulting state is published back when the command completes. */
void ld2410_request_switch(uint8_t ep, bool on);
