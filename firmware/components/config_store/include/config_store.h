/**
 * @file config_store.h
 * @brief HardwareOS Device Config Store Module (M06)
 *
 * Provides persistent, atomic, and versioned storage for device configuration.
 * Abstracts NVS operations and ensures data integrity across power cycles.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_CONFIG_STORE.md
 */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define CONFIG_MAX_ZONES            16
#define CONFIG_MAX_VERTICES         8
#define CONFIG_ZONE_ID_LEN          16
#define CONFIG_ZONE_NAME_LEN        32
#define CONFIG_DEVICE_NAME_LEN      32
#define CONFIG_FRIENDLY_NAME_LEN    48
#define CONFIG_SSID_LEN             33
#define CONFIG_PASSWORD_LEN         65
#define CONFIG_API_PASSWORD_LEN     33

#define CONFIG_NVS_NAMESPACE        "rs1"
#define CONFIG_MAX_ZONE_SIZE        4096

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
    CONFIG_OK = 0,
    CONFIG_ERR_NOT_FOUND,           /**< Key doesn't exist */
    CONFIG_ERR_INVALID,             /**< Validation failed */
    CONFIG_ERR_CHECKSUM,            /**< CRC mismatch */
    CONFIG_ERR_FULL,                /**< NVS full */
    CONFIG_ERR_FLASH,               /**< Flash write failed */
    CONFIG_ERR_ROLLBACK_UNAVAIL,    /**< No previous version */
    CONFIG_ERR_NOT_INITIALIZED,     /**< Module not initialized */
} config_err_t;

/* ============================================================================
 * Zone Configuration Types
 * ============================================================================ */

/**
 * @brief Zone type
 */
typedef enum {
    ZONE_TYPE_INCLUDE = 0,
    ZONE_TYPE_EXCLUDE
} config_zone_type_t;

/**
 * @brief Single zone configuration
 */
typedef struct {
    char id[CONFIG_ZONE_ID_LEN];
    char name[CONFIG_ZONE_NAME_LEN];
    config_zone_type_t type;
    int16_t vertices[CONFIG_MAX_VERTICES][2];
    uint8_t vertex_count;
    uint8_t sensitivity;
} config_zone_t;

/**
 * @brief Zone store (all zones with metadata)
 */
typedef struct {
    uint32_t version;               /**< Incrementing version */
    uint32_t updated_at;            /**< Unix timestamp */
    config_zone_t zones[CONFIG_MAX_ZONES];
    uint8_t zone_count;
    uint16_t checksum;              /**< CRC16 for integrity */
} config_zone_store_t;

/* ============================================================================
 * Device Settings Types
 * ============================================================================ */

/**
 * @brief Device settings
 */
typedef struct {
    char device_name[CONFIG_DEVICE_NAME_LEN];       /**< mDNS name */
    char friendly_name[CONFIG_FRIENDLY_NAME_LEN];   /**< Display name */
    uint8_t default_sensitivity;                    /**< Global sensitivity (0-100) */
    bool telemetry_enabled;                         /**< Opt-in telemetry */
    uint16_t state_throttle_ms;                     /**< API update throttle */
} config_device_t;

/* ============================================================================
 * Network Configuration Types
 * ============================================================================ */

/**
 * @brief Network configuration
 */
typedef struct {
    char ssid[CONFIG_SSID_LEN];                 /**< Wi-Fi SSID */
    char password[CONFIG_PASSWORD_LEN];         /**< Wi-Fi password */
    bool static_ip;                             /**< Use static IP */
    uint32_t ip_addr;                           /**< Static IP (if enabled) */
    uint32_t gateway;                           /**< Gateway (if static) */
    uint32_t subnet;                            /**< Subnet mask (if static) */
    uint32_t dns;                               /**< DNS server (if static) */
} config_network_t;

/* ============================================================================
 * Security Configuration Types
 * ============================================================================ */

/**
 * @brief Security configuration
 */
typedef struct {
    char api_password[CONFIG_API_PASSWORD_LEN]; /**< Legacy API password */
    uint8_t encryption_key[32];                 /**< Noise PSK */
    bool encryption_enabled;                    /**< Require encryption */
    uint8_t pairing_token[16];                  /**< Local pairing token */
} config_security_t;

/* ============================================================================
 * Calibration Types
 * ============================================================================ */

/**
 * @brief Mounting type
 */
typedef enum {
    CONFIG_MOUNT_WALL = 0,
    CONFIG_MOUNT_CEILING,
    CONFIG_MOUNT_CUSTOM
} config_mount_type_t;

/**
 * @brief Calibration data
 */
typedef struct {
    int16_t x_offset_mm;            /**< Radar X offset correction */
    int16_t y_offset_mm;            /**< Radar Y offset correction */
    float rotation_deg;             /**< Rotation correction */
    config_mount_type_t mounting;   /**< Mounting type */
    uint32_t calibrated_at;         /**< Calibration timestamp */
} config_calibration_t;

/* ============================================================================
 * Statistics Types
 * ============================================================================ */

/**
 * @brief Config store statistics
 */
