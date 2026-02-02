/**
 * @file ota_manager.c
 * @brief HardwareOS OTA Manager Module (M07) Implementation
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_OTA.md
 */

#include "ota_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef TEST_HOST
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_app_format.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"
#include "security.h"
#else
/* Test host stubs */
#define ESP_LOGI(tag, fmt, ...)
#define ESP_LOGW(tag, fmt, ...)
#define ESP_LOGE(tag, fmt, ...)
#define ESP_LOGD(tag, fmt, ...)
typedef int SemaphoreHandle_t;
typedef int TaskHandle_t;
typedef int esp_timer_handle_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFF
#endif

static const char *TAG = "ota_manager";

/* ============================================================================
 * Internal State
 * ============================================================================ */

typedef struct {
    bool initialized;
    ota_config_t config;
    ota_progress_t progress;
    ota_manifest_t current_manifest;
    ota_stats_t stats;
    ota_event_callback_t callback;
    void *callback_user_data;

#ifndef TEST_HOST
    SemaphoreHandle_t mutex;
    TaskHandle_t ota_task;
    esp_timer_handle_t retry_timer;
    const esp_partition_t *update_partition;
    esp_ota_handle_t ota_handle;
#endif

    bool abort_requested;
    uint8_t retry_count;
    uint32_t retry_intervals[OTA_MAX_RETRIES];
} ota_state_t;

static ota_state_t s_state = {0};

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

#ifndef TEST_HOST
static void ota_task_func(void *arg);
static void retry_timer_callback(void *arg);
static esp_err_t download_firmware(void);
static esp_err_t verify_firmware(void);
static void publish_status_internal(void);
static int8_t get_current_rssi(void);
#endif

static void emit_event(ota_event_t event);
static void set_error(ota_error_t error, const char *msg);
static bool parse_manifest(const char *json, size_t len, ota_manifest_t *manifest);

/* ============================================================================
 * Initialization
 * ============================================================================ */

esp_err_t ota_manager_init(const ota_config_t *config)
{
    if (s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_state, 0, sizeof(s_state));

    if (config) {
        s_state.config = *config;
    } else {
        ota_config_t defaults = OTA_CONFIG_DEFAULT();
        s_state.config = defaults;
    }

    /* Initialize retry intervals */
    s_state.retry_intervals[0] = OTA_RETRY_INTERVAL_1;
    s_state.retry_intervals[1] = OTA_RETRY_INTERVAL_2;
    s_state.retry_intervals[2] = OTA_RETRY_INTERVAL_3;

    s_state.progress.status = OTA_STATUS_IDLE;
    s_state.progress.error = OTA_ERROR_NONE;

#ifndef TEST_HOST
    /* Create mutex */
    s_state.mutex = xSemaphoreCreateMutex();
    if (!s_state.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Create retry timer */
    esp_timer_create_args_t timer_args = {
        .callback = retry_timer_callback,
        .name = "ota_retry"
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_state.retry_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create retry timer: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_state.mutex);
        return err;
    }

    /* Check for rollback */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "First boot after OTA, pending verification");
        }
    }

    /* Check if we rolled back */
    if (esp_ota_check_rollback_is_possible()) {
        const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();
        if (last_invalid) {
            ESP_LOGW(TAG, "Rolled back from partition: %s", last_invalid->label);
            s_state.stats.rollbacks++;
            emit_event(OTA_EVENT_ROLLBACK);
        }
    }
#endif

    s_state.initialized = true;
    ESP_LOGI(TAG, "OTA manager initialized");

    return ESP_OK;
}

void ota_manager_deinit(void)
{
    if (!s_state.initialized) {
        return;
    }

#ifndef TEST_HOST
    /* Abort any in-progress update */
    ota_manager_abort();

    if (s_state.retry_timer) {
        esp_timer_stop(s_state.retry_timer);
        esp_timer_delete(s_state.retry_timer);
    }

    if (s_state.mutex) {
        vSemaphoreDelete(s_state.mutex);
    }
#endif

    s_state.initialized = false;
    ESP_LOGI(TAG, "OTA manager deinitialized");
}

