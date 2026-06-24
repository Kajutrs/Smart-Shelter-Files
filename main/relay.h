#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t relay_init(void);
esp_err_t relay_set(bool on);
bool      relay_get(void);
