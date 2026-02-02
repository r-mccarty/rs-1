/**
 * @file ota_manager.h
 * @brief HardwareOS OTA Manager Module (M07)
 *
 * Handles firmware updates via MQTT trigger and HTTPS download.
 * Supports automatic rollback on failed boot and staged rollouts.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_OTA.md
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define OTA_URL_MAX_LEN             256
#define OTA_SHA256_LEN              32
#define OTA_SHA256_HEX_LEN          65
#define OTA_ROLLOUT_ID_LEN          37      /* UUID string */
#define OTA_ERROR_MSG_LEN           128

/* Retry backoff intervals (seconds) */
#define OTA_RETRY_INTERVAL_1        60      /* 1 minute */
#define OTA_RETRY_INTERVAL_2        300     /* 5 minutes */
#define OTA_RETRY_INTERVAL_3        1800    /* 30 minutes */
#define OTA_MAX_RETRIES             3

/* Download parameters */
#define OTA_CHUNK_SIZE              4096
#define OTA_DOWNLOAD_TIMEOUT_SEC    300     /* 5 minutes max download */
#define OTA_MIN_FREE_HEAP           65536   /* 64KB minimum during OTA */

/* MQTT topics (format strings) */
#define OTA_TOPIC_TRIGGER_FMT       "opticworks/%s/ota/trigger"
#define OTA_TOPIC_STATUS_FMT        "opticworks/%s/ota/status"

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief OTA status codes
 */
typedef enum {
    OTA_STATUS_IDLE = 0,            /**< No update in progress */
    OTA_STATUS_PENDING,             /**< Update triggered, waiting to start */
    OTA_STATUS_DOWNLOADING,         /**< Downloading firmware */
    OTA_STATUS_VERIFYING,           /**< Verifying SHA256 and signature */
    OTA_STATUS_INSTALLING,          /**< Writing to flash */
    OTA_STATUS_SUCCESS,             /**< Update complete, pending reboot */
    OTA_STATUS_FAILED,              /**< Update failed */
    OTA_STATUS_ROLLBACK,            /**< Rolled back to previous version */
} ota_status_t;

/**
 * @brief OTA error codes
 */
typedef enum {
    OTA_ERROR_NONE = 0,
    OTA_ERROR_INVALID_MANIFEST,     /**< Manifest parse error */
    OTA_ERROR_VERSION_CHECK,        /**< Version not newer or rollback blocked */
    OTA_ERROR_RSSI_TOO_LOW,         /**< WiFi signal below min_rssi */
    OTA_ERROR_DOWNLOAD_FAILED,      /**< HTTP download error */
    OTA_ERROR_HASH_MISMATCH,        /**< SHA256 verification failed */
    OTA_ERROR_SIGNATURE_INVALID,    /**< Firmware signature invalid */
    OTA_ERROR_FLASH_WRITE,          /**< Flash write error */
    OTA_ERROR_NO_PARTITION,         /**< No valid OTA partition */
    OTA_ERROR_LOW_MEMORY,           /**< Insufficient heap for OTA */
    OTA_ERROR_TIMEOUT,              /**< Download timeout */
    OTA_ERROR_BUSY,                 /**< Another update in progress */
} ota_error_t;

/**
 * @brief OTA trigger manifest (from MQTT)
 */
typedef struct {
    uint32_t version;               /**< Target firmware version */
    char url[OTA_URL_MAX_LEN];      /**< HTTPS download URL */
    uint8_t sha256[OTA_SHA256_LEN]; /**< Expected SHA256 hash */
    int8_t min_rssi;                /**< Minimum WiFi RSSI required */
    char rollout_id[OTA_ROLLOUT_ID_LEN]; /**< Rollout campaign ID */
    bool force;                     /**< Force update (skip version check) */
} ota_manifest_t;

/**
 * @brief OTA progress information
 */
typedef struct {
    ota_status_t status;
    ota_error_t error;
    uint32_t target_version;        /**< Version being installed */
    uint32_t bytes_downloaded;      /**< Bytes downloaded so far */
    uint32_t total_bytes;           /**< Total firmware size */
    uint8_t progress_percent;       /**< 0-100 */
    uint8_t retry_count;            /**< Current retry attempt */
    char rollout_id[OTA_ROLLOUT_ID_LEN];
    char error_msg[OTA_ERROR_MSG_LEN];
} ota_progress_t;

/**
 * @brief OTA configuration
 */
typedef struct {
    bool auto_reboot;               /**< Reboot automatically after success */
    uint16_t reboot_delay_sec;      /**< Delay before reboot */
    bool verify_signature;          /**< Require firmware signature */
    bool check_rollback;            /**< Check anti-rollback counter */
    int8_t min_rssi_override;       /**< Override manifest min_rssi (-128 = use manifest) */
} ota_config_t;

/**
 * @brief Default OTA configuration
 */
#define OTA_CONFIG_DEFAULT() { \
    .auto_reboot = true, \
    .reboot_delay_sec = 5, \
    .verify_signature = true, \
    .check_rollback = true, \
    .min_rssi_override = -128, \
}

/**
 * @brief OTA event types
 */
