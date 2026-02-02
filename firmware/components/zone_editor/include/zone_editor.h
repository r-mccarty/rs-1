/**
 * @file zone_editor.h
 * @brief HardwareOS Zone Editor Interface Module (M11)
 *
 * Provides REST API for zone configuration, WebSocket streaming of live
 * targets, and coordinate conversion between meters (API) and mm (internal).
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_ZONE_EDITOR.md
 */

#ifndef ZONE_EDITOR_H
#define ZONE_EDITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define ZONE_EDITOR_HTTP_PORT           80
#define ZONE_EDITOR_MAX_CLIENTS         4
#define ZONE_EDITOR_MAX_ZONES           16
#define ZONE_EDITOR_MAX_VERTICES        8
#define ZONE_EDITOR_MAX_NAME_LEN        32
#define ZONE_EDITOR_MAX_ID_LEN          32

/* Target streaming */
#define ZONE_EDITOR_STREAM_RATE_HZ      10
#define ZONE_EDITOR_MAX_TARGETS         3
#define ZONE_EDITOR_WS_BUFFER_SIZE      512

/* Coordinate limits (mm) */
#define ZONE_EDITOR_MAX_RANGE_MM        6000
#define ZONE_EDITOR_MIN_RANGE_MM        -6000

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Zone type
 */
typedef enum {
    ZONE_TYPE_INCLUDE = 0,      /**< Include zone (normal detection) */
    ZONE_TYPE_EXCLUDE,          /**< Exclude zone (ignore targets) */
} zone_type_t;

/**
 * @brief Vertex (mm coordinates)
 */
typedef struct {
    int16_t x;
    int16_t y;
} zone_vertex_t;

/**
 * @brief Zone definition
 */
typedef struct {
    char id[ZONE_EDITOR_MAX_ID_LEN];
    char name[ZONE_EDITOR_MAX_NAME_LEN];
    zone_type_t type;
    zone_vertex_t vertices[ZONE_EDITOR_MAX_VERTICES];
    uint8_t vertex_count;
    uint8_t sensitivity;        /**< 0-100, default 50 */
} zone_def_t;

/**
 * @brief Zone configuration
 */
typedef struct {
    uint32_t version;           /**< Config version for optimistic locking */
    char updated_at[32];        /**< ISO 8601 timestamp */
    zone_def_t zones[ZONE_EDITOR_MAX_ZONES];
    uint8_t zone_count;
} zone_config_t;

/**
 * @brief Target position for streaming
 */
typedef struct {
    int16_t x;                  /**< X position (mm) */
    int16_t y;                  /**< Y position (mm) */
    int16_t vx;                 /**< X velocity (mm/s) */
    int16_t vy;                 /**< Y velocity (mm/s) */
    uint8_t confidence;         /**< 0-100 */
    uint8_t track_id;           /**< Track identifier */
    bool active;                /**< Target is active */
} stream_target_t;

/**
 * @brief Target stream frame
 */
typedef struct {
    uint32_t timestamp_ms;
    stream_target_t targets[ZONE_EDITOR_MAX_TARGETS];
    uint8_t target_count;
    uint8_t frame_seq;          /**< Sequence number for drop detection */
} target_frame_t;

/**
 * @brief Zone editor configuration
 */
typedef struct {
    uint16_t http_port;         /**< HTTP server port */
    uint8_t max_clients;        /**< Max WebSocket clients */
    uint8_t stream_rate_hz;     /**< Target stream rate */
    bool require_auth;          /**< Require pairing token */
} zone_editor_config_t;

/**
 * @brief Default configuration
 */
#define ZONE_EDITOR_CONFIG_DEFAULT() { \
    .http_port = ZONE_EDITOR_HTTP_PORT, \
    .max_clients = ZONE_EDITOR_MAX_CLIENTS, \
    .stream_rate_hz = ZONE_EDITOR_STREAM_RATE_HZ, \
    .require_auth = true, \
}

/**
 * @brief Zone editor event types
 */
typedef enum {
    ZONE_EDITOR_EVENT_CLIENT_CONNECTED = 0,
    ZONE_EDITOR_EVENT_CLIENT_DISCONNECTED,
    ZONE_EDITOR_EVENT_CONFIG_UPDATED,
    ZONE_EDITOR_EVENT_CONFIG_REJECTED,
    ZONE_EDITOR_EVENT_STREAM_STARTED,
    ZONE_EDITOR_EVENT_STREAM_STOPPED,
} zone_editor_event_t;

/**
 * @brief Event callback
 */
typedef void (*zone_editor_event_cb_t)(zone_editor_event_t event, void *user_data);

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize zone editor
 *
 * @param config Configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t zone_editor_init(const zone_editor_config_t *config);

/**
 * @brief Deinitialize zone editor
 */
void zone_editor_deinit(void);

/**
 * @brief Set event callback
 *
 * @param callback Callback function
 * @param user_data User data
 */
