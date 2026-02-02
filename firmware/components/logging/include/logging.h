/**
 * @file logging.h
 * @brief HardwareOS Logging + Diagnostics Module (M09)
 *
 * Provides local logging, telemetry metrics, and system diagnostics.
 * Supports serial output, RAM ring buffer, optional flash persistence,
 * and opt-in cloud telemetry via MQTT.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_LOGGING.md
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define LOG_BUFFER_SIZE             16384   /* 16KB RAM ring buffer */
#define LOG_TAG_MAX_LEN             16
#define LOG_MESSAGE_MAX_LEN         128
#define LOG_METRIC_NAME_MAX_LEN     32
#define LOG_MAX_METRICS             32
#define LOG_HISTOGRAM_BUCKETS       8
#define LOG_FLASH_SIZE_DEFAULT      65536   /* 64KB */

/* Telemetry MQTT topics (format strings) */
#define LOG_TOPIC_TELEMETRY_FMT     "opticworks/%s/telemetry"
#define LOG_TOPIC_DIAG_REQUEST_FMT  "opticworks/%s/diag/request"
#define LOG_TOPIC_DIAG_RESPONSE_FMT "opticworks/%s/diag/response"

/* ============================================================================
 * Log Levels
 * ============================================================================ */

/**
 * @brief Log levels (compatible with ESP-IDF)
 */
typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_VERBOSE,
} log_level_t;

/**
 * @brief Log macros (module-tagged)
 */
#define LOG_E(tag, fmt, ...) log_write(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) log_write(LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) log_write(LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) log_write(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_V(tag, fmt, ...) log_write(LOG_LEVEL_VERBOSE, tag, fmt, ##__VA_ARGS__)

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Log entry structure
 */
typedef struct {
    uint32_t timestamp_ms;
    log_level_t level;
    char tag[LOG_TAG_MAX_LEN];
    char message[LOG_MESSAGE_MAX_LEN];
} log_entry_t;

/**
 * @brief Metric type
 */
typedef enum {
    METRIC_TYPE_COUNTER = 0,
    METRIC_TYPE_GAUGE,
    METRIC_TYPE_HISTOGRAM,
} metric_type_t;

/**
 * @brief Histogram data
 */
typedef struct {
    float sum;
    uint32_t count;
    float buckets[LOG_HISTOGRAM_BUCKETS];
    float bucket_bounds[LOG_HISTOGRAM_BUCKETS];
} histogram_data_t;

/**
 * @brief Telemetry metric
 */
typedef struct {
    char name[LOG_METRIC_NAME_MAX_LEN];
    metric_type_t type;
    union {
        uint32_t counter;
        float gauge;
        histogram_data_t histogram;
    } value;
    uint32_t last_update_ms;
} telemetry_metric_t;

/**
 * @brief System diagnostics
 */
typedef struct {
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t uptime_ms;
    int8_t wifi_rssi;
    uint32_t radar_frames_total;
    uint32_t radar_frames_dropped;
    uint8_t active_tracks;
    uint8_t zones_occupied;
    float cpu_usage_percent;
    uint32_t watchdog_resets;
    uint32_t boot_count;
    char reset_reason[32];
} system_diagnostics_t;

/**
 * @brief Logging configuration
 */
typedef struct {
    log_level_t default_level;          /**< Default log level */
    bool log_to_flash;                  /**< Enable flash logging */
    uint32_t flash_log_size;            /**< Flash log size bytes */
    bool telemetry_enabled;             /**< Cloud telemetry opt-in */
    bool telemetry_include_logs;        /**< Include error logs in telemetry */
    uint32_t telemetry_interval_ms;     /**< Telemetry upload interval */
} logging_config_t;

/**
 * @brief Default logging configuration
 */
#define LOGGING_CONFIG_DEFAULT() { \
    .default_level = LOG_LEVEL_INFO, \
    .log_to_flash = false, \
    .flash_log_size = LOG_FLASH_SIZE_DEFAULT, \
    .telemetry_enabled = false, \
    .telemetry_include_logs = false, \
    .telemetry_interval_ms = 60000, \
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize logging module
 *
 * @param config Configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t logging_init(const logging_config_t *config);

/**
 * @brief Deinitialize logging module
 */
void logging_deinit(void);

/* ============================================================================
 * Core Logging
 * ============================================================================ */

/**
 * @brief Write log entry
 *
 * @param level Log level
 * @param tag Module tag (max 16 chars)
 * @param fmt Format string
 * @param ... Format arguments
 */
