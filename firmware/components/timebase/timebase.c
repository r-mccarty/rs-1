/**
 * @file timebase.c
 * @brief HardwareOS Timebase / Scheduler Module Implementation (M08)
 *
 * Provides stable timing services, frame synchronization, and task scheduling.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_TIMEBASE.md
 */

#include "timebase.h"
#include <string.h>
#include <stdlib.h>

#ifndef TEST_HOST
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#else
/* Host testing mocks */
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
extern uint64_t esp_timer_get_time(void);
#endif

static const char *TAG = "timebase";

/* ============================================================================
 * Module State
 * ============================================================================ */

static struct {
    bool initialized;
    timebase_config_t config;

    /* System time */
    bool ntp_synced;
    ntp_callback_t ntp_callback;

    /* Frame timing */
    frame_timer_t frame_timer;

    /* Scheduler */
    scheduled_task_t tasks[TIMEBASE_MAX_SCHEDULED_TASKS];
    uint8_t task_count;

    /* Watchdog */
    watchdog_state_t watchdog;
    const char *source_names[WATCHDOG_SOURCE_MAX];
    uint8_t source_count;
    bool radar_disconnected;

    /* Stats */
    uint32_t total_task_runs;
    uint32_t watchdog_resets;
} s_timebase = {0};

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

static inline uint32_t uptime_ms_internal(void)
{
#ifndef TEST_HOST
    return (uint32_t)(esp_timer_get_time() / 1000);
#else
    return (uint32_t)(esp_timer_get_time() / 1000);
#endif
}

