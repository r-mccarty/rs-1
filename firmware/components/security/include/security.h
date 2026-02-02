/**
 * @file security.h
 * @brief HardwareOS Security Module (M10)
 *
 * Provides security services including firmware signature validation,
 * device authentication, TLS configuration, and key management.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_SECURITY.md
 */

#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define SECURITY_DEVICE_ID_LEN          16
#define SECURITY_DEVICE_SECRET_LEN      32
#define SECURITY_MQTT_USERNAME_LEN      33      /* Hex-encoded device ID */
#define SECURITY_MQTT_PASSWORD_LEN      65      /* Base64-encoded HMAC */
#define SECURITY_API_PASSWORD_LEN       33
#define SECURITY_SIGNATURE_LEN          64
#define SECURITY_PUBLIC_KEY_LEN         64
#define SECURITY_HASH_LEN               32

#define SECURITY_DEFAULT_PASSWORD_LEN   8
#define SECURITY_MAX_TRUSTED_KEYS       4

/* eFuse budget thresholds */
#define SECURITY_EFUSE_WARNING_THRESHOLD    24
#define SECURITY_EFUSE_EXHAUSTED_THRESHOLD  32

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Device identity
 */
typedef struct {
    uint8_t device_id[SECURITY_DEVICE_ID_LEN];          /**< Derived from MAC */
    uint8_t device_secret[SECURITY_DEVICE_SECRET_LEN];  /**< Provisioned secret */
    char mqtt_username[SECURITY_MQTT_USERNAME_LEN];     /**< Device ID as hex */
    char mqtt_password[SECURITY_MQTT_PASSWORD_LEN];     /**< Generated credential */
} device_identity_t;

/**
 * @brief Firmware signature block (appended to firmware binary)
 */
typedef struct __attribute__((packed)) {
    uint8_t magic[4];                           /**< "OPFW" */
    uint32_t version;                           /**< Signature format version */
    uint8_t fw_hash[SECURITY_HASH_LEN];         /**< SHA-256 of firmware */
    uint8_t signature[SECURITY_SIGNATURE_LEN];  /**< ECDSA signature (r || s) */
    uint8_t public_key[SECURITY_PUBLIC_KEY_LEN];/**< Public key for verification */
    uint32_t fw_version;                        /**< Firmware version */
    uint32_t build_timestamp;                   /**< Build time */
    uint8_t reserved[32];                       /**< Future use */
    uint8_t block_hash[SECURITY_HASH_LEN];      /**< SHA-256 of block */
} firmware_signature_block_t;

/**
 * @brief Trusted signing key
 */
typedef struct {
    uint8_t key[SECURITY_PUBLIC_KEY_LEN];
    uint32_t valid_from;
    uint32_t valid_until;
    bool revoked;
} trusted_key_t;

/**
 * @brief Device authentication config
 */
typedef struct {
    char username[16];                          /**< Always "admin" */
    uint8_t password_hash[SECURITY_HASH_LEN];   /**< SHA-256 of password */
    uint8_t password_salt[16];                  /**< Random salt */
    bool password_changed;                      /**< User changed default */
} device_auth_t;

/**
 * @brief Security configuration
 */
typedef struct {
    bool secure_boot_enabled;
    bool flash_encryption_enabled;
    uint8_t tls_min_version;                    /**< 0x0303 = TLS 1.2 */
    bool api_encryption_required;
    uint16_t pairing_timeout_sec;
    uint32_t session_timeout_sec;
    uint32_t ap_mode_timeout_sec;
    uint8_t provision_rate_limit;
} security_config_t;

/**
 * @brief Default configuration initializer
 */
#define SECURITY_CONFIG_DEFAULT() { \
    .secure_boot_enabled = true, \
    .flash_encryption_enabled = false, \
    .tls_min_version = 0x03, \
    .api_encryption_required = false, \
    .pairing_timeout_sec = 300, \
    .session_timeout_sec = 3600, \
    .ap_mode_timeout_sec = 600, \
    .provision_rate_limit = 3, \
}

/**
 * @brief Security event type
 */
typedef enum {
    SEC_EVENT_BOOT_VERIFIED = 0,
    SEC_EVENT_BOOT_FAILED,
    SEC_EVENT_AUTH_FAILED,
    SEC_EVENT_AUTH_SUCCESS,
    SEC_EVENT_PAIRING_ATTEMPT,
    SEC_EVENT_ROLLBACK_BLOCKED,
    SEC_EVENT_PROVISION_START,
    SEC_EVENT_PROVISION_SUCCESS,
    SEC_EVENT_PROVISION_FAILED,
} security_event_t;

/**
 * @brief Security event callback
 */
typedef void (*security_event_callback_t)(security_event_t event, void *user_data);

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize the security module
 *
 * @param config Configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t security_init(const security_config_t *config);

/**
 * @brief Deinitialize the security module
 */
void security_deinit(void);

/**
 * @brief Set event callback
 *
 * @param callback Callback function
 * @param user_data User data for callback
 */
void security_set_event_callback(security_event_callback_t callback,
                                  void *user_data);

/* ============================================================================
 * Device Identity
 * ============================================================================ */

/**
 * @brief Get device identity
 *
 * @param identity Output identity structure
 * @return ESP_OK on success
 */
esp_err_t security_get_device_identity(device_identity_t *identity);

