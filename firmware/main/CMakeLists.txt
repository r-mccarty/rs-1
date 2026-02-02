# Main component CMakeLists.txt
idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_timer
        nvs_flash
        esp_event
        esp_wifi
        freertos
        radar_ingest
)
