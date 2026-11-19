#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Called (from the Zigbee task) when the coordinator changes an On/Off switch endpoint. */
typedef void (*zigbee_switch_cb_t)(uint8_t ep, bool on);

/* Create the Zigbee task and start the stack. Call once, after NVS init. */
esp_err_t zigbee_device_start(zigbee_switch_cb_t switch_cb);

/* Thread-safe. Updates the Occupancy attribute and reports it, only if it changed. */
void zigbee_device_set_occupancy(uint8_t ep, bool occupied);

/* Thread-safe. Updates an On/Off endpoint (e.g. to publish the persisted state). */
void zigbee_device_set_switch(uint8_t ep, bool on);