void zone_editor_set_callback(zone_editor_event_cb_t callback, void *user_data);

/**
 * @brief Start HTTP/WebSocket server
 *
 * @return ESP_OK on success
 */
esp_err_t zone_editor_start(void);

/**
 * @brief Stop HTTP/WebSocket server
 */
void zone_editor_stop(void);

/* ============================================================================
 * Zone Configuration
 * ============================================================================ */

/**
 * @brief Get current zone configuration
 *
 * @param config Output configuration
 * @return ESP_OK on success
 */
esp_err_t zone_editor_get_config(zone_config_t *config);

/**
 * @brief Set zone configuration
 *
 * Validates and stores the configuration. Returns error if:
 * - Version doesn't match (optimistic lock failure)
 * - Polygon validation fails
 * - Zone count exceeds limit
 *
 * @param config New configuration
 * @param expected_version Expected current version (0 to skip check)
 * @return ESP_OK on success
 */
esp_err_t zone_editor_set_config(const zone_config_t *config,
                                  uint32_t expected_version);

/**
 * @brief Get config version
 *
 * @return Current config version
 */
uint32_t zone_editor_get_version(void);

/* ============================================================================
 * Target Streaming
 * ============================================================================ */

/**
 * @brief Update targets for streaming
 *
 * Called by tracking module to provide latest target positions.
 *
 * @param frame Target frame to stream
 */
void zone_editor_update_targets(const target_frame_t *frame);

/**
 * @brief Get connected client count
 *
 * @return Number of connected WebSocket clients
 */
int zone_editor_get_client_count(void);

/**
 * @brief Check if streaming is active
 *
 * @return True if any clients connected
 */
bool zone_editor_is_streaming(void);

/* ============================================================================
 * Coordinate Conversion
 * ============================================================================ */

/**
 * @brief Convert meters to mm
 *
 * @param meters Value in meters
 * @return Value in millimeters
 */
int16_t zone_editor_meters_to_mm(float meters);

/**
 * @brief Convert mm to meters
 *
 * @param mm Value in millimeters
 * @return Value in meters
 */
float zone_editor_mm_to_meters(int16_t mm);

/**
 * @brief Convert zone config from meters to mm
 *
 * Used when receiving config from API (meters → mm for storage).
 *
 * @param config Zone config with meter coordinates (modified in place)
 */
void zone_editor_config_to_mm(zone_config_t *config);

/**
 * @brief Convert zone config from mm to meters
 *
 * Used when sending config to API (mm → meters for display).
 * Returns a copy with float coordinates.
 *
 * @param config Zone config with mm coordinates
 * @param json_out Output JSON buffer
 * @param json_len Buffer length
 * @return Length of JSON output
 */
int zone_editor_config_to_json(const zone_config_t *config,
                                char *json_out, size_t json_len);

/* ============================================================================
 * Validation
 * ============================================================================ */

/**
 * @brief Validation error codes
 */
typedef enum {
    ZONE_VALID_OK = 0,
    ZONE_VALID_TOO_FEW_VERTICES,
    ZONE_VALID_TOO_MANY_VERTICES,
    ZONE_VALID_SELF_INTERSECTING,
    ZONE_VALID_OUT_OF_RANGE,
    ZONE_VALID_DUPLICATE_ID,
    ZONE_VALID_INVALID_NAME,
    ZONE_VALID_TOO_MANY_ZONES,
    ZONE_VALID_VERSION_MISMATCH,
} zone_validation_t;

/**
 * @brief Validate zone configuration
 *
 * @param config Configuration to validate
 * @param error_zone Index of invalid zone (if error)
 * @return Validation result
 */
zone_validation_t zone_editor_validate(const zone_config_t *config,
                                        int *error_zone);

/**
 * @brief Get validation error string
 *
 * @param error Validation error code
 * @return Human-readable error message
 */
const char *zone_editor_validation_str(zone_validation_t error);

/* ============================================================================
 * Authentication
 * ============================================================================ */

/**
 * @brief Set pairing token for authentication
 *
 * @param token Pairing token (NULL to disable auth)
 */
void zone_editor_set_auth_token(const char *token);

/**
 * @brief Validate authorization header
 *
 * @param auth_header Authorization header value
 * @return True if authorized
 */
bool zone_editor_check_auth(const char *auth_header);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Zone editor statistics
 */
typedef struct {
    uint32_t requests_total;
    uint32_t requests_success;
    uint32_t requests_auth_failed;
    uint32_t config_updates;
    uint32_t config_rejections;
    uint32_t ws_frames_sent;
    uint32_t ws_frames_dropped;
    uint32_t clients_connected;
} zone_editor_stats_t;

/**
 * @brief Get statistics
 *
 * @param stats Output statistics
 */
void zone_editor_get_stats(zone_editor_stats_t *stats);

/**
 * @brief Reset statistics
 */
void zone_editor_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* ZONE_EDITOR_H */
