/**
 * @file radar_ingest.h
 * @brief M01 Radar Ingest Module Public API
 *
 * This module is the sole interface between radar hardware and the processing
 * pipeline. It handles UART parsing for both LD2450 (Pro) and LD2410 (Lite/Pro)
 * radars.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md
 *
 * @version 0.1.0
 */

#ifndef RADAR_INGEST_H
#define RADAR_INGEST_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * LD2450 Detection Structures (Pro - Target Tracking)
 * Reference: HARDWAREOS_MODULE_RADAR_INGEST.md Section 4.1.1
 * ============================================================================ */

/**
 * @brief Single target detection from LD2450
 *
 * Coordinates are in millimeters, sensor-centric:
 * - Origin at sensor
 * - +X: right when facing sensor
 * - +Y: away from sensor (into room)
 */
typedef struct {
    int16_t x_mm;           /**< X position (-6000 to +6000 mm) */
    int16_t y_mm;           /**< Y position (0 to 6000 mm) */
    int16_t speed_cm_s;     /**< Speed in cm/s (negative = approaching) */
    uint16_t resolution_mm; /**< Distance resolution from radar */
    uint8_t signal_quality; /**< Derived quality metric (0-100) */
    bool valid;             /**< Target present in this slot */
} radar_detection_t;

/**
 * @brief Frame containing up to 3 target detections from LD2450
 */
typedef struct {
    radar_detection_t targets[3]; /**< Up to 3 targets */
    uint8_t target_count;         /**< Number of valid targets (0-3) */
    uint32_t timestamp_ms;        /**< Frame timestamp (system tick) */
    uint32_t frame_seq;           /**< Monotonic frame sequence number */
} radar_detection_frame_t;

/* ============================================================================
 * LD2410 Presence Structures (Lite and Pro)
 * Reference: HARDWAREOS_MODULE_RADAR_INGEST.md Section 4.1.2
 * ============================================================================ */

/**
 * @brief LD2410 target state enumeration
 */
typedef enum {
    LD2410_NO_TARGET = 0x00,              /**< No presence detected */
    LD2410_MOVING = 0x01,                 /**< Moving target only */
    LD2410_STATIONARY = 0x02,             /**< Stationary target only */
    LD2410_MOVING_AND_STATIONARY = 0x03   /**< Both types detected */
} ld2410_target_state_t;

/**
 * @brief LD2410 Engineering Mode presence frame
 *
 * Contains binary presence state plus energy levels per gate
 * for more nuanced presence detection.
 */
typedef struct {
    ld2410_target_state_t state;      /**< Target state enum */
    uint16_t moving_distance_cm;      /**< Moving target distance */
    uint8_t moving_energy;            /**< Moving target energy (0-100) */
    uint16_t stationary_distance_cm;  /**< Stationary target distance */
    uint8_t stationary_energy;        /**< Stationary target energy (0-100) */
    uint8_t moving_gates[9];          /**< Per-gate moving energy (gates 0-8) */
    uint8_t stationary_gates[9];      /**< Per-gate stationary energy (gates 0-8) */
    uint32_t timestamp_ms;            /**< Frame timestamp */
    uint32_t frame_seq;               /**< Monotonic frame sequence */
} radar_presence_frame_t;

/* ============================================================================
 * Radar State
 * ============================================================================ */

/**
 * @brief Radar connection state
 */
typedef enum {
    RADAR_STATE_DISCONNECTED,  /**< No frames received within timeout */
    RADAR_STATE_CONNECTED      /**< Frames being received normally */
} radar_state_t;

/**
 * @brief Radar sensor identifier
 */
typedef enum {
    RADAR_SENSOR_LD2410,  /**< LD2410 presence radar */
    RADAR_SENSOR_LD2450   /**< LD2450 tracking radar (Pro only) */
} radar_sensor_t;

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * @brief Callback for LD2450 detection frames (Pro only)
 *
 * Called from radar task context when a valid LD2450 frame is parsed.
 * Must return quickly to avoid blocking frame processing.
 *
 * @param frame Pointer to detection frame (valid only during callback)
 * @param user_ctx User context provided at registration
 */
typedef void (*radar_detection_callback_t)(const radar_detection_frame_t *frame,
                                            void *user_ctx);

/**
 * @brief Callback for LD2410 presence frames (Lite and Pro)
 *
 * Called from radar task context when a valid LD2410 frame is parsed.
 *
 * @param frame Pointer to presence frame (valid only during callback)
 * @param user_ctx User context provided at registration
 */
typedef void (*radar_presence_callback_t)(const radar_presence_frame_t *frame,
                                           void *user_ctx);

/**
 * @brief Callback for radar state changes
 *
 * Called when radar transitions between CONNECTED and DISCONNECTED states.
 *
 * @param sensor Which sensor changed state
 * @param new_state New state
 * @param user_ctx User context provided at registration
 */
