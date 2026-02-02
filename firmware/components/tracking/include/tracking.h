/**
 * @file tracking.h
 * @brief HardwareOS Tracking Module (M02)
 *
 * Provides Kalman filter-based multi-target tracking for the RS-1 Pro.
 * Transforms raw detections from M01 into stable, persistent tracks.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_TRACKING.md
 *
 * Note: This module is only active in RS-1 Pro variant.
 * RS-1 Lite bypasses tracking, going directly from M01 to M04.
 */

#ifndef TRACKING_H
#define TRACKING_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "radar_ingest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define TRACKING_MAX_TRACKS             3       /* LD2450 hardware limit */
#define TRACKING_CONFIRM_THRESHOLD      2       /* Frames to confirm tentative */
#define TRACKING_TENTATIVE_DROP         3       /* Misses to drop tentative */
#define TRACKING_OCCLUSION_TIMEOUT      66      /* ~2 seconds at 33 Hz */
#define TRACKING_GATE_DISTANCE_MM       600     /* Max association distance */
#define TRACKING_DT_SEC                 0.030f  /* 30ms frame interval */

/* Kalman filter noise parameters */
#define TRACKING_PROCESS_NOISE_POS      50      /* Position noise (mm) */
#define TRACKING_PROCESS_NOISE_VEL      200     /* Velocity noise (mm/s) */
#define TRACKING_MEASUREMENT_NOISE      100     /* Measurement noise (mm) */

/* Divergence detection thresholds */
#define TRACKING_MAX_COVARIANCE         1e6f
#define TRACKING_MIN_COVARIANCE         1e-6f

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Track lifecycle states
 */
typedef enum {
    TRACK_STATE_RETIRED = 0,    /**< Track marked for removal */
    TRACK_STATE_TENTATIVE,      /**< New track, awaiting confirmation */
    TRACK_STATE_CONFIRMED,      /**< Confirmed track, actively reporting */
    TRACK_STATE_OCCLUDED        /**< Confirmed track, temporarily missing */
} track_state_t;

/**
 * @brief Single track data structure
 */
typedef struct {
    /* Identity */
    uint8_t track_id;                   /**< Unique ID (1-255, 0 = invalid) */
    track_state_t state;                /**< Current lifecycle state */

    /* Position and velocity (derived from Kalman state) */
    int16_t x_mm;                       /**< Estimated X position */
    int16_t y_mm;                       /**< Estimated Y position */
    int16_t vx_mm_s;                    /**< Estimated X velocity (mm/s) */
    int16_t vy_mm_s;                    /**< Estimated Y velocity (mm/s) */

    /* Kalman filter internals */
    float x_state[4];                   /**< State vector [x, y, vx, vy] */
    float P[4][4];                      /**< State covariance matrix */

    /* Confidence and timing */
    uint8_t confidence;                 /**< Track confidence (0-100) */
    uint8_t consecutive_hits;           /**< Consecutive frames with match */
    uint8_t consecutive_misses;         /**< Consecutive frames without match */
    uint32_t first_seen_ms;             /**< Timestamp of track creation */
    uint32_t last_seen_ms;              /**< Timestamp of last detection match */

    /* Association */
    uint8_t last_detection_idx;         /**< Index of last matched detection */
} track_t;

/**
 * @brief Tracker state container
 */
typedef struct {
    track_t tracks[TRACKING_MAX_TRACKS];    /**< Active tracks */
    uint8_t active_count;                   /**< Number of non-retired tracks */
    uint8_t next_track_id;                  /**< Next available track ID */
    uint32_t frame_count;                   /**< Total frames processed */

    /* Statistics */
    uint32_t confirmations;                 /**< Tracks promoted to confirmed */
    uint32_t retirements;                   /**< Tracks retired */
    uint32_t id_switches;                   /**< Track ID changes */
    uint32_t filter_resets;                 /**< Kalman filter resets */
} tracker_state_t;

/**
 * @brief Single track output (for M03 consumption)
 */
typedef struct {
    uint8_t track_id;                   /**< Track identifier */
    int16_t x_mm;                       /**< Position X */
    int16_t y_mm;                       /**< Position Y */
    int16_t vx_mm_s;                    /**< Velocity X */
    int16_t vy_mm_s;                    /**< Velocity Y */
    uint8_t confidence;                 /**< Confidence (0-100) */
    track_state_t state;                /**< Current state */
} track_output_t;

/**
 * @brief Track frame output (for M03 consumption)
 */
