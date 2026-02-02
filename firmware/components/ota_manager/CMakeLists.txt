# OTA Manager Component (M07)
# Firmware updates via MQTT trigger and HTTPS download

idf_component_register(
    SRCS
        "ota_manager.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        security
        timebase
    PRIV_REQUIRES
        esp_https_ota
        esp_http_client
        app_update
        json
        mbedtls
        esp_wifi
)
