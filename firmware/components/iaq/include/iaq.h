/**
 * @file iaq.h
 * @brief HardwareOS IAQ Module (M12)
 *
 * Manages the optional IAQ (Indoor Air Quality) daughterboard module.
 * Handles ENS160 sensor initialization, calibration, and readings.
 * Supports hot-plug detection and OTA-based capability unlock.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_IAQ.md
 */

#ifndef IAQ_H
#define IAQ_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/* ENS160 I2C addresses */
#define IAQ_ENS160_ADDR_DEFAULT     0x52
#define IAQ_ENS160_ADDR_ALT         0x53

/* ENS160 registers */
#define IAQ_REG_PART_ID             0x00
#define IAQ_REG_OPMODE              0x10
#define IAQ_REG_DATA_STATUS         0x20
#define IAQ_REG_DATA_AQI            0x21
#define IAQ_REG_DATA_TVOC           0x22
#define IAQ_REG_DATA_ECO2           0x24
#define IAQ_REG_DATA_T              0x30
#define IAQ_REG_DATA_RH             0x32

/* ENS160 identification */
#define IAQ_ENS160_PART_ID          0x0160

/* ENS160 operating modes */
#define IAQ_OPMODE_DEEP_SLEEP       0x00
#define IAQ_OPMODE_IDLE             0x01
#define IAQ_OPMODE_STANDARD         0x02
#define IAQ_OPMODE_RESET            0xF0

/* Data validity flags */
#define IAQ_STATUS_NEWDAT           0x02
#define IAQ_STATUS_NEWGPR           0x01
#define IAQ_STATUS_VALIDITY_MASK    0x0C
#define IAQ_STATUS_VALIDITY_NORMAL  0x00
#define IAQ_STATUS_VALIDITY_WARMUP  0x04
#define IAQ_STATUS_VALIDITY_INITIAL 0x08
#define IAQ_STATUS_VALIDITY_INVALID 0x0C

/* Timing constants */
#define IAQ_WARMUP_TIME_MS          (3 * 60 * 1000)     /* 3 minutes */
#define IAQ_CONDITIONING_TIME_MS    (48ULL * 60 * 60 * 1000)  /* 48 hours */
#define IAQ_POLL_INTERVAL_MS        1000                /* 1 second */
#define IAQ_DETECT_INTERVAL_MS      5000                /* 5 seconds */
#define IAQ_RETRY_COUNT             3
#define IAQ_RETRY_DELAY_MS          100

/* Value ranges */
#define IAQ_TVOC_MAX                65000
#define IAQ_ECO2_MIN                400
#define IAQ_ECO2_MAX                65000
#define IAQ_AQI_MIN                 1
#define IAQ_AQI_MAX                 5

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief IAQ module status
 */
typedef enum {
    IAQ_STATUS_NOT_DETECTED = 0,    /**< Module not present */
    IAQ_STATUS_DETECTED,            /**< Module present, not licensed */
    IAQ_STATUS_INITIALIZING,        /**< Warming up (3 min) */
    IAQ_STATUS_CONDITIONING,        /**< First 48 hours */
    IAQ_STATUS_READY,               /**< Normal operation */
    IAQ_STATUS_ERROR,               /**< Sensor error */
} iaq_status_t;

/**
 * @brief AQI interpretation levels
 */
typedef enum {
    IAQ_AQI_EXCELLENT = 1,          /**< Clean air */
    IAQ_AQI_GOOD = 2,               /**< Acceptable */
    IAQ_AQI_MODERATE = 3,           /**< Some pollutants */
    IAQ_AQI_POOR = 4,               /**< Health effects possible */
    IAQ_AQI_UNHEALTHY = 5,          /**< Immediate action needed */
} iaq_aqi_level_t;

/**
 * @brief IAQ sensor reading
 */
typedef struct {
    uint16_t tvoc_ppb;              /**< Total VOCs (0-65000 ppb) */
    uint16_t eco2_ppm;              /**< Equivalent CO2 (400-65000 ppm) */
    uint8_t aqi_uba;                /**< UBA Air Quality Index (1-5) */
    uint8_t data_validity;          /**< ENS160 status register */
    iaq_status_t status;            /**< Module/sensor status */
    uint32_t timestamp_ms;          /**< Reading timestamp */
    bool licensed;                  /**< OTA entitlement verified */
    bool stale;                     /**< Reading may be outdated */
} iaq_reading_t;

/**
 * @brief IAQ module configuration
 */
typedef struct {
    uint8_t i2c_address;            /**< Primary I2C address (0x52 or 0x53) */
    uint16_t poll_interval_ms;      /**< Reading interval (default 1000) */
    bool auto_detect;               /**< Enable detection polling */
    uint16_t detect_interval_ms;    /**< Detection poll interval */
    int i2c_port;                   /**< I2C port number */
    int sda_pin;                    /**< SDA GPIO pin */
    int scl_pin;                    /**< SCL GPIO pin */
} iaq_config_t;

/**
 * @brief Default configuration
 */
#define IAQ_CONFIG_DEFAULT() { \
    .i2c_address = IAQ_ENS160_ADDR_DEFAULT, \
    .poll_interval_ms = IAQ_POLL_INTERVAL_MS, \
    .auto_detect = true, \
    .detect_interval_ms = IAQ_DETECT_INTERVAL_MS, \
    .i2c_port = 0, \
    .sda_pin = 21, \
    .scl_pin = 22, \
}

/**
 * @brief Entitlement cache entry
 */
typedef struct {
    char feature[16];               /**< "iaq" */
    bool granted;                   /**< License valid */
    uint32_t expires;               /**< Unix timestamp (0 = perpetual) */
    uint32_t checked_at;            /**< Last cloud check timestamp */
} iaq_entitlement_t;

