# M01 Radar Ingest Component
# Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md

idf_component_register(
    SRCS
        "radar_ingest.c"
        "ld2450_parser.c"
        "ld2410_parser.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_timer
        freertos
)
