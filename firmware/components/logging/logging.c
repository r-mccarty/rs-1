/**
 * @file logging.c
 * @brief HardwareOS Logging + Diagnostics Module (M09) Implementation
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_LOGGING.md
 */

#include "logging.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef TEST_HOST
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "security.h"
#else
/* Test host stubs */
#define ESP_LOGI(tag, fmt, ...)
#define ESP_LOGW(tag, fmt, ...)
#define ESP_LOGE(tag, fmt, ...)
typedef int SemaphoreHandle_t;
typedef int esp_timer_handle_t;
#define pdTRUE 1
#define portMAX_DELAY 0xFFFFFFFF
static inline uint32_t esp_get_free_heap_size(void) { return 100000; }
static inline uint32_t esp_get_minimum_free_heap_size(void) { return 80000; }
static inline uint32_t esp_log_timestamp(void) { return 0; }
#endif

static const char *TAG = "logging";

/* ============================================================================
 * Internal State
 * ============================================================================ */

/**
 * @brief Binary log entry in ring buffer
 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint8_t level;
    uint8_t tag_len;
    uint8_t msg_len;
    /* followed by: tag[tag_len], message[msg_len] */
} log_buffer_entry_t;

/**
 * @brief Per-tag log level override
 */
typedef struct {
    char tag[LOG_TAG_MAX_LEN];
    log_level_t level;
} tag_level_t;

#define MAX_TAG_LEVELS 16

typedef struct {
    bool initialized;
    logging_config_t config;

    /* Ring buffer */
    uint8_t *buffer;
    uint32_t buffer_size;
    uint32_t write_pos;
    uint32_t read_pos;
    uint32_t entry_count;
    uint32_t overflow_count;

    /* Tag-specific log levels */
    tag_level_t tag_levels[MAX_TAG_LEVELS];
    int tag_level_count;

    /* Telemetry metrics */
    telemetry_metric_t metrics[LOG_MAX_METRICS];
    int metric_count;

    /* Diagnostics */
    system_diagnostics_t diagnostics;

#ifndef TEST_HOST
    SemaphoreHandle_t buffer_mutex;
    SemaphoreHandle_t metric_mutex;
    esp_timer_handle_t telemetry_timer;
#endif

    /* Uptime tracking */
    uint32_t boot_time_ms;

} logging_state_t;

static logging_state_t s_state = {0};

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void write_to_buffer(log_level_t level, const char *tag,
                            const char *message, uint32_t timestamp);
static telemetry_metric_t *find_or_create_metric(const char *name,
                                                   metric_type_t type);
#ifndef TEST_HOST
static void telemetry_timer_callback(void *arg);
static void publish_telemetry(void);
#endif

/* ============================================================================
 * Initialization
 * ============================================================================ */

esp_err_t logging_init(const logging_config_t *config)
{
    if (s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_state, 0, sizeof(s_state));

    if (config) {
        s_state.config = *config;
    } else {
        logging_config_t defaults = LOGGING_CONFIG_DEFAULT();
        s_state.config = defaults;
    }

    /* Allocate ring buffer */
    s_state.buffer_size = LOG_BUFFER_SIZE;
    s_state.buffer = (uint8_t *)malloc(s_state.buffer_size);
    if (!s_state.buffer) {
        ESP_LOGE(TAG, "Failed to allocate log buffer");
        return ESP_ERR_NO_MEM;
    }

#ifndef TEST_HOST
    /* Create mutexes */
    s_state.buffer_mutex = xSemaphoreCreateMutex();
    s_state.metric_mutex = xSemaphoreCreateMutex();

    if (!s_state.buffer_mutex || !s_state.metric_mutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        free(s_state.buffer);
        return ESP_ERR_NO_MEM;
    }

    /* Create telemetry timer */
    if (s_state.config.telemetry_enabled) {
        esp_timer_create_args_t timer_args = {
            .callback = telemetry_timer_callback,
            .name = "telemetry"
        };
        esp_err_t err = esp_timer_create(&timer_args, &s_state.telemetry_timer);
        if (err == ESP_OK) {
            esp_timer_start_periodic(s_state.telemetry_timer,
                                     s_state.config.telemetry_interval_ms * 1000ULL);
        }
    }

    /* Record boot time */
    s_state.boot_time_ms = esp_log_timestamp();

    /* Initialize diagnostics */
    s_state.diagnostics.boot_count++;

    /* Get reset reason */
    esp_reset_reason_t reason = esp_reset_reason();
    const char *reason_str;
    switch (reason) {
        case ESP_RST_POWERON: reason_str = "Power-on"; break;
        case ESP_RST_SW: reason_str = "Software"; break;
        case ESP_RST_PANIC: reason_str = "Panic"; break;
        case ESP_RST_INT_WDT: reason_str = "Int WDT"; break;
        case ESP_RST_TASK_WDT: reason_str = "Task WDT"; break;
        case ESP_RST_WDT: reason_str = "WDT"; break;
        case ESP_RST_DEEPSLEEP: reason_str = "Deep sleep"; break;
        case ESP_RST_BROWNOUT: reason_str = "Brownout"; break;
        default: reason_str = "Unknown"; break;
    }
    strncpy(s_state.diagnostics.reset_reason, reason_str,
            sizeof(s_state.diagnostics.reset_reason) - 1);

    if (reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
        reason == ESP_RST_WDT) {
        s_state.diagnostics.watchdog_resets++;
    }
#endif

    s_state.initialized = true;
    ESP_LOGI(TAG, "Logging initialized (buffer: %lu bytes)",
             (unsigned long)s_state.buffer_size);

    return ESP_OK;
}