typedef struct {
    uint32_t writes_total;          /**< Total config writes */
    uint32_t rollbacks;             /**< Rollback operations */
    uint32_t validation_failures;   /**< Rejected config updates */
    uint32_t nvs_used_bytes;        /**< NVS usage estimate */
    uint32_t nvs_free_bytes;        /**< NVS free space */
} config_stats_t;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize the config store module
 *
 * Opens NVS, validates existing config, and recovers from interrupted writes.
 *
 * @return ESP_OK on success
 */
esp_err_t config_store_init(void);

/**
 * @brief Deinitialize the config store module
 */
void config_store_deinit(void);

/* ============================================================================
 * Zone Configuration API
 * ============================================================================ */

/**
 * @brief Read zone configuration
 *
 * @param out Output zone store
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_get_zones(config_zone_store_t *out);

/**
 * @brief Read specific zone by ID
 *
 * @param zone_id Zone identifier
 * @param out Output zone config
 * @return ESP_OK if found, CONFIG_ERR_NOT_FOUND otherwise
 */
esp_err_t config_get_zone(const char *zone_id, config_zone_t *out);

/**
 * @brief Write zone configuration (atomic, versioned)
 *
 * Performs atomic write with rollback support.
 * Version is auto-incremented and checksum is computed.
 *
 * @param zones Zone store to write
 * @return ESP_OK on success
 */
esp_err_t config_set_zones(const config_zone_store_t *zones);

/**
 * @brief Get current zone config version
 *
 * @return Current version number
 */
uint32_t config_get_zone_version(void);

/**
 * @brief Rollback zones to previous version
 *
 * @return ESP_OK on success, CONFIG_ERR_ROLLBACK_UNAVAIL if no backup
 */
esp_err_t config_rollback_zones(void);

/**
 * @brief Check if rollback is available
 *
 * @return True if previous zone version exists
 */
bool config_has_zone_rollback(void);

/* ============================================================================
 * Device Settings API
 * ============================================================================ */

/**
 * @brief Read device settings
 *
 * @param out Output device settings
 * @return ESP_OK on success
 */
esp_err_t config_get_device(config_device_t *out);

/**
 * @brief Write device settings
 *
 * @param settings Device settings to write
 * @return ESP_OK on success
 */
esp_err_t config_set_device(const config_device_t *settings);

/**
 * @brief Get default device settings
 *
 * @param out Output default settings
 */
void config_get_device_defaults(config_device_t *out);

/* ============================================================================
 * Network Configuration API
 * ============================================================================ */

/**
 * @brief Read network configuration
 *
 * @param out Output network config (password decrypted)
 * @return ESP_OK on success
 */
esp_err_t config_get_network(config_network_t *out);

/**
 * @brief Write network configuration
 *
 * Password is encrypted at rest.
 *
 * @param network Network config to write
 * @return ESP_OK on success
 */
esp_err_t config_set_network(const config_network_t *network);

/**
 * @brief Check if network is configured
 *
 * @return True if valid SSID is stored
 */
bool config_has_network(void);

/* ============================================================================
 * Security Configuration API
 * ============================================================================ */

/**
 * @brief Read security configuration
 *
 * Sensitive fields are decrypted.
 *
 * @param out Output security config
 * @return ESP_OK on success
 */
esp_err_t config_get_security(config_security_t *out);

/**
 * @brief Write security configuration
 *
 * Sensitive fields are encrypted at rest.
 *
 * @param security Security config to write
 * @return ESP_OK on success
 */
esp_err_t config_set_security(const config_security_t *security);

/* ============================================================================
 * Calibration API
 * ============================================================================ */

/**
 * @brief Read calibration data
 *
 * @param out Output calibration data
 * @return ESP_OK on success
 */
esp_err_t config_get_calibration(config_calibration_t *out);

/**
 * @brief Write calibration data
 *
 * @param calibration Calibration data to write
 * @return ESP_OK on success
 */
esp_err_t config_set_calibration(const config_calibration_t *calibration);

/* ============================================================================
 * Maintenance API
 * ============================================================================ */

/**
 * @brief Factory reset (erase all config)
 *
 * Erases all configuration and resets to defaults.
 *
 * @return ESP_OK on success
 */
esp_err_t config_factory_reset(void);

/**
 * @brief Erase specific config domain
 *
 * @param key Domain key ("zones", "device", "network", "security", "calibration")
 * @return ESP_OK on success
 */
esp_err_t config_erase(const char *key);

/**
 * @brief Get storage statistics
 *
 * @param stats Output statistics
 * @return ESP_OK on success
 */
esp_err_t config_get_stats(config_stats_t *stats);

/* ============================================================================
 * Validation API
 * ============================================================================ */

/**
 * @brief Validate zone configuration
 *
 * Checks all validation rules (vertex count, ID format, etc.)
 *
 * @param zone Zone to validate
 * @return ESP_OK if valid, error code otherwise
 */
esp_err_t config_validate_zone(const config_zone_t *zone);

/**
 * @brief Validate zone store
 *
 * Checks all zones and computes/validates checksum.
 *
 * @param zones Zone store to validate
 * @return ESP_OK if valid, error code otherwise
 */
esp_err_t config_validate_zone_store(const config_zone_store_t *zones);

/**
 * @brief Compute CRC16 for zone store
 *
 * @param zones Zone store
 * @return CRC16 checksum
 */
uint16_t config_compute_checksum(const config_zone_store_t *zones);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_STORE_H */
