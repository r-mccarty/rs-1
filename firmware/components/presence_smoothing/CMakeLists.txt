# Presence Smoothing Component (M04)
# Applies hysteresis and hold timers for stable presence output

idf_component_register(
    SRCS
        "presence_smoothing.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        zone_engine
        timebase
)
