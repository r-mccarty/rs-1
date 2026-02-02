/**
 * @file zone_engine.c
 * @brief HardwareOS Zone Engine Module Implementation (M03)
 *
 * Maps confirmed tracks to user-defined polygon zones.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md
 */

#include "zone_engine.h"
#include "timebase.h"
#include <string.h>
#include <math.h>

#ifndef TEST_HOST
#include "esp_log.h"
#else
#include <stdio.h>
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

static const char *TAG = "zone_engine";

/* ============================================================================
 * Module State
 * ============================================================================ */

static struct {
    bool initialized;
    zone_engine_config_t config;

    /* Zone configuration */
    zone_map_t zone_map;

    /* Runtime state */
    zone_state_t states[ZONE_MAX_ZONES];
    uint8_t previous_track_zones[TRACKING_MAX_TRACKS][ZONE_MAX_ZONES]; /* Track-zone membership */

    /* Statistics */
    uint32_t frames_processed;
    uint32_t occupancy_changes;
    uint32_t tracks_excluded;
    uint32_t processing_time_us;
    uint32_t max_processing_time_us;
    uint32_t zone_evaluations;
} s_zone_engine = {0};

/* ============================================================================
 * Internal: Geometry Functions
 * ============================================================================ */

/**
 * @brief Check if two line segments intersect
 */
static bool segments_intersect(int16_t ax1, int16_t ay1, int16_t ax2, int16_t ay2,
                               int16_t bx1, int16_t by1, int16_t bx2, int16_t by2)
{
    /* Cross product helper */
    int32_t d1 = (int32_t)(bx2 - bx1) * (ay1 - by1) - (int32_t)(by2 - by1) * (ax1 - bx1);
    int32_t d2 = (int32_t)(bx2 - bx1) * (ay2 - by1) - (int32_t)(by2 - by1) * (ax2 - bx1);
    int32_t d3 = (int32_t)(ax2 - ax1) * (by1 - ay1) - (int32_t)(ay2 - ay1) * (bx1 - ax1);
    int32_t d4 = (int32_t)(ax2 - ax1) * (by2 - ay1) - (int32_t)(ay2 - ay1) * (bx2 - ax1);

    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
        ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
        return true;
    }

    return false;
}

bool zone_is_simple_polygon(const int16_t vertices[][2], uint8_t vertex_count)
{
    if (vertex_count < 3) {
        return false;
    }

    /* Check each edge against non-adjacent edges */
    for (int i = 0; i < vertex_count; i++) {
        int i_next = (i + 1) % vertex_count;

        for (int j = i + 2; j < vertex_count; j++) {
            /* Skip adjacent edges */
            if (j == (i + vertex_count - 1) % vertex_count) {
                continue;
            }

            int j_next = (j + 1) % vertex_count;

            if (segments_intersect(
                    vertices[i][0], vertices[i][1],
                    vertices[i_next][0], vertices[i_next][1],
                    vertices[j][0], vertices[j][1],
                    vertices[j_next][0], vertices[j_next][1])) {
                return false;
            }
        }
    }

    return true;
}