void logging_deinit(void)
{
    if (!s_state.initialized) {
        return;
    }

#ifndef TEST_HOST
    if (s_state.telemetry_timer) {
        esp_timer_stop(s_state.telemetry_timer);
        esp_timer_delete(s_state.telemetry_timer);
    }

    if (s_state.buffer_mutex) {
        vSemaphoreDelete(s_state.buffer_mutex);
    }
    if (s_state.metric_mutex) {
        vSemaphoreDelete(s_state.metric_mutex);
    }
#endif

    if (s_state.buffer) {
        free(s_state.buffer);
    }

    s_state.initialized = false;
}

/* ============================================================================
 * Core Logging
 * ============================================================================ */

void log_write(log_level_t level, const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_writev(level, tag, fmt, args);
    va_end(args);
}

void log_writev(log_level_t level, const char *tag, const char *fmt,
                va_list args)
{
    if (!s_state.initialized) {
        return;
    }

    /* Check log level */
    log_level_t threshold = log_get_level(tag);
    if (level > threshold) {
        return;
    }

    /* Format message */
    char message[LOG_MESSAGE_MAX_LEN];
    vsnprintf(message, sizeof(message), fmt, args);

    /* Get timestamp */
    uint32_t timestamp;
#ifndef TEST_HOST
    timestamp = esp_log_timestamp();
#else
    timestamp = 0;
#endif

    /* Write to buffer */
    write_to_buffer(level, tag, message, timestamp);

    /* Write to serial (ESP-IDF log) */
#ifndef TEST_HOST
    switch (level) {
        case LOG_LEVEL_ERROR:
            ESP_LOGE(tag, "%s", message);
            break;
        case LOG_LEVEL_WARN:
            ESP_LOGW(tag, "%s", message);
            break;
        case LOG_LEVEL_INFO:
            ESP_LOGI(tag, "%s", message);
            break;
        case LOG_LEVEL_DEBUG:
            ESP_LOGD(tag, "%s", message);
            break;
        case LOG_LEVEL_VERBOSE:
            ESP_LOGV(tag, "%s", message);
            break;
        default:
            break;
    }
#endif
}

void log_set_level(const char *tag, log_level_t level)
{
    if (!tag) {
        /* Set default level */
        s_state.config.default_level = level;
#ifndef TEST_HOST
        esp_log_level_set("*", (esp_log_level_t)level);
#endif
        return;
    }

    /* Find existing override */
    for (int i = 0; i < s_state.tag_level_count; i++) {
        if (strcmp(s_state.tag_levels[i].tag, tag) == 0) {
            s_state.tag_levels[i].level = level;
#ifndef TEST_HOST
            esp_log_level_set(tag, (esp_log_level_t)level);
#endif
            return;
        }
    }

    /* Add new override */
    if (s_state.tag_level_count < MAX_TAG_LEVELS) {
        strncpy(s_state.tag_levels[s_state.tag_level_count].tag, tag,
                LOG_TAG_MAX_LEN - 1);
        s_state.tag_levels[s_state.tag_level_count].level = level;
        s_state.tag_level_count++;
#ifndef TEST_HOST
        esp_log_level_set(tag, (esp_log_level_t)level);
#endif
    }
}

