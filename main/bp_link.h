#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// BluePill sensor hub link. ESP receives a single line ~1 Hz on UART:
//   S:T=23.45 H=41.23 D=1238 O=0 ST=1 SV=1\n
//
// ST=1 means the SHT40 line on the BluePill side was recently parsed OK.
// SV=1 means the last VL53L1X read had range_status == 0.
// Accessors below return ESP_ERR_TIMEOUT when no fresh data is available
// for the corresponding sensor (stale BluePill, or ST/SV reported 0).
esp_err_t bp_link_init(void);

esp_err_t bp_link_get_temp_humidity(float *temp_c, float *rh_pct);
esp_err_t bp_link_get_distance_mm(uint16_t *mm);
esp_err_t bp_link_get_occupied(bool *occupied);