bool zone_point_in_polygon(int16_t x, int16_t y,
                           const int16_t vertices[][2],
                           uint8_t vertex_count)
{
    if (vertex_count < 3) {
        return false;
    }

    bool inside = false;
    int j = vertex_count - 1;

    for (int i = 0; i < vertex_count; i++) {
        int16_t xi = vertices[i][0];
        int16_t yi = vertices[i][1];
        int16_t xj = vertices[j][0];
        int16_t yj = vertices[j][1];

        /* Ray casting: count crossings */
        if (((yi > y) != (yj > y)) &&
            (x < (int32_t)(xj - xi) * (y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }

        j = i;
    }

    return inside;
}

/**
 * @brief Check if point is in zone (with edge margin handling)
 */
static bool point_in_zone(int16_t x, int16_t y, const zone_config_t *zone)
{
    /* For include zones: standard check */
    /* For exclude zones: also use standard check */
    /* Edge cases (point exactly on boundary) are considered INSIDE */
    return zone_point_in_polygon(x, y, zone->vertices, zone->vertex_count);
}

/* ============================================================================
 * Internal: Event Emission
 * ============================================================================ */

static void emit_event(zone_event_type_t type, const char *zone_id,
                       uint8_t track_id, uint32_t timestamp_ms)
{
    if (!s_zone_engine.config.event_callback) {
        return;
    }

    zone_event_t event = {
        .type = type,
        .track_id = track_id,
        .timestamp_ms = timestamp_ms
    };
    strncpy(event.zone_id, zone_id, ZONE_ID_MAX_LEN - 1);
    event.zone_id[ZONE_ID_MAX_LEN - 1] = '\0';

    s_zone_engine.config.event_callback(&event,
                                         s_zone_engine.config.callback_user_data);
}

/* ============================================================================
 * Public API: Initialization
 * ============================================================================ */

esp_err_t zone_engine_init(const zone_engine_config_t *config)
{
    if (s_zone_engine.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Apply configuration */
    if (config) {
        s_zone_engine.config = *config;
    } else {
        zone_engine_config_t defaults = ZONE_ENGINE_CONFIG_DEFAULT();
        s_zone_engine.config = defaults;
    }

    /* Clear state */
    memset(&s_zone_engine.zone_map, 0, sizeof(s_zone_engine.zone_map));
    memset(s_zone_engine.states, 0, sizeof(s_zone_engine.states));
    memset(s_zone_engine.previous_track_zones, 0,
           sizeof(s_zone_engine.previous_track_zones));

    s_zone_engine.initialized = true;
    ESP_LOGI(TAG, "Initialized (moving_threshold=%d cm/s, debounce=%d frames)",
             s_zone_engine.config.moving_threshold_cm_s,
             s_zone_engine.config.debounce_frames);

    return ESP_OK;
}

void zone_engine_deinit(void)
{
    if (!s_zone_engine.initialized) {
        return;
    }

    memset(&s_zone_engine, 0, sizeof(s_zone_engine));
    ESP_LOGI(TAG, "Deinitialized");
}

void zone_engine_reset(void)
{
    memset(s_zone_engine.states, 0, sizeof(s_zone_engine.states));
    memset(s_zone_engine.previous_track_zones, 0,
           sizeof(s_zone_engine.previous_track_zones));

    /* Reset state for each configured zone */
    for (int z = 0; z < s_zone_engine.zone_map.zone_count; z++) {
        strncpy(s_zone_engine.states[z].zone_id,
                s_zone_engine.zone_map.zones[z].id,
                ZONE_ID_MAX_LEN - 1);
    }

    s_zone_engine.frames_processed = 0;
    ESP_LOGI(TAG, "Reset");
}

/* ============================================================================
 * Public API: Zone Configuration
 * ============================================================================ */

esp_err_t zone_engine_validate_zone(const zone_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check vertex count */
    if (config->vertex_count < 3 || config->vertex_count > ZONE_MAX_VERTICES) {
        ESP_LOGE(TAG, "Zone '%s': invalid vertex count %d (must be 3-8)",
                 config->id, config->vertex_count);
        return ESP_ERR_INVALID_ARG;
    }

    /* Check for self-intersection */
    if (!zone_is_simple_polygon(config->vertices, config->vertex_count)) {
        ESP_LOGE(TAG, "Zone '%s': self-intersecting polygon", config->id);
        return ESP_ERR_INVALID_ARG;
    }

    /* Check zone ID */
    if (config->id[0] == '\0') {
        ESP_LOGE(TAG, "Zone has empty ID");
        return ESP_ERR_INVALID_ARG;
    }

    /* Warn about out-of-range vertices (but allow) */
    for (int i = 0; i < config->vertex_count; i++) {
        int16_t x = config->vertices[i][0];
        int16_t y = config->vertices[i][1];

        if (x < -6000 || x > 6000 || y < 0 || y > 6000) {
            ESP_LOGW(TAG, "Zone '%s': vertex %d (%d, %d) outside sensor range",
                     config->id, i, x, y);
        }
    }

    return ESP_OK;
}

esp_err_t zone_engine_load_zones(const zone_map_t *zone_map)
{
    if (!s_zone_engine.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!zone_map) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate all zones first */
    for (int i = 0; i < zone_map->zone_count; i++) {
        esp_err_t err = zone_engine_validate_zone(&zone_map->zones[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Zone validation failed at index %d", i);
            return err;
        }

        /* Check for duplicate IDs */
        for (int j = 0; j < i; j++) {
            if (strcmp(zone_map->zones[i].id, zone_map->zones[j].id) == 0) {
                ESP_LOGE(TAG, "Duplicate zone ID: %s", zone_map->zones[i].id);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    /* Atomic replacement */
    memcpy(&s_zone_engine.zone_map, zone_map, sizeof(zone_map_t));

    /* Reset states for new zones */
    memset(s_zone_engine.states, 0, sizeof(s_zone_engine.states));
    for (int z = 0; z < s_zone_engine.zone_map.zone_count; z++) {
        strncpy(s_zone_engine.states[z].zone_id,
                s_zone_engine.zone_map.zones[z].id,
                ZONE_ID_MAX_LEN - 1);
    }

    memset(s_zone_engine.previous_track_zones, 0,
           sizeof(s_zone_engine.previous_track_zones));

    ESP_LOGI(TAG, "Loaded %d zones (version %lu)",
             s_zone_engine.zone_map.zone_count,
             (unsigned long)s_zone_engine.zone_map.version);

    return ESP_OK;
}

esp_err_t zone_engine_get_zones(zone_map_t *zone_map)
{
    if (!zone_map) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(zone_map, &s_zone_engine.zone_map, sizeof(zone_map_t));
    return ESP_OK;
}

esp_err_t zone_engine_get_zone(const char *zone_id, zone_config_t *config)
{
    if (!zone_id || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < s_zone_engine.zone_map.zone_count; i++) {
        if (strcmp(s_zone_engine.zone_map.zones[i].id, zone_id) == 0) {
            memcpy(config, &s_zone_engine.zone_map.zones[i], sizeof(zone_config_t));
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/* ============================================================================
 * Public API: Main Processing
 * ============================================================================ */

esp_err_t zone_engine_process_frame(const track_frame_t *tracks,
                                     zone_frame_t *output)
{
    if (!s_zone_engine.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!tracks || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t start_us = timebase_monotonic_us();

    /* Initialize output */
    memset(output, 0, sizeof(zone_frame_t));
    output->timestamp_ms = tracks->timestamp_ms;
    output->zone_count = s_zone_engine.zone_map.zone_count;

    /* Track exclusion flags */
    bool track_excluded[TRACKING_MAX_TRACKS] = {false};

    /* Step 1: Check exclude zones first */
    for (int t = 0; t < tracks->track_count && t < TRACKING_MAX_TRACKS; t++) {
        const track_output_t *track = &tracks->tracks[t];

        /* Only process confirmed/occluded tracks */
        if (track->state != TRACK_STATE_CONFIRMED &&
            track->state != TRACK_STATE_OCCLUDED) {
            continue;
        }

        for (int z = 0; z < s_zone_engine.zone_map.zone_count; z++) {
            const zone_config_t *zone = &s_zone_engine.zone_map.zones[z];

            if (zone->type != ZONE_TYPE_EXCLUDE) {
                continue;
            }

            s_zone_engine.zone_evaluations++;

            if (point_in_zone(track->x_mm, track->y_mm, zone)) {
                track_excluded[t] = true;
                s_zone_engine.tracks_excluded++;
                ESP_LOGD(TAG, "Track %d excluded by zone '%s'",
                         track->track_id, zone->id);
                break;  /* No need to check other exclude zones */
            }
        }
    }

    /* Step 2: Process include zones */
    bool current_track_zones[TRACKING_MAX_TRACKS][ZONE_MAX_ZONES] = {{false}};

    for (int z = 0; z < s_zone_engine.zone_map.zone_count; z++) {
        const zone_config_t *zone = &s_zone_engine.zone_map.zones[z];
        zone_state_t *state = &s_zone_engine.states[z];

        /* Copy zone ID to state */
        strncpy(state->zone_id, zone->id, ZONE_ID_MAX_LEN - 1);

        /* Skip exclude zones (already processed) */
        if (zone->type == ZONE_TYPE_EXCLUDE) {
            /* Exclude zones are never "occupied" in output */
            state->occupied = false;
            state->target_count = 0;
            state->has_moving = false;
            continue;
        }

        /* Reset zone state for this frame */
        uint8_t new_target_count = 0;
        uint8_t new_track_ids[ZONE_MAX_TRACKS_PER_ZONE] = {0};
        bool new_has_moving = false;

        /* Check each track against this zone */
        for (int t = 0; t < tracks->track_count && t < TRACKING_MAX_TRACKS; t++) {
            const track_output_t *track = &tracks->tracks[t];

            /* Skip non-confirmed tracks */
            if (track->state != TRACK_STATE_CONFIRMED &&
                track->state != TRACK_STATE_OCCLUDED) {
                continue;
            }

            /* Skip excluded tracks */
            if (track_excluded[t]) {
                continue;
            }

            s_zone_engine.zone_evaluations++;

            if (point_in_zone(track->x_mm, track->y_mm, zone)) {
                current_track_zones[t][z] = true;

                if (new_target_count < ZONE_MAX_TRACKS_PER_ZONE) {
                    new_track_ids[new_target_count] = track->track_id;
                    new_target_count++;
                }

                /* Check if moving */
                int32_t speed_cm_s = (int32_t)track->vx_mm_s * track->vx_mm_s +
                                     (int32_t)track->vy_mm_s * track->vy_mm_s;
                speed_cm_s = (int32_t)sqrtf((float)speed_cm_s) / 10;  /* mm/s to cm/s */

                if (speed_cm_s >= s_zone_engine.config.moving_threshold_cm_s) {
                    new_has_moving = true;
                }
            }
        }

        /* Update zone state */
        bool new_occupied = (new_target_count > 0);
        bool occupancy_changed = (state->occupied != new_occupied);

        state->target_count = new_target_count;
        memcpy(state->track_ids, new_track_ids, sizeof(new_track_ids));
        state->has_moving = new_has_moving;

        if (occupancy_changed) {
            state->occupied = new_occupied;
            state->last_change_ms = tracks->timestamp_ms;
            s_zone_engine.occupancy_changes++;

            /* Emit events */
            emit_event(new_occupied ? ZONE_EVENT_OCCUPIED : ZONE_EVENT_VACANT,
                       zone->id, 0, tracks->timestamp_ms);

            ESP_LOGD(TAG, "Zone '%s' %s", zone->id,
                     new_occupied ? "occupied" : "vacant");
        }
    }

    /* Step 3: Emit ENTER/EXIT events for track-zone membership changes */
    for (int t = 0; t < TRACKING_MAX_TRACKS; t++) {
        for (int z = 0; z < s_zone_engine.zone_map.zone_count; z++) {
            bool was_in = s_zone_engine.previous_track_zones[t][z];
            bool is_in = current_track_zones[t][z];

            if (!was_in && is_in) {
                /* Track entered zone */
                uint8_t track_id = 0;
                for (int i = 0; i < tracks->track_count; i++) {
                    if (i == t) {
                        track_id = tracks->tracks[i].track_id;
                        break;
                    }
                }
                emit_event(ZONE_EVENT_ENTER,
                           s_zone_engine.zone_map.zones[z].id,
                           track_id, tracks->timestamp_ms);
            } else if (was_in && !is_in) {
                /* Track exited zone */
                emit_event(ZONE_EVENT_EXIT,
                           s_zone_engine.zone_map.zones[z].id,
                           0, tracks->timestamp_ms);  /* Track ID may be retired */
            }
        }
    }

    /* Update previous state */
    memcpy(s_zone_engine.previous_track_zones, current_track_zones,
           sizeof(s_zone_engine.previous_track_zones));

    /* Build output */
    memcpy(output->states, s_zone_engine.states,
           sizeof(zone_state_t) * output->zone_count);

    s_zone_engine.frames_processed++;

    /* Record processing time */
    uint64_t end_us = timebase_monotonic_us();
    s_zone_engine.processing_time_us = (uint32_t)(end_us - start_us);
    if (s_zone_engine.processing_time_us > s_zone_engine.max_processing_time_us) {
        s_zone_engine.max_processing_time_us = s_zone_engine.processing_time_us;
    }

    return ESP_OK;
}

esp_err_t zone_engine_get_state(const char *zone_id, zone_state_t *state)
{
    if (!zone_id || !state) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < s_zone_engine.zone_map.zone_count; i++) {
        if (strcmp(s_zone_engine.states[i].zone_id, zone_id) == 0) {
            memcpy(state, &s_zone_engine.states[i], sizeof(zone_state_t));
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t zone_engine_get_all_states(zone_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    frame->zone_count = s_zone_engine.zone_map.zone_count;
    frame->timestamp_ms = timebase_uptime_ms();
    memcpy(frame->states, s_zone_engine.states,
           sizeof(zone_state_t) * frame->zone_count);

    return ESP_OK;
}

/* ============================================================================
 * Public API: Statistics
 * ============================================================================ */

void zone_engine_get_stats(zone_engine_stats_t *stats)
{
    if (!stats) return;

    stats->frames_processed = s_zone_engine.frames_processed;
    stats->occupancy_changes = s_zone_engine.occupancy_changes;
    stats->tracks_excluded = s_zone_engine.tracks_excluded;
    stats->processing_time_us = s_zone_engine.processing_time_us;
    stats->max_processing_time_us = s_zone_engine.max_processing_time_us;
    stats->zone_evaluations = s_zone_engine.zone_evaluations;
}

void zone_engine_reset_stats(void)
{
    s_zone_engine.frames_processed = 0;
    s_zone_engine.occupancy_changes = 0;
    s_zone_engine.tracks_excluded = 0;
    s_zone_engine.processing_time_us = 0;
    s_zone_engine.max_processing_time_us = 0;
    s_zone_engine.zone_evaluations = 0;
}
