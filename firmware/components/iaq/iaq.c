/**
 * @file iaq.c
 * @brief HardwareOS IAQ Module (M12) Implementation
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_IAQ.md
 */

#include "iaq.h"

#include <string.h>
#include <stdio.h>

#ifndef TEST_HOST
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#else
/* Test host stubs */
#define ESP_LOGI(tag, fmt, ...)
#define ESP_LOGW(tag, fmt, ...)
#define ESP_LOGE(tag, fmt, ...)
typedef int SemaphoreHandle_t;
typedef int esp_timer_handle_t;
typedef int TaskHandle_t;
#define portMAX_DELAY 0xFFFFFFFF
#define pdMS_TO_TICKS(x) (x)
#endif

static const char *TAG = "iaq";

/* ============================================================================
 * Internal State
 * ============================================================================ */

typedef struct {
    bool initialized;
    bool running;
    iaq_config_t config;
    iaq_stats_t stats;
    iaq_event_cb_t callback;
    void *callback_user_data;

    /* Module state */
    iaq_status_t status;
    uint8_t detected_address;
    iaq_reading_t last_reading;
    iaq_entitlement_t entitlement;

    /* Timing */
    uint32_t warmup_start_ms;
    uint32_t conditioning_start_ms;
    bool warmup_complete;
    bool conditioning_complete;

#ifndef TEST_HOST
    SemaphoreHandle_t mutex;
    esp_timer_handle_t detect_timer;
    esp_timer_handle_t poll_timer;
    TaskHandle_t task;
#endif

} iaq_state_t;

static iaq_state_t s_state = {0};

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void emit_event(iaq_event_t event);
static bool detect_module(void);
static bool verify_part_id(uint8_t address);
static esp_err_t set_operating_mode(uint8_t mode);
static esp_err_t read_sensor_data(iaq_reading_t *reading);
static void update_status(void);

#ifndef TEST_HOST
static void detect_timer_callback(void *arg);
static void poll_timer_callback(void *arg);
static uint32_t get_time_ms(void);
#endif

/* ============================================================================
 * Initialization
 * ============================================================================ */

esp_err_t iaq_init(const iaq_config_t *config)
{
    if (s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_state, 0, sizeof(s_state));

    if (config) {
        s_state.config = *config;
    } else {
        iaq_config_t defaults = IAQ_CONFIG_DEFAULT();
        s_state.config = defaults;
    }

    s_state.status = IAQ_STATUS_NOT_DETECTED;
    s_state.last_reading.status = IAQ_STATUS_NOT_DETECTED;

#ifndef TEST_HOST
    /* Create mutex */
    s_state.mutex = xSemaphoreCreateMutex();
    if (!s_state.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Initialize I2C (if not already done by other sensors) */
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = s_state.config.sda_pin,
        .scl_io_num = s_state.config.scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,  /* 100 kHz */
    };

    /* Configure I2C - may already be configured by other sensors */
    i2c_param_config(s_state.config.i2c_port, &i2c_conf);
    i2c_driver_install(s_state.config.i2c_port, I2C_MODE_MASTER, 0, 0, 0);

    /* Create detection timer */
    esp_timer_create_args_t detect_args = {
        .callback = detect_timer_callback,
        .name = "iaq_detect"
    };
    esp_timer_create(&detect_args, &s_state.detect_timer);

    /* Create poll timer */
    esp_timer_create_args_t poll_args = {
        .callback = poll_timer_callback,
        .name = "iaq_poll"
    };
    esp_timer_create(&poll_args, &s_state.poll_timer);

    /* Load cached entitlement */
    nvs_handle_t nvs;
    if (nvs_open("iaq", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(s_state.entitlement);
        nvs_get_blob(nvs, "entitlement", &s_state.entitlement, &len);
        nvs_close(nvs);
    }

    /* Load calibration state */
    iaq_load_calibration();
#endif

    s_state.initialized = true;
    ESP_LOGI(TAG, "IAQ module initialized");

    return ESP_OK;
}

void iaq_deinit(void)
{
    if (!s_state.initialized) {
        return;
    }

    iaq_stop();

#ifndef TEST_HOST
    if (s_state.detect_timer) {
        esp_timer_delete(s_state.detect_timer);
    }
    if (s_state.poll_timer) {
        esp_timer_delete(s_state.poll_timer);
    }
    if (s_state.mutex) {
        vSemaphoreDelete(s_state.mutex);
    }
#endif

    s_state.initialized = false;
    ESP_LOGI(TAG, "IAQ module deinitialized");
}

void iaq_set_callback(iaq_event_cb_t callback, void *user_data)
{
    s_state.callback = callback;
    s_state.callback_user_data = user_data;
}

esp_err_t iaq_start_detection(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_state.running) {
        return ESP_OK;
    }

    /* Do initial detection */
    if (detect_module()) {
        ESP_LOGI(TAG, "ENS160 detected at address 0x%02X", s_state.detected_address);
    }

#ifndef TEST_HOST
    /* Start detection timer */
    if (s_state.config.auto_detect) {
        esp_timer_start_periodic(s_state.detect_timer,
                                 s_state.config.detect_interval_ms * 1000ULL);
    }
#endif

    s_state.running = true;
    ESP_LOGI(TAG, "IAQ detection started");

    return ESP_OK;
}