log_level_t log_get_level(const char *tag)
{
    if (tag) {
        for (int i = 0; i < s_state.tag_level_count; i++) {
            if (strcmp(s_state.tag_levels[i].tag, tag) == 0) {
                return s_state.tag_levels[i].level;
            }
        }
    }
    return s_state.config.default_level;
}

void log_flush(void)
{
    if (!s_state.config.log_to_flash) {
        return;
    }

    /* TODO: Write buffer to flash */
    ESP_LOGI(TAG, "Flushing logs to flash");
}

/* ============================================================================
 * Log Buffer Access
 * ============================================================================ */

int log_read_recent(log_entry_t *entries, int max_count)
{
    if (!entries || max_count <= 0 || !s_state.initialized) {
        return 0;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.buffer_mutex, portMAX_DELAY);
#endif

    int count = 0;
    uint32_t pos = s_state.read_pos;

    while (count < max_count && pos != s_state.write_pos) {
        /* Read entry header */
        if (pos + sizeof(log_buffer_entry_t) > s_state.buffer_size) {
            pos = 0;  /* Wrap around */
        }

        log_buffer_entry_t *header = (log_buffer_entry_t *)&s_state.buffer[pos];
        pos += sizeof(log_buffer_entry_t);

        /* Read tag */
        if (pos + header->tag_len > s_state.buffer_size) {
            pos = 0;
        }
        memcpy(entries[count].tag, &s_state.buffer[pos], header->tag_len);
        entries[count].tag[header->tag_len] = '\0';
        pos += header->tag_len;

        /* Read message */
        if (pos + header->msg_len > s_state.buffer_size) {
            pos = 0;
        }
        memcpy(entries[count].message, &s_state.buffer[pos], header->msg_len);
        entries[count].message[header->msg_len] = '\0';
        pos += header->msg_len;

        entries[count].timestamp_ms = header->timestamp_ms;
        entries[count].level = (log_level_t)header->level;

        count++;
    }

#ifndef TEST_HOST
    xSemaphoreGive(s_state.buffer_mutex);
#endif

    return count;
}

void log_clear(void)
{
#ifndef TEST_HOST
    xSemaphoreTake(s_state.buffer_mutex, portMAX_DELAY);
#endif

    s_state.write_pos = 0;
    s_state.read_pos = 0;
    s_state.entry_count = 0;

#ifndef TEST_HOST
    xSemaphoreGive(s_state.buffer_mutex);
#endif
}

void log_get_stats(uint32_t *total_entries, uint32_t *bytes_used,
                   uint32_t *overflow_count)
{
    if (total_entries) {
        *total_entries = s_state.entry_count;
    }
    if (bytes_used) {
        if (s_state.write_pos >= s_state.read_pos) {
            *bytes_used = s_state.write_pos - s_state.read_pos;
        } else {
            *bytes_used = s_state.buffer_size - s_state.read_pos + s_state.write_pos;
        }
    }
    if (overflow_count) {
        *overflow_count = s_state.overflow_count;
    }
}

/* ============================================================================
 * Telemetry Metrics
 * ============================================================================ */

void telemetry_counter_inc(const char *name)
{
    telemetry_counter_add(name, 1);
}

void telemetry_counter_add(const char *name, uint32_t value)
{
    if (!s_state.initialized || !name) {
        return;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.metric_mutex, portMAX_DELAY);
#endif

    telemetry_metric_t *metric = find_or_create_metric(name, METRIC_TYPE_COUNTER);
    if (metric) {
        metric->value.counter += value;
        metric->last_update_ms = logging_get_uptime_ms();
    }

#ifndef TEST_HOST
    xSemaphoreGive(s_state.metric_mutex);
#endif
}

void telemetry_gauge_set(const char *name, float value)
{
    if (!s_state.initialized || !name) {
        return;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.metric_mutex, portMAX_DELAY);
#endif

    telemetry_metric_t *metric = find_or_create_metric(name, METRIC_TYPE_GAUGE);
    if (metric) {
        metric->value.gauge = value;
        metric->last_update_ms = logging_get_uptime_ms();
    }

#ifndef TEST_HOST
    xSemaphoreGive(s_state.metric_mutex);
#endif
}

