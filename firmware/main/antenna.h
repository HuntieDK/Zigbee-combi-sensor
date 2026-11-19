#pragma once
#include "esp_err.h"

/* Drive the XIAO ESP32-C6 RF switch according to APP_USE_EXTERNAL_ANTENNA.
 * Must be called before the Zigbee stack starts. */
esp_err_t antenna_init(void);
