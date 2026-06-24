#include "sensor_task.h"

#include "bp_link.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

static const char *TAG = "sensors";

#define BACKOFF_INITIAL_S 2
#define BACKOFF_MAX_S     30

static int dhome_connect(void) {
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", CONFIG_DHOME_PORT);
    if (getaddrinfo(CONFIG_DHOME_HOST, port_str, &hints, &res) != 0 || !res) {
        ESP_LOGE(TAG, "DNS lookup '%s' failed", CONFIG_DHOME_HOST);
        return -1;
    }

    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) { freeaddrinfo(res); return -1; }

    // Short timeouts so a stalled dhome (often during heavy RTSP traffic)
    // doesn't block this task — the thermostat is now in its own task
    // and doesn't care, but we still want the dhome cycle to stay close
    // to CONFIG_SENSOR_PERIOD_MS.
    struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int yes = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "connect %s:%d failed",
                 CONFIG_DHOME_HOST, CONFIG_DHOME_PORT);
        close(s); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    ESP_LOGI(TAG, "connected to %s:%d", CONFIG_DHOME_HOST, CONFIG_DHOME_PORT);
    return s;
}

static void sensor_task(void *arg) {
    (void)arg;
    int backoff = BACKOFF_INITIAL_S;

    while (true) {
        int sock = dhome_connect();
        if (sock < 0) {
            ESP_LOGW(TAG, "reconnecting in %ds", backoff);
            vTaskDelay(pdMS_TO_TICKS(backoff * 1000));
            backoff = backoff < BACKOFF_MAX_S ? backoff * 2 : BACKOFF_MAX_S;
            continue;
        }
        backoff = BACKOFF_INITIAL_S;

        while (true) {
            float    temp = 0, rh = 0;
            uint16_t dist = 0;
            bool occupied = false;

            // Each accessor returns OK only if its sub-sensor was flagged
            // fresh by the BluePill (ST=1 / SV=1) AND the link message itself
            // is recent. The dhome JSON `status` field aggregates them.
            bool t_ok = (bp_link_get_temp_humidity(&temp, &rh) == ESP_OK);
            bool d_ok = (bp_link_get_distance_mm(&dist)        == ESP_OK);
            (void)bp_link_get_occupied(&occupied);          // matches d_ok by definition

            bool status = t_ok && d_ok;

            char line[256];
            int n = snprintf(line, sizeof(line),
                "{\"name\":\"%s\","
                "\"temperature\":%.2f,"
                "\"humidity\":%.2f,"
                "\"distance_mm\":%u,"
                "\"occupied\":%s,"
                "\"status\":%s}\n",
                CONFIG_SENSOR_NAME,
                temp, rh, (unsigned)dist,
                occupied ? "true" : "false",
                status   ? "true" : "false");

            if (send(sock, line, n, 0) != n) {
                ESP_LOGW(TAG, "send failed; reconnecting");
                break;
            }

            // Drain the server's ACK so its TCP buffer doesn't fill.
            char ack[128];
            recv(sock, ack, sizeof(ack), 0);

            vTaskDelay(pdMS_TO_TICKS(CONFIG_SENSOR_PERIOD_MS));
        }
        close(sock);
    }
}

esp_err_t sensor_task_start(void) {
    if (bp_link_init() != ESP_OK) {
        ESP_LOGW(TAG, "bp_link init failed — continuing without sensor data");
    }
    return xTaskCreate(sensor_task, "sensors", 6144, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_FAIL;
}
