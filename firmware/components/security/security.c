/**
 * @file security.c
 * @brief HardwareOS Security Module Implementation (M10)
 *
 * Firmware verification, device authentication, and key management.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_SECURITY.md
 */

#include "security.h"
#include "timebase.h"
#include <string.h>

#ifndef TEST_HOST
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/base64.h"
#else
#include <stdio.h>
#include <stdlib.h>
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

static const char *TAG = "security";

/* Firmware signature magic */
static const uint8_t FW_SIGNATURE_MAGIC[4] = {'O', 'P', 'F', 'W'};

/* ============================================================================
 * Module State
 * ============================================================================ */

static struct {
    bool initialized;
    security_config_t config;

    /* Device identity */
    device_identity_t identity;
    bool identity_derived;

    /* Authentication */
    device_auth_t auth;
    bool auth_loaded;

    /* Trusted keys */
    trusted_key_t trusted_keys[SECURITY_MAX_TRUSTED_KEYS];
    uint8_t trusted_key_count;

    /* Sessions */
    struct {
        char token[33];
        uint32_t created_ms;
        bool valid;
    } sessions[4];

    /* Callbacks */
    security_event_callback_t event_callback;
    void *callback_user_data;

    /* Statistics */
    security_stats_t stats;
} s_security = {0};

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

static void emit_event(security_event_t event)
{
    if (s_security.event_callback) {
        s_security.event_callback(event, s_security.callback_user_data);
    }
}

/* ============================================================================
 * Cryptographic Utilities
 * ============================================================================ */

void security_sha256(const uint8_t *data, size_t len, uint8_t *hash)
{
#ifndef TEST_HOST
    mbedtls_sha256(data, len, hash, 0);
#else
    /* Simple test hash (not cryptographic) */
    memset(hash, 0, 32);
    for (size_t i = 0; i < len && i < 32; i++) {
        hash[i] = data[i];
    }
#endif
}

void security_hmac_sha256(const uint8_t *key, size_t key_len,
                           const uint8_t *data, size_t data_len,
                           uint8_t *hmac)
{
#ifndef TEST_HOST
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, key, key_len);
    mbedtls_md_hmac_update(&ctx, data, data_len);
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);
#else
    /* Simple test HMAC */
    security_sha256(data, data_len, hmac);
    for (size_t i = 0; i < 32 && i < key_len; i++) {
        hmac[i] ^= key[i];
    }
#endif
}

esp_err_t security_random_bytes(uint8_t *buffer, size_t len)
{
#ifndef TEST_HOST
    esp_fill_random(buffer, len);
#else
    for (size_t i = 0; i < len; i++) {
        buffer[i] = (uint8_t)rand();
    }
#endif
    return ESP_OK;
}