void log_write(log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * @brief Write log entry (va_list version)
 *
 * @param level Log level
 * @param tag Module tag
 * @param fmt Format string
 * @param args Format arguments
 */
void log_writev(log_level_t level, const char *tag, const char *fmt,
                va_list args);

/**
 * @brief Set log level for specific tag
 *
 * @param tag Module tag (NULL for global default)
 * @param level Log level
 */
void log_set_level(const char *tag, log_level_t level);

/**
 * @brief Get log level for specific tag
 *
 * @param tag Module tag (NULL for global default)
 * @return Current log level
 */
log_level_t log_get_level(const char *tag);

/**
 * @brief Flush logs to flash (if enabled)
 */
void log_flush(void);

/* ============================================================================
 * Log Buffer Access
 * ============================================================================ */

/**
 * @brief Read recent log entries from ring buffer
 *
 * @param entries Output array
 * @param max_count Maximum entries to read
 * @return Number of entries read
 */
int log_read_recent(log_entry_t *entries, int max_count);

/**
 * @brief Clear log buffer
 */
void log_clear(void);

/**
 * @brief Get log buffer statistics
 *
 * @param total_entries Total entries in buffer
 * @param bytes_used Bytes used in buffer
 * @param overflow_count Number of overflows
 */
void log_get_stats(uint32_t *total_entries, uint32_t *bytes_used,
                   uint32_t *overflow_count);

/* ============================================================================
 * Telemetry Metrics
 * ============================================================================ */

/**
 * @brief Increment counter metric
 *
 * @param name Metric name (e.g., "system.boot_count")
 */
void telemetry_counter_inc(const char *name);

/**
 * @brief Add value to counter metric
 *
 * @param name Metric name
 * @param value Value to add
 */
void telemetry_counter_add(const char *name, uint32_t value);

/**
 * @brief Set gauge metric value
 *
 * @param name Metric name (e.g., "system.free_heap")
 * @param value Current value
 */
void telemetry_gauge_set(const char *name, float value);

/**
 * @brief Record histogram observation
 *
 * @param name Metric name (e.g., "zone.processing_us")
 * @param value Observed value
 */
void telemetry_histogram_observe(const char *name, float value);

/**
 * @brief Configure histogram buckets
 *
 * @param name Metric name
 * @param bounds Bucket upper bounds (ascending)
 * @param count Number of buckets (max 8)
 */
void telemetry_histogram_set_buckets(const char *name, const float *bounds,
                                      int count);

/**
 * @brief Enable or disable telemetry
 *
 * @param enabled Enable telemetry
 */
void telemetry_enable(bool enabled);

/**
 * @brief Check if telemetry is enabled
 *
 * @return True if telemetry is enabled
 */
bool telemetry_is_enabled(void);

/**
 * @brief Force flush telemetry to cloud
 *
 * @return ESP_OK on success
 */
esp_err_t telemetry_flush(void);

/**
 * @brief Get metric by name
 *
 * @param name Metric name
 * @return Metric pointer or NULL
 */
const telemetry_metric_t *telemetry_get_metric(const char *name);

/**
 * @brief Get all metrics
 *
 * @param metrics Output array
 * @param max_count Maximum metrics to retrieve
 * @return Number of metrics
 */
int telemetry_get_all_metrics(telemetry_metric_t *metrics, int max_count);

/**
 * @brief Reset all metrics
 */
void telemetry_reset_metrics(void);

/* ============================================================================
 * System Diagnostics
 * ============================================================================ */

/**
 * @brief Get system diagnostics
 *
 * @param diag Output diagnostics
 */
void diagnostics_get(system_diagnostics_t *diag);

/**
 * @brief Dump diagnostics to log
 */
void diagnostics_dump(void);

/**
 * @brief Update radar statistics (called by M01)
 *
 * @param frames_total Total frames received
 * @param frames_dropped Frames dropped
 */
void diagnostics_update_radar(uint32_t frames_total, uint32_t frames_dropped);

/**
 * @brief Update tracking statistics (called by M02)
 *
 * @param active_tracks Current active track count
 */
void diagnostics_update_tracking(uint8_t active_tracks);

/**
 * @brief Update zone statistics (called by M03)
 *
 * @param zones_occupied Current occupied zone count
 */
void diagnostics_update_zones(uint8_t zones_occupied);

/**
 * @brief Record watchdog reset
 */
void diagnostics_record_watchdog_reset(void);

/* ============================================================================
 * Flash Logging
 * ============================================================================ */

/**
 * @brief Enable flash logging
 *
 * @param enabled Enable flash persistence
 * @return ESP_OK on success
 */
esp_err_t log_flash_enable(bool enabled);

/**
 * @brief Read logs from flash
 *
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Bytes read
 */
size_t log_flash_read(char *buffer, size_t buffer_size);

/**
 * @brief Clear flash logs
 *
 * @return ESP_OK on success
 */
esp_err_t log_flash_clear(void);

/**
 * @brief Get crash log from previous boot
 *
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Bytes read (0 if no crash log)
 */
size_t log_get_crash_log(char *buffer, size_t buffer_size);

/* ============================================================================
 * MQTT Integration
 * ============================================================================ */

/**
 * @brief Get telemetry topic for this device
 *
 * @param topic Output buffer
 * @param topic_len Buffer size
 * @return ESP_OK on success
 */
esp_err_t logging_get_telemetry_topic(char *topic, size_t topic_len);

/**
 * @brief Handle diagnostic request from cloud
 *
 * @param payload Request payload (JSON)
 * @param payload_len Payload length
 * @return ESP_OK on success
 */
esp_err_t logging_handle_diag_request(const char *payload, size_t payload_len);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Get log level name
 *
 * @param level Log level
 * @return Level name string ("E", "W", "I", "D", "V")
 */
const char *log_level_to_str(log_level_t level);

/**
 * @brief Parse log level from string
 *
 * @param str Level string
 * @return Log level
 */
log_level_t log_level_from_str(const char *str);

/**
 * @brief Get uptime in milliseconds
 *
 * @return Uptime in ms
 */
uint32_t logging_get_uptime_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* LOGGING_H */
