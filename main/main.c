            #include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "camera.h"
#include "http_server.h"
#include "relay.h"
#include "rtsp_publisher.h"
#include "rtsp_server.h"
#include "sensor_task.h"
#if CONFIG_RELAY_AUTO_TEMP && CONFIG_BP_LINK_ENABLED
#include "thermostat.h"
#endif
#include "wifi_sta.h"

static const char *TAG = "app";

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

#if CONFIG_RELAY_ENABLED
    ESP_LOGI(TAG, "init relay ...");
    ESP_ERROR_CHECK(relay_init());
#endif

    ESP_LOGI(TAG, "init camera ...");
    ESP_ERROR_CHECK(camera_start());

    ESP_LOGI(TAG, "connect Wi-Fi ...");
    ESP_ERROR_CHECK(wifi_sta_start());

    ESP_LOGI(TAG, "start HTTP server ...");
    ESP_ERROR_CHECK(http_server_start());

    ESP_LOGI(TAG, "start RTSP server ...");
    ESP_ERROR_CHECK(rtsp_server_start(CONFIG_RTSP_SERVER_PORT));

    ESP_LOGI(TAG, "start RTSP publisher → %s:%d/%s ...",
             CONFIG_MTX_HOST, CONFIG_MTX_PORT, CONFIG_MTX_PATH);
    ESP_ERROR_CHECK(rtsp_publisher_start());

#if CONFIG_SENSORS_ENABLED
    ESP_LOGI(TAG, "start sensor task → dhome %s:%d ...",
             CONFIG_DHOME_HOST, CONFIG_DHOME_PORT);
    ESP_ERROR_CHECK(sensor_task_start());
#else
    ESP_LOGI(TAG, "sensors disabled (enable via menuconfig)");
#endif

#if CONFIG_RELAY_AUTO_TEMP && CONFIG_BP_LINK_ENABLED
    ESP_ERROR_CHECK(thermostat_start());
#endif

    ESP_LOGI(TAG, "ready — local HTTP %d, local RTSP %d, publishing to %s",
             CONFIG_HTTP_SERVER_PORT, CONFIG_RTSP_SERVER_PORT, CONFIG_MTX_HOST);
}
