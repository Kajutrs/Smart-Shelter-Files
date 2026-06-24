#include "rtsp_server.h"
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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "rtsp";

#define STREAM_PATH "/cam"

typedef struct {
    int      ctrl_sock;
    int      rtp_sock;
    struct sockaddr_in client_rtp;
    uint16_t client_rtp_port;
    uint16_t client_rtcp_port;
    uint16_t server_rtp_port;
    uint16_t server_rtcp_port;
    uint32_t session_id;
    uint32_t ssrc;
    uint16_t seq;
    uint32_t timestamp;
    volatile bool playing;
    volatile bool shutdown;
    char     client_ip[INET_ADDRSTRLEN];
    SemaphoreHandle_t streamer_done;
} rtsp_session_t;

typedef struct {
    int sock;
    const struct sockaddr_in *dst;
} udp_sink_ctx_t;

static int udp_sink(void *c, const uint8_t *pkt, size_t len) {
    udp_sink_ctx_t *u = c;
    return sendto(u->sock, pkt, len, 0,
                  (const struct sockaddr *)u->dst, sizeof(*u->dst));
}

static const char *find_header(const char *req, const char *name,
                                char *out, size_t out_sz) {
    const char *p = strcasestr(req, name);
    if (!p) return NULL;
    p += strlen(name);
    while (*p == ' ' || *p == ':') p++;
    const char *end = strstr(p, "\r\n");
    if (!end) return NULL;
    size_t n = end - p;
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return out;
}

static int cseq_of(const char *req) {
    char buf[16];
    return find_header(req, "CSeq", buf, sizeof(buf)) ? atoi(buf) : 0;
}

static void send_resp(int s, const char *resp) {
    send(s, resp, strlen(resp), 0);
}

static void reply_options(int s, int cseq) {
    char r[256];
    snprintf(r, sizeof(r),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n"
        "\r\n", cseq);
    send_resp(s, r);
}

static void reply_describe(int s, int cseq, const char *url) {
    char sdp[384];
    int sdp_len = snprintf(sdp, sizeof(sdp),
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=ESP32-S3 Camera\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "t=0 0\r\n"
        "a=control:*\r\n"
        "m=video 0 RTP/AVP %d\r\n"
        "a=rtpmap:%d JPEG/%d\r\n"
        "a=control:track1\r\n",
        RTP_PT_JPEG, RTP_PT_JPEG, RTP_CLOCK_HZ);

    char r[768];
    snprintf(r, sizeof(r),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Base: %s/\r\n"
        "Content-Length: %d\r\n"
        "\r\n%s", cseq, url, sdp_len, sdp);
    send_resp(s, r);
}

static esp_err_t setup_rtp_socket(rtsp_session_t *S) {
    S->rtp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (S->rtp_sock < 0) return ESP_FAIL;
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = 0,
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(S->rtp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(S->rtp_sock); S->rtp_sock = -1;
        return ESP_FAIL;
    }
    socklen_t alen = sizeof(addr);
    getsockname(S->rtp_sock, (struct sockaddr *)&addr, &alen);
    S->server_rtp_port  = ntohs(addr.sin_port);
    S->server_rtcp_port = S->server_rtp_port + 1;
    return ESP_OK;
}

static void reply_setup(rtsp_session_t *S, int cseq, const char *transport) {
    const char *cp = strstr(transport, "client_port=");
    if (cp) {
        cp += strlen("client_port=");
        S->client_rtp_port = atoi(cp);
        const char *dash = strchr(cp, '-');
        S->client_rtcp_port = dash ? atoi(dash + 1) : S->client_rtp_port + 1;
    }

    if (setup_rtp_socket(S) != ESP_OK) {
        char r[128];
        snprintf(r, sizeof(r),
            "RTSP/1.0 500 Internal Server Error\r\nCSeq: %d\r\n\r\n", cseq);
        send_resp(S->ctrl_sock, r);
        return;
    }

    S->client_rtp.sin_family = AF_INET;
    S->client_rtp.sin_port   = htons(S->client_rtp_port);
    inet_aton(S->client_ip, &S->client_rtp.sin_addr);

    S->session_id = esp_random();
    S->ssrc       = esp_random();
    S->seq        = (uint16_t)esp_random();
    S->timestamp  = 0;

    char r[384];
    snprintf(r, sizeof(r),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Transport: RTP/AVP;unicast;client_port=%u-%u;server_port=%u-%u\r\n"
        "Session: %u\r\n"
        "\r\n",
        cseq, S->client_rtp_port, S->client_rtcp_port,
        S->server_rtp_port, S->server_rtcp_port,
        (unsigned)S->session_id);
    send_resp(S->ctrl_sock, r);
}

