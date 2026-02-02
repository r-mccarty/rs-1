/**
 * @file zone_engine.h
 * @brief HardwareOS Zone Engine Module (M03)
 *
 * Maps confirmed tracks from M02 to user-defined polygon zones
 * and emits per-zone occupancy states.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md
 *
 * Note: This module is only active in RS-1 Pro variant.
 * RS-1 Lite bypasses zone engine (pipeline: M01→M04→M05).
 */

#ifndef ZONE_ENGINE_H
#define ZONE_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "tracking.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define ZONE_MAX_ZONES              16      /* Maximum zones supported */
#define ZONE_MAX_VERTICES           8       /* Maximum vertices per zone */
#define ZONE_MAX_TRACKS_PER_ZONE    3       /* LD2450 hardware limit */
#define ZONE_ID_MAX_LEN             16      /* Zone ID string length */
#define ZONE_NAME_MAX_LEN           32      /* Zone display name length */

#define ZONE_MOVING_THRESHOLD_CM_S  10      /* Speed threshold for "moving" */
#define ZONE_DEBOUNCE_FRAMES        2       /* Frames before emitting change */
#define ZONE_EDGE_MARGIN_MM         1       /* Margin for edge inclusion */

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Zone type
 */
typedef enum {
    ZONE_TYPE_INCLUDE = 0,  /**< Track inside → zone occupied */
    ZONE_TYPE_EXCLUDE       /**< Track inside → ignored */
} zone_type_t;

/**
 * @brief Zone configuration
 */
typedef struct {
    char id[ZONE_ID_MAX_LEN];           /**< Zone identifier (e.g., "zone_living") */
    char name[ZONE_NAME_MAX_LEN];       /**< Display name (e.g., "Living Room") */
    zone_type_t type;                   /**< Include or exclude zone */
    int16_t vertices[ZONE_MAX_VERTICES][2]; /**< Polygon vertices (x, y) in mm */
    uint8_t vertex_count;               /**< Number of vertices (3-8) */
    uint8_t sensitivity;                /**< Sensitivity preset (0-100) */
} zone_config_t;

/**
 * @brief Zone map (all zone configurations)
 */
typedef struct {
    zone_config_t zones[ZONE_MAX_ZONES];    /**< Zone definitions */
    uint8_t zone_count;                     /**< Number of defined zones */
    uint32_t version;                       /**< Config version for sync */
} zone_map_t;

/**
 * @brief Zone occupancy state
 */
typedef struct {
    char zone_id[ZONE_ID_MAX_LEN];      /**< Zone identifier */
    bool occupied;                       /**< Any track in zone */
    uint8_t target_count;               /**< Number of tracks in zone (0-3) */
    uint8_t track_ids[ZONE_MAX_TRACKS_PER_ZONE]; /**< IDs of tracks in zone */
    bool has_moving;                    /**< Any track moving in zone */
    uint32_t last_change_ms;            /**< Timestamp of last occupancy change */
} zone_state_t;

/**
 * @brief Zone frame output (for M04 consumption)
 */
typedef struct {
    zone_state_t states[ZONE_MAX_ZONES];    /**< Per-zone states */
    uint8_t zone_count;                     /**< Number of zones */
    uint32_t timestamp_ms;                  /**< Frame timestamp */
} zone_frame_t;

/**
 * @brief Zone event type
 */
typedef enum {
    ZONE_EVENT_ENTER = 0,   /**< Track entered zone */
    ZONE_EVENT_EXIT,        /**< Track left zone */
    ZONE_EVENT_OCCUPIED,    /**< Zone transitioned to occupied */
    ZONE_EVENT_VACANT       /**< Zone transitioned to vacant */
} zone_event_type_t;

/**
 * @brief Zone event
 */
typedef struct {
    zone_event_type_t type;             /**< Event type */
    char zone_id[ZONE_ID_MAX_LEN];      /**< Zone identifier */
    uint8_t track_id;                   /**< Track ID (for ENTER/EXIT events) */
    uint32_t timestamp_ms;              /**< Event timestamp */
} zone_event_t;

/**
 * @brief Zone event callback type
 */
typedef void (*zone_event_callback_t)(const zone_event_t *event, void *user_data);

/**
 * @brief Zone engine configuration
 */
