/**
 * @file config_store.c
 * @brief HardwareOS Device Config Store Module Implementation (M06)
 *
 * Provides persistent, atomic, and versioned storage via NVS.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_CONFIG_STORE.md
 */

#include "config_store.h"
#include <string.h>
#include <ctype.h>

#ifndef TEST_HOST
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#else
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

static const char *TAG = "config_store";

/* ============================================================================
 * NVS Keys
 * ============================================================================ */

#define KEY_ZONES           "zones"
#define KEY_ZONES_PREV      "zones_prev"
#define KEY_ZONES_NEW       "zones_new"
#define KEY_DEVICE          "device"
#define KEY_NETWORK         "network"
#define KEY_SECURITY        "security"
#define KEY_CALIBRATION     "calibration"

/* ============================================================================
 * Module State
 * ============================================================================ */

static struct {
    bool initialized;
#ifndef TEST_HOST
    nvs_handle_t nvs_handle;
#endif
    uint8_t device_key[16];         /* Derived encryption key */
    config_stats_t stats;
} s_config = {0};

/* ============================================================================
 * CRC16 Implementation (CCITT)
 * ============================================================================ */

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

uint16_t config_compute_checksum(const config_zone_store_t *zones)
{
    /* Compute CRC over everything except the checksum field */
    size_t len = offsetof(config_zone_store_t, checksum);
    return crc16_ccitt((const uint8_t *)zones, len);
}

/* ============================================================================
 * Encryption Helpers
 * ============================================================================ */

#ifndef TEST_HOST
static void derive_device_key(uint8_t key[16])
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    /* Simple HKDF-like derivation using HMAC-SHA256 */
    const char *salt = "rs1_config_key_v1";
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t *)salt, strlen(salt));
    mbedtls_md_hmac_update(&ctx, mac, 6);

    uint8_t hash[32];
    mbedtls_md_hmac_finish(&ctx, hash);
    mbedtls_md_free(&ctx);

    memcpy(key, hash, 16);
}

static esp_err_t encrypt_blob(const uint8_t *plain, uint8_t *cipher,
                               size_t len, const uint8_t key[16])
{
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);

    /* Simple ECB mode for small blobs - adequate for config storage */
    size_t blocks = (len + 15) / 16;
    uint8_t block[16];

    for (size_t i = 0; i < blocks; i++) {
        size_t offset = i * 16;
        size_t block_len = (len - offset < 16) ? (len - offset) : 16;

        memset(block, 0, 16);
        memcpy(block, plain + offset, block_len);
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, block, cipher + offset);
    }

    mbedtls_aes_free(&aes);
    return ESP_OK;
}

static esp_err_t decrypt_blob(const uint8_t *cipher, uint8_t *plain,
                               size_t len, const uint8_t key[16])
{
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, key, 128);

    size_t blocks = (len + 15) / 16;
    for (size_t i = 0; i < blocks; i++) {
        size_t offset = i * 16;
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT,
                              cipher + offset, plain + offset);
    }

    mbedtls_aes_free(&aes);
    return ESP_OK;
}
#endif

/* ============================================================================
 * NVS Helpers
 * ============================================================================ */

#ifndef TEST_HOST
static bool nvs_key_exists(const char *key)
{
    size_t len = 0;
    esp_err_t err = nvs_get_blob(s_config.nvs_handle, key, NULL, &len);
    return (err == ESP_OK && len > 0);
}

static esp_err_t nvs_read_blob(const char *key, void *data, size_t len)
{
    size_t actual_len = len;
    esp_err_t err = nvs_get_blob(s_config.nvs_handle, key, data, &actual_len);
    if (err != ESP_OK) {
        return err;
    }
    if (actual_len != len) {
        ESP_LOGW(TAG, "Blob size mismatch for %s: expected %d, got %d",
                 key, (int)len, (int)actual_len);
    }
    return ESP_OK;
}