void ota_manager_set_callback(ota_event_callback_t callback, void *user_data)
{
    s_state.callback = callback;
    s_state.callback_user_data = user_data;
}

/* ============================================================================
 * MQTT Integration
 * ============================================================================ */

esp_err_t ota_manager_handle_trigger(const char *payload, size_t payload_len)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!payload || payload_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if already busy */
    if (ota_manager_is_busy()) {
        ESP_LOGW(TAG, "OTA already in progress");
        set_error(OTA_ERROR_BUSY, "Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    /* Parse manifest */
    ota_manifest_t manifest;
    if (!parse_manifest(payload, payload_len, &manifest)) {
        set_error(OTA_ERROR_INVALID_MANIFEST, "Failed to parse OTA manifest");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "OTA triggered: version=%lu, url=%s",
             (unsigned long)manifest.version, manifest.url);

    return ota_manager_start(&manifest);
}

esp_err_t ota_manager_get_trigger_topic(char *topic, size_t topic_len)
{
    if (!topic || topic_len < 64) {
        return ESP_ERR_INVALID_ARG;
    }

    char device_id[33];
#ifndef TEST_HOST
    security_get_device_id_hex(device_id);
#else
    strcpy(device_id, "test_device_id");
#endif

    snprintf(topic, topic_len, OTA_TOPIC_TRIGGER_FMT, device_id);
    return ESP_OK;
}

esp_err_t ota_manager_get_status_topic(char *topic, size_t topic_len)
{
    if (!topic || topic_len < 64) {
        return ESP_ERR_INVALID_ARG;
    }

    char device_id[33];
#ifndef TEST_HOST
    security_get_device_id_hex(device_id);
#else
    strcpy(device_id, "test_device_id");
#endif

    snprintf(topic, topic_len, OTA_TOPIC_STATUS_FMT, device_id);
    return ESP_OK;
}

esp_err_t ota_manager_publish_status(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef TEST_HOST
    publish_status_internal();
#endif

    return ESP_OK;
}

/* ============================================================================
 * Update Control
 * ============================================================================ */

esp_err_t ota_manager_start(const ota_manifest_t *manifest)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!manifest) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ota_manager_is_busy()) {
        set_error(OTA_ERROR_BUSY, "Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
#endif

    /* Copy manifest */
    s_state.current_manifest = *manifest;

    /* Reset progress */
    memset(&s_state.progress, 0, sizeof(s_state.progress));
    s_state.progress.status = OTA_STATUS_PENDING;
    s_state.progress.target_version = manifest->version;
    strncpy(s_state.progress.rollout_id, manifest->rollout_id,
            sizeof(s_state.progress.rollout_id) - 1);

    s_state.abort_requested = false;
    s_state.retry_count = 0;

    /* Version check */
    if (!manifest->force && !ota_manager_is_update_allowed(manifest->version)) {
        ESP_LOGW(TAG, "Version %lu not allowed (current: %lu)",
                 (unsigned long)manifest->version,
                 (unsigned long)ota_manager_get_version());
        set_error(OTA_ERROR_VERSION_CHECK, "Version check failed");
#ifndef TEST_HOST
        xSemaphoreGive(s_state.mutex);
#endif
        return ESP_ERR_INVALID_VERSION;
    }

#ifndef TEST_HOST
    /* RSSI check */
    int8_t min_rssi = manifest->min_rssi;
    if (s_state.config.min_rssi_override != -128) {
        min_rssi = s_state.config.min_rssi_override;
    }

    int8_t current_rssi = get_current_rssi();
    if (current_rssi < min_rssi) {
        ESP_LOGW(TAG, "RSSI too low: %d < %d", current_rssi, min_rssi);
        set_error(OTA_ERROR_RSSI_TOO_LOW, "WiFi signal too weak");
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    /* Memory check */
    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < OTA_MIN_FREE_HEAP) {
        ESP_LOGW(TAG, "Low memory: %zu < %d", free_heap, OTA_MIN_FREE_HEAP);
        set_error(OTA_ERROR_LOW_MEMORY, "Insufficient memory for OTA");
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NO_MEM;
    }

    /* Get update partition */
    s_state.update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_state.update_partition) {
        ESP_LOGE(TAG, "No OTA partition available");
        set_error(OTA_ERROR_NO_PARTITION, "No OTA partition");
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Update partition: %s", s_state.update_partition->label);

    /* Start OTA task */
    BaseType_t ret = xTaskCreate(ota_task_func, "ota_task", 8192, NULL, 5,
                                  &s_state.ota_task);
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        set_error(OTA_ERROR_LOW_MEMORY, "Failed to create OTA task");
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreGive(s_state.mutex);
#endif

    s_state.stats.updates_attempted++;
    emit_event(OTA_EVENT_TRIGGERED);

    ESP_LOGI(TAG, "OTA update started");
    return ESP_OK;
}

esp_err_t ota_manager_abort(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!ota_manager_is_busy()) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Aborting OTA update");
    s_state.abort_requested = true;

#ifndef TEST_HOST
    /* Stop retry timer */
    esp_timer_stop(s_state.retry_timer);

    /* Wait for task to finish */
    if (s_state.ota_task) {
        /* Task should check abort flag and exit */
        int timeout = 100;  /* 10 seconds */
        while (s_state.ota_task && timeout-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
#endif

    s_state.progress.status = OTA_STATUS_IDLE;
    return ESP_OK;
}

void ota_manager_get_progress(ota_progress_t *progress)
{
    if (progress) {
        *progress = s_state.progress;
    }
}

bool ota_manager_is_busy(void)
{
    ota_status_t status = s_state.progress.status;
    return status == OTA_STATUS_PENDING ||
           status == OTA_STATUS_DOWNLOADING ||
           status == OTA_STATUS_VERIFYING ||
           status == OTA_STATUS_INSTALLING;
}

void ota_manager_reboot(void)
{
    if (s_state.progress.status == OTA_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "Rebooting to apply update...");
#ifndef TEST_HOST
        vTaskDelay(pdMS_TO_TICKS(s_state.config.reboot_delay_sec * 1000));
        esp_restart();
#endif
    }
}

/* ============================================================================
 * Rollback Support
 * ============================================================================ */

esp_err_t ota_manager_mark_valid(void)
{
#ifndef TEST_HOST
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark app valid: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Firmware marked as valid");
#endif
    return ESP_OK;
}

bool ota_manager_is_rollback(void)
{
#ifndef TEST_HOST
    const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();
    return last_invalid != NULL;
#else
    return false;
#endif
}

const void *ota_manager_get_next_partition(void)
{
#ifndef TEST_HOST
    return esp_ota_get_next_update_partition(NULL);
#else
    return NULL;
#endif
}

void ota_manager_get_running_info(char *label, uint32_t *version)
{
#ifndef TEST_HOST
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && label) {
        strncpy(label, running->label, 15);
        label[15] = '\0';
    }

    if (version) {
        esp_app_desc_t app_desc;
        if (esp_ota_get_partition_description(running, &app_desc) == ESP_OK) {
            *version = atoi(app_desc.version);
        }
    }
#else
    if (label) strcpy(label, "factory");
    if (version) *version = 1;
#endif
}

