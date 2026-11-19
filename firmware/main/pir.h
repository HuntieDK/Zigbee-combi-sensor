#pragma once
#include "esp_err.h"

/* Start polling the two PIR inputs and publish changes via zigbee_device_set_occupancy(). */
esp_err_t pir_start(void);
