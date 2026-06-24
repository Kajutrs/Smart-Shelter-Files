#pragma once

#include "esp_err.h"

// Launches a background task that publishes the local camera as MJPEG over
// RTSP (TCP interleaved transport, mode=record) to a remote MediaMTX server.
// Config (host/port/path) is taken from Kconfig:
//   CONFIG_MTX_HOST, CONFIG_MTX_PORT, CONFIG_MTX_PATH
// The task reconnects on failure with backoff and runs forever.
esp_err_t rtsp_publisher_start(void);