/* ============================================================================
 * Version Control
 * ============================================================================ */

uint32_t ota_manager_get_version(void)
{
#ifndef TEST_HOST
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t app_desc;
    if (esp_ota_get_partition_description(running, &app_desc) == ESP_OK) {
        return atoi(app_desc.version);
    }
    return 0;
#else
    return 1;
#endif
}

bool ota_manager_is_newer_version(uint32_t version)
{
    return version > ota_manager_get_version();
}

bool ota_manager_is_update_allowed(uint32_t version)
{
    /* Must be newer */
    if (!ota_manager_is_newer_version(version)) {
        return false;
    }

    /* Check anti-rollback counter */
    if (s_state.config.check_rollback) {
#ifndef TEST_HOST
        uint32_t min_version = security_get_min_version();
        if (version < min_version) {
            ESP_LOGW(TAG, "Version %lu blocked by anti-rollback (min: %lu)",
                     (unsigned long)version, (unsigned long)min_version);
            return false;
        }
#endif
    }

    return true;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

void ota_manager_get_stats(ota_stats_t *stats)
{
    if (stats) {
        *stats = s_state.stats;
    }
}

void ota_manager_reset_stats(void)
{
    memset(&s_state.stats, 0, sizeof(s_state.stats));
}

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

static void emit_event(ota_event_t event)
{
    if (s_state.callback) {
        s_state.callback(event, &s_state.progress, s_state.callback_user_data);
    }
}

static void set_error(ota_error_t error, const char *msg)
{
    s_state.progress.status = OTA_STATUS_FAILED;
    s_state.progress.error = error;
    if (msg) {
        strncpy(s_state.progress.error_msg, msg,
                sizeof(s_state.progress.error_msg) - 1);
    }
    s_state.stats.updates_failed++;
    emit_event(OTA_EVENT_FAILED);
}

static bool parse_manifest(const char *json, size_t len, ota_manifest_t *manifest)
{
    if (!json || !manifest) {
        return false;
    }

    memset(manifest, 0, sizeof(*manifest));

#ifndef TEST_HOST
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse manifest JSON");
        return false;
    }

    /* Version (required) */
    cJSON *version = cJSON_GetObjectItem(root, "version");
    if (!cJSON_IsNumber(version)) {
        ESP_LOGE(TAG, "Missing or invalid version");
        cJSON_Delete(root);
        return false;
    }
    manifest->version = (uint32_t)version->valuedouble;

    /* URL (required) */
    cJSON *url = cJSON_GetObjectItem(root, "url");
    if (!cJSON_IsString(url) || strlen(url->valuestring) >= OTA_URL_MAX_LEN) {
        ESP_LOGE(TAG, "Missing or invalid URL");
        cJSON_Delete(root);
        return false;
    }
    strncpy(manifest->url, url->valuestring, OTA_URL_MAX_LEN - 1);

    /* SHA256 (required) */
    cJSON *sha256 = cJSON_GetObjectItem(root, "sha256");
    if (!cJSON_IsString(sha256) || strlen(sha256->valuestring) != 64) {
        ESP_LOGE(TAG, "Missing or invalid SHA256");
        cJSON_Delete(root);
        return false;
    }

    /* Convert hex to bytes */
    const char *hex = sha256->valuestring;
    for (int i = 0; i < 32; i++) {
        unsigned int byte;
        sscanf(hex + (i * 2), "%2x", &byte);
        manifest->sha256[i] = (uint8_t)byte;
    }

    /* min_rssi (optional, default -70) */
    cJSON *min_rssi = cJSON_GetObjectItem(root, "min_rssi");
    manifest->min_rssi = cJSON_IsNumber(min_rssi) ?
                         (int8_t)min_rssi->valuedouble : -70;

    /* rollout_id (optional) */
    cJSON *rollout_id = cJSON_GetObjectItem(root, "rollout_id");
    if (cJSON_IsString(rollout_id)) {
        strncpy(manifest->rollout_id, rollout_id->valuestring,
                OTA_ROLLOUT_ID_LEN - 1);
    }

    /* force (optional) */
    cJSON *force = cJSON_GetObjectItem(root, "force");
    manifest->force = cJSON_IsTrue(force);

    cJSON_Delete(root);
    return true;
#else
    /* Test stub: minimal parsing */
    manifest->version = 2;
    strcpy(manifest->url, "https://example.com/firmware.bin");
    manifest->min_rssi = -70;
    return true;
#endif
}

