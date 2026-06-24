#include "rtp_mjpeg.h"

#include <string.h>

#define RTP_HDR_SZ        12
#define MJPEG_HDR_SZ       8
#define MJPEG_QTHDR_SZ     4
#define MAX_RTP_PAYLOAD  1400

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  type;          // 0 = YUV 4:2:2, 1 = YUV 4:2:0
    bool     has_qtables;
    uint8_t  qtables[128];  // luma (0..63) + chroma (64..127)
    const uint8_t *scan;
    size_t   scan_len;
} jpeg_info_t;

static bool parse_jpeg(const uint8_t *buf, size_t len, jpeg_info_t *info) {
    memset(info, 0, sizeof(*info));
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) return false;

    size_t i = 2;
    while (i + 1 < len) {
        if (buf[i] != 0xFF) return false;
        uint8_t marker = buf[i + 1];
        i += 2;
        if (marker == 0xD8 || marker == 0xD9 ||
            (marker >= 0xD0 && marker <= 0xD7)) continue;

        if (i + 1 >= len) return false;
        size_t seg_len = ((size_t)buf[i] << 8) | buf[i + 1];
        if (seg_len < 2 || i + seg_len > len) return false;

        if (marker == 0xDB) {                         // DQT
            size_t p = i + 2, end = i + seg_len;
            while (p < end) {
                uint8_t pq_tq = buf[p++];
                uint8_t precision = pq_tq >> 4;
                uint8_t tq = pq_tq & 0xF;
                if (precision != 0 || tq >= 2 || p + 64 > end) return false;
                memcpy(info->qtables + tq * 64, buf + p, 64);
                info->has_qtables = true;
                p += 64;
            }
        } else if (marker == 0xC0) {                  // SOF0 baseline
            if (seg_len < 8) return false;
            info->height = ((uint16_t)buf[i + 3] << 8) | buf[i + 4];
            info->width  = ((uint16_t)buf[i + 5] << 8) | buf[i + 6];
            uint8_t nc = buf[i + 7];
            if (nc >= 1 && i + 8 + 3 <= len) {
                uint8_t y_hv = buf[i + 8 + 1];
                info->type = (y_hv == 0x22) ? 1 : 0;
            }
        } else if (marker == 0xDA) {                  // SOS
            size_t scan_start = i + seg_len;
            size_t scan_end = len;
            if (scan_end >= 2 && buf[scan_end - 2] == 0xFF &&
                buf[scan_end - 1] == 0xD9) {
                scan_end -= 2;
            }
            if (scan_end < scan_start) return false;
            info->scan = buf + scan_start;
            info->scan_len = scan_end - scan_start;
            return true;
        }
        i += seg_len;
    }
    return false;
}

int rtp_mjpeg_send_frame(rtp_sink_fn sink, void *ctx,
                         const uint8_t *jpeg, size_t jpeg_len,
                         uint16_t *seq, uint32_t ts, uint32_t ssrc) {
    jpeg_info_t ji;
    if (!parse_jpeg(jpeg, jpeg_len, &ji)) return -1;
    if (ji.width == 0 || ji.height == 0 ||
        ji.width > 2040 || ji.height > 2040) return -1;

    const uint8_t q = ji.has_qtables ? 255 : 90;
    uint8_t pkt[1500];
    size_t offset = 0;
    size_t remaining = ji.scan_len;
    bool first = true;

    while (remaining > 0) {
        size_t hdr_total = RTP_HDR_SZ + MJPEG_HDR_SZ;
        if (first && q >= 128) hdr_total += MJPEG_QTHDR_SZ + 128;
        size_t max_payload = MAX_RTP_PAYLOAD - hdr_total;
        size_t chunk = remaining > max_payload ? max_payload : remaining;
        bool last = (chunk == remaining);

        pkt[0]  = 0x80;
        pkt[1]  = (last ? 0x80 : 0x00) | RTP_PT_JPEG;
        pkt[2]  = (*seq >> 8) & 0xFF;
        pkt[3]  = (*seq)      & 0xFF;
        pkt[4]  = (ts >> 24)  & 0xFF;
        pkt[5]  = (ts >> 16)  & 0xFF;
        pkt[6]  = (ts >> 8)   & 0xFF;
        pkt[7]  = (ts)        & 0xFF;
        pkt[8]  = (ssrc >> 24) & 0xFF;
        pkt[9]  = (ssrc >> 16) & 0xFF;
        pkt[10] = (ssrc >> 8)  & 0xFF;
        pkt[11] = (ssrc)       & 0xFF;

        pkt[12] = 0;
        pkt[13] = (offset >> 16) & 0xFF;
        pkt[14] = (offset >> 8)  & 0xFF;
        pkt[15] = (offset)       & 0xFF;
        pkt[16] = ji.type;
        pkt[17] = q;
        pkt[18] = ji.width  / 8;
        pkt[19] = ji.height / 8;

        size_t p = 20;
        if (first && q >= 128) {
            pkt[p++] = 0;
            pkt[p++] = 0;
            pkt[p++] = 0;
            pkt[p++] = 128;
            memcpy(pkt + p, ji.qtables, 128);
            p += 128;
        }
        memcpy(pkt + p, ji.scan + offset, chunk);
        p += chunk;

        if (sink(ctx, pkt, p) < 0) return -1;

        (*seq)++;
        offset    += chunk;
        remaining -= chunk;
        first = false;
    }
    return 0;
}
