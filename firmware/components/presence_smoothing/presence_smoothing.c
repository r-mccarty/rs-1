/**
 * @file presence_smoothing.c
 * @brief HardwareOS Presence Smoothing Module Implementation (M04)
 *
 * Applies hysteresis, hold timers, and confidence-based smoothing.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md
 */

#include "presence_smoothing.h"
#include "timebase.h"
#include <string.h>

#ifndef TEST_HOST
#include "esp_log.h"
#else
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

static const char *TAG = "smoothing";

/* ============================================================================
 * Internal Data Structures
 * ============================================================================ */

/**
 * @brief Internal zone state for smoothing
 */
typedef struct {
    char zone_id[SMOOTHING_ZONE_ID_MAX_LEN];
    smoothing_state_t state;
    bool raw_occupied;
    bool smoothed_occupied;
    uint8_t target_count;
    uint8_t last_confidence;
    uint32_t state_enter_ms;        /* When entered current state */
    uint32_t occupied_since_ms;
    uint32_t vacant_since_ms;

    /* Configuration */
    uint8_t sensitivity;
    uint16_t hold_time_ms;
    uint16_t enter_delay_ms;

    /* Timers */
    uint32_t timer_start_ms;
    uint16_t timer_duration_ms;
} internal_zone_state_t;

/* ============================================================================
 * Module State
 * ============================================================================ */

static struct {
    bool initialized;
    presence_smoothing_config_t config;

    /* Zone states */
    internal_zone_state_t zones[SMOOTHING_MAX_ZONES];
    uint8_t zone_count;

    /* Global binary presence (for Lite variant) */
    internal_zone_state_t global_state;

    /* Statistics */
    uint32_t frames_processed;
    uint32_t state_changes;
    uint32_t hold_extensions;
    uint32_t false_occupancy_prevented;
    uint32_t false_vacancy_prevented;
    uint32_t processing_time_us;
    uint32_t max_processing_time_us;
} s_smoothing = {0};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

uint16_t presence_smoothing_calc_hold_time(uint8_t sensitivity)
{
    /* hold_time_ms = (100 - sensitivity) * 50 → 0-5000ms */
    return (uint16_t)(100 - sensitivity) * 50;
}

uint16_t presence_smoothing_calc_enter_delay(uint8_t sensitivity)
{
    /* enter_delay_ms = (100 - sensitivity) * 5 → 0-500ms */
    return (uint16_t)(100 - sensitivity) * 5;
}

/**
 * @brief Find zone state by ID
 */
static internal_zone_state_t *find_zone(const char *zone_id)
{
    for (int i = 0; i < s_smoothing.zone_count; i++) {
        if (strcmp(s_smoothing.zones[i].zone_id, zone_id) == 0) {
            return &s_smoothing.zones[i];
        }
    }
    return NULL;
}

/**
 * @brief Initialize a zone state
 */
static void init_zone_state(internal_zone_state_t *zone, const char *zone_id)
{
    memset(zone, 0, sizeof(internal_zone_state_t));
    strncpy(zone->zone_id, zone_id, SMOOTHING_ZONE_ID_MAX_LEN - 1);
    zone->state = SMOOTHING_STATE_VACANT;
    zone->sensitivity = s_smoothing.config.default_sensitivity;
    zone->hold_time_ms = presence_smoothing_calc_hold_time(zone->sensitivity);
    zone->enter_delay_ms = presence_smoothing_calc_enter_delay(zone->sensitivity);
    zone->vacant_since_ms = timebase_uptime_ms();
}

/**
 * @brief Calculate effective hold time with confidence weighting
 */
