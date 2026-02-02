# Zone Editor Component (M11)
# REST API and WebSocket streaming for zone configuration

idf_component_register(
    SRCS
        "zone_editor.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        config_store
        zone_engine
        timebase
    PRIV_REQUIRES
        esp_http_server
        json
        esp_timer
)
