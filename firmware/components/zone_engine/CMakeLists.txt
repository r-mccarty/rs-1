# Zone Engine Component (M03)
# Maps tracks to polygon zones (RS-1 Pro only)

idf_component_register(
    SRCS
        "zone_engine.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        tracking
        timebase
)
