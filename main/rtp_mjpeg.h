#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RTP_PT_JPEG    26
#define RTP_CLOCK_HZ   90000

// Sink callback. Implementations send `pkt[0..len)` to wherever they like
// (UDP socket, TCP interleaved, etc.). Return < 0 on error.
typedef int (*rtp_sink_fn)(void *ctx, const uint8_t *pkt, size_t len);

// Parse `jpeg` (a single JPEG frame), packetize into RFC 2435 RTP/MJPEG
// packets and push each to `sink(ctx, ...)`. Increments *seq for each packet.
// Returns 0 on success, < 0 if the JPEG is unparsable or the sink errored.
int rtp_mjpeg_send_frame(rtp_sink_fn sink, void *ctx,
                         const uint8_t *jpeg, size_t jpeg_len,
                         uint16_t *seq, uint32_t ts, uint32_t ssrc);