static void reply_play(rtsp_session_t *S, int cseq) {
    char r[256];
    snprintf(r, sizeof(r),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Session: %u\r\n"
        "Range: npt=0.000-\r\n"
        "\r\n", cseq, (unsigned)S->session_id);
    send_resp(S->ctrl_sock, r);
    S->playing = true;
}

static void reply_teardown(rtsp_session_t *S, int cseq) {
    char r[256];
    snprintf(r, sizeof(r),
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %d\r\n"
        "Session: %u\r\n"
        "\r\n", cseq, (unsigned)S->session_id);
    send_resp(S->ctrl_sock, r);
    S->playing = false;
}

static void streamer_task(void *arg) {
    rtsp_session_t *S = arg;
    ESP_LOGI(TAG, "streaming to %s:%u", S->client_ip, S->client_rtp_port);
    int64_t last_log = esp_timer_get_time();
    uint32_t frames = 0;
    udp_sink_ctx_t sctx = { .sock = S->rtp_sock, .dst = &S->client_rtp };

    while (S->playing && !S->shutdown) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        if (fb->format == PIXFORMAT_JPEG) {
            rtp_mjpeg_send_frame(udp_sink, &sctx, fb->buf, fb->len,
                                  &S->seq, S->timestamp, S->ssrc);
            S->timestamp += 3000;
            frames++;
        }
        esp_camera_fb_return(fb);

        int64_t now = esp_timer_get_time();
        if (now - last_log >= 5LL * 1000 * 1000) {
            ESP_LOGI(TAG, "rtp: %.1f fps", frames * 1e6f / (now - last_log));
            frames = 0;
            last_log = now;
        }
    }
    ESP_LOGI(TAG, "streamer exit");
    xSemaphoreGive(S->streamer_done);
    vTaskDelete(NULL);
}

static void client_task(void *arg) {
    rtsp_session_t *S = arg;
    char buf[1024];
    TaskHandle_t streamer = NULL;

    while (!S->shutdown) {
        int n = recv(S->ctrl_sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';

        char method[16] = {0}, url[256] = {0};
        sscanf(buf, "%15s %255s", method, url);
        int cseq = cseq_of(buf);

        if (!strcasecmp(method, "OPTIONS")) {
            reply_options(S->ctrl_sock, cseq);
        } else if (!strcasecmp(method, "DESCRIBE")) {
            reply_describe(S->ctrl_sock, cseq, url);
        } else if (!strcasecmp(method, "SETUP")) {
            char tr[256] = {0};
            find_header(buf, "Transport", tr, sizeof(tr));
            reply_setup(S, cseq, tr);
        } else if (!strcasecmp(method, "PLAY")) {
            reply_play(S, cseq);
            if (!streamer) {
                xTaskCreate(streamer_task, "rtsp_strm", 6144, S, 5, &streamer);
            }
        } else if (!strcasecmp(method, "TEARDOWN")) {
            reply_teardown(S, cseq);
            break;
        } else {
            char r[128];
            snprintf(r, sizeof(r),
                "RTSP/1.0 501 Not Implemented\r\nCSeq: %d\r\n\r\n", cseq);
            send_resp(S->ctrl_sock, r);
        }
    }

    S->playing = false;
    S->shutdown = true;
    if (streamer) xSemaphoreTake(S->streamer_done, portMAX_DELAY);

    if (S->rtp_sock >= 0) close(S->rtp_sock);
    close(S->ctrl_sock);
    vSemaphoreDelete(S->streamer_done);
    ESP_LOGI(TAG, "client %s disconnected", S->client_ip);
    free(S);
    vTaskDelete(NULL);
}

static void server_task(void *arg) {
    uint16_t port = (uint16_t)(uintptr_t)arg;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind :%u failed", port);
        vTaskDelete(NULL);
    }
    listen(srv, 2);
    ESP_LOGI(TAG, "RTSP listening on :%u  (rtsp://<ip>:%u%s)",
             port, port, STREAM_PATH);

    while (true) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int cs = accept(srv, (struct sockaddr *)&peer, &plen);
        if (cs < 0) continue;

        rtsp_session_t *S = calloc(1, sizeof(*S));
        if (!S) { close(cs); continue; }
        S->ctrl_sock = cs;
        S->rtp_sock  = -1;
        S->streamer_done = xSemaphoreCreateBinary();
        inet_ntoa_r(peer.sin_addr, S->client_ip, sizeof(S->client_ip));
        ESP_LOGI(TAG, "client %s connected", S->client_ip);

        if (xTaskCreate(client_task, "rtsp_cli", 6144, S, 5, NULL) != pdPASS) {
            close(cs);
            vSemaphoreDelete(S->streamer_done);
            free(S);
        }
    }
}

esp_err_t rtsp_server_start(uint16_t port) {
    if (xTaskCreate(server_task, "rtsp_srv", 4096,
                    (void *)(uintptr_t)port, 5, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