/**
 * @brief IAQ event types
 */
typedef enum {
    IAQ_EVENT_MODULE_ATTACHED = 0,
    IAQ_EVENT_MODULE_DETACHED,
    IAQ_EVENT_WARMUP_COMPLETE,
    IAQ_EVENT_CALIBRATION_COMPLETE,
    IAQ_EVENT_ENTITLEMENT_GRANTED,
    IAQ_EVENT_ENTITLEMENT_DENIED,
    IAQ_EVENT_ERROR,
    IAQ_EVENT_READING_AVAILABLE,
} iaq_event_t;

/**
 * @brief Event callback
 */
typedef void (*iaq_event_cb_t)(iaq_event_t event, const iaq_reading_t *reading,
                                void *user_data);

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * @brief Initialize IAQ module
 *
 * @param config Configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t iaq_init(const iaq_config_t *config);

/**
 * @brief Deinitialize IAQ module
 */
void iaq_deinit(void);

/**
 * @brief Set event callback
 *
 * @param callback Callback function
 * @param user_data User data
 */
void iaq_set_callback(iaq_event_cb_t callback, void *user_data);

/**
 * @brief Start detection polling
 *
 * @return ESP_OK on success
 */
esp_err_t iaq_start_detection(void);

/**
 * @brief Stop detection and sensor reading
 */
void iaq_stop(void);

/**
 * @brief Shutdown module (for deep sleep or OTA)
 *
 * @return ESP_OK on success
 */
esp_err_t iaq_shutdown(void);

/* ============================================================================
 * Sensor Access
 * ============================================================================ */

/**
 * @brief Get current reading
 *
 * Returns the last cached reading. Call frequently for up-to-date values.
 *
 * @param reading Output reading structure
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no module
 */
esp_err_t iaq_get_reading(iaq_reading_t *reading);

/**
 * @brief Get module status
 *
 * @return Current status
 */
iaq_status_t iaq_get_status(void);

/**
 * @brief Check if module is detected
 *
 * @return True if ENS160 is responding
 */
bool iaq_is_detected(void);

/**
 * @brief Check if module is licensed
 *
 * @return True if entitlement is granted
 */
bool iaq_is_licensed(void);

/**
 * @brief Check if readings are available
 *
 * @return True if status is READY and licensed
 */
bool iaq_readings_available(void);

/* ============================================================================
 * Entitlement
 * ============================================================================ */

/**
 * @brief Force entitlement re-check
 *
 * Triggers a cloud query for entitlement status.
 *
 * @return ESP_OK if check initiated
 */
esp_err_t iaq_check_entitlement(void);

/**
 * @brief Set entitlement (for testing or local override)
 *
 * @param granted License granted
 * @param expires Expiry timestamp (0 = perpetual)
 */
void iaq_set_entitlement(bool granted, uint32_t expires);

/**
 * @brief Get entitlement cache
 *
 * @param entitlement Output entitlement
 * @return ESP_OK if cache exists
 */
esp_err_t iaq_get_entitlement(iaq_entitlement_t *entitlement);

/* ============================================================================
 * Calibration
 * ============================================================================ */

/**
 * @brief Get conditioning progress
 *
 * @return Progress 0-100 (100 = calibration complete)
 */
uint8_t iaq_get_conditioning_progress(void);

/**
 * @brief Get hours since calibration started
 *
 * @return Hours elapsed
 */
uint32_t iaq_get_conditioning_hours(void);

/**
 * @brief Check if sensor is fully conditioned
 *
 * @return True if 48+ hours have elapsed
 */
bool iaq_is_conditioned(void);

/**
 * @brief Save calibration state to NVS
 *
 * @return ESP_OK on success
 */
esp_err_t iaq_save_calibration(void);

/**
 * @brief Load calibration state from NVS
 *
 * @return ESP_OK if calibration loaded
 */
esp_err_t iaq_load_calibration(void);

/* ============================================================================
 * Diagnostics
 * ============================================================================ */

/**
 * @brief Get AQI level description
 *
 * @param aqi AQI value (1-5)
 * @return Human-readable level name
 */
const char *iaq_aqi_level_str(uint8_t aqi);

/**
 * @brief Get status description
 *
 * @param status Status enum
 * @return Human-readable status name
 */
const char *iaq_status_str(iaq_status_t status);

/**
 * @brief IAQ statistics
 */
typedef struct {
    uint32_t readings_total;
    uint32_t readings_valid;
    uint32_t readings_invalid;
    uint32_t attach_count;
    uint32_t detach_count;
    uint32_t i2c_errors;
    uint32_t uptime_hours;          /**< Hours since warmup complete */
    uint32_t last_reading_ms;
} iaq_stats_t;

/**
 * @brief Get statistics
 *
 * @param stats Output statistics
 */
void iaq_get_stats(iaq_stats_t *stats);

/**
 * @brief Reset statistics
 */
void iaq_reset_stats(void);

/* ============================================================================
 * Low-Level Access (for testing)
 * ============================================================================ */

/**
 * @brief Read ENS160 register
 *
 * @param reg Register address
 * @param data Output data
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t iaq_read_reg(uint8_t reg, uint8_t *data, size_t len);

/**
 * @brief Write ENS160 register
 *
 * @param reg Register address
 * @param data Data to write
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t iaq_write_reg(uint8_t reg, const uint8_t *data, size_t len);

/**
 * @brief Get detected I2C address
 *
 * @return I2C address (0 if not detected)
 */
uint8_t iaq_get_address(void);

#ifdef __cplusplus
}
#endif

#endif /* IAQ_H */