typedef struct {
    track_output_t tracks[TRACKING_MAX_TRACKS];
    uint8_t track_count;                /**< Confirmed + occluded tracks */
    uint32_t timestamp_ms;              /**< Frame timestamp */
    uint32_t frame_seq;                 /**< Frame sequence number */
} track_frame_t;

/**
 * @brief Tracker configuration
 */
typedef struct {
    uint8_t confirm_threshold;          /**< Frames to confirm tentative */
    uint8_t tentative_drop;             /**< Misses to drop tentative */
    uint8_t occlusion_timeout_frames;   /**< Frames before occlusion retirement */
    uint16_t gate_distance_mm;          /**< Association gate distance */
    uint16_t process_noise_pos;         /**< Kalman Q position (mm) */
    uint16_t process_noise_vel;         /**< Kalman Q velocity (mm/s) */
    uint16_t measurement_noise;         /**< Kalman R (mm) */
} tracker_config_t;

/**
 * @brief Default configuration initializer
 */
#define TRACKER_CONFIG_DEFAULT() { \
    .confirm_threshold = TRACKING_CONFIRM_THRESHOLD, \
    .tentative_drop = TRACKING_TENTATIVE_DROP, \
    .occlusion_timeout_frames = TRACKING_OCCLUSION_TIMEOUT, \
    .gate_distance_mm = TRACKING_GATE_DISTANCE_MM, \
    .process_noise_pos = TRACKING_PROCESS_NOISE_POS, \
    .process_noise_vel = TRACKING_PROCESS_NOISE_VEL, \
    .measurement_noise = TRACKING_MEASUREMENT_NOISE, \
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize the tracking module
 *
 * @param config Configuration parameters (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t tracking_init(const tracker_config_t *config);

/**
 * @brief Deinitialize the tracking module
 */
void tracking_deinit(void);

/**
 * @brief Reset tracker state (clear all tracks)
 */
void tracking_reset(void);

/* ============================================================================
 * Main Processing
 * ============================================================================ */

/**
 * @brief Process a detection frame
 *
 * Main tracking pipeline: predict -> associate -> update -> spawn -> cleanup
 *
 * @param detections Input detection frame from M01
 * @param output Output track frame for M03
 * @return ESP_OK on success
 */
esp_err_t tracking_process_frame(const radar_detection_frame_t *detections,
                                  track_frame_t *output);

/**
 * @brief Get current tracker state (for debugging)
 *
 * @param state Output state structure
 */
void tracking_get_state(tracker_state_t *state);

/* ============================================================================
 * Track Query Functions
 * ============================================================================ */

/**
 * @brief Get track by ID
 *
 * @param track_id Track ID to find
 * @param track Output track structure
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t tracking_get_track(uint8_t track_id, track_output_t *track);

/**
 * @brief Get number of active tracks
 *
 * Active means TENTATIVE, CONFIRMED, or OCCLUDED (not RETIRED).
 *
 * @return Number of active tracks
 */
uint8_t tracking_get_active_count(void);

/**
 * @brief Get number of confirmed tracks
 *
 * Confirmed means CONFIRMED or OCCLUDED (not TENTATIVE or RETIRED).
 *
 * @return Number of confirmed tracks
 */
uint8_t tracking_get_confirmed_count(void);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Tracking statistics
 */
typedef struct {
    uint32_t frames_processed;          /**< Total frames processed */
    uint32_t confirmations;             /**< Tracks promoted to confirmed */
    uint32_t retirements;               /**< Tracks retired */
    uint32_t id_switches;               /**< Track ID changes */
    uint32_t filter_resets;             /**< Kalman filter resets */
    uint32_t processing_time_us;        /**< Last frame processing time */
    uint32_t max_processing_time_us;    /**< Max processing time observed */
} tracking_stats_t;

/**
 * @brief Get tracking statistics
 *
 * @param stats Output statistics structure
 */
void tracking_get_stats(tracking_stats_t *stats);

/**
 * @brief Reset tracking statistics
 */
void tracking_reset_stats(void);

/* ============================================================================
 * Configuration Updates
 * ============================================================================ */

/**
 * @brief Update gate distance at runtime
 *
 * @param gate_mm New gate distance in millimeters
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range
 */
esp_err_t tracking_set_gate_distance(uint16_t gate_mm);

/**
 * @brief Update occlusion timeout at runtime
 *
 * @param frames New timeout in frames
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range
 */
esp_err_t tracking_set_occlusion_timeout(uint8_t frames);

#ifdef __cplusplus
}
#endif

#endif /* TRACKING_H */
