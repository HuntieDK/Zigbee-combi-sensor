#pragma once
#include "esp_err.h"

/* BOOT button long-press = leave the Zigbee network and wipe pairing, then look for a new network.
 * The status LED blinks while the press is long enough to be a warning. */
esp_err_t button_start(void);
