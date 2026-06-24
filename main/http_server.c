#include "http_server.h"

#include <stdio.h>
#include <string.h>

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#if CONFIG_RELAY_ENABLED
#include "relay.h"
#endif

static const char *TAG = "http";

#define PART_BOUNDARY "frame"
static const char STREAM_CONTENT_TYPE[] =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char STREAM_BOUNDARY[] = "\r\n--" PART_BOUNDARY "\r\n";
static const char STREAM_PART_FMT[] =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char INDEX_HTML[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<title>T-SIMCAM</title>"
    "<style>body{margin:0;background:#111;display:flex;align-items:center;"
    "justify-content:center;height:100vh}img{max-width:100%;max-height:100%}"
    "</style></head><body><img src=\"/stream\"></body></html>";

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "30");

    char part_hdr[64];
    int64_t last = esp_timer_get_time();
    uint32_t frames = 0;

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "fb_get failed");
            res = ESP_FAIL;
            break;
        }
        size_t hlen = snprintf(part_hdr, sizeof(part_hdr), STREAM_PART_FMT, fb->len);

        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, sizeof(STREAM_BOUNDARY) - 1);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_hdr, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            ESP_LOGI(TAG, "client disconnected");
            break;
        }

        frames++;
        int64_t now = esp_timer_get_time();
        if (now - last >= 5 * 1000 * 1000) {
            ESP_LOGI(TAG, "stream: %.1f fps", frames * 1e6f / (now - last));
            frames = 0;
            last = now;
        }
    }
    return res;
}

#if CONFIG_RELAY_ENABLED
static esp_err_t relay_reply(httpd_req_t *req) {
    const char *body = relay_get() ? "{\"relay\":\"on\"}\n" : "{\"relay\":\"off\"}\n";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t relay_get_handler(httpd_req_t *req)    { return relay_reply(req); }
static esp_err_t relay_on_handler(httpd_req_t *req)     { relay_set(true);  return relay_reply(req); }
static esp_err_t relay_off_handler(httpd_req_t *req)    { relay_set(false); return relay_reply(req); }
static esp_err_t relay_toggle_handler(httpd_req_t *req) { relay_set(!relay_get()); return relay_reply(req); }
#endif

esp_err_t http_server_start(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = CONFIG_HTTP_SERVER_PORT;
    cfg.ctrl_port += CONFIG_HTTP_SERVER_PORT;  // avoid collision when port != 80
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: 0x%x", err);
        return err;
    }

    httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = index_handler };
    httpd_uri_t uri_capture = {
        .uri = "/capture", .method = HTTP_GET, .handler = capture_handler };
    httpd_uri_t uri_stream = {
        .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };

    httpd_register_uri_handler(server, &uri_index);
    httpd_register_uri_handler(server, &uri_capture);
    httpd_register_uri_handler(server, &uri_stream);

#if CONFIG_RELAY_ENABLED
    httpd_uri_t uri_relay = {
        .uri = "/relay", .method = HTTP_GET, .handler = relay_get_handler };
    httpd_uri_t uri_relay_on = {
        .uri = "/relay/on", .method = HTTP_GET, .handler = relay_on_handler };
    httpd_uri_t uri_relay_off = {
        .uri = "/relay/off", .method = HTTP_GET, .handler = relay_off_handler };
    httpd_uri_t uri_relay_toggle = {
        .uri = "/relay/toggle", .method = HTTP_GET, .handler = relay_toggle_handler };
    httpd_register_uri_handler(server, &uri_relay);
    httpd_register_uri_handler(server, &uri_relay_on);
    httpd_register_uri_handler(server, &uri_relay_off);
    httpd_register_uri_handler(server, &uri_relay_toggle);
#endif

    ESP_LOGI(TAG, "HTTP server listening on port %d", cfg.server_port);
    return ESP_OK;
}