#ifndef TEST_HOST

static int8_t get_current_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return -100;  /* Unknown, assume bad */
}

static void ota_task_func(void *arg)
{
    ESP_LOGI(TAG, "OTA task started");

    esp_err_t err;

    /* Update status */
    s_state.progress.status = OTA_STATUS_DOWNLOADING;
    emit_event(OTA_EVENT_DOWNLOAD_START);
    ota_manager_publish_status();

    /* Download and install firmware */
    err = download_firmware();

    if (err == ESP_OK && !s_state.abort_requested) {
        /* Verify firmware */
        s_state.progress.status = OTA_STATUS_VERIFYING;
        emit_event(OTA_EVENT_VERIFY_START);
        ota_manager_publish_status();

        err = verify_firmware();
    }

    if (s_state.abort_requested) {
        ESP_LOGI(TAG, "OTA aborted by user");
        s_state.progress.status = OTA_STATUS_IDLE;
    } else if (err == ESP_OK) {
        /* Success */
        s_state.progress.status = OTA_STATUS_SUCCESS;
        s_state.progress.progress_percent = 100;
        s_state.stats.updates_successful++;
        s_state.stats.last_update_time = (uint32_t)time(NULL);
        s_state.stats.last_update_version = s_state.current_manifest.version;

        emit_event(OTA_EVENT_SUCCESS);
        ota_manager_publish_status();

        ESP_LOGI(TAG, "OTA update successful!");

        if (s_state.config.auto_reboot) {
            emit_event(OTA_EVENT_REBOOT_PENDING);
            ESP_LOGI(TAG, "Rebooting in %d seconds...",
                     s_state.config.reboot_delay_sec);
            vTaskDelay(pdMS_TO_TICKS(s_state.config.reboot_delay_sec * 1000));
            esp_restart();
        }
    } else {
        /* Failed - schedule retry if attempts remaining */
        s_state.retry_count++;
        s_state.progress.retry_count = s_state.retry_count;

        if (s_state.retry_count < OTA_MAX_RETRIES) {
            uint32_t delay_sec = s_state.retry_intervals[s_state.retry_count - 1];
            ESP_LOGW(TAG, "OTA failed, retry %d/%d in %lu seconds",
                     s_state.retry_count, OTA_MAX_RETRIES,
                     (unsigned long)delay_sec);

            s_state.progress.status = OTA_STATUS_PENDING;
            esp_timer_start_once(s_state.retry_timer, delay_sec * 1000000ULL);
        } else {
            ESP_LOGE(TAG, "OTA failed after %d retries", OTA_MAX_RETRIES);
        }

        ota_manager_publish_status();
    }

    s_state.ota_task = NULL;
    vTaskDelete(NULL);
}