void security_hex_encode(const uint8_t *data, size_t len, char *hex)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex[i * 2] = hex_chars[(data[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[data[i] & 0x0F];
    }
    hex[len * 2] = '\0';
}

size_t security_base64_encode(const uint8_t *data, size_t len,
                               char *base64, size_t base64_len)
{
#ifndef TEST_HOST
    size_t olen = 0;
    mbedtls_base64_encode((unsigned char *)base64, base64_len, &olen, data, len);
    return olen;
#else
    /* Simple test encoding */
    size_t out_len = (len + 2) / 3 * 4;
    if (out_len >= base64_len) {
        out_len = base64_len - 1;
    }
    memset(base64, 'A', out_len);
    base64[out_len] = '\0';
    return out_len;
#endif
}

/* ============================================================================
 * Public API: Initialization
 * ============================================================================ */

esp_err_t security_init(const security_config_t *config)
{
    if (s_security.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Apply configuration */
    if (config) {
        s_security.config = *config;
    } else {
        security_config_t defaults = SECURITY_CONFIG_DEFAULT();
        s_security.config = defaults;
    }

    /* Initialize state */
    memset(&s_security.stats, 0, sizeof(s_security.stats));
    memset(s_security.sessions, 0, sizeof(s_security.sessions));
    s_security.trusted_key_count = 0;

    /* Check eFuse budget on startup */
    security_check_efuse_budget();

    s_security.initialized = true;
    ESP_LOGI(TAG, "Initialized (secure_boot=%s, flash_enc=%s)",
             s_security.config.secure_boot_enabled ? "on" : "off",
             s_security.config.flash_encryption_enabled ? "on" : "off");

    return ESP_OK;
}

void security_deinit(void)
{
    if (!s_security.initialized) {
        return;
    }

    /* Clear sensitive data */
    memset(&s_security.identity, 0, sizeof(s_security.identity));
    memset(&s_security.auth, 0, sizeof(s_security.auth));
    memset(s_security.sessions, 0, sizeof(s_security.sessions));

    memset(&s_security, 0, sizeof(s_security));
    ESP_LOGI(TAG, "Deinitialized");
}

void security_set_event_callback(security_event_callback_t callback,
                                  void *user_data)
{
    s_security.event_callback = callback;
    s_security.callback_user_data = user_data;
}

/* ============================================================================
 * Public API: Device Identity
 * ============================================================================ */

esp_err_t security_get_device_identity(device_identity_t *identity)
{
    if (!s_security.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!identity) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Derive identity if not already done */
    if (!s_security.identity_derived) {
        uint8_t mac[6];
#ifndef TEST_HOST
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
#else
        memset(mac, 0xAA, 6);
#endif

        /* Device ID: SHA-256(mac || "opticworks-rs1")[:16] */
        uint8_t hash_input[20];  /* 6 + 14 */
        memcpy(hash_input, mac, 6);
        memcpy(hash_input + 6, "opticworks-rs1", 14);

        uint8_t hash[32];
        security_sha256(hash_input, 20, hash);
        memcpy(s_security.identity.device_id, hash, SECURITY_DEVICE_ID_LEN);

        /* Format as hex string for MQTT username */
        security_hex_encode(s_security.identity.device_id,
                            SECURITY_DEVICE_ID_LEN,
                            s_security.identity.mqtt_username);

        /* Device secret would be loaded from NVS in production */
        /* For now, derive a test secret */
        security_sha256(hash, 32, s_security.identity.device_secret);

        s_security.identity_derived = true;
        ESP_LOGI(TAG, "Device identity derived: %s",
                 s_security.identity.mqtt_username);
    }

    memcpy(identity, &s_security.identity, sizeof(device_identity_t));
    return ESP_OK;
}

void security_get_device_id_hex(char *out)
{
    if (!s_security.identity_derived) {
        device_identity_t id;
        security_get_device_identity(&id);
    }
    strcpy(out, s_security.identity.mqtt_username);
}

esp_err_t security_generate_mqtt_credentials(device_identity_t *identity,
                                              uint32_t timestamp)
{
    if (!identity) {
        return ESP_ERR_INVALID_ARG;
    }

    /* MQTT password: HMAC-SHA256(device_secret, device_id || timestamp) */
    uint8_t data[SECURITY_DEVICE_ID_LEN + 4];
    memcpy(data, identity->device_id, SECURITY_DEVICE_ID_LEN);
    memcpy(data + SECURITY_DEVICE_ID_LEN, &timestamp, 4);

    uint8_t hmac[32];
    security_hmac_sha256(identity->device_secret, SECURITY_DEVICE_SECRET_LEN,
                          data, sizeof(data), hmac);

    security_base64_encode(hmac, 32, identity->mqtt_password,
                            SECURITY_MQTT_PASSWORD_LEN);

    return ESP_OK;
}

/* ============================================================================
 * Public API: Firmware Verification
 * ============================================================================ */

esp_err_t security_verify_firmware(const uint8_t *fw_data, size_t fw_size)
{
    if (!s_security.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!fw_data || fw_size < sizeof(firmware_signature_block_t)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Locate signature block at end of firmware */
    const firmware_signature_block_t *sig =
        (const firmware_signature_block_t *)(fw_data + fw_size - sizeof(firmware_signature_block_t));

    /* Verify magic */
    if (memcmp(sig->magic, FW_SIGNATURE_MAGIC, 4) != 0) {
        ESP_LOGE(TAG, "Invalid firmware signature magic");
        emit_event(SEC_EVENT_BOOT_FAILED);
        return ESP_ERR_INVALID_ARG;
    }

    /* Verify public key is trusted */
    if (!security_is_trusted_key(sig->public_key)) {
        ESP_LOGE(TAG, "Firmware signed with untrusted key");
        emit_event(SEC_EVENT_BOOT_FAILED);
        return ESP_ERR_INVALID_STATE;
    }

    /* Compute firmware hash */
    size_t fw_content_size = fw_size - sizeof(firmware_signature_block_t);
    uint8_t computed_hash[32];
    security_sha256(fw_data, fw_content_size, computed_hash);

    /* Verify hash matches */
    if (memcmp(computed_hash, sig->fw_hash, 32) != 0) {
        ESP_LOGE(TAG, "Firmware hash mismatch");
        emit_event(SEC_EVENT_BOOT_FAILED);
        return ESP_ERR_INVALID_CRC;
    }

    /* Verify ECDSA signature */
#ifndef TEST_HOST
    mbedtls_ecdsa_context ecdsa;
    mbedtls_ecp_group grp;

    mbedtls_ecdsa_init(&ecdsa);
    mbedtls_ecp_group_init(&grp);

    int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        mbedtls_ecdsa_free(&ecdsa);
        mbedtls_ecp_group_free(&grp);
        return ESP_FAIL;
    }

    /* Load public key */
    ret = mbedtls_ecp_point_read_binary(&grp, &ecdsa.Q,
                                         sig->public_key, SECURITY_PUBLIC_KEY_LEN);
    if (ret != 0) {
        mbedtls_ecdsa_free(&ecdsa);
        mbedtls_ecp_group_free(&grp);
        ESP_LOGE(TAG, "Invalid public key format");
        emit_event(SEC_EVENT_BOOT_FAILED);
        return ESP_ERR_INVALID_ARG;
    }

    /* Verify signature */
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_read_binary(&r, sig->signature, 32);
    mbedtls_mpi_read_binary(&s, sig->signature + 32, 32);

    ret = mbedtls_ecdsa_verify(&grp, sig->fw_hash, 32, &ecdsa.Q, &r, &s);

    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecdsa_free(&ecdsa);
    mbedtls_ecp_group_free(&grp);

    if (ret != 0) {
        ESP_LOGE(TAG, "Firmware signature verification failed");
        emit_event(SEC_EVENT_BOOT_FAILED);
        return ESP_ERR_INVALID_MAC;
    }
#endif

    /* Anti-rollback check */
    if (sig->fw_version < security_get_min_version()) {
        ESP_LOGE(TAG, "Firmware version %lu blocked by anti-rollback (min=%lu)",
                 (unsigned long)sig->fw_version,
                 (unsigned long)security_get_min_version());
        emit_event(SEC_EVENT_ROLLBACK_BLOCKED);
        s_security.stats.rollback_blocked++;
        return ESP_ERR_NOT_SUPPORTED;
    }

    emit_event(SEC_EVENT_BOOT_VERIFIED);
    ESP_LOGI(TAG, "Firmware verified (version=%lu)", (unsigned long)sig->fw_version);

    return ESP_OK;
}

bool security_is_trusted_key(const uint8_t *public_key)
{
    if (!public_key) {
        return false;
    }

    for (int i = 0; i < s_security.trusted_key_count; i++) {
        if (s_security.trusted_keys[i].revoked) {
            continue;
        }

        if (memcmp(s_security.trusted_keys[i].key, public_key,
                   SECURITY_PUBLIC_KEY_LEN) == 0) {
            /* Check validity period */
            uint32_t now = timebase_unix_time();
            if (now > 0) {
                if (now < s_security.trusted_keys[i].valid_from ||
                    now > s_security.trusted_keys[i].valid_until) {
                    continue;
                }
            }
            return true;
        }
    }

    /* For development, accept any key if no trusted keys configured */
    if (s_security.trusted_key_count == 0) {
        ESP_LOGW(TAG, "No trusted keys configured, accepting any key");
        return true;
    }

    return false;
}

uint32_t security_get_min_version(void)
{
#ifndef TEST_HOST
    /* Read from eFuse anti-rollback counter */
    /* For now, return 0 (no minimum) */
    return 0;
#else
    return 0;
#endif
}

esp_err_t security_update_rollback_counter(uint32_t new_version)
{
#ifndef TEST_HOST
    /* This would burn eFuse bits */
    /* Requires careful consideration per spec */
    ESP_LOGW(TAG, "Anti-rollback update to %lu (would burn eFuse)",
             (unsigned long)new_version);
    /* esp_efuse_write_field_cnt(ESP_EFUSE_SECURE_VERSION, new_version); */
#endif
    return ESP_OK;
}

void security_check_efuse_budget(void)
{
    uint8_t burned = 0;
#ifndef TEST_HOST
    /* burned = esp_efuse_read_field_cnt(ESP_EFUSE_SECURE_VERSION); */
#endif

    s_security.stats.efuse_burned = burned;

    if (burned >= SECURITY_EFUSE_EXHAUSTED_THRESHOLD) {
        ESP_LOGW(TAG, "Anti-rollback eFuses exhausted - rollback protection disabled");
    } else if (burned >= SECURITY_EFUSE_WARNING_THRESHOLD) {
        ESP_LOGI(TAG, "Anti-rollback eFuse budget low: %d/32 used", burned);
    }
}

uint8_t security_get_efuse_remaining(void)
{
    return SECURITY_EFUSE_EXHAUSTED_THRESHOLD - s_security.stats.efuse_burned;
}

/* ============================================================================
 * Public API: Password Authentication
 * ============================================================================ */

bool security_validate_password(const char *password)
{
    if (!password) {
        return false;
    }

    /* Compute hash of provided password with salt */
    uint8_t salted[80];  /* max password + salt */
    size_t pw_len = strlen(password);
    if (pw_len > 64) pw_len = 64;

    memcpy(salted, s_security.auth.password_salt, 16);
    memcpy(salted + 16, password, pw_len);

    uint8_t hash[32];
    security_sha256(salted, 16 + pw_len, hash);

    if (memcmp(hash, s_security.auth.password_hash, 32) == 0) {
        s_security.stats.auth_successes++;
        emit_event(SEC_EVENT_AUTH_SUCCESS);
        return true;
    }

    s_security.stats.auth_failures++;
    emit_event(SEC_EVENT_AUTH_FAILED);
    return false;
}

esp_err_t security_set_password(const char *new_password)
{
    if (!new_password || strlen(new_password) < 8) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Generate new salt */
    security_random_bytes(s_security.auth.password_salt, 16);

    /* Hash password with salt */
    uint8_t salted[80];
    size_t pw_len = strlen(new_password);
    if (pw_len > 64) pw_len = 64;

    memcpy(salted, s_security.auth.password_salt, 16);
    memcpy(salted + 16, new_password, pw_len);

    security_sha256(salted, 16 + pw_len, s_security.auth.password_hash);
    s_security.auth.password_changed = true;
    s_security.auth_loaded = true;

    /* TODO: Save to NVS via config_store */

    ESP_LOGI(TAG, "Password updated");
    return ESP_OK;
}

esp_err_t security_get_default_password(char *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    /* In production, read from manufacturing flash partition */
    /* For development, generate from MAC */
#ifndef TEST_HOST
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, 9, "%02X%02X%02X%02X",
             mac[2], mac[3], mac[4], mac[5]);
#else
    strcpy(out, "test1234");
#endif

    return ESP_OK;
}

esp_err_t security_reset_password(void)
{
    char default_pw[9];
    esp_err_t err = security_get_default_password(default_pw);
    if (err != ESP_OK) {
        return err;
    }

    err = security_set_password(default_pw);
    if (err == ESP_OK) {
        s_security.auth.password_changed = false;
    }

    ESP_LOGI(TAG, "Password reset to default");
    return err;
}

bool security_password_changed(void)
{
    return s_security.auth.password_changed;
}

/* ============================================================================
 * Public API: Session Management
 * ============================================================================ */

esp_err_t security_generate_session_token(char *token)
{
    if (!token) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Find free session slot */
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (!s_security.sessions[i].valid) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        /* Expire oldest session */
        uint32_t oldest_time = UINT32_MAX;
        for (int i = 0; i < 4; i++) {
            if (s_security.sessions[i].created_ms < oldest_time) {
                oldest_time = s_security.sessions[i].created_ms;
                slot = i;
            }
        }
    }

    /* Generate random token */
    uint8_t random[16];
    security_random_bytes(random, 16);
    security_hex_encode(random, 16, s_security.sessions[slot].token);

    s_security.sessions[slot].created_ms = timebase_uptime_ms();
    s_security.sessions[slot].valid = true;

    strcpy(token, s_security.sessions[slot].token);

    return ESP_OK;
}