static esp_err_t nvs_write_blob(const char *key, const void *data, size_t len)
{
    esp_err_t err = nvs_set_blob(s_config.nvs_handle, key, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s: %s", key, esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t nvs_erase(const char *key)
{
    return nvs_erase_key(s_config.nvs_handle, key);
}
#else
/* Test stubs */
static bool nvs_key_exists(const char *key) { return false; }
static esp_err_t nvs_read_blob(const char *key, void *data, size_t len) {
    return ESP_ERR_NVS_NOT_FOUND;
}
static esp_err_t nvs_write_blob(const char *key, const void *data, size_t len) {
    return ESP_OK;
}
static esp_err_t nvs_erase(const char *key) { return ESP_OK; }
#endif

/* ============================================================================
 * Validation
 * ============================================================================ */

static bool is_valid_zone_id(const char *id)
{
    if (!id || id[0] == '\0') {
        return false;
    }

    for (int i = 0; id[i] && i < CONFIG_ZONE_ID_LEN; i++) {
        char c = id[i];
        if (!isalnum(c) && c != '_') {
            return false;
        }
    }

    return true;
}

esp_err_t config_validate_zone(const config_zone_t *zone)
{
    if (!zone) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check ID */
    if (!is_valid_zone_id(zone->id)) {
        ESP_LOGE(TAG, "Invalid zone ID");
        return CONFIG_ERR_INVALID;
    }

    /* Check name */
    if (zone->name[0] == '\0') {
        ESP_LOGE(TAG, "Zone '%s': empty name", zone->id);
        return CONFIG_ERR_INVALID;
    }

    /* Check vertex count */
    if (zone->vertex_count < 3 || zone->vertex_count > CONFIG_MAX_VERTICES) {
        ESP_LOGE(TAG, "Zone '%s': invalid vertex count %d",
                 zone->id, zone->vertex_count);
        return CONFIG_ERR_INVALID;
    }

    /* Warn about out-of-range vertices */
    for (int i = 0; i < zone->vertex_count; i++) {
        int16_t x = zone->vertices[i][0];
        int16_t y = zone->vertices[i][1];
        if (x < -6000 || x > 6000 || y < 0 || y > 6000) {
            ESP_LOGW(TAG, "Zone '%s': vertex %d (%d, %d) outside sensor range",
                     zone->id, i, x, y);
        }
    }

    /* Check sensitivity */
    if (zone->sensitivity > 100) {
        ESP_LOGE(TAG, "Zone '%s': invalid sensitivity %d",
                 zone->id, zone->sensitivity);
        return CONFIG_ERR_INVALID;
    }

    return ESP_OK;
}

esp_err_t config_validate_zone_store(const config_zone_store_t *zones)
{
    if (!zones) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate checksum */
    uint16_t computed = config_compute_checksum(zones);
    if (zones->checksum != 0 && zones->checksum != computed) {
        ESP_LOGE(TAG, "Checksum mismatch: stored=0x%04X, computed=0x%04X",
                 zones->checksum, computed);
        return CONFIG_ERR_CHECKSUM;
    }

    /* Validate zone count */
    if (zones->zone_count > CONFIG_MAX_ZONES) {
        ESP_LOGE(TAG, "Zone count %d exceeds maximum %d",
                 zones->zone_count, CONFIG_MAX_ZONES);
        return CONFIG_ERR_INVALID;
    }

    /* Validate each zone */
    for (int i = 0; i < zones->zone_count; i++) {
        esp_err_t err = config_validate_zone(&zones->zones[i]);
        if (err != ESP_OK) {
            return err;
        }

        /* Check for duplicate IDs */
        for (int j = 0; j < i; j++) {
            if (strcmp(zones->zones[i].id, zones->zones[j].id) == 0) {
                ESP_LOGE(TAG, "Duplicate zone ID: %s", zones->zones[i].id);
                return CONFIG_ERR_INVALID;
            }
        }
    }

    return ESP_OK;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

esp_err_t config_store_init(void)
{
    if (s_config.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

#ifndef TEST_HOST
    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Open NVS namespace */
    err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &s_config.nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Derive device encryption key */
    derive_device_key(s_config.device_key);
#endif

    /* Check for incomplete transaction */
    if (nvs_key_exists(KEY_ZONES_NEW)) {
        ESP_LOGW(TAG, "Found incomplete zone write, discarding");
        nvs_erase(KEY_ZONES_NEW);
#ifndef TEST_HOST
        nvs_commit(s_config.nvs_handle);
#endif
    }

    /* Validate primary zone config */
    if (nvs_key_exists(KEY_ZONES)) {
        config_zone_store_t zones;
        if (nvs_read_blob(KEY_ZONES, &zones, sizeof(zones)) == ESP_OK) {
            if (config_validate_zone_store(&zones) != ESP_OK) {
                ESP_LOGW(TAG, "Zone config corrupted, attempting rollback");
                if (config_has_zone_rollback()) {
                    config_rollback_zones();
                } else {
                    ESP_LOGW(TAG, "No rollback available, erasing zones");
                    nvs_erase(KEY_ZONES);
#ifndef TEST_HOST
                    nvs_commit(s_config.nvs_handle);
#endif
                }
            }
        }
    }

    memset(&s_config.stats, 0, sizeof(s_config.stats));
    s_config.initialized = true;
    ESP_LOGI(TAG, "Initialized");

    return ESP_OK;
}

void config_store_deinit(void)
{
    if (!s_config.initialized) {
        return;
    }

#ifndef TEST_HOST
    nvs_close(s_config.nvs_handle);
#endif

    memset(&s_config, 0, sizeof(s_config));
    ESP_LOGI(TAG, "Deinitialized");
}

/* ============================================================================
 * Zone Configuration
 * ============================================================================ */

esp_err_t config_get_zones(config_zone_store_t *out)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_read_blob(KEY_ZONES, out, sizeof(config_zone_store_t));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Return empty zone store */
        memset(out, 0, sizeof(config_zone_store_t));
        return ESP_OK;
    }

    return err;
}

esp_err_t config_get_zone(const char *zone_id, config_zone_t *out)
{
    if (!zone_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    config_zone_store_t store;
    esp_err_t err = config_get_zones(&store);
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < store.zone_count; i++) {
        if (strcmp(store.zones[i].id, zone_id) == 0) {
            memcpy(out, &store.zones[i], sizeof(config_zone_t));
            return ESP_OK;
        }
    }

    return CONFIG_ERR_NOT_FOUND;
}

esp_err_t config_set_zones(const config_zone_store_t *zones)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!zones) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate */
    esp_err_t err = config_validate_zone_store(zones);
    if (err != ESP_OK) {
        s_config.stats.validation_failures++;
        return err;
    }

    /* Prepare versioned copy */
    config_zone_store_t versioned = *zones;
    versioned.version = config_get_zone_version() + 1;
#ifndef TEST_HOST
    versioned.updated_at = (uint32_t)time(NULL);
#else
    versioned.updated_at = 0;
#endif
    versioned.checksum = config_compute_checksum(&versioned);

    /* Atomic write sequence */
    /* 1. Write to shadow key */
    err = nvs_write_blob(KEY_ZONES_NEW, &versioned, sizeof(versioned));
    if (err != ESP_OK) {
        return CONFIG_ERR_FLASH;
    }

    /* 2. Backup current to previous (if exists) */
    if (nvs_key_exists(KEY_ZONES)) {
        config_zone_store_t current;
        if (nvs_read_blob(KEY_ZONES, &current, sizeof(current)) == ESP_OK) {
            nvs_write_blob(KEY_ZONES_PREV, &current, sizeof(current));
        }
    }

    /* 3. Copy shadow to primary */
    err = nvs_write_blob(KEY_ZONES, &versioned, sizeof(versioned));
    if (err != ESP_OK) {
        return CONFIG_ERR_FLASH;
    }

    /* 4. Erase shadow */
    nvs_erase(KEY_ZONES_NEW);

    /* 5. Commit */
#ifndef TEST_HOST
    err = nvs_commit(s_config.nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        return CONFIG_ERR_FLASH;
    }
#endif

    s_config.stats.writes_total++;
    ESP_LOGI(TAG, "Zones saved (version=%lu, count=%d)",
             (unsigned long)versioned.version, versioned.zone_count);

    return ESP_OK;
}

