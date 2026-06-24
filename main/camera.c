#include "camera.h"
#include "camera_pins.h"

#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "camera";

#if defined(CONFIG_CAMERA_FRAMESIZE_QQVGA)
#  define CFG_FRAMESIZE FRAMESIZE_QQVGA
#elif defined(CONFIG_CAMERA_FRAMESIZE_QVGA)
#  define CFG_FRAMESIZE FRAMESIZE_QVGA
#elif defined(CONFIG_CAMERA_FRAMESIZE_HVGA)
#  define CFG_FRAMESIZE FRAMESIZE_HVGA
#elif defined(CONFIG_CAMERA_FRAMESIZE_SVGA)
#  define CFG_FRAMESIZE FRAMESIZE_SVGA
#elif defined(CONFIG_CAMERA_FRAMESIZE_HD)
#  define CFG_FRAMESIZE FRAMESIZE_HD
#else
#  define CFG_FRAMESIZE FRAMESIZE_VGA
#endif

static void board_power_on(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CAM_PIN_PWR_EN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(CAM_PIN_PWR_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "PWR_EN (GPIO %d) high", CAM_PIN_PWR_EN);
}

esp_err_t camera_start(void) {
    board_power_on();

    camera_config_t cfg = {
        .pin_pwdn       = CAM_PIN_PWDN,
        .pin_reset      = CAM_PIN_RESET,
        .pin_xclk       = CAM_PIN_XCLK,
        .pin_sccb_sda   = CAM_PIN_SIOD,
        .pin_sccb_scl   = CAM_PIN_SIOC,

        .pin_d7         = CAM_PIN_D9,
        .pin_d6         = CAM_PIN_D8,
        .pin_d5         = CAM_PIN_D7,
        .pin_d4         = CAM_PIN_D6,
        .pin_d3         = CAM_PIN_D5,
        .pin_d2         = CAM_PIN_D4,
        .pin_d1         = CAM_PIN_D3,
        .pin_d0         = CAM_PIN_D2,

        .pin_vsync      = CAM_PIN_VSYNC,
        .pin_href       = CAM_PIN_HREF,
        .pin_pclk       = CAM_PIN_PCLK,

        .xclk_freq_hz   = 20 * 1000 * 1000,
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,

        .pixel_format   = PIXFORMAT_JPEG,
        .frame_size     = CFG_FRAMESIZE,
        .jpeg_quality   = CONFIG_CAMERA_JPEG_QUALITY,
        .fb_count       = 2,
        .fb_location    = CAMERA_FB_IN_PSRAM,
        .grab_mode      = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: 0x%x", err);
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "sensor PID=0x%02x ver=0x%02x", s->id.PID, s->id.VER);
    }
    return ESP_OK;
}
