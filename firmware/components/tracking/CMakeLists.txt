# Tracking Component (M02)
# Kalman filter-based multi-target tracking (RS-1 Pro only)

idf_component_register(
    SRCS
        "tracking.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        radar_ingest
        timebase
)