static uint16_t calc_effective_hold(internal_zone_state_t *zone)
{
    uint16_t base_hold = zone->hold_time_ms;

    if (!s_smoothing.config.use_confidence_weighting) {
        return base_hold;
    }

    /* Apply confidence weighting */
    if (zone->last_confidence > s_smoothing.config.confidence_boost_threshold) {
        /* High confidence: extend hold by 50% */
        uint16_t extended = base_hold + base_hold / 2;
        s_smoothing.hold_extensions++;
        return MIN(extended, s_smoothing.config.max_hold_ms);
    } else if (zone->last_confidence < 30) {
        /* Low confidence: reduce hold by 50% */
        return MAX(base_hold / 2, s_smoothing.config.min_hold_ms);
    }

    return base_hold;
}

/**
 * @brief Process state machine for a zone
 */
static void process_zone_state_machine(internal_zone_state_t *zone,
                                        bool raw_occupied,
                                        uint8_t target_count,
                                        uint8_t avg_confidence,
                                        uint32_t timestamp_ms)
{
    zone->raw_occupied = raw_occupied;
    zone->target_count = target_count;
    zone->last_confidence = avg_confidence;

    bool prev_smoothed = zone->smoothed_occupied;
    smoothing_state_t prev_state = zone->state;

    switch (zone->state) {
        case SMOOTHING_STATE_VACANT:
            if (raw_occupied) {
                /* Start entering */
                zone->state = SMOOTHING_STATE_ENTERING;
                zone->state_enter_ms = timestamp_ms;
                zone->timer_start_ms = timestamp_ms;
                zone->timer_duration_ms = zone->enter_delay_ms;
                ESP_LOGD(TAG, "Zone '%s': VACANT → ENTERING", zone->zone_id);
            }
            zone->smoothed_occupied = false;
            break;

        case SMOOTHING_STATE_ENTERING:
            if (!raw_occupied) {
                /* False alarm - go back to vacant */
                zone->state = SMOOTHING_STATE_VACANT;
                zone->state_enter_ms = timestamp_ms;
                s_smoothing.false_occupancy_prevented++;
                ESP_LOGD(TAG, "Zone '%s': ENTERING → VACANT (canceled)", zone->zone_id);
            } else if (timestamp_ms - zone->timer_start_ms >= zone->timer_duration_ms) {
                /* Enter delay expired with raw still occupied */
                zone->state = SMOOTHING_STATE_OCCUPIED;
                zone->state_enter_ms = timestamp_ms;
                zone->occupied_since_ms = timestamp_ms;
                zone->vacant_since_ms = 0;
                zone->smoothed_occupied = true;
                ESP_LOGD(TAG, "Zone '%s': ENTERING → OCCUPIED", zone->zone_id);
            }
            /* Keep smoothed_occupied = false while entering */
            zone->smoothed_occupied = (zone->state == SMOOTHING_STATE_OCCUPIED);
            break;

        case SMOOTHING_STATE_OCCUPIED:
            if (!raw_occupied) {
                /* Start holding */
                zone->state = SMOOTHING_STATE_HOLDING;
                zone->state_enter_ms = timestamp_ms;
                zone->timer_start_ms = timestamp_ms;
                zone->timer_duration_ms = calc_effective_hold(zone);
                ESP_LOGD(TAG, "Zone '%s': OCCUPIED → HOLDING (hold=%dms)",
                         zone->zone_id, zone->timer_duration_ms);
            }
            zone->smoothed_occupied = true;
            break;

        case SMOOTHING_STATE_HOLDING:
            if (raw_occupied) {
                /* Cancel hold - back to occupied */
                zone->state = SMOOTHING_STATE_OCCUPIED;
                zone->state_enter_ms = timestamp_ms;
                s_smoothing.false_vacancy_prevented++;
                ESP_LOGD(TAG, "Zone '%s': HOLDING → OCCUPIED (canceled)", zone->zone_id);
            } else if (timestamp_ms - zone->timer_start_ms >= zone->timer_duration_ms) {
                /* Hold expired */
                zone->state = SMOOTHING_STATE_VACANT;
                zone->state_enter_ms = timestamp_ms;
                zone->vacant_since_ms = timestamp_ms;
                zone->occupied_since_ms = 0;
                zone->smoothed_occupied = false;
                ESP_LOGD(TAG, "Zone '%s': HOLDING → VACANT (expired)", zone->zone_id);
            } else {
                /* Still holding */
                zone->smoothed_occupied = true;
            }
            break;
    }

    /* Track state changes */
    if (zone->state != prev_state) {
        s_smoothing.state_changes++;
    }

    /* Callback on smoothed occupancy change */
    if (zone->smoothed_occupied != prev_smoothed) {
        if (s_smoothing.config.state_change_callback) {
            s_smoothing.config.state_change_callback(
                zone->zone_id,
                zone->smoothed_occupied,
                s_smoothing.config.callback_user_data);
        }
    }
}