bool security_validate_session_token(const char *token)
{
    if (!token) {
        return false;
    }

    uint32_t now = timebase_uptime_ms();
    uint32_t timeout_ms = s_security.config.session_timeout_sec * 1000;

    for (int i = 0; i < 4; i++) {
        if (!s_security.sessions[i].valid) {
            continue;
        }

        /* Check expiration */
        if (now - s_security.sessions[i].created_ms > timeout_ms) {
            s_security.sessions[i].valid = false;
            continue;
        }

        if (strcmp(s_security.sessions[i].token, token) == 0) {
            return true;
        }
    }

    return false;
}

void security_invalidate_session(const char *token)
{
    if (!token) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        if (s_security.sessions[i].valid &&
            strcmp(s_security.sessions[i].token, token) == 0) {
            s_security.sessions[i].valid = false;
            memset(s_security.sessions[i].token, 0, 33);
            break;
        }
    }
}

void security_invalidate_all_sessions(void)
{
    memset(s_security.sessions, 0, sizeof(s_security.sessions));
}

/* ============================================================================
 * Public API: Statistics
 * ============================================================================ */

void security_get_stats(security_stats_t *stats)
{
    if (!stats) return;
    memcpy(stats, &s_security.stats, sizeof(security_stats_t));
}

void security_reset_stats(void)
{
    uint8_t efuse_burned = s_security.stats.efuse_burned;
    memset(&s_security.stats, 0, sizeof(security_stats_t));
    s_security.stats.efuse_burned = efuse_burned;  /* Preserve eFuse count */
}
