#pragma once

#include <stdint.h>
#include "esp_err.h"

// Starts a tiny RTSP/MJPEG (RFC 2435) server bound to `port`.
// MediaMTX (or VLC/ffplay) can pull the live stream from
//   rtsp://<board-ip>:<port>/cam
esp_err_t rtsp_server_start(uint16_t port);
