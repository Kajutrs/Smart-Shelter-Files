#include "thermostat.h"

#include "bp_link.h"
#include "relay.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "thermostat";

#define DEBOUNCE_HITS  3
#define POLL_PERIOD_MS 500

static void thermostat_task(void *arg) {
    (void)arg;
    int on_hits = 0, off_hits = 0;

    while (1) {
        float temp = 0, rh = 0;
        if (bp_link_get_temp_humidity(&temp, &rh) == ESP_OK) {
            bool occupied = false;
            // occ_known is false when the distance sensor isn't reporting
            // (VL53L1X timeout / failure). In that case we fall back to
            // temperature only and heat as if possibly occupied.
            bool occ_known = (bp_link_get_occupied(&occupied) == ESP_OK);
            bool vacant = occ_known && !occupied;  // positively empty shelter
            bool cold = temp < (float)CONFIG_RELAY_TEMP_MIN_C;
            bool hot  = temp > (float)CONFIG_RELAY_TEMP_MAX_C;

            // Heat only when it's cold AND the shelter isn't known empty.
            // Turn off when it's warm enough OR the shelter was vacated.
            bool want_on  = cold && !vacant;
            bool want_off = hot || vacant;

            on_hits  = want_on  ? on_hits + 1  : 0;
            off_hits = want_off ? off_hits + 1 : 0;

            const char *occ_str = occ_known ? (occupied ? "yes" : "no") : "unknown";

            bool cur = relay_get();
            if (!cur && on_hits >= DEBOUNCE_HITS) {
                ESP_LOGI(TAG, "temp=%.1f°C < %d, occupied=%s (%dx) → relay ON",
                         temp, CONFIG_RELAY_TEMP_MIN_C, occ_str, on_hits);
                relay_set(true);
                on_hits = 0;
            } else if (cur && off_hits >= DEBOUNCE_HITS) {
                ESP_LOGI(TAG, "temp=%.1f°C, occupied=%s (%dx) → relay OFF (%s)",
                         temp, occ_str, off_hits, hot ? "warm" : "vacant");
                relay_set(false);
                off_hits = 0;
            }
        } else {
            // No fresh reading; hold state, reset counters so a stale
            // value doesn't accumulate hits and then act on recovery.
            on_hits = 0; off_hits = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

esp_err_t thermostat_start(void) {
    ESP_LOGI(TAG, "starting (MIN=%d°C, MAX=%d°C, %dms poll, %dx debounce)",
             CONFIG_RELAY_TEMP_MIN_C, CONFIG_RELAY_TEMP_MAX_C,
             POLL_PERIOD_MS, DEBOUNCE_HITS);
    return xTaskCreate(thermostat_task, "thermostat",
                       3072, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_FAIL;
}
