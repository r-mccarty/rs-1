/**
 * @file presence_smoothing.h
 * @brief HardwareOS Presence Smoothing Module (M04)
 *
 * Applies hysteresis, hold timers, and confidence-based smoothing to raw
 * zone occupancy. Provides stable, flicker-free presence output for Home Assistant.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md
 */

#ifndef PRESENCE_SMOOTHING_H
#define PRESENCE_SMOOTHING_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "zone_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

#define SMOOTHING_MAX_ZONES             ZONE_MAX_ZONES
#define SMOOTHING_ZONE_ID_MAX_LEN       ZONE_ID_MAX_LEN

#define SMOOTHING_DEFAULT_SENSITIVITY   50      /* 0-100 */
#define SMOOTHING_MIN_HOLD_MS           100     /* Minimum hold time */
#define SMOOTHING_MAX_HOLD_MS           10000   /* Maximum hold time */
#define SMOOTHING_CONFIDENCE_BOOST_TH   80      /* Confidence for extended hold */

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Smoothing state machine states
 */
typedef enum {
    SMOOTHING_STATE_VACANT = 0, /**< No presence, idle */
    SMOOTHING_STATE_ENTERING,   /**< Raw present, waiting for enter delay */
    SMOOTHING_STATE_OCCUPIED,   /**< Confirmed presence */
    SMOOTHING_STATE_HOLDING     /**< Raw absent, holding for occlusion bridge */
} smoothing_state_t;

/**
 * @brief Raw zone state (input from M03 or M01)
 */
typedef struct {
    char zone_id[SMOOTHING_ZONE_ID_MAX_LEN];
    bool raw_occupied;          /**< Instantaneous occupancy */
    uint8_t target_count;       /**< Current track count in zone */
    uint8_t avg_confidence;     /**< Average track confidence (0-100) */
    bool has_moving;            /**< Any moving track in zone */
    uint32_t timestamp_ms;      /**< Frame timestamp */
} zone_raw_state_t;

/**
 * @brief Per-zone smoothing configuration
 */
typedef struct {
    char zone_id[SMOOTHING_ZONE_ID_MAX_LEN];
    uint8_t sensitivity;        /**< 0 (max hold) to 100 (instant response) */
    uint16_t hold_time_ms;      /**< Derived from sensitivity, or override */
    uint16_t enter_delay_ms;    /**< Delay before confirming occupancy */
} zone_smoothing_config_t;

/**
 * @brief Smoothed zone state (output)
 */
typedef struct {
    char zone_id[SMOOTHING_ZONE_ID_MAX_LEN];
    bool occupied;              /**< Smoothed occupancy (stable) */
    bool raw_occupied;          /**< Instantaneous (for debugging) */
    uint8_t target_count;       /**< Current count */
    uint32_t occupied_since_ms; /**< When occupancy started (0 if vacant) */
    uint32_t vacant_since_ms;   /**< When vacancy started (0 if occupied) */
    smoothing_state_t state;    /**< Internal state machine state */
} zone_smoothed_state_t;

/**
 * @brief Smoothed frame output (for M05 consumption)
 */
typedef struct {
    zone_smoothed_state_t zones[SMOOTHING_MAX_ZONES];
    uint8_t zone_count;         /**< Number of zones */
    uint32_t timestamp_ms;      /**< Frame timestamp */
} smoothed_frame_t;

/**
 * @brief State change callback type
 */
typedef void (*smoothing_callback_t)(const char *zone_id, bool occupied,
                                      void *user_data);

/**
 * @brief Presence smoothing module configuration
 */
typedef struct {
    uint8_t default_sensitivity;        /**< Global sensitivity (0-100) */
    uint16_t min_hold_ms;               /**< Minimum hold even at sensitivity=100 */
    uint16_t max_hold_ms;               /**< Maximum configurable hold */
    bool use_confidence_weighting;      /**< Enable confidence-weighted hold */
    uint8_t confidence_boost_threshold; /**< Confidence for extended hold */
    smoothing_callback_t state_change_callback; /**< Callback on state change */
    void *callback_user_data;           /**< User data for callback */
} presence_smoothing_config_t;

/**
 * @brief Default configuration initializer
 */