void telemetry_histogram_observe(const char *name, float value)
{
    if (!s_state.initialized || !name) {
        return;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.metric_mutex, portMAX_DELAY);
#endif

    telemetry_metric_t *metric = find_or_create_metric(name, METRIC_TYPE_HISTOGRAM);
    if (metric) {
        metric->value.histogram.sum += value;
        metric->value.histogram.count++;

        /* Update bucket counts */
        for (int i = 0; i < LOG_HISTOGRAM_BUCKETS; i++) {
            if (value <= metric->value.histogram.bucket_bounds[i]) {
                metric->value.histogram.buckets[i]++;
                break;
            }
        }

        metric->last_update_ms = logging_get_uptime_ms();
    }

#ifndef TEST_HOST
    xSemaphoreGive(s_state.metric_mutex);
#endif
}

void telemetry_histogram_set_buckets(const char *name, const float *bounds,
                                      int count)
{
    if (!s_state.initialized || !name || !bounds || count <= 0) {
        return;
    }

    if (count > LOG_HISTOGRAM_BUCKETS) {
        count = LOG_HISTOGRAM_BUCKETS;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.metric_mutex, portMAX_DELAY);
#endif

    telemetry_metric_t *metric = find_or_create_metric(name, METRIC_TYPE_HISTOGRAM);
    if (metric) {
        for (int i = 0; i < count; i++) {
            metric->value.histogram.bucket_bounds[i] = bounds[i];
        }
    }

#ifndef TEST_HOST
    xSemaphoreGive(s_state.metric_mutex);
#endif
}

void telemetry_enable(bool enabled)
{
    s_state.config.telemetry_enabled = enabled;

#ifndef TEST_HOST
    if (enabled && s_state.telemetry_timer) {
        esp_timer_start_periodic(s_state.telemetry_timer,
                                 s_state.config.telemetry_interval_ms * 1000ULL);
    } else if (!enabled && s_state.telemetry_timer) {
        esp_timer_stop(s_state.telemetry_timer);
    }
#endif
}

bool telemetry_is_enabled(void)
{
    return s_state.config.telemetry_enabled;
}

esp_err_t telemetry_flush(void)
{
    if (!s_state.config.telemetry_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef TEST_HOST
    publish_telemetry();
#endif

    return ESP_OK;
}

const telemetry_metric_t *telemetry_get_metric(const char *name)
{
    if (!name) {
        return NULL;
    }

    for (int i = 0; i < s_state.metric_count; i++) {
        if (strcmp(s_state.metrics[i].name, name) == 0) {
            return &s_state.metrics[i];
        }
    }

    return NULL;
}

int telemetry_get_all_metrics(telemetry_metric_t *metrics, int max_count)
{
    if (!metrics || max_count <= 0) {
        return 0;
    }

    int count = s_state.metric_count;
    if (count > max_count) {
        count = max_count;
    }

    memcpy(metrics, s_state.metrics, count * sizeof(telemetry_metric_t));
    return count;
}

void telemetry_reset_metrics(void)
{
#ifndef TEST_HOST
    xSemaphoreTake(s_state.metric_mutex, portMAX_DELAY);
#endif

    s_state.metric_count = 0;
    memset(s_state.metrics, 0, sizeof(s_state.metrics));

#ifndef TEST_HOST
    xSemaphoreGive(s_state.metric_mutex);
#endif
}

/* ============================================================================
 * System Diagnostics
 * ============================================================================ */

void diagnostics_get(system_diagnostics_t *diag)
{
    if (!diag) {
        return;
    }

    /* Update dynamic values */
#ifndef TEST_HOST
    s_state.diagnostics.free_heap = esp_get_free_heap_size();
    s_state.diagnostics.min_free_heap = esp_get_minimum_free_heap_size();
    s_state.diagnostics.uptime_ms = logging_get_uptime_ms();

    /* Get WiFi RSSI */
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_state.diagnostics.wifi_rssi = ap_info.rssi;
    } else {
        s_state.diagnostics.wifi_rssi = -100;
    }

    /* CPU usage would require additional tracking */
    s_state.diagnostics.cpu_usage_percent = 0;  /* TODO */
#else
    s_state.diagnostics.free_heap = 100000;
    s_state.diagnostics.min_free_heap = 80000;
    s_state.diagnostics.uptime_ms = 0;
    s_state.diagnostics.wifi_rssi = -50;
#endif

    *diag = s_state.diagnostics;
}