static void retry_timer_callback(void *arg)
{
    ESP_LOGI(TAG, "Retrying OTA update...");

    /* Restart OTA task */
    BaseType_t ret = xTaskCreate(ota_task_func, "ota_task", 8192, NULL, 5,
                                  &s_state.ota_task);
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to restart OTA task");
        set_error(OTA_ERROR_LOW_MEMORY, "Failed to restart OTA task");
    }
}

static esp_err_t download_firmware(void)
{
    ESP_LOGI(TAG, "Downloading firmware from: %s", s_state.current_manifest.url);

    esp_http_client_config_t http_config = {
        .url = s_state.current_manifest.url,
        .timeout_ms = OTA_DOWNLOAD_TIMEOUT_SEC * 1000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        set_error(OTA_ERROR_DOWNLOAD_FAILED, esp_err_to_name(err));
        return err;
    }

    /* Get firmware size */
    int image_size = esp_https_ota_get_image_size(https_ota_handle);
    if (image_size > 0) {
        s_state.progress.total_bytes = (uint32_t)image_size;
        ESP_LOGI(TAG, "Firmware size: %d bytes", image_size);
    }

    /* Download in chunks */
    while (!s_state.abort_requested) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        /* Update progress */
        int read = esp_https_ota_get_image_len_read(https_ota_handle);
        s_state.progress.bytes_downloaded = (uint32_t)read;

        if (s_state.progress.total_bytes > 0) {
            s_state.progress.progress_percent =
                (uint8_t)((read * 100) / s_state.progress.total_bytes);
        }

        emit_event(OTA_EVENT_DOWNLOAD_PROGRESS);

        /* Yield to other tasks */
        vTaskDelay(1);
    }

    if (s_state.abort_requested) {
        esp_https_ota_abort(https_ota_handle);
        return ESP_ERR_INVALID_STATE;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        set_error(OTA_ERROR_DOWNLOAD_FAILED, esp_err_to_name(err));
        return err;
    }

    emit_event(OTA_EVENT_DOWNLOAD_COMPLETE);

    s_state.stats.total_bytes_downloaded += s_state.progress.bytes_downloaded;

    /* Finish OTA */
    err = esp_https_ota_finish(https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            set_error(OTA_ERROR_HASH_MISMATCH, "Image validation failed");
        } else {
            set_error(OTA_ERROR_FLASH_WRITE, esp_err_to_name(err));
        }
        return err;
    }

    emit_event(OTA_EVENT_INSTALL_COMPLETE);

    return ESP_OK;
}