uint32_t config_get_zone_version(void)
{
    config_zone_store_t store;
    if (config_get_zones(&store) == ESP_OK) {
        return store.version;
    }
    return 0;
}

esp_err_t config_rollback_zones(void)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }

    if (!config_has_zone_rollback()) {
        return CONFIG_ERR_ROLLBACK_UNAVAIL;
    }

    config_zone_store_t prev;
    esp_err_t err = nvs_read_blob(KEY_ZONES_PREV, &prev, sizeof(prev));
    if (err != ESP_OK) {
        return CONFIG_ERR_ROLLBACK_UNAVAIL;
    }

    /* Validate previous config */
    if (config_validate_zone_store(&prev) != ESP_OK) {
        ESP_LOGE(TAG, "Rollback config is also corrupted");
        return CONFIG_ERR_INVALID;
    }

    /* Restore */
    err = nvs_write_blob(KEY_ZONES, &prev, sizeof(prev));
    if (err != ESP_OK) {
        return CONFIG_ERR_FLASH;
    }

#ifndef TEST_HOST
    nvs_commit(s_config.nvs_handle);
#endif

    s_config.stats.rollbacks++;
    ESP_LOGI(TAG, "Zones rolled back to version %lu", (unsigned long)prev.version);

    return ESP_OK;
}

