#include "rtsp_publisher.h"
#include "rtp_mjpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "rtsp-pub";

#define RTP_CHANNEL       0
#define BACKOFF_INITIAL_S 2
#define BACKOFF_MAX_S     30
#define IO_TIMEOUT_MS     5000

typedef struct {
    int sock;
} tcp_sink_ctx_t;

// Wraps an RTP packet in the "$<ch><len:2BE><data>" interleaved framing
// (RFC 2326 §10.12) and writes it to the TCP control connection.
static int tcp_interleaved_sink(void *c, const uint8_t *pkt, size_t len) {
    tcp_sink_ctx_t *t = c;
    uint8_t buf[1700];
    if (len + 4 > sizeof(buf)) return -1;
    buf[0] = '$';
    buf[1] = RTP_CHANNEL;
    buf[2] = (uint8_t)(len >> 8);
    buf[3] = (uint8_t)(len & 0xFF);
    memcpy(buf + 4, pkt, len);
    int n = send(t->sock, buf, len + 4, 0);
    return (n == (int)(len + 4)) ? 0 : -1;
}

// --------------------------------------------------------------------------
// RTSP handshake helpers

static int read_until_blankline(int sock, char *buf, size_t cap) {
    size_t off = 0;
    while (off + 1 < cap) {
        int n = recv(sock, buf + off, cap - 1 - off, 0);
        if (n <= 0) return -1;
        off += n;
        buf[off] = '\0';
        if (strstr(buf, "\r\n\r\n")) return (int)off;
    }
    return -1;
}

static int parse_status(const char *resp) {
    int code;
    return (sscanf(resp, "RTSP/1.0 %d", &code) == 1) ? code : -1;
}

static bool extract_header(const char *resp, const char *name,
                            char *out, size_t out_sz) {
    const char *p = strcasestr(resp, name);
    if (!p) return false;
    p += strlen(name);
    while (*p == ' ' || *p == ':') p++;
    const char *end = strstr(p, "\r\n");
    if (!end) return false;
    size_t n = end - p;
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

// Returns 0 on success, -1 on failure.
static int send_request(int sock, const char *req, char *resp, size_t resp_sz,
                         int expected_code) {
    ESP_LOGD(TAG, "→\n%s", req);
    if (send(sock, req, strlen(req), 0) <= 0) return -1;
    int n = read_until_blankline(sock, resp, resp_sz);
    if (n < 0) return -1;
    ESP_LOGD(TAG, "←\n%.*s", n, resp);
    int code = parse_status(resp);
    if (code != expected_code) {
        ESP_LOGE(TAG, "expected %d, got %d", expected_code, code);
        return -1;
    }
    return 0;
}

static int rtsp_handshake(int sock, const char *url, char *session_out,
                           size_t session_sz) {
    char req[1024], resp[1024];
    int cseq = 1;

    // 1. OPTIONS
    snprintf(req, sizeof(req),
        "OPTIONS %s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "User-Agent: tsimcam/1\r\n"
        "\r\n", url, cseq++);
    if (send_request(sock, req, resp, sizeof(resp), 200) < 0) return -1;

    // 2. ANNOUNCE with SDP
    char sdp[384];
    int sdp_len = snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=ESP32-S3 Camera\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "t=0 0\r\n"
        "a=tool:tsimcam\r\n"
        "m=video 0 RTP/AVP %d\r\n"
        "a=rtpmap:%d JPEG/%d\r\n"
        "a=control:streamid=0\r\n",
        RTP_PT_JPEG, RTP_PT_JPEG, RTP_CLOCK_HZ);

    snprintf(req, sizeof(req),
        "ANNOUNCE %s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "User-Agent: tsimcam/1\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: %d\r\n"
        "\r\n%s", url, cseq++, sdp_len, sdp);
    if (send_request(sock, req, resp, sizeof(resp), 200) < 0) return -1;

    // Some servers hand out the Session header here, others at SETUP.
    extract_header(resp, "Session", session_out, session_sz);
    char *semi = strchr(session_out, ';');
    if (semi) *semi = '\0';

    // 3. SETUP — TCP interleaved, mode=record
    snprintf(req, sizeof(req),
        "SETUP %s/streamid=0 RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "User-Agent: tsimcam/1\r\n"
        "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d;mode=record\r\n"
        "%s%s%s"
        "\r\n",
        url, cseq++, RTP_CHANNEL, RTP_CHANNEL + 1,
        session_out[0] ? "Session: " : "",
        session_out[0] ? session_out : "",
        session_out[0] ? "\r\n"      : "");
    if (send_request(sock, req, resp, sizeof(resp), 200) < 0) return -1;

    if (!session_out[0]) {
        extract_header(resp, "Session", session_out, session_sz);
        char *s = strchr(session_out, ';');
        if (s) *s = '\0';
    }

    // 4. RECORD
    snprintf(req, sizeof(req),
        "RECORD %s RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "User-Agent: tsimcam/1\r\n"
        "Session: %s\r\n"
        "Range: npt=0.000-\r\n"
        "\r\n", url, cseq++, session_out);
    if (send_request(sock, req, resp, sizeof(resp), 200) < 0) return -1;

    return 0;
}