typedef struct {
    uint16_t moving_threshold_cm_s;     /**< Speed threshold for "moving" */
    uint8_t debounce_frames;            /**< Frames before emitting change */
    zone_event_callback_t event_callback; /**< Event callback (may be NULL) */
    void *callback_user_data;           /**< User data for callback */
} zone_engine_config_t;

/**
 * @brief Default configuration initializer
 */
#define ZONE_ENGINE_CONFIG_DEFAULT() { \
    .moving_threshold_cm_s = ZONE_MOVING_THRESHOLD_CM_S, \
    .debounce_frames = ZONE_DEBOUNCE_FRAMES, \
    .event_callback = NULL, \
    .callback_user_data = NULL, \
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize the zone engine module
 *
 * @param config Configuration parameters (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t zone_engine_init(const zone_engine_config_t *config);

/**
 * @brief Deinitialize the zone engine module
 */
void zone_engine_deinit(void);

/**
 * @brief Reset zone engine state (clear all occupancy)
 */
void zone_engine_reset(void);

/* ============================================================================
 * Zone Configuration
 * ============================================================================ */

/**
 * @brief Load zone map (replaces all zones atomically)
 *
 * @param zone_map Zone map to load
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if validation fails
 */
esp_err_t zone_engine_load_zones(const zone_map_t *zone_map);

/**
 * @brief Get current zone map
 *
 * @param zone_map Output zone map
 * @return ESP_OK on success
 */
esp_err_t zone_engine_get_zones(zone_map_t *zone_map);

/**
 * @brief Get zone config by ID
 *
 * @param zone_id Zone identifier
 * @param config Output zone config
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t zone_engine_get_zone(const char *zone_id, zone_config_t *config);

/**
 * @brief Validate a zone configuration
 *
 * Checks: vertex count, self-intersection, coordinate bounds.
 *
 * @param config Zone config to validate
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG otherwise
 */
esp_err_t zone_engine_validate_zone(const zone_config_t *config);

/* ============================================================================
 * Main Processing
 * ============================================================================ */

/**
 * @brief Process a track frame
 *
 * Evaluates all tracks against all zones and updates occupancy.
 *
 * @param tracks Input track frame from M02
 * @param output Output zone frame for M04
 * @return ESP_OK on success
 */
esp_err_t zone_engine_process_frame(const track_frame_t *tracks,
                                     zone_frame_t *output);

/**
 * @brief Get current zone state by ID
 *
 * @param zone_id Zone identifier
 * @param state Output zone state
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t zone_engine_get_state(const char *zone_id, zone_state_t *state);

/**
 * @brief Get all zone states
 *
 * @param frame Output zone frame
 * @return ESP_OK on success
 */
esp_err_t zone_engine_get_all_states(zone_frame_t *frame);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Check if a point is inside a polygon
 *
 * Uses ray casting algorithm with O(n) complexity.
 * Points on edges/vertices are considered INSIDE.
 *
 * @param x Point X coordinate (mm)
 * @param y Point Y coordinate (mm)
 * @param vertices Polygon vertices
 * @param vertex_count Number of vertices
 * @return True if point is inside polygon
 */
bool zone_point_in_polygon(int16_t x, int16_t y,
                           const int16_t vertices[][2],
                           uint8_t vertex_count);

/**
 * @brief Check if a polygon is simple (non-self-intersecting)
 *
 * @param vertices Polygon vertices
 * @param vertex_count Number of vertices
 * @return True if polygon is simple
 */
bool zone_is_simple_polygon(const int16_t vertices[][2], uint8_t vertex_count);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Zone engine statistics
 */
typedef struct {
    uint32_t frames_processed;          /**< Total frames processed */
    uint32_t occupancy_changes;         /**< Total occupancy transitions */
    uint32_t tracks_excluded;           /**< Tracks filtered by exclude zones */
    uint32_t processing_time_us;        /**< Last frame processing time */
    uint32_t max_processing_time_us;    /**< Max processing time observed */
    uint32_t zone_evaluations;          /**< Total zone checks performed */
} zone_engine_stats_t;

/**
 * @brief Get zone engine statistics
 *
 * @param stats Output statistics structure
 */
void zone_engine_get_stats(zone_engine_stats_t *stats);

/**
 * @brief Reset zone engine statistics
 */
void zone_engine_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* ZONE_ENGINE_H */
