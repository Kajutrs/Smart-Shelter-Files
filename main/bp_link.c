#include "bp_link.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "bp_link";

#define BP_LINK_UART      ((uart_port_t)CONFIG_BP_LINK_UART_NUM)
#define BP_LINK_RX_BUF_SZ 1024
#define BP_LINK_LINE_MAX  128
#define STALE_AFTER_MS    30000

static SemaphoreHandle_t s_lock;
static float    s_temp     = 0.0f;
static float    s_rh       = 0.0f;
static uint16_t s_dist_mm  = 0;
static bool     s_occupied = false;
static bool     s_st_ok    = false;   // SHT40 freshness flag from BluePill
static bool     s_sv_ok    = false;   // VL53L1X freshness flag from BluePill
static uint64_t s_last_us  = 0;       // last successfully parsed message

static void publish_reading(float t, float rh, uint16_t mm,
                            bool occ, bool st_ok, bool sv_ok) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_temp     = t;
    s_rh       = rh;
    s_dist_mm  = mm;
    s_occupied = occ;
    s_st_ok    = st_ok;
    s_sv_ok    = sv_ok;
    s_last_us  = esp_timer_get_time();
    xSemaphoreGive(s_lock);
}

static void bp_link_uart_task(void *arg) {
    (void)arg;
    char line[BP_LINK_LINE_MAX];
    int  len = 0;
    uint8_t chunk[64];

    while (1) {
        int n = uart_read_bytes(BP_LINK_UART, chunk, sizeof(chunk),
                                pdMS_TO_TICKS(500));
        if (n <= 0) continue;

        for (int i = 0; i < n; i++) {
            uint8_t b = chunk[i];
            if (b == '\r') continue;
            if (b == '\n' || len >= BP_LINK_LINE_MAX - 1) {
                line[len] = '\0';
                if (len > 0) {
                    float t, rh;
                    unsigned int mm, occ, st, sv;
                    if (sscanf(line,
                            "S:T=%f H=%f D=%u O=%u ST=%u SV=%u",
                            &t, &rh, &mm, &occ, &st, &sv) == 6 &&
                        t  >= -40.0f && t  <= 125.0f &&
                        rh >= 0.0f   && rh <= 100.0f &&
                        mm <= 0xFFFF) {
                        publish_reading(t, rh, (uint16_t)mm,
                                        occ != 0, st != 0, sv != 0);
                    } else {
                        ESP_LOGW(TAG, "bad line: %s", line);
                    }
                }
                len = 0;
            } else {
                line[len++] = (char)b;
            }
        }
    }
}

esp_err_t bp_link_init(void) {
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    const uart_config_t cfg = {
        .baud_rate  = CONFIG_BP_LINK_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(BP_LINK_UART, BP_LINK_RX_BUF_SZ, 0,
                                        0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: 0x%x", err);
        return err;
    }
    if ((err = uart_param_config(BP_LINK_UART, &cfg)) != ESP_OK) return err;
    if ((err = uart_set_pin(BP_LINK_UART,
                            CONFIG_BP_LINK_TX_PIN, CONFIG_BP_LINK_RX_PIN,
                            UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)) != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "UART%d ready (TX=%d, RX=%d, %d baud)",
             BP_LINK_UART, CONFIG_BP_LINK_TX_PIN, CONFIG_BP_LINK_RX_PIN,
             CONFIG_BP_LINK_BAUD);

    if (xTaskCreate(bp_link_uart_task, "bp_link",
                    3072, NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start UART reader task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool snapshot(float *t, float *rh, uint16_t *mm, bool *occ,
                     bool *st_ok, bool *sv_ok, uint64_t *age_ms_out) {
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint64_t now = esp_timer_get_time();
    uint64_t age_ms = (now - s_last_us) / 1000;
    bool any_msg = (s_last_us != 0);
    if (t)     *t     = s_temp;
    if (rh)    *rh    = s_rh;
    if (mm)    *mm    = s_dist_mm;
    if (occ)   *occ   = s_occupied;
    if (st_ok) *st_ok = s_st_ok;
    if (sv_ok) *sv_ok = s_sv_ok;
    xSemaphoreGive(s_lock);
    if (age_ms_out) *age_ms_out = age_ms;
    return any_msg && age_ms <= STALE_AFTER_MS;
}

esp_err_t bp_link_get_temp_humidity(float *temp_c, float *rh_pct) {
    float t, rh;
    bool  st_ok;
    uint64_t age;
    bool link_ok = snapshot(&t, &rh, NULL, NULL, &st_ok, NULL, &age);
    if (!link_ok || !st_ok) return ESP_ERR_TIMEOUT;
    if (temp_c) *temp_c = t;
    if (rh_pct) *rh_pct = rh;
    return ESP_OK;
}

esp_err_t bp_link_get_distance_mm(uint16_t *mm) {
    uint16_t d;
    bool sv_ok;
    uint64_t age;
    bool link_ok = snapshot(NULL, NULL, &d, NULL, NULL, &sv_ok, &age);
    if (!link_ok || !sv_ok) return ESP_ERR_TIMEOUT;
    if (mm) *mm = d;
    return ESP_OK;
}

esp_err_t bp_link_get_occupied(bool *occupied) {
    bool occ, sv_ok;
    uint64_t age;
    bool link_ok = snapshot(NULL, NULL, NULL, &occ, NULL, &sv_ok, &age);
    if (!link_ok || !sv_ok) return ESP_ERR_TIMEOUT;
    if (occupied) *occupied = occ;
    return ESP_OK;
}
