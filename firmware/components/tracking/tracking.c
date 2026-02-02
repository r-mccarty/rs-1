/**
 * @file tracking.c
 * @brief HardwareOS Tracking Module Implementation (M02)
 *
 * Kalman filter-based multi-target tracking for RS-1 Pro.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_TRACKING.md
 */

#include "tracking.h"
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

static const char *TAG = "tracking";

/* ============================================================================
 * Helper Macros
 * ============================================================================ */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) (MIN(MAX(x, lo), hi))

/* ============================================================================
 * Module State
 * ============================================================================ */

static struct {
    bool initialized;
    tracker_config_t config;
    tracker_state_t state;

    /* Statistics */
    uint32_t processing_time_us;
    uint32_t max_processing_time_us;

    /* Kalman filter matrices (precomputed) */
    float Q[4][4];          /* Process noise covariance */
    float R[2][2];          /* Measurement noise covariance */
} s_tracker = {0};

/* State transition matrix F (constant velocity model) */
static const float F[4][4] = {
    {1.0f, 0.0f, TRACKING_DT_SEC, 0.0f},
    {0.0f, 1.0f, 0.0f, TRACKING_DT_SEC},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f}
};

/* Measurement matrix H (observe position only) */
static const float H[2][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f}
};

/* ============================================================================
 * Matrix Operations
 * ============================================================================ */

/**
 * @brief Matrix multiplication: C = A * B (4x4 * 4x4 -> 4x4)
 */
static void mat4_mult(const float A[4][4], const float B[4][4], float C[4][4])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            C[i][j] = 0.0f;
            for (int k = 0; k < 4; k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
}

/**
 * @brief Matrix transpose 4x4
 */
static void mat4_transpose(const float A[4][4], float At[4][4])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            At[i][j] = A[j][i];
        }
    }
}

/**
 * @brief Matrix addition: C = A + B (4x4)
 */
static void mat4_add(const float A[4][4], const float B[4][4], float C[4][4])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            C[i][j] = A[i][j] + B[i][j];
        }
    }
}

/**
 * @brief 2x2 matrix inversion
 */
static bool mat2_invert(const float A[2][2], float Ainv[2][2])
{
    float det = A[0][0] * A[1][1] - A[0][1] * A[1][0];
    if (fabsf(det) < 1e-10f) {
        return false;  /* Singular matrix */
    }
    float inv_det = 1.0f / det;
    Ainv[0][0] = A[1][1] * inv_det;
    Ainv[0][1] = -A[0][1] * inv_det;
    Ainv[1][0] = -A[1][0] * inv_det;
    Ainv[1][1] = A[0][0] * inv_det;
    return true;
}

/* ============================================================================
 * Kalman Filter Core
 * ============================================================================ */

/**
 * @brief Initialize Kalman filter for a track
 */
static void kalman_init(track_t *track, int16_t x_mm, int16_t y_mm)
{
    /* Initialize state vector */
    track->x_state[0] = (float)x_mm;
    track->x_state[1] = (float)y_mm;
    track->x_state[2] = 0.0f;  /* Zero initial velocity */
    track->x_state[3] = 0.0f;

    /* Initialize covariance matrix (high initial uncertainty) */
    memset(track->P, 0, sizeof(track->P));
    track->P[0][0] = 1000.0f;   /* Position X uncertainty */
    track->P[1][1] = 1000.0f;   /* Position Y uncertainty */
    track->P[2][2] = 10000.0f;  /* Velocity X uncertainty */
    track->P[3][3] = 10000.0f;  /* Velocity Y uncertainty */

    /* Copy to output fields */
    track->x_mm = x_mm;
    track->y_mm = y_mm;
    track->vx_mm_s = 0;
    track->vy_mm_s = 0;
}

/**
 * @brief Check if Kalman filter has diverged
 */