void iaq_stop(void)
{
    if (!s_state.running) {
        return;
    }

#ifndef TEST_HOST
    esp_timer_stop(s_state.detect_timer);
    esp_timer_stop(s_state.poll_timer);
#endif

    /* Put sensor to sleep */
    if (s_state.detected_address) {
        set_operating_mode(IAQ_OPMODE_DEEP_SLEEP);
    }

    s_state.running = false;
    ESP_LOGI(TAG, "IAQ detection stopped");
}

esp_err_t iaq_shutdown(void)
{
    /* Save calibration before shutdown */
    iaq_save_calibration();

    iaq_stop();

    /* Put sensor in deep sleep */
    if (s_state.detected_address) {
        set_operating_mode(IAQ_OPMODE_DEEP_SLEEP);
    }

    return ESP_OK;
}

/* ============================================================================
 * Sensor Access
 * ============================================================================ */

esp_err_t iaq_get_reading(iaq_reading_t *reading)
{
    if (!reading) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_state.status == IAQ_STATUS_NOT_DETECTED) {
        return ESP_ERR_NOT_FOUND;
    }

#ifndef TEST_HOST
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
#endif

    *reading = s_state.last_reading;

#ifndef TEST_HOST
    xSemaphoreGive(s_state.mutex);
#endif

    return ESP_OK;
}

iaq_status_t iaq_get_status(void)
{
    return s_state.status;
}

bool iaq_is_detected(void)
{
    return s_state.detected_address != 0;
}

bool iaq_is_licensed(void)
{
    return s_state.entitlement.granted;
}

bool iaq_readings_available(void)
{
    return s_state.status == IAQ_STATUS_READY && s_state.entitlement.granted;
}

/* ============================================================================
 * Entitlement
 * ============================================================================ */

esp_err_t iaq_check_entitlement(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* TODO: Trigger MQTT query for entitlement */
    /* mqtt_publish("device/{device_id}/entitlement/request",
     *              "{\"feature\":\"iaq\"}"); */

    ESP_LOGI(TAG, "Entitlement check initiated");
    return ESP_OK;
}

