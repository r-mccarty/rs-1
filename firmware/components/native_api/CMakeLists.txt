# Native API Component (M05)
# ESPHome Native API-compatible server for Home Assistant

idf_component_register(
    SRCS
        "native_api.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        presence_smoothing
        timebase
        mdns
    PRIV_REQUIRES
        lwip
)
