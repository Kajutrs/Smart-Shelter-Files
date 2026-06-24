#pragma once

#include "esp_err.h"

// Initializes the user I2C bus, both sensors, and starts a background
// task that periodically reads SHT40 + VL53L1X and ships the readings
// as newline-delimited JSON over TCP to the dhome server. Reconnects on
// failure. Call after Wi-Fi has come up.
esp_err_t sensor_task_start(void);
