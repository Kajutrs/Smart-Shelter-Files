#pragma once

// LilyGO T-SIMCAM (ESP32-S3 + OV2640)
// Source: github.com/Xinyuan-LilyGO/LilyGo-Camera-Series, docs/T_SIMCAM.md
// (NOT the LilyGo-Cam-ESP32S3 repo — that one is for T-Camera-S3.)
// Drives the on-board power switch that feeds the OV2640 rails.
// On T-SIMCAM the camera is dead until this pin is driven HIGH.
#define CAM_PIN_PWR_EN   1

#define CAM_PIN_PWDN    -1
// RESET is GPIO 18 on T-SIMCAM V1.2 and is repurposed as the IR-cut
// filter on V1.3. -1 is safe for both: the OV2640 has internal POR.
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    14
#define CAM_PIN_SIOD     4  // SDA
#define CAM_PIN_SIOC     5  // SCL

#define CAM_PIN_D9      15  // Y9
#define CAM_PIN_D8      16  // Y8
#define CAM_PIN_D7      17  // Y7
#define CAM_PIN_D6      12  // Y6
#define CAM_PIN_D5      10  // Y5
#define CAM_PIN_D4       8  // Y4
#define CAM_PIN_D3       9  // Y3
#define CAM_PIN_D2      11  // Y2

#define CAM_PIN_VSYNC    6
#define CAM_PIN_HREF     7
#define CAM_PIN_PCLK    13