void diagnostics_dump(void)
{
    system_diagnostics_t diag;
    diagnostics_get(&diag);

    LOG_I(TAG, "=== System Diagnostics ===");
    LOG_I(TAG, "Uptime: %lu ms", (unsigned long)diag.uptime_ms);
    LOG_I(TAG, "Free heap: %lu / Min: %lu",
          (unsigned long)diag.free_heap, (unsigned long)diag.min_free_heap);
    LOG_I(TAG, "WiFi RSSI: %d dBm", diag.wifi_rssi);
    LOG_I(TAG, "Radar frames: %lu (dropped: %lu)",
          (unsigned long)diag.radar_frames_total,
          (unsigned long)diag.radar_frames_dropped);
    LOG_I(TAG, "Active tracks: %u, Zones occupied: %u",
          diag.active_tracks, diag.zones_occupied);
    LOG_I(TAG, "Boot count: %lu, WDT resets: %lu",
          (unsigned long)diag.boot_count, (unsigned long)diag.watchdog_resets);
    LOG_I(TAG, "Reset reason: %s", diag.reset_reason);
}

void diagnostics_update_radar(uint32_t frames_total, uint32_t frames_dropped)
{
    s_state.diagnostics.radar_frames_total = frames_total;
    s_state.diagnostics.radar_frames_dropped = frames_dropped;
}

void diagnostics_update_tracking(uint8_t active_tracks)
{
    s_state.diagnostics.active_tracks = active_tracks;
}

void diagnostics_update_zones(uint8_t zones_occupied)
{
    s_state.diagnostics.zones_occupied = zones_occupied;
}

void diagnostics_record_watchdog_reset(void)
{
    s_state.diagnostics.watchdog_resets++;
}

/* ============================================================================
 * Flash Logging
 * ============================================================================ */

esp_err_t log_flash_enable(bool enabled)
{
    s_state.config.log_to_flash = enabled;

    if (enabled) {
        /* TODO: Initialize SPIFFS/LittleFS partition */
        ESP_LOGI(TAG, "Flash logging enabled");
    }

    return ESP_OK;
}

size_t log_flash_read(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return 0;
    }

    /* TODO: Read from flash */
    return 0;
}

esp_err_t log_flash_clear(void)
{
    /* TODO: Clear flash logs */
    return ESP_OK;
}

size_t log_get_crash_log(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return 0;
    }

    /* TODO: Read crash log from RTC memory */
    return 0;
}

/* ============================================================================
 * MQTT Integration
 * ============================================================================ */

esp_err_t logging_get_telemetry_topic(char *topic, size_t topic_len)
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

    snprintf(topic, topic_len, LOG_TOPIC_TELEMETRY_FMT, device_id);
    return ESP_OK;
}

