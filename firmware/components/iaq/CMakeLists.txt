# IAQ Component (M12)
# Indoor Air Quality module with ENS160 sensor support

idf_component_register(
    SRCS
        "iaq.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        timebase
    PRIV_REQUIRES
        driver
        nvs_flash
        esp_timer
)