#define PRESENCE_SMOOTHING_CONFIG_DEFAULT() { \
    .default_sensitivity = SMOOTHING_DEFAULT_SENSITIVITY, \
    .min_hold_ms = SMOOTHING_MIN_HOLD_MS, \
    .max_hold_ms = SMOOTHING_MAX_HOLD_MS, \
    .use_confidence_weighting = true, \
    .confidence_boost_threshold = SMOOTHING_CONFIDENCE_BOOST_TH, \
    .state_change_callback = NULL, \
    .callback_user_data = NULL, \
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize the presence smoothing module
 *
 * @param config Configuration parameters (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t presence_smoothing_init(const presence_smoothing_config_t *config);

/**
 * @brief Deinitialize the presence smoothing module
 */
void presence_smoothing_deinit(void);

/**
 * @brief Reset presence smoothing state (all zones to VACANT)
 */
void presence_smoothing_reset(void);

/* ============================================================================
 * Zone Configuration
 * ============================================================================ */

/**
 * @brief Set sensitivity for a zone
 *
 * @param zone_id Zone identifier (NULL for global default)
 * @param sensitivity Sensitivity value (0-100)
 * @return ESP_OK on success
 */
esp_err_t presence_smoothing_set_sensitivity(const char *zone_id,
                                              uint8_t sensitivity);

/**
 * @brief Get sensitivity for a zone
 *
 * @param zone_id Zone identifier (NULL for global default)
 * @return Sensitivity value (0-100)
 */
uint8_t presence_smoothing_get_sensitivity(const char *zone_id);

/**
 * @brief Calculate hold time from sensitivity
 *
 * Formula: hold_time_ms = (100 - sensitivity) * 50
 *
 * @param sensitivity Sensitivity value (0-100)
 * @return Hold time in milliseconds
 */
uint16_t presence_smoothing_calc_hold_time(uint8_t sensitivity);

/**
 * @brief Calculate enter delay from sensitivity
 *
 * Formula: enter_delay_ms = (100 - sensitivity) * 5
 *
 * @param sensitivity Sensitivity value (0-100)
 * @return Enter delay in milliseconds
 */
uint16_t presence_smoothing_calc_enter_delay(uint8_t sensitivity);

/* ============================================================================
 * Main Processing
 * ============================================================================ */

/**
 * @brief Process a zone frame from M03
 *
 * @param zone_frame Input zone frame from M03
 * @param output Output smoothed frame for M05
 * @return ESP_OK on success
 */
esp_err_t presence_smoothing_process_frame(const zone_frame_t *zone_frame,
                                            smoothed_frame_t *output);

/**
 * @brief Process binary presence from LD2410 (Lite variant)
 *
 * For RS-1 Lite, which bypasses M02/M03 tracking and zone engine.
 *
 * @param raw_occupied Binary presence from LD2410
 * @param timestamp_ms Current timestamp
 * @param output Output smoothed state
 * @return ESP_OK on success
 */
esp_err_t presence_smoothing_process_binary(bool raw_occupied,
                                             uint32_t timestamp_ms,
                                             zone_smoothed_state_t *output);

/**
 * @brief Get smoothed state for a zone
 *
 * @param zone_id Zone identifier
 * @param state Output smoothed state
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t presence_smoothing_get_state(const char *zone_id,
                                        zone_smoothed_state_t *state);

/**
 * @brief Get all smoothed states
 *
 * @param frame Output smoothed frame
 * @return ESP_OK on success
 */
esp_err_t presence_smoothing_get_all_states(smoothed_frame_t *frame);

/**
 * @brief Check if any zone is occupied
 *
 * @return True if at least one zone has smoothed occupied=true
 */
bool presence_smoothing_any_occupied(void);

/**
 * @brief Get total occupied zone count
 *
 * @return Number of zones with smoothed occupied=true
 */
uint8_t presence_smoothing_occupied_count(void);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Presence smoothing statistics
 */
typedef struct {
    uint32_t frames_processed;                  /**< Total frames processed */
    uint32_t state_changes;                     /**< Total state transitions */
    uint32_t hold_extensions;                   /**< Times hold extended by confidence */
    uint32_t false_occupancy_prevented;         /**< Enter delay prevented flicker */
    uint32_t false_vacancy_prevented;           /**< Hold timer prevented flicker */
    uint32_t processing_time_us;                /**< Last frame processing time */
    uint32_t max_processing_time_us;            /**< Max processing time observed */
} presence_smoothing_stats_t;

/**
 * @brief Get presence smoothing statistics
 *
 * @param stats Output statistics structure
 */
void presence_smoothing_get_stats(presence_smoothing_stats_t *stats);

/**
 * @brief Reset presence smoothing statistics
 */
void presence_smoothing_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* PRESENCE_SMOOTHING_H */