bool config_has_zone_rollback(void)
{
    return nvs_key_exists(KEY_ZONES_PREV);
}

/* ============================================================================
 * Device Settings
 * ============================================================================ */

void config_get_device_defaults(config_device_t *out)
{
    memset(out, 0, sizeof(config_device_t));
    strncpy(out->device_name, "rs1-sensor", CONFIG_DEVICE_NAME_LEN - 1);
    strncpy(out->friendly_name, "RS-1 Presence Sensor", CONFIG_FRIENDLY_NAME_LEN - 1);
    out->default_sensitivity = 50;
    out->telemetry_enabled = false;
    out->state_throttle_ms = 100;
}

esp_err_t config_get_device(config_device_t *out)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_read_blob(KEY_DEVICE, out, sizeof(config_device_t));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        config_get_device_defaults(out);
        return ESP_OK;
    }

    return err;
}

esp_err_t config_set_device(const config_device_t *settings)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!settings) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_write_blob(KEY_DEVICE, settings, sizeof(config_device_t));
    if (err != ESP_OK) {
        return CONFIG_ERR_FLASH;
    }

#ifndef TEST_HOST
    nvs_commit(s_config.nvs_handle);
#endif

    s_config.stats.writes_total++;
    ESP_LOGI(TAG, "Device settings saved");

    return ESP_OK;
}

/* ============================================================================
 * Network Configuration
 * ============================================================================ */

esp_err_t config_get_network(config_network_t *out)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read encrypted blob */
    uint8_t encrypted[sizeof(config_network_t)];
    esp_err_t err = nvs_read_blob(KEY_NETWORK, encrypted, sizeof(encrypted));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(out, 0, sizeof(config_network_t));
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

#ifndef TEST_HOST
    /* Decrypt */
    decrypt_blob(encrypted, (uint8_t *)out, sizeof(config_network_t),
                 s_config.device_key);
#else
    memcpy(out, encrypted, sizeof(config_network_t));
#endif

    return ESP_OK;
}

esp_err_t config_set_network(const config_network_t *network)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!network) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate SSID */
    if (network->ssid[0] != '\0') {
        if (strlen(network->ssid) > 32) {
            return CONFIG_ERR_INVALID;
        }
    }

    /* Encrypt and write */
    uint8_t encrypted[sizeof(config_network_t)];
#ifndef TEST_HOST
    encrypt_blob((const uint8_t *)network, encrypted,
                 sizeof(config_network_t), s_config.device_key);
#else
    memcpy(encrypted, network, sizeof(config_network_t));
#endif

    esp_err_t err = nvs_write_blob(KEY_NETWORK, encrypted, sizeof(encrypted));
    if (err != ESP_OK) {
        return CONFIG_ERR_FLASH;
    }

#ifndef TEST_HOST
    nvs_commit(s_config.nvs_handle);
