#include "wifi_sta.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

typedef struct {
    const char *ssid;
    const char *pass;
} wifi_cred_t;

static EventGroupHandle_t s_wifi_events;
static wifi_cred_t s_creds[3];
static int s_n_creds = 0;   // number of configured (non-empty) networks
static int s_cur = 0;       // index of the network currently being tried
static int s_retry = 0;     // failed attempts on the current network
static int s_cycles = 0;    // completed passes through all networks

// Push the credential at s_cur into the Wi-Fi driver.
static void wifi_apply_cred(void) {
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, s_creds[s_cur].ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, s_creds[s_cur].pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (++s_retry >= CONFIG_WIFI_MAX_RETRY) {
            // Exhausted this network: advance to the next configured one.
            s_retry = 0;
            if (++s_cur >= s_n_creds) {
                s_cur = 0;
                if (++s_cycles >= CONFIG_WIFI_MAX_CYCLES) {
                    ESP_LOGE(TAG, "all networks failed after %d cycles", s_cycles);
                    xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
                    return;
                }
            }
            ESP_LOGW(TAG, "switching to SSID '%s'", s_creds[s_cur].ssid);
            wifi_apply_cred();
        } else {
            ESP_LOGW(TAG, "reconnect to '%s' attempt %d", s_creds[s_cur].ssid, s_retry);
        }
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "connected to '%s', got IP: " IPSTR,
                 s_creds[s_cur].ssid, IP2STR(&event->ip_info.ip));
        s_retry = 0;
        s_cycles = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_sta_start(void) {
    s_wifi_events = xEventGroupCreate();

    // Collect the configured networks, skipping any with an empty SSID.
    const char *ssids[] = {CONFIG_WIFI_SSID, CONFIG_WIFI_SSID_2, CONFIG_WIFI_SSID_3};
    const char *passes[] = {CONFIG_WIFI_PASSWORD, CONFIG_WIFI_PASSWORD_2, CONFIG_WIFI_PASSWORD_3};
    for (int i = 0; i < 3; i++) {
        if (ssids[i][0] != '\0') {
            s_creds[s_n_creds].ssid = ssids[i];
            s_creds[s_n_creds].pass = passes[i];
            s_n_creds++;
        }
    }
    if (s_n_creds == 0) {
        ESP_LOGE(TAG, "no Wi-Fi SSID configured");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_apply_cred();
    ESP_ERROR_CHECK(esp_wifi_start());
    // Disable Wi-Fi power-save: significant throughput boost for streaming.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "connecting, %d network(s) configured, starting with '%s' ...",
             s_n_creds, s_creds[s_cur].ssid);
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
    ESP_LOGE(TAG, "Wi-Fi connection failed");
    return ESP_FAIL;
}