/**
 * @brief Get device ID as hex string
 *
 * @param out Output buffer (at least 33 chars)
 */
void security_get_device_id_hex(char *out);

/**
 * @brief Generate MQTT credentials
 *
 * Generates time-limited MQTT password using HMAC-SHA256.
 *
 * @param identity Device identity
 * @param timestamp Current timestamp
 * @return ESP_OK on success
 */
esp_err_t security_generate_mqtt_credentials(device_identity_t *identity,
                                              uint32_t timestamp);

/* ============================================================================
 * Firmware Verification
 * ============================================================================ */

/**
 * @brief Verify firmware signature
 *
 * @param fw_data Firmware data
 * @param fw_size Firmware size (including signature block)
 * @return ESP_OK if valid, error code otherwise
 */
esp_err_t security_verify_firmware(const uint8_t *fw_data, size_t fw_size);

/**
 * @brief Check if public key is trusted
 *
 * @param public_key 64-byte public key
 * @return True if key is in trusted list
 */
bool security_is_trusted_key(const uint8_t *public_key);

/**
 * @brief Get minimum firmware version (anti-rollback)
 *
 * @return Minimum allowed firmware version
 */
uint32_t security_get_min_version(void);

/**
 * @brief Update anti-rollback counter
 *
 * Burns eFuse to update minimum allowed version.
 *
 * @param new_version New minimum version
 * @return ESP_OK on success
 */
esp_err_t security_update_rollback_counter(uint32_t new_version);

/**
 * @brief Check eFuse budget
 *
 * Logs warning if eFuse budget is low or exhausted.
 */
void security_check_efuse_budget(void);

/**
 * @brief Get remaining eFuse count
 *
 * @return Number of eFuse bits remaining (0-32)
 */
uint8_t security_get_efuse_remaining(void);

/* ============================================================================
 * Password Authentication
 * ============================================================================ */

/**
 * @brief Validate password
 *
 * @param password Password to validate
 * @return True if password is correct
 */
bool security_validate_password(const char *password);

/**
 * @brief Set new password
 *
 * @param new_password New password (min 8 chars)
 * @return ESP_OK on success
 */
esp_err_t security_set_password(const char *new_password);

/**
 * @brief Get default password (from manufacturing data)
 *
 * @param out Output buffer (at least 9 chars)
 * @return ESP_OK on success
 */
esp_err_t security_get_default_password(char *out);

/**
 * @brief Reset password to default
 *
 * @return ESP_OK on success
 */
esp_err_t security_reset_password(void);

/**
 * @brief Check if password was changed from default
 *
 * @return True if user changed password
 */
bool security_password_changed(void);

/* ============================================================================
 * Session Management
 * ============================================================================ */

/**
 * @brief Generate session token
 *
 * @param token Output buffer (at least 33 chars)
 * @return ESP_OK on success
 */
esp_err_t security_generate_session_token(char *token);

/**
 * @brief Validate session token
 *
 * @param token Session token to validate
 * @return True if token is valid and not expired
 */
bool security_validate_session_token(const char *token);

/**
 * @brief Invalidate session token
 *
 * @param token Token to invalidate
 */
void security_invalidate_session(const char *token);

/**
 * @brief Invalidate all sessions
 */
void security_invalidate_all_sessions(void);

/* ============================================================================
 * Cryptographic Utilities
 * ============================================================================ */

/**
 * @brief Compute SHA-256 hash
 *
 * @param data Input data
 * @param len Data length
 * @param hash Output hash (32 bytes)
 */
void security_sha256(const uint8_t *data, size_t len, uint8_t *hash);

/**
 * @brief Compute HMAC-SHA256
 *
 * @param key Key
 * @param key_len Key length
 * @param data Input data
 * @param data_len Data length
 * @param hmac Output HMAC (32 bytes)
 */
void security_hmac_sha256(const uint8_t *key, size_t key_len,
                           const uint8_t *data, size_t data_len,
                           uint8_t *hmac);

/**
 * @brief Generate random bytes
 *
 * @param buffer Output buffer
 * @param len Number of bytes to generate
 * @return ESP_OK on success
 */
esp_err_t security_random_bytes(uint8_t *buffer, size_t len);

/**
 * @brief Hex encode data
 *
 * @param data Input data
 * @param len Data length
 * @param hex Output hex string (must be 2*len+1)
 */
void security_hex_encode(const uint8_t *data, size_t len, char *hex);

/**
 * @brief Base64 encode data
 *
 * @param data Input data
 * @param len Data length
 * @param base64 Output base64 string
 * @param base64_len Output buffer size
 * @return Encoded length
 */
size_t security_base64_encode(const uint8_t *data, size_t len,
                               char *base64, size_t base64_len);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Security statistics
 */
typedef struct {
    uint32_t auth_failures;
    uint32_t auth_successes;
    uint32_t pairing_attempts;
    uint32_t tls_handshakes;
    uint32_t provision_attempts;
    uint32_t provision_successes;
    uint32_t rollback_blocked;
    uint8_t efuse_burned;
} security_stats_t;

/**
 * @brief Get security statistics
 *
 * @param stats Output statistics
 */
void security_get_stats(security_stats_t *stats);

/**
 * @brief Reset security statistics
 */
void security_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* SECURITY_H */
