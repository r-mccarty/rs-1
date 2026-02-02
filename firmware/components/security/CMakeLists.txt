# Security Component (M10)
# Device authentication, firmware verification, and key management

idf_component_register(
    SRCS
        "security.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        timebase
    PRIV_REQUIRES
        mbedtls
        esp_hw_support
)