void iaq_set_entitlement(bool granted, uint32_t expires)
{
#ifndef TEST_HOST
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
#endif

    s_state.entitlement.granted = granted;
    s_state.entitlement.expires = expires;
#ifndef TEST_HOST
    s_state.entitlement.checked_at = (uint32_t)(esp_timer_get_time() / 1000000);
#endif
    strcpy(s_state.entitlement.feature, "iaq");

#ifndef TEST_HOST
    xSemaphoreGive(s_state.mutex);
#endif

    /* Save to NVS */
#ifndef TEST_HOST
    nvs_handle_t nvs;
    if (nvs_open("iaq", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_blob(nvs, "entitlement", &s_state.entitlement,
                     sizeof(s_state.entitlement));
        nvs_commit(nvs);
        nvs_close(nvs);
    }
#endif

    if (granted) {
        emit_event(IAQ_EVENT_ENTITLEMENT_GRANTED);
        ESP_LOGI(TAG, "IAQ entitlement granted");

        /* Start sensor if detected */
        if (s_state.detected_address &&
            s_state.status == IAQ_STATUS_DETECTED) {
            s_state.status = IAQ_STATUS_INITIALIZING;
#ifndef TEST_HOST
            s_state.warmup_start_ms = get_time_ms();
#endif
            set_operating_mode(IAQ_OPMODE_STANDARD);

            /* Start poll timer */
#ifndef TEST_HOST
            esp_timer_start_periodic(s_state.poll_timer,
                                     s_state.config.poll_interval_ms * 1000ULL);
#endif
        }
    } else {
        emit_event(IAQ_EVENT_ENTITLEMENT_DENIED);
        ESP_LOGW(TAG, "IAQ entitlement denied");
    }
}

esp_err_t iaq_get_entitlement(iaq_entitlement_t *entitlement)
{
    if (!entitlement) {
        return ESP_ERR_INVALID_ARG;
    }

    *entitlement = s_state.entitlement;
    return ESP_OK;
}

/* ============================================================================
 * Calibration
 * ============================================================================ */

uint8_t iaq_get_conditioning_progress(void)
{
    if (!s_state.conditioning_start_ms) {
        return 0;
    }

    if (s_state.conditioning_complete) {
        return 100;
    }

#ifndef TEST_HOST
    uint32_t elapsed_ms = get_time_ms() - s_state.conditioning_start_ms;
    uint32_t progress = (elapsed_ms * 100) / IAQ_CONDITIONING_TIME_MS;
    return progress > 100 ? 100 : (uint8_t)progress;
#else
    return 0;
#endif
}

uint32_t iaq_get_conditioning_hours(void)
{
    if (!s_state.conditioning_start_ms) {
        return 0;
    }

#ifndef TEST_HOST
    uint32_t elapsed_ms = get_time_ms() - s_state.conditioning_start_ms;
    return elapsed_ms / (60 * 60 * 1000);
#else
    return 0;
#endif
}

bool iaq_is_conditioned(void)
{
    return s_state.conditioning_complete;
}

esp_err_t iaq_save_calibration(void)
{
#ifndef TEST_HOST
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("iaq", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    nvs_set_u32(nvs, "warmup_done", s_state.warmup_complete ? 1 : 0);
    nvs_set_u32(nvs, "cond_done", s_state.conditioning_complete ? 1 : 0);
    nvs_set_u32(nvs, "cond_start", s_state.conditioning_start_ms);
    nvs_set_u32(nvs, "uptime_h", s_state.stats.uptime_hours);

    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGI(TAG, "Calibration state saved");
#endif
    return ESP_OK;
}

esp_err_t iaq_load_calibration(void)
{
#ifndef TEST_HOST
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("iaq", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t val;
    if (nvs_get_u32(nvs, "warmup_done", &val) == ESP_OK) {
        s_state.warmup_complete = val != 0;
    }
    if (nvs_get_u32(nvs, "cond_done", &val) == ESP_OK) {
        s_state.conditioning_complete = val != 0;
    }
    if (nvs_get_u32(nvs, "cond_start", &val) == ESP_OK) {
        s_state.conditioning_start_ms = val;
    }
    if (nvs_get_u32(nvs, "uptime_h", &val) == ESP_OK) {
        s_state.stats.uptime_hours = val;
    }

    nvs_close(nvs);

    ESP_LOGI(TAG, "Calibration state loaded (conditioned: %s)",
             s_state.conditioning_complete ? "yes" : "no");
#endif
    return ESP_OK;
}

/* ============================================================================
 * Diagnostics
 * ============================================================================ */

const char *iaq_aqi_level_str(uint8_t aqi)
{
    switch (aqi) {
        case IAQ_AQI_EXCELLENT:  return "Excellent";
        case IAQ_AQI_GOOD:       return "Good";
        case IAQ_AQI_MODERATE:   return "Moderate";
        case IAQ_AQI_POOR:       return "Poor";
        case IAQ_AQI_UNHEALTHY:  return "Unhealthy";
        default:                 return "Unknown";
    }
}

const char *iaq_status_str(iaq_status_t status)
{
    switch (status) {
        case IAQ_STATUS_NOT_DETECTED:   return "Not Detected";
        case IAQ_STATUS_DETECTED:       return "Detected (Unlicensed)";
        case IAQ_STATUS_INITIALIZING:   return "Initializing";
        case IAQ_STATUS_CONDITIONING:   return "Conditioning";
        case IAQ_STATUS_READY:          return "Ready";
        case IAQ_STATUS_ERROR:          return "Error";
        default:                        return "Unknown";
    }
}

void iaq_get_stats(iaq_stats_t *stats)
{
    if (stats) {
        *stats = s_state.stats;
    }
}

void iaq_reset_stats(void)
{
    uint32_t uptime = s_state.stats.uptime_hours;
    memset(&s_state.stats, 0, sizeof(s_state.stats));
    s_state.stats.uptime_hours = uptime;
}

/* ============================================================================
 * Low-Level Access
 * ============================================================================ */

esp_err_t iaq_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    if (!s_state.detected_address || !data) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef TEST_HOST
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_state.detected_address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_state.detected_address << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(s_state.config.i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (err != ESP_OK) {
        s_state.stats.i2c_errors++;
    }

    return err;
#else
    return ESP_OK;
#endif
}

esp_err_t iaq_write_reg(uint8_t reg, const uint8_t *data, size_t len)
{
    if (!s_state.detected_address) {
        return ESP_ERR_INVALID_STATE;
    }

#ifndef TEST_HOST
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_state.detected_address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    if (data && len > 0) {
        i2c_master_write(cmd, data, len, true);
    }
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(s_state.config.i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (err != ESP_OK) {
        s_state.stats.i2c_errors++;
    }

    return err;
#else
    return ESP_OK;
#endif
}

uint8_t iaq_get_address(void)
{
    return s_state.detected_address;
}

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

static void emit_event(iaq_event_t event)
{
    if (s_state.callback) {
        s_state.callback(event, &s_state.last_reading, s_state.callback_user_data);
    }
}

static bool detect_module(void)
{
    /* Try primary address */
    if (verify_part_id(s_state.config.i2c_address)) {
        if (s_state.detected_address != s_state.config.i2c_address) {
            s_state.detected_address = s_state.config.i2c_address;
            s_state.stats.attach_count++;
            s_state.status = IAQ_STATUS_DETECTED;
            emit_event(IAQ_EVENT_MODULE_ATTACHED);

            /* Check entitlement */
            if (s_state.entitlement.granted) {
                s_state.status = IAQ_STATUS_INITIALIZING;
#ifndef TEST_HOST
                s_state.warmup_start_ms = get_time_ms();
#endif
                set_operating_mode(IAQ_OPMODE_STANDARD);

#ifndef TEST_HOST
                esp_timer_start_periodic(s_state.poll_timer,
                                         s_state.config.poll_interval_ms * 1000ULL);
#endif
            }
        }
        return true;
    }

    /* Try alternate address */
    uint8_t alt_addr = (s_state.config.i2c_address == IAQ_ENS160_ADDR_DEFAULT) ?
                       IAQ_ENS160_ADDR_ALT : IAQ_ENS160_ADDR_DEFAULT;

    if (verify_part_id(alt_addr)) {
        if (s_state.detected_address != alt_addr) {
            s_state.detected_address = alt_addr;
            s_state.stats.attach_count++;
            s_state.status = IAQ_STATUS_DETECTED;
            emit_event(IAQ_EVENT_MODULE_ATTACHED);
        }
        return true;
    }

    /* Module not found */
    if (s_state.detected_address != 0) {
        ESP_LOGW(TAG, "IAQ module detached");
        s_state.detected_address = 0;
        s_state.status = IAQ_STATUS_NOT_DETECTED;
        s_state.stats.detach_count++;

#ifndef TEST_HOST
        esp_timer_stop(s_state.poll_timer);
#endif

        emit_event(IAQ_EVENT_MODULE_DETACHED);
    }

    return false;
}

static bool verify_part_id(uint8_t address)
{
#ifndef TEST_HOST
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, IAQ_REG_PART_ID, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_READ, true);

    uint8_t part_id[2] = {0};
    i2c_master_read(cmd, part_id, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(s_state.config.i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (err != ESP_OK) {
        return false;
    }

    uint16_t id = part_id[0] | (part_id[1] << 8);
    return id == IAQ_ENS160_PART_ID;
#else
    (void)address;
    return false;
#endif
}

static esp_err_t set_operating_mode(uint8_t mode)
{
    return iaq_write_reg(IAQ_REG_OPMODE, &mode, 1);
}

static esp_err_t read_sensor_data(iaq_reading_t *reading)
{
    if (!reading) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read status register */
    uint8_t status;
    esp_err_t err = iaq_read_reg(IAQ_REG_DATA_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }

    reading->data_validity = status;

    /* Check if new data available */
    if (!(status & IAQ_STATUS_NEWDAT)) {
        return ESP_ERR_NOT_FOUND;  /* No new data */
    }

    /* Read AQI */
    err = iaq_read_reg(IAQ_REG_DATA_AQI, &reading->aqi_uba, 1);
    if (err != ESP_OK) {
        return err;
    }

    /* Clamp AQI to valid range */
    if (reading->aqi_uba < IAQ_AQI_MIN) reading->aqi_uba = IAQ_AQI_MIN;
    if (reading->aqi_uba > IAQ_AQI_MAX) reading->aqi_uba = IAQ_AQI_MAX;

    /* Read TVOC */
    uint8_t tvoc[2];
    err = iaq_read_reg(IAQ_REG_DATA_TVOC, tvoc, 2);
    if (err != ESP_OK) {
        return err;
    }
    reading->tvoc_ppb = tvoc[0] | (tvoc[1] << 8);
    if (reading->tvoc_ppb > IAQ_TVOC_MAX) {
        reading->tvoc_ppb = IAQ_TVOC_MAX;
    }

    /* Read eCO2 */
    uint8_t eco2[2];
    err = iaq_read_reg(IAQ_REG_DATA_ECO2, eco2, 2);
    if (err != ESP_OK) {
        return err;
    }
    reading->eco2_ppm = eco2[0] | (eco2[1] << 8);
    if (reading->eco2_ppm < IAQ_ECO2_MIN) reading->eco2_ppm = IAQ_ECO2_MIN;
    if (reading->eco2_ppm > IAQ_ECO2_MAX) reading->eco2_ppm = IAQ_ECO2_MAX;

#ifndef TEST_HOST
    reading->timestamp_ms = get_time_ms();
#endif
    reading->status = s_state.status;
    reading->licensed = s_state.entitlement.granted;
    reading->stale = false;

    return ESP_OK;
}

static void update_status(void)
{
#ifndef TEST_HOST
    uint32_t now = get_time_ms();

    /* Check warmup progress */
    if (s_state.status == IAQ_STATUS_INITIALIZING) {
        if (now - s_state.warmup_start_ms >= IAQ_WARMUP_TIME_MS) {
            s_state.warmup_complete = true;
            s_state.status = IAQ_STATUS_CONDITIONING;
            s_state.conditioning_start_ms = now;

            emit_event(IAQ_EVENT_WARMUP_COMPLETE);
            ESP_LOGI(TAG, "Warmup complete, entering conditioning phase");
        }
    }

    /* Check conditioning progress */
    if (s_state.status == IAQ_STATUS_CONDITIONING) {
        if (now - s_state.conditioning_start_ms >= IAQ_CONDITIONING_TIME_MS) {
            s_state.conditioning_complete = true;
            s_state.status = IAQ_STATUS_READY;

            emit_event(IAQ_EVENT_CALIBRATION_COMPLETE);
            ESP_LOGI(TAG, "Calibration complete, sensor ready");

            /* Save calibration state */
            iaq_save_calibration();
        }
    }
#endif
}

#ifndef TEST_HOST

static void detect_timer_callback(void *arg)
{
    (void)arg;
    detect_module();
}

static void poll_timer_callback(void *arg)
{
    (void)arg;

    if (s_state.status == IAQ_STATUS_NOT_DETECTED ||
        !s_state.entitlement.granted) {
        return;
    }

    /* Update status (warmup/conditioning) */
    update_status();

    /* Read sensor data */
    iaq_reading_t reading = {0};
    esp_err_t err = read_sensor_data(&reading);

    if (err == ESP_OK) {
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
        s_state.last_reading = reading;
        s_state.stats.readings_total++;
        s_state.stats.readings_valid++;
        s_state.stats.last_reading_ms = reading.timestamp_ms;
        xSemaphoreGive(s_state.mutex);

        emit_event(IAQ_EVENT_READING_AVAILABLE);
    } else if (err != ESP_ERR_NOT_FOUND) {
        /* I2C error */
        s_state.stats.readings_invalid++;

        /* Mark reading as stale */
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
        s_state.last_reading.stale = true;
        xSemaphoreGive(s_state.mutex);
    }
}

static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

#endif /* TEST_HOST */
