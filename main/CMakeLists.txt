set(srcs
    "main.c"
    "wifi_sta.c"
    "camera.c"
    "http_server.c"
    "rtp_mjpeg.c"
    "rtsp_server.c"
    "rtsp_publisher.c"
)

if(CONFIG_RELAY_ENABLED)
    list(APPEND srcs "relay.c")
    if(CONFIG_RELAY_AUTO_TEMP AND CONFIG_BP_LINK_ENABLED)
        list(APPEND srcs "thermostat.c")
    endif()
endif()

if(CONFIG_SENSORS_ENABLED)
    list(APPEND srcs "sensor_task.c")
    if(CONFIG_BP_LINK_ENABLED)
        list(APPEND srcs "bp_link.c")
    endif()
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        esp_event
        esp_netif
        esp_wifi
        esp_http_server
        esp_timer
        esp_driver_gpio
        esp_driver_uart
        lwip
        espressif__esp32-camera
)