esp_err_t logging_handle_diag_request(const char *payload, size_t payload_len)
{
    (void)payload;
    (void)payload_len;

    /* Dump diagnostics in response */
    diagnostics_dump();

    /* TODO: Publish response to MQTT */
    return ESP_OK;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char *log_level_to_str(log_level_t level)
{
    switch (level) {
        case LOG_LEVEL_ERROR:   return "E";
        case LOG_LEVEL_WARN:    return "W";
        case LOG_LEVEL_INFO:    return "I";
        case LOG_LEVEL_DEBUG:   return "D";
        case LOG_LEVEL_VERBOSE: return "V";
        default:                return "?";
    }
}

log_level_t log_level_from_str(const char *str)
{
    if (!str || strlen(str) == 0) {
        return LOG_LEVEL_INFO;
    }

    switch (str[0]) {
        case 'E': case 'e': return LOG_LEVEL_ERROR;
        case 'W': case 'w': return LOG_LEVEL_WARN;
        case 'I': case 'i': return LOG_LEVEL_INFO;
        case 'D': case 'd': return LOG_LEVEL_DEBUG;
        case 'V': case 'v': return LOG_LEVEL_VERBOSE;
        default: return LOG_LEVEL_INFO;
    }
}

uint32_t logging_get_uptime_ms(void)
{
#ifndef TEST_HOST
    return esp_log_timestamp() - s_state.boot_time_ms;
#else
    return 0;
#endif
}

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

static void write_to_buffer(log_level_t level, const char *tag,
                            const char *message, uint32_t timestamp)
{
    if (!s_state.buffer || !tag || !message) {
        return;
    }

    uint8_t tag_len = strlen(tag);
    uint8_t msg_len = strlen(message);

    if (tag_len > LOG_TAG_MAX_LEN - 1) {
        tag_len = LOG_TAG_MAX_LEN - 1;
    }
    if (msg_len > LOG_MESSAGE_MAX_LEN - 1) {
        msg_len = LOG_MESSAGE_MAX_LEN - 1;
    }

    uint32_t entry_size = sizeof(log_buffer_entry_t) + tag_len + msg_len;

#ifndef TEST_HOST
    xSemaphoreTake(s_state.buffer_mutex, portMAX_DELAY);
#endif

    /* Check if we have space, wrap if needed */
    if (s_state.write_pos + entry_size > s_state.buffer_size) {
        /* Wrap to beginning */
        s_state.write_pos = 0;
        s_state.overflow_count++;
    }

    /* Write header */
    log_buffer_entry_t header = {
        .timestamp_ms = timestamp,
        .level = (uint8_t)level,
        .tag_len = tag_len,
        .msg_len = msg_len
    };
    memcpy(&s_state.buffer[s_state.write_pos], &header, sizeof(header));
    s_state.write_pos += sizeof(header);

    /* Write tag */
    memcpy(&s_state.buffer[s_state.write_pos], tag, tag_len);
    s_state.write_pos += tag_len;

    /* Write message */
    memcpy(&s_state.buffer[s_state.write_pos], message, msg_len);
    s_state.write_pos += msg_len;

    s_state.entry_count++;

#ifndef TEST_HOST
    xSemaphoreGive(s_state.buffer_mutex);
#endif
}

static telemetry_metric_t *find_or_create_metric(const char *name,
                                                   metric_type_t type)
{
    /* Find existing metric */
    for (int i = 0; i < s_state.metric_count; i++) {
        if (strcmp(s_state.metrics[i].name, name) == 0) {
            return &s_state.metrics[i];
        }
    }

    /* Create new metric */
    if (s_state.metric_count >= LOG_MAX_METRICS) {
        ESP_LOGW(TAG, "Metric limit reached");
        return NULL;
    }

    telemetry_metric_t *metric = &s_state.metrics[s_state.metric_count];
    memset(metric, 0, sizeof(*metric));
    strncpy(metric->name, name, LOG_METRIC_NAME_MAX_LEN - 1);
    metric->type = type;

    /* Set default histogram buckets */
    if (type == METRIC_TYPE_HISTOGRAM) {
        float default_bounds[LOG_HISTOGRAM_BUCKETS] = {
            10, 50, 100, 250, 500, 1000, 5000, 10000
        };
        memcpy(metric->value.histogram.bucket_bounds, default_bounds,
               sizeof(default_bounds));
    }

    s_state.metric_count++;
    return metric;
}

#ifndef TEST_HOST

static void telemetry_timer_callback(void *arg)
{
    (void)arg;
    publish_telemetry();
}

static void publish_telemetry(void)
{
    if (!s_state.config.telemetry_enabled) {
        return;
    }

    /* Build telemetry JSON */
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }

    char device_id[33];
    security_get_device_id_hex(device_id);
    cJSON_AddStringToObject(root, "device_id", device_id);

    /* Add timestamp */
    time_t now;
    time(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    cJSON_AddStringToObject(root, "timestamp", timestamp);

    /* Add metrics */
    cJSON *metrics = cJSON_CreateObject();
    if (metrics) {
        xSemaphoreTake(s_state.metric_mutex, portMAX_DELAY);

        for (int i = 0; i < s_state.metric_count; i++) {
            telemetry_metric_t *m = &s_state.metrics[i];
            switch (m->type) {
                case METRIC_TYPE_COUNTER:
                    cJSON_AddNumberToObject(metrics, m->name, m->value.counter);
                    break;
                case METRIC_TYPE_GAUGE:
                    cJSON_AddNumberToObject(metrics, m->name, m->value.gauge);
                    break;
                case METRIC_TYPE_HISTOGRAM:
                    /* Add mean value */
                    if (m->value.histogram.count > 0) {
                        float mean = m->value.histogram.sum /
                                     m->value.histogram.count;
                        cJSON_AddNumberToObject(metrics, m->name, mean);
                    }
                    break;
            }
        }

        xSemaphoreGive(s_state.metric_mutex);
        cJSON_AddItemToObject(root, "metrics", metrics);
    }

    /* Add error logs if enabled */
    if (s_state.config.telemetry_include_logs) {
        cJSON *logs = cJSON_CreateArray();
        if (logs) {
            /* TODO: Add recent error logs */
            cJSON_AddItemToObject(root, "logs", logs);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        /* TODO: Publish via MQTT client */
        /* mqtt_publish(telemetry_topic, json); */
        ESP_LOGD(TAG, "Telemetry: %s", json);
        free(json);
    }

    cJSON_Delete(root);
}

#endif /* TEST_HOST */