#endif

    s_config.stats.writes_total++;
    ESP_LOGI(TAG, "Network config saved");

    return ESP_OK;
}

bool config_has_network(void)
{
    config_network_t net;
    if (config_get_network(&net) != ESP_OK) {
        return false;
    }
    return net.ssid[0] != '\0';
}

/* ============================================================================
 * Security Configuration
 * ============================================================================ */

esp_err_t config_get_security(config_security_t *out)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t encrypted[sizeof(config_security_t)];
    esp_err_t err = nvs_read_blob(KEY_SECURITY, encrypted, sizeof(encrypted));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(out, 0, sizeof(config_security_t));
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

#ifndef TEST_HOST
    decrypt_blob(encrypted, (uint8_t *)out, sizeof(config_security_t),
                 s_config.device_key);
#else
    memcpy(out, encrypted, sizeof(config_security_t));
#endif

    return ESP_OK;
}

esp_err_t config_set_security(const config_security_t *security)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!security) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t encrypted[sizeof(config_security_t)];
#ifndef TEST_HOST
    encrypt_blob((const uint8_t *)security, encrypted,
                 sizeof(config_security_t), s_config.device_key);
#else
    memcpy(encrypted, security, sizeof(config_security_t));
#endif

    esp_err_t err = nvs_write_blob(KEY_SECURITY, encrypted, sizeof(encrypted));
    if (err != ESP_OK) {
        return CONFIG_ERR_FLASH;
    }

#ifndef TEST_HOST
    nvs_commit(s_config.nvs_handle);
#endif

    s_config.stats.writes_total++;
    ESP_LOGI(TAG, "Security config saved");

    return ESP_OK;
}

/* ============================================================================
 * Calibration
 * ============================================================================ */

esp_err_t config_get_calibration(config_calibration_t *out)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_read_blob(KEY_CALIBRATION, out, sizeof(config_calibration_t));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(out, 0, sizeof(config_calibration_t));
        out->mounting = CONFIG_MOUNT_WALL;
        return ESP_OK;
    }

    return err;
}

esp_err_t config_set_calibration(const config_calibration_t *calibration)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!calibration) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_write_blob(KEY_CALIBRATION, calibration,
                                    sizeof(config_calibration_t));
    if (err != ESP_OK) {
        return CONFIG_ERR_FLASH;
    }

#ifndef TEST_HOST
    nvs_commit(s_config.nvs_handle);
#endif

    s_config.stats.writes_total++;
    ESP_LOGI(TAG, "Calibration saved");

    return ESP_OK;
}

/* ============================================================================
 * Maintenance
 * ============================================================================ */

esp_err_t config_factory_reset(void)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }

    ESP_LOGW(TAG, "Factory reset initiated");

    nvs_erase(KEY_ZONES);
    nvs_erase(KEY_ZONES_PREV);
    nvs_erase(KEY_ZONES_NEW);
    nvs_erase(KEY_DEVICE);
    nvs_erase(KEY_NETWORK);
    nvs_erase(KEY_SECURITY);
    nvs_erase(KEY_CALIBRATION);

#ifndef TEST_HOST
    nvs_commit(s_config.nvs_handle);
#endif

    ESP_LOGI(TAG, "Factory reset complete");

    return ESP_OK;
}

esp_err_t config_erase(const char *key)
{
    if (!s_config.initialized) {
        return CONFIG_ERR_NOT_INITIALIZED;
    }
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_erase(key);
#ifndef TEST_HOST
    nvs_commit(s_config.nvs_handle);
#endif

    return err;
}

esp_err_t config_get_stats(config_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(stats, &s_config.stats, sizeof(config_stats_t));

#ifndef TEST_HOST
    /* Get NVS usage stats */
    nvs_stats_t nvs_stats;
    if (nvs_get_stats(CONFIG_NVS_NAMESPACE, &nvs_stats) == ESP_OK) {
        stats->nvs_used_bytes = nvs_stats.used_entries * 32;  /* Approximate */
        stats->nvs_free_bytes = nvs_stats.free_entries * 32;
    }
#endif

    return ESP_OK;
}