typedef void (*radar_state_callback_t)(radar_sensor_t sensor,
                                        radar_state_t new_state,
                                        void *user_ctx);

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief Radar ingest module configuration
 */
typedef struct {
    /* LD2410 configuration (required for both variants) */
    int ld2410_uart_num;        /**< UART peripheral number */
    int ld2410_rx_pin;          /**< RX GPIO pin */
    int ld2410_tx_pin;          /**< TX GPIO pin */

    /* LD2450 configuration (Pro only, set to -1 to disable) */
    int ld2450_uart_num;        /**< UART peripheral number (-1 if disabled) */
    int ld2450_rx_pin;          /**< RX GPIO pin */
    int ld2450_tx_pin;          /**< TX GPIO pin */

    /* Filtering thresholds */
    uint16_t min_range_mm;      /**< Minimum valid Y distance */
    uint16_t max_range_mm;      /**< Maximum valid Y distance */
    uint16_t max_speed_cm_s;    /**< Maximum valid speed */
    uint8_t ld2410_min_energy;  /**< Minimum energy for presence */

    /* Timing */
    uint32_t disconnect_timeout_ms; /**< Timeout before declaring disconnect */

    /* Task configuration */
    int task_core;              /**< Core to pin radar task to */
    uint32_t task_stack_size;   /**< Task stack size in bytes */
    int task_priority;          /**< Task priority */
} radar_ingest_config_t;

/**
 * @brief Default configuration macro
 */
#define RADAR_INGEST_CONFIG_DEFAULT() {                 \
    .ld2410_uart_num = 1,                               \
    .ld2410_rx_pin = 5,                                 \
    .ld2410_tx_pin = 4,                                 \
    .ld2450_uart_num = 2,                               \
    .ld2450_rx_pin = 17,                                \
    .ld2450_tx_pin = 16,                                \
    .min_range_mm = 100,                                \
    .max_range_mm = 6000,                               \
    .max_speed_cm_s = 500,                              \
    .ld2410_min_energy = 10,                            \
    .disconnect_timeout_ms = 3000,                      \
    .task_core = 1,                                     \
    .task_stack_size = 2048,                            \
    .task_priority = configMAX_PRIORITIES - 1,          \
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize the radar ingest module
 *
 * Configures UART(s), creates parsing task, and starts frame reception.
 * Must be called before any other radar_ingest functions.
 *
 * @param config Configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t radar_ingest_init(const radar_ingest_config_t *config);

/**
 * @brief Deinitialize the radar ingest module
 *
 * Stops the radar task, releases UART resources.
 *
 * @return ESP_OK on success
 */
esp_err_t radar_ingest_deinit(void);

/**
 * @brief Register callback for LD2450 detection frames (Pro only)
 *
 * @param callback Callback function
 * @param user_ctx User context passed to callback
 * @return ESP_OK on success
 */
esp_err_t radar_ingest_register_detection_callback(radar_detection_callback_t callback,
                                                    void *user_ctx);

/**
 * @brief Register callback for LD2410 presence frames
 *
 * @param callback Callback function
 * @param user_ctx User context passed to callback
 * @return ESP_OK on success
 */
esp_err_t radar_ingest_register_presence_callback(radar_presence_callback_t callback,
                                                   void *user_ctx);

/**
 * @brief Register callback for radar state changes
 *
 * @param callback Callback function
 * @param user_ctx User context passed to callback
 * @return ESP_OK on success
 */
esp_err_t radar_ingest_register_state_callback(radar_state_callback_t callback,
                                                void *user_ctx);

/**
 * @brief Get current radar connection state
 *
 * @param sensor Which sensor to query
 * @return Current state
 */
radar_state_t radar_ingest_get_state(radar_sensor_t sensor);

/**
 * @brief Check if LD2450 (tracking) is enabled
 *
 * @return true if LD2450 is configured and enabled
 */
bool radar_ingest_has_tracking(void);

/* ============================================================================
 * Statistics / Telemetry
 * ============================================================================ */

/**
 * @brief Radar statistics for telemetry (M09 integration)
 */
typedef struct {
    uint32_t frames_received;   /**< Total valid frames parsed */
    uint32_t frames_invalid;    /**< Checksum/format failures */
    uint32_t bytes_received;    /**< Total bytes received */
    float avg_targets_per_frame;/**< Rolling average target count (LD2450) */
    uint32_t last_frame_ms;     /**< Timestamp of last valid frame */
    float frame_rate_hz;        /**< Measured frame rate */
} radar_stats_t;

/**
 * @brief Get radar statistics
 *
 * @param sensor Which sensor to query
 * @param stats Output structure
 * @return ESP_OK on success
 */
esp_err_t radar_ingest_get_stats(radar_sensor_t sensor, radar_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_INGEST_H */
