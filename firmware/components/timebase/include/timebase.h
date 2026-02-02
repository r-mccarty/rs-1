/**
 * @file timebase.h
 * @brief HardwareOS Timebase / Scheduler Module (M08)
 *
 * Provides stable timing services, frame synchronization, and task scheduling
 * for the HardwareOS processing pipeline.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_TIMEBASE.md
 */

#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define TIMEBASE_DEFAULT_FRAME_INTERVAL_MS      30      /* LD2450 ~33 Hz */
#define TIMEBASE_DEFAULT_PUBLISH_THROTTLE_MS    100     /* 10 Hz to HA */
#define TIMEBASE_DEFAULT_WATCHDOG_TIMEOUT_MS    5000    /* 5 second timeout */
#define TIMEBASE_DEFAULT_NTP_SYNC_INTERVAL_MS   3600000 /* Hourly */
#define TIMEBASE_MAX_SCHEDULED_TASKS            16

/* ============================================================================
 * Watchdog Source IDs
 * ============================================================================ */

#define WATCHDOG_SOURCE_MAIN_LOOP   0
#define WATCHDOG_SOURCE_RADAR       1
#define WATCHDOG_SOURCE_WIFI        2
#define WATCHDOG_SOURCE_MAX         8

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief System time structure
 */
typedef struct {
    uint64_t boot_time_us;      /**< Microseconds since boot (esp_timer) */
    uint32_t uptime_ms;         /**< Milliseconds since boot */
    uint32_t unix_time;         /**< Unix timestamp (NTP-synced, 0 if unavailable) */
    bool ntp_synced;            /**< True if NTP time is valid */
} system_time_t;

/**
 * @brief Frame timing statistics
 */
typedef struct {
    uint32_t expected_interval_ms;  /**< Expected interval (30ms for LD2450) */
    uint32_t actual_interval_ms;    /**< Last measured interval */
    uint32_t jitter_ms;             /**< Maximum deviation observed */
    uint32_t missed_frames;         /**< Frames not received in time */
    uint32_t last_frame_ms;         /**< Timestamp of last frame */
    uint32_t total_frames;          /**< Total frames received */
} frame_timer_t;

/**
 * @brief Task callback function type
 */
typedef void (*task_callback_t)(void *arg);

/**
 * @brief NTP sync callback function type
 */
typedef void (*ntp_callback_t)(bool success);

/**
 * @brief Scheduled task information
 */
typedef struct {
    const char *name;               /**< Task name */
    task_callback_t callback;       /**< Callback function */
    void *arg;                      /**< Callback argument */
    uint32_t interval_ms;           /**< Execution interval */
    uint32_t last_run_ms;           /**< Last execution timestamp */
    uint32_t run_count;             /**< Total executions */
    uint32_t max_duration_us;       /**< Max observed execution time */
    bool enabled;                   /**< Task enabled flag */
} scheduled_task_t;

/**
 * @brief Watchdog state
 */
typedef struct {
    uint32_t timeout_ms;            /**< Watchdog timeout */
    uint32_t last_feed_ms;          /**< Last feed timestamp */
    uint8_t feed_sources;           /**< Bitmask of sources that fed */
    uint8_t expected_sources;       /**< Bitmask of expected sources */
    bool triggered;                 /**< True if watchdog triggered */
} watchdog_state_t;

/**
 * @brief Timebase configuration
 */
typedef struct {
    uint32_t frame_expected_ms;     /**< Expected radar frame interval */
    uint32_t publish_throttle_ms;   /**< State publish throttle */
    uint32_t watchdog_timeout_ms;   /**< Watchdog timeout */
    const char *ntp_server;         /**< NTP server address */
    uint32_t ntp_sync_interval_ms;  /**< NTP resync interval */
} timebase_config_t;

/**
 * @brief Default configuration initializer
 */
