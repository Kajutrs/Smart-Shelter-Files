#include "relay.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "relay";
static bool s_state;

static inline int level_for(bool on) {
#if CONFIG_RELAY_ACTIVE_LOW
    return on ? 0 : 1;
#else
    return on ? 1 : 0;
#endif
}

esp_err_t relay_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_RELAY_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

#ifdef CONFIG_RELAY_DEFAULT_ON
    s_state = true;
#else
    s_state = false;
#endif
    gpio_set_level(CONFIG_RELAY_GPIO, level_for(s_state));
    ESP_LOGI(TAG, "ready (GPIO %d, active-%s, default %s)",
             CONFIG_RELAY_GPIO,
#if CONFIG_RELAY_ACTIVE_LOW
             "low",
#else
             "high",
#endif
             s_state ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t relay_set(bool on) {
    s_state = on;
    return gpio_set_level(CONFIG_RELAY_GPIO, level_for(on));
}

bool relay_get(void) { return s_state; }
