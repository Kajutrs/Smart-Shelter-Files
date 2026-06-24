#pragma once

#include "esp_err.h"

// Standalone task that polls sht40_read() and drives the relay based on
// CONFIG_RELAY_TEMP_MIN_C / MAX_C. Independent of the dhome reporter so
// network slowness never delays a thermostat action.
esp_err_t thermostat_start(void);