typedef enum {
    OTA_EVENT_TRIGGERED = 0,        /**< Update triggered */
    OTA_EVENT_DOWNLOAD_START,       /**< Download starting */
    OTA_EVENT_DOWNLOAD_PROGRESS,    /**< Download progress update */
    OTA_EVENT_DOWNLOAD_COMPLETE,    /**< Download finished */
    OTA_EVENT_VERIFY_START,         /**< Verification starting */
    OTA_EVENT_VERIFY_COMPLETE,      /**< Verification passed */
    OTA_EVENT_INSTALL_START,        /**< Installing to flash */
    OTA_EVENT_INSTALL_COMPLETE,     /**< Installation complete */
    OTA_EVENT_SUCCESS,              /**< Update successful */
    OTA_EVENT_FAILED,               /**< Update failed */
    OTA_EVENT_REBOOT_PENDING,       /**< Reboot scheduled */
    OTA_EVENT_ROLLBACK,             /**< Rollback detected */
} ota_event_t;

/**
 * @brief OTA event callback
 */
typedef void (*ota_event_callback_t)(ota_event_t event,
                                      const ota_progress_t *progress,
                                      void *user_data);

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize OTA manager
 *
 * @param config Configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t ota_manager_init(const ota_config_t *config);

/**
 * @brief Deinitialize OTA manager
 */
void ota_manager_deinit(void);

/**
 * @brief Set event callback
 *
 * @param callback Callback function
 * @param user_data User data for callback
 */
void ota_manager_set_callback(ota_event_callback_t callback, void *user_data);

/* ============================================================================
 * MQTT Integration
 * ============================================================================ */

/**
 * @brief Handle incoming OTA trigger message
 *
 * Called by MQTT handler when message received on trigger topic.
 *
 * @param payload JSON payload from MQTT
 * @param payload_len Payload length
 * @return ESP_OK if update triggered successfully
 */
esp_err_t ota_manager_handle_trigger(const char *payload, size_t payload_len);

/**
 * @brief Get MQTT trigger topic for this device
 *
 * @param topic Output buffer
 * @param topic_len Buffer size
 * @return ESP_OK on success
 */
esp_err_t ota_manager_get_trigger_topic(char *topic, size_t topic_len);

/**
 * @brief Get MQTT status topic for this device
 *
 * @param topic Output buffer
 * @param topic_len Buffer size
 * @return ESP_OK on success
 */
esp_err_t ota_manager_get_status_topic(char *topic, size_t topic_len);

/**
 * @brief Publish current OTA status to MQTT
 *
 * @return ESP_OK on success
 */
esp_err_t ota_manager_publish_status(void);

/* ============================================================================
 * Update Control
 * ============================================================================ */

/**
 * @brief Start OTA update with manifest
 *
 * @param manifest OTA manifest
 * @return ESP_OK if update started
 */
esp_err_t ota_manager_start(const ota_manifest_t *manifest);

/**
 * @brief Abort current OTA update
 *
 * @return ESP_OK on success
 */
esp_err_t ota_manager_abort(void);

/**
 * @brief Get current OTA progress
 *
 * @param progress Output progress structure
 */
void ota_manager_get_progress(ota_progress_t *progress);

/**
 * @brief Check if OTA is in progress
 *
 * @return True if update is in progress
 */
bool ota_manager_is_busy(void);

/**
 * @brief Trigger reboot to complete update
 *
 * Call after OTA_STATUS_SUCCESS to reboot into new firmware.
 */
void ota_manager_reboot(void);

/* ============================================================================
 * Rollback Support
 * ============================================================================ */

/**
 * @brief Mark current firmware as valid
 *
 * Call after successful boot to prevent rollback.
 * Must be called within first boot after OTA update.
 *
 * @return ESP_OK on success
 */
esp_err_t ota_manager_mark_valid(void);

/**
 * @brief Check if running after rollback
 *
 * @return True if rolled back from failed update
 */
bool ota_manager_is_rollback(void);

/**
 * @brief Get pending update partition
 *
 * @return Partition to write next update, or NULL
 */
const void *ota_manager_get_next_partition(void);

/**
 * @brief Get running partition info
 *
 * @param label Output label buffer (at least 16 chars)
 * @param version Output current firmware version
 */
void ota_manager_get_running_info(char *label, uint32_t *version);

/* ============================================================================
 * Version Control
 * ============================================================================ */

/**
 * @brief Get current firmware version
 *
 * @return Firmware version number
 */
uint32_t ota_manager_get_version(void);

/**
 * @brief Check if version is newer than current
 *
 * @param version Version to check
 * @return True if version is newer
 */
bool ota_manager_is_newer_version(uint32_t version);

/**
 * @brief Check if update to version is allowed
 *
 * Checks both version comparison and anti-rollback counter.
 *
 * @param version Target version
 * @return True if update is allowed
 */
bool ota_manager_is_update_allowed(uint32_t version);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief OTA statistics
 */
typedef struct {
    uint32_t updates_attempted;
    uint32_t updates_successful;
    uint32_t updates_failed;
    uint32_t rollbacks;
    uint32_t last_update_time;      /**< Unix timestamp */
    uint32_t last_update_version;
    uint32_t total_bytes_downloaded;
} ota_stats_t;

/**
 * @brief Get OTA statistics
 *
 * @param stats Output statistics
 */
void ota_manager_get_stats(ota_stats_t *stats);

/**
 * @brief Reset OTA statistics
 */
void ota_manager_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */
