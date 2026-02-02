/**
 * @file native_api.h
 * @brief HardwareOS Native API Server Module (M05)
 *
 * Implements ESPHome Native API-compatible server for Home Assistant integration.
 * Exposes presence entities with auto-discovery via mDNS.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md
 */

#ifndef NATIVE_API_H
#define NATIVE_API_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "presence_smoothing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define NATIVE_API_PORT                 6053
#define NATIVE_API_MAX_ENTITIES         50
#define NATIVE_API_MAX_CONNECTIONS      1       /* MVP: single connection */
#define NATIVE_API_STATE_THROTTLE_MS    100     /* 10 Hz max */

#define NATIVE_API_OBJECT_ID_LEN        32
#define NATIVE_API_NAME_LEN             48
#define NATIVE_API_DEVICE_CLASS_LEN     16
#define NATIVE_API_UNIT_LEN             8
#define NATIVE_API_ICON_LEN             24

/* ============================================================================
 * Entity Types
 * ============================================================================ */

/**
 * @brief Entity type enumeration
 */
typedef enum {
    ENTITY_TYPE_BINARY_SENSOR = 0,
    ENTITY_TYPE_SENSOR,
    ENTITY_TYPE_TEXT_SENSOR,
    ENTITY_TYPE_SWITCH,
    ENTITY_TYPE_BUTTON,
} entity_type_t;

/**
 * @brief Entity definition
 */
typedef struct {
    uint32_t key;                               /**< Unique entity key (hash) */
    entity_type_t type;                         /**< Entity type */
    char object_id[NATIVE_API_OBJECT_ID_LEN];   /**< ESPHome object_id */
    char name[NATIVE_API_NAME_LEN];             /**< Human-readable name */
    char device_class[NATIVE_API_DEVICE_CLASS_LEN]; /**< HA device class */
    char unit[NATIVE_API_UNIT_LEN];             /**< Unit of measurement */
    char icon[NATIVE_API_ICON_LEN];             /**< MDI icon */
    bool enabled;                               /**< Entity enabled */
} entity_def_t;

/* ============================================================================
 * Connection State
 * ============================================================================ */

/**
 * @brief Connection state
 */
typedef enum {
    CONN_STATE_DISCONNECTED = 0,
    CONN_STATE_CONNECTED,
    CONN_STATE_AUTHENTICATED,
    CONN_STATE_SUBSCRIBED,
} conn_state_t;

/**
 * @brief Connection info
 */
typedef struct {
    conn_state_t state;
    uint32_t connected_at_ms;
    uint32_t last_activity_ms;
    uint32_t messages_sent;
    uint32_t messages_received;
    char client_info[32];
} connection_info_t;

/* ============================================================================
 * Device Info
 * ============================================================================ */

/**
 * @brief Device info for Native API
 */
typedef struct {
    char name[32];                  /**< Device name (mDNS) */
    char friendly_name[48];         /**< Display name */
    char mac_address[18];           /**< MAC address string */
    char model[16];                 /**< Model (RS-1) */
    char manufacturer[16];          /**< Manufacturer */
    char firmware_version[16];      /**< Firmware version */
    char project_name[32];          /**< Project name */
    char project_version[16];       /**< Project version */
} native_api_device_info_t;

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief Native API configuration
 */
typedef struct {
    uint16_t port;                      /**< TCP port (default 6053) */
    char api_password[33];              /**< Legacy password (optional) */
    uint8_t encryption_key[32];         /**< Noise PSK (optional) */
    bool encryption_enabled;            /**< Require encryption */
    uint32_t reboot_timeout_ms;         /**< Reboot if no connection (0=disabled) */
    uint16_t state_throttle_ms;         /**< Min interval between updates */
} native_api_config_t;

/**
 * @brief Default configuration initializer
 */
#define NATIVE_API_CONFIG_DEFAULT() { \
    .port = NATIVE_API_PORT, \
    .api_password = "", \
    .encryption_key = {0}, \
    .encryption_enabled = false, \
    .reboot_timeout_ms = 0, \
    .state_throttle_ms = NATIVE_API_STATE_THROTTLE_MS, \
}

/* ============================================================================
 * Callbacks
 * ============================================================================ */

/**
 * @brief Connection event callback
 */
typedef void (*native_api_conn_callback_t)(conn_state_t state, void *user_data);

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize the Native API module
 *
 * @param config Configuration (NULL for defaults)
 * @param device_info Device information
 * @return ESP_OK on success
 */
esp_err_t native_api_init(const native_api_config_t *config,
                           const native_api_device_info_t *device_info);

/**
 * @brief Deinitialize the Native API module
 */
void native_api_deinit(void);

/**
 * @brief Start the API server
 *
 * Starts TCP listener and mDNS advertisement.
 *
 * @return ESP_OK on success
 */
esp_err_t native_api_start(void);