static bool kalman_check_divergence(const track_t *track)
{
    /* Check for NaN/Inf in state */
    for (int i = 0; i < 4; i++) {
        if (isnan(track->x_state[i]) || isinf(track->x_state[i])) {
            return true;
        }
    }

    /* Check for NaN/Inf or explosion in covariance diagonal */
    for (int i = 0; i < 4; i++) {
        if (isnan(track->P[i][i]) || isinf(track->P[i][i])) {
            return true;
        }
        if (track->P[i][i] > TRACKING_MAX_COVARIANCE) {
            return true;
        }
        if (track->P[i][i] < TRACKING_MIN_COVARIANCE) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Reset Kalman filter after divergence
 */
static void kalman_reset(track_t *track, int16_t x_mm, int16_t y_mm)
{
    kalman_init(track, x_mm, y_mm);
    s_tracker.state.filter_resets++;
    ESP_LOGW(TAG, "Track %d filter reset due to divergence", track->track_id);
}

/**
 * @brief Kalman predict step
 */
static void kalman_predict(track_t *track)
{
    /* x_pred = F * x */
    float x_pred[4];
    x_pred[0] = track->x_state[0] + TRACKING_DT_SEC * track->x_state[2];
    x_pred[1] = track->x_state[1] + TRACKING_DT_SEC * track->x_state[3];
    x_pred[2] = track->x_state[2];
    x_pred[3] = track->x_state[3];

    /* P_pred = F * P * F^T + Q */
    float Ft[4][4], FP[4][4], FPFt[4][4], P_pred[4][4];
    mat4_transpose(F, Ft);
    mat4_mult(F, track->P, FP);
    mat4_mult(FP, Ft, FPFt);
    mat4_add(FPFt, s_tracker.Q, P_pred);

    /* Update track state */
    memcpy(track->x_state, x_pred, sizeof(x_pred));
    memcpy(track->P, P_pred, sizeof(P_pred));

    /* Update position estimates */
    track->x_mm = (int16_t)track->x_state[0];
    track->y_mm = (int16_t)track->x_state[1];
    track->vx_mm_s = (int16_t)(track->x_state[2] * 1000.0f);  /* m/s to mm/s */
    track->vy_mm_s = (int16_t)(track->x_state[3] * 1000.0f);
}

/**
 * @brief Kalman update step with measurement
 */
static void kalman_update(track_t *track, int16_t z_x, int16_t z_y)
{
    /* Check for divergence before update */
    if (kalman_check_divergence(track)) {
        kalman_reset(track, z_x, z_y);
        return;
    }

    /* Innovation: y = z - H * x */
    float y[2];
    y[0] = (float)z_x - track->x_state[0];
    y[1] = (float)z_y - track->x_state[1];

    /* Innovation covariance: S = H * P * H^T + R */
    /* Since H selects first two rows/cols: S = P[0:2,0:2] + R */
    float S[2][2];
    S[0][0] = track->P[0][0] + s_tracker.R[0][0];
    S[0][1] = track->P[0][1] + s_tracker.R[0][1];
    S[1][0] = track->P[1][0] + s_tracker.R[1][0];
    S[1][1] = track->P[1][1] + s_tracker.R[1][1];

    /* Invert S */
    float Sinv[2][2];
    if (!mat2_invert(S, Sinv)) {
        ESP_LOGW(TAG, "Track %d: singular S matrix", track->track_id);
        kalman_reset(track, z_x, z_y);
        return;
    }

    /* Kalman gain: K = P * H^T * S^-1 */
    /* K is 4x2, H^T is 4x2 (columns of H transposed) */
    float K[4][2];
    for (int i = 0; i < 4; i++) {
        /* P * H^T extracts first two columns of P */
        float PHt_i0 = track->P[i][0];
        float PHt_i1 = track->P[i][1];
        /* Multiply by Sinv */
        K[i][0] = PHt_i0 * Sinv[0][0] + PHt_i1 * Sinv[1][0];
        K[i][1] = PHt_i0 * Sinv[0][1] + PHt_i1 * Sinv[1][1];
    }

    /* State update: x = x + K * y */
    for (int i = 0; i < 4; i++) {
        track->x_state[i] += K[i][0] * y[0] + K[i][1] * y[1];
    }

    /* Covariance update: P = (I - K * H) * P */
    /* I - K*H subtracts K*H from identity */
    float IKH[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float kh = K[i][0] * H[0][j] + K[i][1] * H[1][j];
            IKH[i][j] = (i == j ? 1.0f : 0.0f) - kh;
        }
    }

    float P_new[4][4];
    mat4_mult(IKH, track->P, P_new);
    memcpy(track->P, P_new, sizeof(P_new));

    /* Update position estimates */
    track->x_mm = (int16_t)track->x_state[0];
    track->y_mm = (int16_t)track->x_state[1];
    track->vx_mm_s = (int16_t)(track->x_state[2] * 1000.0f);
    track->vy_mm_s = (int16_t)(track->x_state[3] * 1000.0f);

    /* Check for divergence after update */
    if (kalman_check_divergence(track)) {
        kalman_reset(track, z_x, z_y);
    }
}

/* ============================================================================
 * Track Lifecycle
 * ============================================================================ */

/**
 * @brief Handle detection match for a track
 */
static void track_handle_match(track_t *track, uint32_t timestamp_ms)
{
    track->consecutive_hits++;
    track->consecutive_misses = 0;
    track->last_seen_ms = timestamp_ms;

    /* Update confidence (increase on match) */
    track->confidence = MIN(100, track->confidence + 5);

    /* State transitions */
    switch (track->state) {
        case TRACK_STATE_TENTATIVE:
            if (track->consecutive_hits >= s_tracker.config.confirm_threshold) {
                track->state = TRACK_STATE_CONFIRMED;
                s_tracker.state.confirmations++;
                ESP_LOGI(TAG, "Track %d confirmed", track->track_id);
            }
            break;
        case TRACK_STATE_OCCLUDED:
            track->state = TRACK_STATE_CONFIRMED;
            ESP_LOGD(TAG, "Track %d recovered from occlusion", track->track_id);
            break;
        case TRACK_STATE_CONFIRMED:
            /* Stay confirmed */
            break;
        default:
            break;
    }
}

/**
 * @brief Handle detection miss for a track
 */
static void track_handle_miss(track_t *track)
{
    track->consecutive_misses++;

    /* Decay confidence (decrease on miss) */
    if (track->confidence >= 10) {
        track->confidence -= 10;
    } else {
        track->confidence = 0;
    }

    /* State transitions */
    switch (track->state) {
        case TRACK_STATE_TENTATIVE:
            if (track->consecutive_misses >= s_tracker.config.tentative_drop) {
                track->state = TRACK_STATE_RETIRED;
                s_tracker.state.retirements++;
                ESP_LOGD(TAG, "Tentative track %d retired", track->track_id);
            }
            break;
        case TRACK_STATE_CONFIRMED:
            track->state = TRACK_STATE_OCCLUDED;
            ESP_LOGD(TAG, "Track %d now occluded", track->track_id);
            break;
        case TRACK_STATE_OCCLUDED:
            if (track->consecutive_misses >= s_tracker.config.occlusion_timeout_frames) {
                track->state = TRACK_STATE_RETIRED;
                s_tracker.state.retirements++;
                ESP_LOGI(TAG, "Track %d retired after occlusion timeout",
                         track->track_id);
            }
            break;
        default:
            break;
    }
}

/**
 * @brief Spawn a new track from an unassigned detection
 */
static void spawn_track(const radar_detection_t *det, uint32_t timestamp_ms)
{
    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < TRACKING_MAX_TRACKS; i++) {
        if (s_tracker.state.tracks[i].state == TRACK_STATE_RETIRED ||
            s_tracker.state.tracks[i].track_id == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        ESP_LOGD(TAG, "No free track slots for new detection");
        return;
    }

    /* Allocate track ID */
    s_tracker.state.next_track_id++;
    if (s_tracker.state.next_track_id == 0) {
        s_tracker.state.next_track_id = 1;  /* Skip 0 */
    }

    track_t *track = &s_tracker.state.tracks[slot];
    memset(track, 0, sizeof(track_t));

    track->track_id = s_tracker.state.next_track_id;
    track->state = TRACK_STATE_TENTATIVE;
    track->confidence = 50;
    track->consecutive_hits = 1;
    track->consecutive_misses = 0;
    track->first_seen_ms = timestamp_ms;
    track->last_seen_ms = timestamp_ms;

    /* Initialize Kalman filter */
    kalman_init(track, det->x_mm, det->y_mm);

    s_tracker.state.active_count++;
    ESP_LOGD(TAG, "Spawned tentative track %d at (%d, %d)",
             track->track_id, det->x_mm, det->y_mm);
}

/**
 * @brief Compute confidence score
 */
static uint8_t compute_confidence(const track_t *track)
{
    int conf = 50;  /* Base confidence */

    /* Bonus for consecutive hits */
    conf += MIN(30, track->consecutive_hits * 5);

    /* Penalty for consecutive misses */
    conf -= MIN(40, track->consecutive_misses * 8);

    /* Bonus for track age (stable tracks are more reliable) */
    uint32_t age_ms = track->last_seen_ms - track->first_seen_ms;
    uint32_t age_sec = age_ms / 1000;
    conf += MIN(20, age_sec * 2);

    return (uint8_t)CLAMP(conf, 0, 100);
}

/* ============================================================================
 * Association
 * ============================================================================ */

/**
 * @brief Compute gate distance for a track (can scale with velocity)
 */
static float compute_gate_distance(const track_t *track)
{
    float base_gate = (float)s_tracker.config.gate_distance_mm;

    /* Scale gate with velocity magnitude */
    float vx = (float)track->vx_mm_s / 1000.0f;  /* Convert to m/s */
    float vy = (float)track->vy_mm_s / 1000.0f;
    float speed = sqrtf(vx * vx + vy * vy);

    /* Allow larger gate for faster targets: 1 m/s → +100mm gate */
    float velocity_scale = speed * 100.0f;

    return fminf(base_gate + velocity_scale, 1000.0f);  /* Cap at 1m */
}

/**
 * @brief Associate detections to tracks using gated nearest-neighbor
 */
static void associate_detections(
    const radar_detection_frame_t *detections,
    bool assigned_tracks[TRACKING_MAX_TRACKS],
    bool assigned_detections[3],
    int8_t track_to_detection[TRACKING_MAX_TRACKS])
{
    /* Build cost matrix: distance between each track prediction and detection */
    float cost[TRACKING_MAX_TRACKS][3];
    float gates[TRACKING_MAX_TRACKS];

    for (int t = 0; t < TRACKING_MAX_TRACKS; t++) {
        track_t *track = &s_tracker.state.tracks[t];
        if (track->state == TRACK_STATE_RETIRED || track->track_id == 0) {
            gates[t] = 0;
            for (int d = 0; d < 3; d++) {
                cost[t][d] = INFINITY;
            }
            continue;
        }

        gates[t] = compute_gate_distance(track);

        for (int d = 0; d < (int)detections->target_count && d < 3; d++) {
            if (!detections->targets[d].valid) {
                cost[t][d] = INFINITY;
                continue;
            }

            float dx = (float)track->x_mm - (float)detections->targets[d].x_mm;
            float dy = (float)track->y_mm - (float)detections->targets[d].y_mm;
            float dist = sqrtf(dx * dx + dy * dy);

            /* Apply gate */
            if (dist > gates[t]) {
                cost[t][d] = INFINITY;
            } else {
                cost[t][d] = dist;
            }
        }
    }

    /* Initialize outputs */
    memset(assigned_tracks, 0, sizeof(bool) * TRACKING_MAX_TRACKS);
    memset(assigned_detections, 0, sizeof(bool) * 3);
    for (int t = 0; t < TRACKING_MAX_TRACKS; t++) {
        track_to_detection[t] = -1;
    }

    /* Greedy nearest-neighbor assignment */
    for (int iter = 0; iter < TRACKING_MAX_TRACKS; iter++) {
        float min_cost = INFINITY;
        int best_track = -1, best_det = -1;

        /* Find minimum cost unassigned pair */
        for (int t = 0; t < TRACKING_MAX_TRACKS; t++) {
            if (assigned_tracks[t]) continue;
            if (s_tracker.state.tracks[t].state == TRACK_STATE_RETIRED) continue;
            if (s_tracker.state.tracks[t].track_id == 0) continue;

            for (int d = 0; d < (int)detections->target_count && d < 3; d++) {
                if (assigned_detections[d]) continue;
                if (cost[t][d] < min_cost) {
                    min_cost = cost[t][d];
                    best_track = t;
                    best_det = d;
                }
            }
        }

        if (best_track < 0 || isinf(min_cost)) break;

        assigned_tracks[best_track] = true;
        assigned_detections[best_det] = true;
        track_to_detection[best_track] = (int8_t)best_det;
    }
}

/* ============================================================================
 * Output Building
 * ============================================================================ */

/**
 * @brief Build output frame from current tracker state
 */
static void build_output_frame(uint32_t timestamp_ms, track_frame_t *output)
{
    memset(output, 0, sizeof(track_frame_t));
    output->timestamp_ms = timestamp_ms;
    output->frame_seq = s_tracker.state.frame_count;

    int out_idx = 0;
    for (int i = 0; i < TRACKING_MAX_TRACKS && out_idx < TRACKING_MAX_TRACKS; i++) {
        track_t *track = &s_tracker.state.tracks[i];

        /* Only output CONFIRMED and OCCLUDED tracks */
        if (track->state != TRACK_STATE_CONFIRMED &&
            track->state != TRACK_STATE_OCCLUDED) {
            continue;
        }

        track_output_t *out = &output->tracks[out_idx];
        out->track_id = track->track_id;
        out->x_mm = track->x_mm;
        out->y_mm = track->y_mm;
        out->vx_mm_s = track->vx_mm_s;
        out->vy_mm_s = track->vy_mm_s;
        out->confidence = compute_confidence(track);
        out->state = track->state;

        out_idx++;
    }

    output->track_count = out_idx;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

esp_err_t tracking_init(const tracker_config_t *config)
{
    if (s_tracker.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Apply configuration */
    if (config) {
        s_tracker.config = *config;
    } else {
        tracker_config_t defaults = TRACKER_CONFIG_DEFAULT();
        s_tracker.config = defaults;
    }

    /* Initialize state */
    memset(&s_tracker.state, 0, sizeof(s_tracker.state));
    s_tracker.state.next_track_id = 1;

    /* Precompute noise matrices */
    float q_pos = (float)s_tracker.config.process_noise_pos;
    float q_vel = (float)s_tracker.config.process_noise_vel;
    float r_meas = (float)s_tracker.config.measurement_noise;

    memset(s_tracker.Q, 0, sizeof(s_tracker.Q));
    s_tracker.Q[0][0] = q_pos * q_pos;
    s_tracker.Q[1][1] = q_pos * q_pos;
    s_tracker.Q[2][2] = q_vel * q_vel;
    s_tracker.Q[3][3] = q_vel * q_vel;

    memset(s_tracker.R, 0, sizeof(s_tracker.R));
    s_tracker.R[0][0] = r_meas * r_meas;
    s_tracker.R[1][1] = r_meas * r_meas;

    s_tracker.initialized = true;
    ESP_LOGI(TAG, "Initialized (gate=%dmm, occlusion=%d frames)",
             s_tracker.config.gate_distance_mm,
             s_tracker.config.occlusion_timeout_frames);

    return ESP_OK;
}

void tracking_deinit(void)
{
    if (!s_tracker.initialized) {
        return;
    }

    memset(&s_tracker, 0, sizeof(s_tracker));
    ESP_LOGI(TAG, "Deinitialized");
}

void tracking_reset(void)
{
    memset(&s_tracker.state, 0, sizeof(s_tracker.state));
    s_tracker.state.next_track_id = 1;
    s_tracker.processing_time_us = 0;
    s_tracker.max_processing_time_us = 0;
    ESP_LOGI(TAG, "Reset");
}

esp_err_t tracking_process_frame(const radar_detection_frame_t *detections,
                                  track_frame_t *output)
{
    if (!s_tracker.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!detections || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t start_us = timebase_monotonic_us();

    /* 1. PREDICT: Advance all tracks using Kalman prediction */
    for (int i = 0; i < TRACKING_MAX_TRACKS; i++) {
        track_t *track = &s_tracker.state.tracks[i];
        if (track->state != TRACK_STATE_RETIRED && track->track_id != 0) {
            kalman_predict(track);
        }
    }

    /* 2. ASSOCIATE: Match detections to predicted track positions */
    bool assigned_tracks[TRACKING_MAX_TRACKS];
    bool assigned_detections[3];
    int8_t track_to_detection[TRACKING_MAX_TRACKS];
    associate_detections(detections, assigned_tracks, assigned_detections,
                         track_to_detection);

    /* 3. UPDATE: Apply Kalman update for matched tracks */
    for (int t = 0; t < TRACKING_MAX_TRACKS; t++) {
        track_t *track = &s_tracker.state.tracks[t];
        if (track->state == TRACK_STATE_RETIRED || track->track_id == 0) {
            continue;
        }

        if (assigned_tracks[t] && track_to_detection[t] >= 0) {
            /* Track matched: update with detection */
            int d = track_to_detection[t];
            track->last_detection_idx = d;
            kalman_update(track, detections->targets[d].x_mm,
                                 detections->targets[d].y_mm);
            track_handle_match(track, detections->timestamp_ms);
        } else {
            /* Track not matched: handle miss */
            track_handle_miss(track);
        }
    }

    /* 4. SPAWN: Create new tracks for unassigned detections */
    for (int d = 0; d < (int)detections->target_count && d < 3; d++) {
        if (!assigned_detections[d] && detections->targets[d].valid) {
            spawn_track(&detections->targets[d], detections->timestamp_ms);
        }
    }

    /* 5. CLEANUP: Update active count */
    s_tracker.state.active_count = 0;
    for (int i = 0; i < TRACKING_MAX_TRACKS; i++) {
        if (s_tracker.state.tracks[i].state != TRACK_STATE_RETIRED &&
            s_tracker.state.tracks[i].track_id != 0) {
            s_tracker.state.active_count++;
        }
    }

    /* 6. OUTPUT: Build output frame */
    build_output_frame(detections->timestamp_ms, output);

    s_tracker.state.frame_count++;

    /* Record processing time */
    uint64_t end_us = timebase_monotonic_us();
    s_tracker.processing_time_us = (uint32_t)(end_us - start_us);
    if (s_tracker.processing_time_us > s_tracker.max_processing_time_us) {
        s_tracker.max_processing_time_us = s_tracker.processing_time_us;
    }

    return ESP_OK;
}

void tracking_get_state(tracker_state_t *state)
{
    if (state) {
        *state = s_tracker.state;
    }
}

esp_err_t tracking_get_track(uint8_t track_id, track_output_t *track)
{
    if (!track) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < TRACKING_MAX_TRACKS; i++) {
        if (s_tracker.state.tracks[i].track_id == track_id &&
            s_tracker.state.tracks[i].state != TRACK_STATE_RETIRED) {
            track_t *src = &s_tracker.state.tracks[i];
            track->track_id = src->track_id;
            track->x_mm = src->x_mm;
            track->y_mm = src->y_mm;
            track->vx_mm_s = src->vx_mm_s;
            track->vy_mm_s = src->vy_mm_s;
            track->confidence = compute_confidence(src);
            track->state = src->state;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

uint8_t tracking_get_active_count(void)
{
    return s_tracker.state.active_count;
}

uint8_t tracking_get_confirmed_count(void)
{
    uint8_t count = 0;
    for (int i = 0; i < TRACKING_MAX_TRACKS; i++) {
        if (s_tracker.state.tracks[i].state == TRACK_STATE_CONFIRMED ||
            s_tracker.state.tracks[i].state == TRACK_STATE_OCCLUDED) {
            count++;
        }
    }
    return count;
}

void tracking_get_stats(tracking_stats_t *stats)
{
    if (!stats) return;

    stats->frames_processed = s_tracker.state.frame_count;
    stats->confirmations = s_tracker.state.confirmations;
    stats->retirements = s_tracker.state.retirements;
    stats->id_switches = s_tracker.state.id_switches;
    stats->filter_resets = s_tracker.state.filter_resets;
    stats->processing_time_us = s_tracker.processing_time_us;
    stats->max_processing_time_us = s_tracker.max_processing_time_us;
}

void tracking_reset_stats(void)
{
    s_tracker.state.confirmations = 0;
    s_tracker.state.retirements = 0;
    s_tracker.state.id_switches = 0;
    s_tracker.state.filter_resets = 0;
    s_tracker.processing_time_us = 0;
    s_tracker.max_processing_time_us = 0;
}

esp_err_t tracking_set_gate_distance(uint16_t gate_mm)
{
    if (gate_mm < 300 || gate_mm > 1000) {
        return ESP_ERR_INVALID_ARG;
    }
    s_tracker.config.gate_distance_mm = gate_mm;
    return ESP_OK;
}

esp_err_t tracking_set_occlusion_timeout(uint8_t frames)
{
    if (frames < 33 || frames > 99) {
        return ESP_ERR_INVALID_ARG;
    }
    s_tracker.config.occlusion_timeout_frames = frames;
    return ESP_OK;
}
