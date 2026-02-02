# Timebase Component (M08)
# Provides timing services, frame synchronization, and task scheduling

idf_component_register(
    SRCS
        "timebase.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_timer
        esp_system
        nvs_flash
        freertos
    PRIV_REQUIRES
        esp_sntp
)