/* ============================================================================
 * Public API: Initialization
 * ============================================================================ */

esp_err_t presence_smoothing_init(const presence_smoothing_config_t *config)
{
    if (s_smoothing.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Apply configuration */
    if (config) {
        s_smoothing.config = *config;
    } else {
        presence_smoothing_config_t defaults = PRESENCE_SMOOTHING_CONFIG_DEFAULT();
        s_smoothing.config = defaults;
    }

    /* Initialize zones */
    memset(s_smoothing.zones, 0, sizeof(s_smoothing.zones));
    s_smoothing.zone_count = 0;

    /* Initialize global state for Lite variant */
    init_zone_state(&s_smoothing.global_state, "global");

    s_smoothing.initialized = true;
    ESP_LOGI(TAG, "Initialized (sensitivity=%d, confidence_weighting=%s)",
             s_smoothing.config.default_sensitivity,
             s_smoothing.config.use_confidence_weighting ? "on" : "off");

    return ESP_OK;
}

void presence_smoothing_deinit(void)
{
    if (!s_smoothing.initialized) {
        return;
    }

    memset(&s_smoothing, 0, sizeof(s_smoothing));
    ESP_LOGI(TAG, "Deinitialized");
}

void presence_smoothing_reset(void)
{
    uint32_t now = timebase_uptime_ms();

    for (int i = 0; i < s_smoothing.zone_count; i++) {
        s_smoothing.zones[i].state = SMOOTHING_STATE_VACANT;
        s_smoothing.zones[i].smoothed_occupied = false;
        s_smoothing.zones[i].raw_occupied = false;
        s_smoothing.zones[i].vacant_since_ms = now;
        s_smoothing.zones[i].occupied_since_ms = 0;
    }

    s_smoothing.global_state.state = SMOOTHING_STATE_VACANT;
    s_smoothing.global_state.smoothed_occupied = false;
    s_smoothing.global_state.raw_occupied = false;
    s_smoothing.global_state.vacant_since_ms = now;

    s_smoothing.frames_processed = 0;
    ESP_LOGI(TAG, "Reset");
}

/* ============================================================================
 * Public API: Zone Configuration
 * ============================================================================ */

esp_err_t presence_smoothing_set_sensitivity(const char *zone_id,
                                              uint8_t sensitivity)
{
    if (sensitivity > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    if (zone_id == NULL) {
        /* Global default */
        s_smoothing.config.default_sensitivity = sensitivity;

        /* Update all zones that don't have custom settings */
        for (int i = 0; i < s_smoothing.zone_count; i++) {
            s_smoothing.zones[i].sensitivity = sensitivity;
            s_smoothing.zones[i].hold_time_ms =
                presence_smoothing_calc_hold_time(sensitivity);
            s_smoothing.zones[i].enter_delay_ms =
                presence_smoothing_calc_enter_delay(sensitivity);
        }

        s_smoothing.global_state.sensitivity = sensitivity;
        s_smoothing.global_state.hold_time_ms =
            presence_smoothing_calc_hold_time(sensitivity);
        s_smoothing.global_state.enter_delay_ms =
            presence_smoothing_calc_enter_delay(sensitivity);

        return ESP_OK;
    }

    internal_zone_state_t *zone = find_zone(zone_id);
    if (!zone) {
        return ESP_ERR_NOT_FOUND;
    }

    zone->sensitivity = sensitivity;
    zone->hold_time_ms = presence_smoothing_calc_hold_time(sensitivity);
    zone->enter_delay_ms = presence_smoothing_calc_enter_delay(sensitivity);

    ESP_LOGD(TAG, "Zone '%s' sensitivity=%d (hold=%dms, delay=%dms)",
             zone_id, sensitivity, zone->hold_time_ms, zone->enter_delay_ms);

    return ESP_OK;
}

uint8_t presence_smoothing_get_sensitivity(const char *zone_id)
{
    if (zone_id == NULL) {
        return s_smoothing.config.default_sensitivity;
    }

    internal_zone_state_t *zone = find_zone(zone_id);
    if (!zone) {
        return s_smoothing.config.default_sensitivity;
    }

    return zone->sensitivity;
}

/* ============================================================================
 * Public API: Main Processing
 * ============================================================================ */

esp_err_t presence_smoothing_process_frame(const zone_frame_t *zone_frame,
                                            smoothed_frame_t *output)
{
    if (!s_smoothing.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!zone_frame || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t start_us = timebase_monotonic_us();

    /* Ensure we have state for all zones in the frame */
    for (int i = 0; i < zone_frame->zone_count; i++) {
        const zone_state_t *input = &zone_frame->states[i];

        internal_zone_state_t *zone = find_zone(input->zone_id);
        if (!zone) {
            /* New zone - initialize it */
            if (s_smoothing.zone_count >= SMOOTHING_MAX_ZONES) {
                ESP_LOGW(TAG, "Max zones reached, ignoring '%s'", input->zone_id);
                continue;
            }
            zone = &s_smoothing.zones[s_smoothing.zone_count];
            init_zone_state(zone, input->zone_id);
            s_smoothing.zone_count++;
            ESP_LOGI(TAG, "Added zone '%s'", input->zone_id);
        }

        /* Calculate average confidence from tracks */
        uint8_t avg_confidence = 50;  /* Default if no tracks */
        if (input->target_count > 0) {
            /* In real implementation, M03 would provide avg confidence */
            /* For now, assume moderate confidence */
            avg_confidence = 60;
        }

        /* Process state machine */
        process_zone_state_machine(zone,
                                    input->occupied,
                                    input->target_count,
                                    avg_confidence,
                                    zone_frame->timestamp_ms);
    }

    /* Build output */
    memset(output, 0, sizeof(smoothed_frame_t));
    output->timestamp_ms = zone_frame->timestamp_ms;
    output->zone_count = s_smoothing.zone_count;

    for (int i = 0; i < s_smoothing.zone_count; i++) {
        internal_zone_state_t *zone = &s_smoothing.zones[i];
        zone_smoothed_state_t *out = &output->zones[i];

        strncpy(out->zone_id, zone->zone_id, SMOOTHING_ZONE_ID_MAX_LEN - 1);
        out->occupied = zone->smoothed_occupied;
        out->raw_occupied = zone->raw_occupied;
        out->target_count = zone->target_count;
        out->occupied_since_ms = zone->occupied_since_ms;
        out->vacant_since_ms = zone->vacant_since_ms;
        out->state = zone->state;
    }

    s_smoothing.frames_processed++;

    /* Record processing time */
    uint64_t end_us = timebase_monotonic_us();
    s_smoothing.processing_time_us = (uint32_t)(end_us - start_us);
    if (s_smoothing.processing_time_us > s_smoothing.max_processing_time_us) {
        s_smoothing.max_processing_time_us = s_smoothing.processing_time_us;
    }

    return ESP_OK;
}

esp_err_t presence_smoothing_process_binary(bool raw_occupied,
                                             uint32_t timestamp_ms,
                                             zone_smoothed_state_t *output)
{
    if (!s_smoothing.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!output) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Process global state for Lite variant */
    process_zone_state_machine(&s_smoothing.global_state,
                                raw_occupied,
                                raw_occupied ? 1 : 0,
                                50,  /* Fixed confidence for binary */
                                timestamp_ms);

    /* Build output */
    strncpy(output->zone_id, "global", SMOOTHING_ZONE_ID_MAX_LEN - 1);
    output->occupied = s_smoothing.global_state.smoothed_occupied;
    output->raw_occupied = s_smoothing.global_state.raw_occupied;
    output->target_count = s_smoothing.global_state.target_count;
    output->occupied_since_ms = s_smoothing.global_state.occupied_since_ms;
    output->vacant_since_ms = s_smoothing.global_state.vacant_since_ms;
    output->state = s_smoothing.global_state.state;

    s_smoothing.frames_processed++;

    return ESP_OK;
}

esp_err_t presence_smoothing_get_state(const char *zone_id,
                                        zone_smoothed_state_t *state)
{
    if (!zone_id || !state) {
        return ESP_ERR_INVALID_ARG;
    }

    internal_zone_state_t *zone = find_zone(zone_id);
    if (!zone) {
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(state->zone_id, zone->zone_id, SMOOTHING_ZONE_ID_MAX_LEN - 1);
    state->occupied = zone->smoothed_occupied;
    state->raw_occupied = zone->raw_occupied;
    state->target_count = zone->target_count;
    state->occupied_since_ms = zone->occupied_since_ms;
    state->vacant_since_ms = zone->vacant_since_ms;
    state->state = zone->state;

    return ESP_OK;
}

esp_err_t presence_smoothing_get_all_states(smoothed_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    frame->timestamp_ms = timebase_uptime_ms();
    frame->zone_count = s_smoothing.zone_count;

    for (int i = 0; i < s_smoothing.zone_count; i++) {
        internal_zone_state_t *zone = &s_smoothing.zones[i];
        zone_smoothed_state_t *out = &frame->zones[i];

        strncpy(out->zone_id, zone->zone_id, SMOOTHING_ZONE_ID_MAX_LEN - 1);
        out->occupied = zone->smoothed_occupied;
        out->raw_occupied = zone->raw_occupied;
        out->target_count = zone->target_count;
        out->occupied_since_ms = zone->occupied_since_ms;
        out->vacant_since_ms = zone->vacant_since_ms;
        out->state = zone->state;
    }

    return ESP_OK;
}

bool presence_smoothing_any_occupied(void)
{
    /* Check global state first (Lite variant) */
    if (s_smoothing.global_state.smoothed_occupied) {
        return true;
    }

    /* Check all zones (Pro variant) */
    for (int i = 0; i < s_smoothing.zone_count; i++) {
        if (s_smoothing.zones[i].smoothed_occupied) {
            return true;
        }
    }

    return false;
}

uint8_t presence_smoothing_occupied_count(void)
{
    uint8_t count = 0;

    for (int i = 0; i < s_smoothing.zone_count; i++) {
        if (s_smoothing.zones[i].smoothed_occupied) {
            count++;
        }
    }

    return count;
}

/* ============================================================================
 * Public API: Statistics
 * ============================================================================ */

void presence_smoothing_get_stats(presence_smoothing_stats_t *stats)
{
    if (!stats) return;

    stats->frames_processed = s_smoothing.frames_processed;
    stats->state_changes = s_smoothing.state_changes;
    stats->hold_extensions = s_smoothing.hold_extensions;
    stats->false_occupancy_prevented = s_smoothing.false_occupancy_prevented;
    stats->false_vacancy_prevented = s_smoothing.false_vacancy_prevented;
    stats->processing_time_us = s_smoothing.processing_time_us;
    stats->max_processing_time_us = s_smoothing.max_processing_time_us;
}

void presence_smoothing_reset_stats(void)
{
    s_smoothing.frames_processed = 0;
    s_smoothing.state_changes = 0;
    s_smoothing.hold_extensions = 0;
    s_smoothing.false_occupancy_prevented = 0;
    s_smoothing.false_vacancy_prevented = 0;
    s_smoothing.processing_time_us = 0;
    s_smoothing.max_processing_time_us = 0;
}