// --------------------------------------------------------------------------
// Networking

static int tcp_connect(const char *host, uint16_t port) {
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        ESP_LOGE(TAG, "DNS lookup '%s' failed", host);
        return -1;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return -1; }

    struct timeval tv = { .tv_sec = IO_TIMEOUT_MS / 1000, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int yes = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    int keepalive = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "connect %s:%u failed", host, port);
        close(sock); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    return sock;
}

// --------------------------------------------------------------------------
// Publisher main loop

static int publish_once(void) {
    char url[160];
    snprintf(url, sizeof(url), "rtsp://%s:%d/%s",
             CONFIG_MTX_HOST, CONFIG_MTX_PORT, CONFIG_MTX_PATH);
    ESP_LOGI(TAG, "connecting to %s", url);

    int sock = tcp_connect(CONFIG_MTX_HOST, CONFIG_MTX_PORT);
    if (sock < 0) return -1;

    char session[64] = {0};
    if (rtsp_handshake(sock, url, session, sizeof(session)) < 0) {
        close(sock);
        return -1;
    }
    ESP_LOGI(TAG, "publishing (session=%s)", session);

    tcp_sink_ctx_t sink_ctx = { .sock = sock };
    uint16_t seq = (uint16_t)esp_random();
    uint32_t ssrc = esp_random();
    int64_t  t0 = esp_timer_get_time();
    uint32_t frames = 0;
    size_t   fb_last_size = 0;
    int64_t  last_log = t0;
    int      rc = 0;

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        if (fb->format == PIXFORMAT_JPEG) {
            // 90 kHz clock: us * 9 / 100
            uint32_t ts = (uint32_t)((esp_timer_get_time() - t0) * 9LL / 100LL);
            fb_last_size = fb->len;
            rc = rtp_mjpeg_send_frame(tcp_interleaved_sink, &sink_ctx,
                                       fb->buf, fb->len, &seq, ts, ssrc);
            frames++;
        }
        esp_camera_fb_return(fb);
        if (rc < 0) { ESP_LOGW(TAG, "send failed; reconnecting"); break; }

        int64_t now = esp_timer_get_time();
        if (now - last_log >= 5LL * 1000 * 1000) {
            float fps = frames * 1e6f / (now - last_log);
            ESP_LOGI(TAG, "publish: %.1f fps  (last jpeg %u bytes)",
                     fps, (unsigned)fb_last_size);
            frames = 0;
            last_log = now;
        }
    }

    close(sock);
    return -1;
}

static void publisher_task(void *arg) {
    (void)arg;
    int backoff = BACKOFF_INITIAL_S;
    while (true) {
        publish_once();   // returns when the session dies
        ESP_LOGW(TAG, "reconnecting in %ds", backoff);
        vTaskDelay(pdMS_TO_TICKS(backoff * 1000));
        backoff = backoff < BACKOFF_MAX_S ? backoff * 2 : BACKOFF_MAX_S;
    }
}

esp_err_t rtsp_publisher_start(void) {
    if (xTaskCreate(publisher_task, "rtsp_pub", 8192, NULL, 5, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
