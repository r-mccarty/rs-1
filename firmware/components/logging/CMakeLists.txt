# Logging Component (M09)
# Diagnostics, telemetry, and system logging

idf_component_register(
    SRCS
        "logging.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        security
        timebase
    PRIV_REQUIRES
        json
        esp_wifi
        esp_timer
)