static scheduled_task_t *find_task(const char *name)
{
    for (int i = 0; i < s_timebase.task_count; i++) {
        if (s_timebase.tasks[i].name &&
            strcmp(s_timebase.tasks[i].name, name) == 0) {
            return &s_timebase.tasks[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * NTP Callback (ESP-IDF)
 * ============================================================================ */

#ifndef TEST_HOST
static void ntp_sync_notification_cb(struct timeval *tv)
{
    s_timebase.ntp_synced = true;
    ESP_LOGI(TAG, "NTP time synchronized");

    if (s_timebase.ntp_callback) {
        s_timebase.ntp_callback(true);
        s_timebase.ntp_callback = NULL;
    }
}
#endif

/* ============================================================================
 * Initialization
 * ============================================================================ */

esp_err_t timebase_init(const timebase_config_t *config)
{
    if (s_timebase.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Apply configuration */
    if (config) {
        s_timebase.config = *config;
    } else {
        timebase_config_t defaults = TIMEBASE_CONFIG_DEFAULT();
        s_timebase.config = defaults;
    }

    /* Initialize frame timer */
    s_timebase.frame_timer.expected_interval_ms = s_timebase.config.frame_expected_ms;
    s_timebase.frame_timer.actual_interval_ms = 0;
    s_timebase.frame_timer.jitter_ms = 0;
    s_timebase.frame_timer.missed_frames = 0;
    s_timebase.frame_timer.last_frame_ms = 0;
    s_timebase.frame_timer.total_frames = 0;

    /* Initialize watchdog */
    s_timebase.watchdog.timeout_ms = s_timebase.config.watchdog_timeout_ms;
    s_timebase.watchdog.last_feed_ms = uptime_ms_internal();
    s_timebase.watchdog.feed_sources = 0;
    s_timebase.watchdog.expected_sources = 0;
    s_timebase.watchdog.triggered = false;
    s_timebase.source_count = 0;
    s_timebase.radar_disconnected = false;

    /* Initialize scheduler */
    memset(s_timebase.tasks, 0, sizeof(s_timebase.tasks));
    s_timebase.task_count = 0;
    s_timebase.total_task_runs = 0;

    /* Load watchdog reset count from NVS */
#ifndef TEST_HOST
    nvs_handle_t nvs;
    if (nvs_open("timebase", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u32(nvs, "wdt_resets", &s_timebase.watchdog_resets);
        nvs_close(nvs);
    }

    /* Initialize task watchdog */
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = s_timebase.config.watchdog_timeout_ms,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_config);
#endif

    s_timebase.initialized = true;
    ESP_LOGI(TAG, "Initialized (frame=%ldms, wdt=%ldms)",
             (long)s_timebase.config.frame_expected_ms,
             (long)s_timebase.config.watchdog_timeout_ms);

    return ESP_OK;
}

void timebase_deinit(void)
{
    if (!s_timebase.initialized) {
        return;
    }

#ifndef TEST_HOST
    /* Stop SNTP if running */
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
#endif

    memset(&s_timebase, 0, sizeof(s_timebase));
    ESP_LOGI(TAG, "Deinitialized");
}

/* ============================================================================
 * Time Functions
 * ============================================================================ */

void timebase_get_time(system_time_t *time)
{
    if (!time) return;

#ifndef TEST_HOST
    time->boot_time_us = esp_timer_get_time();
#else
    time->boot_time_us = esp_timer_get_time();
#endif
    time->uptime_ms = (uint32_t)(time->boot_time_us / 1000);
    time->ntp_synced = s_timebase.ntp_synced;

    if (s_timebase.ntp_synced) {
#ifndef TEST_HOST
        time_t now;
        time(&now);
        time->unix_time = (uint32_t)now;
#else
        time->unix_time = 0;
#endif
    } else {
        time->unix_time = 0;
    }
}

uint32_t timebase_uptime_ms(void)
{
    return uptime_ms_internal();
}

uint64_t timebase_monotonic_us(void)
{
#ifndef TEST_HOST
    return esp_timer_get_time();
#else
    return esp_timer_get_time();
#endif
}

void timebase_ntp_sync(ntp_callback_t callback)
{
#ifndef TEST_HOST
    s_timebase.ntp_callback = callback;

    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, s_timebase.config.ntp_server);
        sntp_set_time_sync_notification_cb(ntp_sync_notification_cb);
        esp_sntp_init();
        ESP_LOGI(TAG, "NTP sync started with %s", s_timebase.config.ntp_server);
    } else {
        /* Force resync */
        esp_sntp_restart();
        ESP_LOGI(TAG, "NTP resync triggered");
    }
#else
    /* Test mode - immediate success */
    s_timebase.ntp_synced = true;
    if (callback) {
        callback(true);
    }
#endif
}

bool timebase_is_ntp_synced(void)
{
    return s_timebase.ntp_synced;
}

uint32_t timebase_unix_time(void)
{
    if (!s_timebase.ntp_synced) {
        return 0;
    }
#ifndef TEST_HOST
    time_t now;
    time(&now);
    return (uint32_t)now;
#else
    return 0;
#endif
}

/* ============================================================================
 * Frame Timing
 * ============================================================================ */

void timebase_frame_received(uint32_t frame_seq)
{
    uint32_t now = uptime_ms_internal();
    frame_timer_t *ft = &s_timebase.frame_timer;

    if (ft->last_frame_ms > 0) {
        uint32_t interval = now - ft->last_frame_ms;
        ft->actual_interval_ms = interval;

        /* Track jitter */
        int32_t deviation = (int32_t)interval - (int32_t)ft->expected_interval_ms;
        if (deviation < 0) deviation = -deviation;
        if ((uint32_t)deviation > ft->jitter_ms) {
            ft->jitter_ms = (uint32_t)deviation;
        }

        /* Detect missed frames */
        if (interval > ft->expected_interval_ms * 2) {
            uint32_t missed = (interval / ft->expected_interval_ms) - 1;
            ft->missed_frames += missed;
            ESP_LOGW(TAG, "Missed %lu radar frames (interval=%lums)",
                     (unsigned long)missed, (unsigned long)interval);
        }
    }

    ft->last_frame_ms = now;
    ft->total_frames++;

    (void)frame_seq; /* Reserved for future use */
}

void timebase_get_frame_stats(frame_timer_t *stats)
{
    if (stats) {
        *stats = s_timebase.frame_timer;
    }
}

bool timebase_frame_late(void)
{
    if (s_timebase.frame_timer.last_frame_ms == 0) {
        return false; /* No frames yet */
    }

    uint32_t now = uptime_ms_internal();
    uint32_t elapsed = now - s_timebase.frame_timer.last_frame_ms;

    /* Frame is late if >2x expected interval */
    return elapsed > s_timebase.frame_timer.expected_interval_ms * 2;
}

void timebase_reset_frame_stats(void)
{
    s_timebase.frame_timer.jitter_ms = 0;
    s_timebase.frame_timer.missed_frames = 0;
    s_timebase.frame_timer.total_frames = 0;
    s_timebase.frame_timer.last_frame_ms = 0;
}

/* ============================================================================
 * Task Scheduling
 * ============================================================================ */

esp_err_t scheduler_register(const char *name,
                             task_callback_t callback,
                             void *arg,
                             uint32_t interval_ms)
{
    if (!name || !callback || interval_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check for duplicate */
    if (find_task(name)) {
        ESP_LOGW(TAG, "Task '%s' already registered", name);
        return ESP_ERR_INVALID_STATE;
    }

    /* Find empty slot */
    if (s_timebase.task_count >= TIMEBASE_MAX_SCHEDULED_TASKS) {
        ESP_LOGE(TAG, "No scheduler slots available");
        return ESP_ERR_NO_MEM;
    }

    scheduled_task_t *task = &s_timebase.tasks[s_timebase.task_count];
    task->name = name;
    task->callback = callback;
    task->arg = arg;
    task->interval_ms = interval_ms;
    task->last_run_ms = uptime_ms_internal();
    task->run_count = 0;
    task->max_duration_us = 0;
    task->enabled = true;

    s_timebase.task_count++;

    ESP_LOGI(TAG, "Registered task '%s' (interval=%lums)", name, (unsigned long)interval_ms);
    return ESP_OK;
}

esp_err_t scheduler_unregister(const char *name)
{
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < s_timebase.task_count; i++) {
        if (s_timebase.tasks[i].name &&
            strcmp(s_timebase.tasks[i].name, name) == 0) {
            /* Shift remaining tasks */
            for (int j = i; j < s_timebase.task_count - 1; j++) {
                s_timebase.tasks[j] = s_timebase.tasks[j + 1];
            }
            s_timebase.task_count--;
            memset(&s_timebase.tasks[s_timebase.task_count], 0, sizeof(scheduled_task_t));

            ESP_LOGI(TAG, "Unregistered task '%s'", name);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t scheduler_enable(const char *name, bool enabled)
{
    scheduled_task_t *task = find_task(name);
    if (!task) {
        return ESP_ERR_NOT_FOUND;
    }

    task->enabled = enabled;
    ESP_LOGD(TAG, "Task '%s' %s", name, enabled ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t scheduler_get_stats(const char *name, scheduled_task_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    scheduled_task_t *task = find_task(name);
    if (!task) {
        return ESP_ERR_NOT_FOUND;
    }

    *stats = *task;
    return ESP_OK;
}

void scheduler_tick(void)
{
    uint32_t now = uptime_ms_internal();

    for (int i = 0; i < s_timebase.task_count; i++) {
        scheduled_task_t *task = &s_timebase.tasks[i];

        if (!task->enabled || !task->callback) {
            continue;
        }

        uint32_t elapsed = now - task->last_run_ms;
        if (elapsed >= task->interval_ms) {
            /* Execute task */
            uint64_t start_us = timebase_monotonic_us();

            task->callback(task->arg);

            uint64_t end_us = timebase_monotonic_us();
            uint32_t duration_us = (uint32_t)(end_us - start_us);

            if (duration_us > task->max_duration_us) {
                task->max_duration_us = duration_us;
            }

            task->last_run_ms = now;
            task->run_count++;
            s_timebase.total_task_runs++;
        }
    }
}

uint8_t scheduler_get_task_count(void)
{
    return s_timebase.task_count;
}

/* ============================================================================
 * Watchdog
 * ============================================================================ */

void watchdog_init(uint32_t timeout_ms)
{
    s_timebase.watchdog.timeout_ms = timeout_ms;
    s_timebase.watchdog.last_feed_ms = uptime_ms_internal();
    s_timebase.watchdog.feed_sources = 0;
    s_timebase.watchdog.expected_sources = 0;
    s_timebase.watchdog.triggered = false;

#ifndef TEST_HOST
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = timeout_ms,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&wdt_config);
#endif

    ESP_LOGI(TAG, "Watchdog initialized (timeout=%lums)", (unsigned long)timeout_ms);
}

uint8_t watchdog_register_source(const char *name)
{
    if (s_timebase.source_count >= WATCHDOG_SOURCE_MAX) {
        ESP_LOGE(TAG, "No watchdog source slots available");
        return 0xFF;
    }

    uint8_t id = s_timebase.source_count;
    s_timebase.source_names[id] = name;
    s_timebase.watchdog.expected_sources |= (1 << id);
    s_timebase.source_count++;

    ESP_LOGI(TAG, "Watchdog source '%s' registered (id=%d)", name, id);
    return id;
}

void watchdog_feed(uint8_t source_id)
{
    if (source_id >= WATCHDOG_SOURCE_MAX) {
        return;
    }

    s_timebase.watchdog.feed_sources |= (1 << source_id);
    s_timebase.watchdog.last_feed_ms = uptime_ms_internal();
}

void watchdog_set_radar_disconnected(bool disconnected)
{
    s_timebase.radar_disconnected = disconnected;

    if (disconnected) {
        /* Remove radar from expected feed sources */
        s_timebase.watchdog.expected_sources &= ~(1 << WATCHDOG_SOURCE_RADAR);
        ESP_LOGW(TAG, "Watchdog: radar feed no longer required");
    } else {
        /* Add radar back to expected sources (if it was registered) */
        if (s_timebase.source_count > WATCHDOG_SOURCE_RADAR) {
            s_timebase.watchdog.expected_sources |= (1 << WATCHDOG_SOURCE_RADAR);
            ESP_LOGI(TAG, "Watchdog: radar feed required again");
        }
    }
}

bool watchdog_healthy(void)
{
    uint8_t expected = s_timebase.watchdog.expected_sources;
    uint8_t actual = s_timebase.watchdog.feed_sources;

    return (actual & expected) == expected;
}

void watchdog_get_state(watchdog_state_t *state)
{
    if (state) {
        *state = s_timebase.watchdog;
    }
}

void watchdog_check(void)
{
    uint8_t expected = s_timebase.watchdog.expected_sources;
    uint8_t actual = s_timebase.watchdog.feed_sources;

    if ((actual & expected) != expected) {
        uint8_t missing = expected & ~actual;
        ESP_LOGW(TAG, "Watchdog: missing feeds from sources 0x%02X", missing);

        /* Log which sources are missing */
        for (int i = 0; i < s_timebase.source_count; i++) {
            if (missing & (1 << i)) {
                ESP_LOGW(TAG, "  - Missing: %s", s_timebase.source_names[i]);
            }
        }
    } else {
        /* All sources fed - reset hardware watchdog */
#ifndef TEST_HOST
        esp_task_wdt_reset();
#endif
    }

    /* Clear feed sources for next interval */
    s_timebase.watchdog.feed_sources = 0;
}

/* ============================================================================
 * Core Pinning Helpers
 * ============================================================================ */

#ifndef TEST_HOST
esp_err_t timebase_pin_to_core0(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    vTaskCoreAffinitySet(task, (1 << 0));
    ESP_LOGD(TAG, "Task pinned to Core 0");
    return ESP_OK;
}

esp_err_t timebase_pin_to_core1(void)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    vTaskCoreAffinitySet(task, (1 << 1));
    ESP_LOGD(TAG, "Task pinned to Core 1");
    return ESP_OK;
}
#else
esp_err_t timebase_pin_to_core0(void) { return ESP_OK; }
esp_err_t timebase_pin_to_core1(void) { return ESP_OK; }
#endif

/* ============================================================================
 * Telemetry Getters
 * ============================================================================ */

uint32_t timebase_get_total_task_runs(void)
{
    return s_timebase.total_task_runs;
}

uint32_t timebase_get_watchdog_resets(void)
{
    return s_timebase.watchdog_resets;
}