/**
 * @brief Stop the API server
 */
void native_api_stop(void);

/**
 * @brief Check if server is running
 *
 * @return True if server is running
 */
bool native_api_is_running(void);

/* ============================================================================
 * Entity Management
 * ============================================================================ */

/**
 * @brief Register a binary sensor entity
 *
 * @param object_id Unique object ID
 * @param name Display name
 * @param device_class HA device class (e.g., "occupancy", "motion")
 * @param icon MDI icon (e.g., "mdi:motion-sensor")
 * @return Entity key, or 0 on failure
 */
uint32_t native_api_register_binary_sensor(const char *object_id,
                                            const char *name,
                                            const char *device_class,
                                            const char *icon);

/**
 * @brief Register a sensor entity
 *
 * @param object_id Unique object ID
 * @param name Display name
 * @param device_class HA device class (e.g., "signal_strength")
 * @param unit Unit of measurement (e.g., "dBm")
 * @param icon MDI icon
 * @return Entity key, or 0 on failure
 */
uint32_t native_api_register_sensor(const char *object_id,
                                     const char *name,
                                     const char *device_class,
                                     const char *unit,
                                     const char *icon);

/**
 * @brief Unregister an entity
 *
 * @param key Entity key
 * @return ESP_OK on success
 */
esp_err_t native_api_unregister_entity(uint32_t key);

/**
 * @brief Clear all entities
 */
void native_api_clear_entities(void);

/**
 * @brief Get entity count
 *
 * @return Number of registered entities
 */
uint8_t native_api_get_entity_count(void);

/* ============================================================================
 * State Publishing
 * ============================================================================ */

/**
 * @brief Publish binary sensor state
 *
 * @param key Entity key
 * @param state Binary state
 * @return ESP_OK on success
 */
esp_err_t native_api_publish_binary_state(uint32_t key, bool state);

/**
 * @brief Publish sensor state
 *
 * @param key Entity key
 * @param value Sensor value
 * @return ESP_OK on success
 */
esp_err_t native_api_publish_sensor_state(uint32_t key, float value);

/**
 * @brief Publish zone states from smoothed frame
 *
 * Convenience function to publish all zone occupancy/count states.
 *
 * @param frame Smoothed presence frame from M04
 * @return ESP_OK on success
 */
esp_err_t native_api_publish_zones(const smoothed_frame_t *frame);

/**
 * @brief Force publish all entity states
 *
 * Used when a client first subscribes.
 */
void native_api_publish_all_states(void);

/* ============================================================================
 * Zone Entity Registration
 * ============================================================================ */

/**
 * @brief Register entities for a zone
 *
 * Creates binary_sensor (occupancy) and sensor (target_count) entities.
 *
 * @param zone_id Zone identifier
 * @param zone_name Zone display name
 * @param occupancy_key Output: occupancy entity key
 * @param count_key Output: target count entity key
 * @return ESP_OK on success
 */
esp_err_t native_api_register_zone(const char *zone_id,
                                    const char *zone_name,
                                    uint32_t *occupancy_key,
                                    uint32_t *count_key);

/* ============================================================================
 * Connection Management
 * ============================================================================ */

/**
 * @brief Set connection callback
 *
 * @param callback Callback function
 * @param user_data User data for callback
 */
void native_api_set_connection_callback(native_api_conn_callback_t callback,
                                         void *user_data);

/**
 * @brief Get connection info
 *
 * @param info Output connection info
 * @return ESP_OK if connected, ESP_ERR_NOT_FOUND if no connection
 */
esp_err_t native_api_get_connection_info(connection_info_t *info);

/**
 * @brief Check if a client is connected and subscribed
 *
 * @return True if client is subscribed to states
 */
bool native_api_has_subscriber(void);

/**
 * @brief Disconnect current client
 */
void native_api_disconnect_client(void);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Native API statistics
 */
typedef struct {
    uint32_t connections_total;         /**< Total connections since boot */
    uint32_t auth_failures;             /**< Failed auth attempts */
    uint32_t messages_sent;             /**< Outbound messages */
    uint32_t messages_received;         /**< Inbound messages */
    uint32_t state_updates;             /**< State updates sent */
    uint32_t state_updates_throttled;   /**< Updates skipped (throttle) */
    uint32_t uptime_ms;                 /**< Server uptime */
} native_api_stats_t;

/**
 * @brief Get statistics
 *
 * @param stats Output statistics
 */
void native_api_get_stats(native_api_stats_t *stats);

/**
 * @brief Reset statistics
 */
void native_api_reset_stats(void);

/* ============================================================================
 * mDNS Helpers
 * ============================================================================ */

/**
 * @brief Get mDNS instance name
 *
 * Format: rs1-{last 6 hex chars of MAC}
 *
 * @param out Output buffer (at least 16 chars)
 */
void native_api_get_mdns_instance(char *out);

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_API_H */