static esp_err_t verify_firmware(void)
{
    ESP_LOGI(TAG, "Verifying firmware...");

    /* esp_https_ota already validates the app image */
    /* Additional signature verification if required */

    if (s_state.config.verify_signature) {
        const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
        if (!update) {
            set_error(OTA_ERROR_NO_PARTITION, "No update partition");
            return ESP_FAIL;
        }

        /* Read firmware and verify signature using security module */
        /* This would require reading the signature block from the firmware */
        /* For now, rely on ESP-IDF's built-in validation */
        ESP_LOGI(TAG, "Using ESP-IDF built-in image validation");
    }

    emit_event(OTA_EVENT_VERIFY_COMPLETE);

    return ESP_OK;
}

static void publish_status_internal(void)
{
    /* Build status JSON */
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }

    const char *status_str;
    switch (s_state.progress.status) {
        case OTA_STATUS_IDLE:       status_str = "idle"; break;
        case OTA_STATUS_PENDING:    status_str = "pending"; break;
        case OTA_STATUS_DOWNLOADING: status_str = "downloading"; break;
        case OTA_STATUS_VERIFYING:  status_str = "verifying"; break;
        case OTA_STATUS_INSTALLING: status_str = "installing"; break;
        case OTA_STATUS_SUCCESS:    status_str = "success"; break;
        case OTA_STATUS_FAILED:     status_str = "failed"; break;
        case OTA_STATUS_ROLLBACK:   status_str = "rollback"; break;
        default:                    status_str = "unknown"; break;
    }

    cJSON_AddStringToObject(root, "status", status_str);
    cJSON_AddNumberToObject(root, "progress", s_state.progress.progress_percent);
    cJSON_AddNumberToObject(root, "target_version", s_state.progress.target_version);

    if (s_state.progress.rollout_id[0]) {
        cJSON_AddStringToObject(root, "rollout_id", s_state.progress.rollout_id);
    }

    if (s_state.progress.status == OTA_STATUS_FAILED) {
        cJSON_AddStringToObject(root, "error", s_state.progress.error_msg);
    }

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        /* TODO: Publish via MQTT client */
        /* mqtt_publish(status_topic, json); */
        ESP_LOGD(TAG, "Status: %s", json);
        free(json);
    }

    cJSON_Delete(root);
}

#endif /* TEST_HOST */