#define TIMEBASE_CONFIG_DEFAULT() { \
    .frame_expected_ms = TIMEBASE_DEFAULT_FRAME_INTERVAL_MS, \
    .publish_throttle_ms = TIMEBASE_DEFAULT_PUBLISH_THROTTLE_MS, \
    .watchdog_timeout_ms = TIMEBASE_DEFAULT_WATCHDOG_TIMEOUT_MS, \
    .ntp_server = "pool.ntp.org", \
    .ntp_sync_interval_ms = TIMEBASE_DEFAULT_NTP_SYNC_INTERVAL_MS, \
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize the timebase module
 *
 * @param config Configuration parameters (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t timebase_init(const timebase_config_t *config);

/**
 * @brief Deinitialize the timebase module
 */
void timebase_deinit(void);

/* ============================================================================
 * Time Functions
 * ============================================================================ */

/**
 * @brief Get current system time
 *
 * @param time Output structure for system time
 */
void timebase_get_time(system_time_t *time);

/**
 * @brief Get uptime in milliseconds
 *
 * @return Milliseconds since boot
 */
uint32_t timebase_uptime_ms(void);

/**
 * @brief Get monotonic timestamp in microseconds
 *
 * High-resolution timer that won't overflow in practical use.
 *
 * @return Microseconds since boot
 */
uint64_t timebase_monotonic_us(void);

/**
 * @brief Trigger NTP synchronization
 *
 * Non-blocking. Result delivered via callback.
 *
 * @param callback Function to call with sync result (may be NULL)
 */
void timebase_ntp_sync(ntp_callback_t callback);

/**
 * @brief Check if NTP time is synchronized
 *
 * @return True if NTP is synced
 */
bool timebase_is_ntp_synced(void);

/**
 * @brief Get Unix timestamp
 *
 * @return Unix timestamp, or 0 if NTP not synced
 */
uint32_t timebase_unix_time(void);

/* ============================================================================
 * Frame Timing
 * ============================================================================ */

/**
 * @brief Notify that a radar frame was received
 *
 * Called by M01 Radar Ingest when a frame is parsed.
 *
 * @param frame_seq Frame sequence number
 */
void timebase_frame_received(uint32_t frame_seq);

/**
 * @brief Get frame timing statistics
 *
 * @param stats Output structure for statistics
 */
void timebase_get_frame_stats(frame_timer_t *stats);

/**
 * @brief Check if radar frame is late
 *
 * @return True if expected frame not received in time
 */
bool timebase_frame_late(void);

/**
 * @brief Reset frame timing statistics
 */
void timebase_reset_frame_stats(void);

/* ============================================================================
 * Task Scheduling
 * ============================================================================ */

/**
 * @brief Register a periodic task
 *
 * @param name Unique task name
 * @param callback Function to call
 * @param arg Argument to pass to callback
 * @param interval_ms Execution interval in milliseconds
 * @return ESP_OK on success, ESP_ERR_NO_MEM if no slots available
 */
esp_err_t scheduler_register(const char *name,
                             task_callback_t callback,
                             void *arg,
                             uint32_t interval_ms);

/**
 * @brief Unregister a periodic task
 *
 * @param name Task name to unregister
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t scheduler_unregister(const char *name);

/**
 * @brief Enable or disable a task
 *
 * @param name Task name
 * @param enabled True to enable, false to disable
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t scheduler_enable(const char *name, bool enabled);

/**
 * @brief Get task statistics
 *
 * @param name Task name
 * @param stats Output structure for statistics
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t scheduler_get_stats(const char *name, scheduled_task_t *stats);

/**
 * @brief Run scheduler tick
 *
 * Should be called from main loop. Executes any tasks due to run.
 */
void scheduler_tick(void);

/**
 * @brief Get count of registered tasks
 *
 * @return Number of registered tasks
 */
uint8_t scheduler_get_task_count(void);

/* ============================================================================
 * Watchdog
 * ============================================================================ */

/**
 * @brief Initialize watchdog with timeout
 *
 * @param timeout_ms Watchdog timeout in milliseconds
 */
void watchdog_init(uint32_t timeout_ms);

/**
 * @brief Register a watchdog feed source
 *
 * @param name Source name (for logging)
 * @return Source ID (0-7), or 0xFF if no slots available
 */
uint8_t watchdog_register_source(const char *name);

/**
 * @brief Feed watchdog from a source
 *
 * @param source_id Source ID from watchdog_register_source()
 */
void watchdog_feed(uint8_t source_id);

/**
 * @brief Set radar disconnect state
 *
 * When radar is disconnected, its watchdog feed is no longer required.
 * This prevents infinite reboot loops on hardware failure.
 *
 * @param disconnected True if radar is disconnected
 */
void watchdog_set_radar_disconnected(bool disconnected);

/**
 * @brief Check if watchdog is healthy
 *
 * @return True if all expected sources have fed recently
 */
bool watchdog_healthy(void);

/**
 * @brief Get watchdog state
 *
 * @param state Output structure for watchdog state
 */
void watchdog_get_state(watchdog_state_t *state);

/**
 * @brief Check watchdog (called from health_check task)
 *
 * This resets the hardware watchdog if all sources have fed.
 */
void watchdog_check(void);

/* ============================================================================
 * Core Pinning Helpers
 * ============================================================================ */

/**
 * @brief Pin current task to Core 0 (network tasks)
 *
 * @return ESP_OK on success
 */
esp_err_t timebase_pin_to_core0(void);

/**
 * @brief Pin current task to Core 1 (radar processing)
 *
 * @return ESP_OK on success
 */
esp_err_t timebase_pin_to_core1(void);

/* ============================================================================
 * Telemetry Getters
 * ============================================================================ */

/**
 * @brief Get total scheduler task executions
 *
 * @return Total task run count across all tasks
 */
uint32_t timebase_get_total_task_runs(void);

/**
 * @brief Get watchdog reset count
 *
 * @return Number of watchdog-triggered resets since manufacture
 */
uint32_t timebase_get_watchdog_resets(void);

#ifdef __cplusplus
}
#endif

#endif /* TIMEBASE_H */
