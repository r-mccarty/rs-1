/**
 * @file radar_ingest.c
 * @brief M01 Radar Ingest Module Implementation
 *
 * Main module that manages UART reception for LD2410 and LD2450 radars,
 * coordinates parsing, and delivers frames to downstream modules.
 *
 * Architecture:
 * - RS-1 Lite: LD2410 only → presence_frame_t to M04
 * - RS-1 Pro:  LD2410 + LD2450 → detection_frame_t to M02, presence for M04
 *
 * Task runs on Core 1 for time-critical radar processing.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md
 */

#include "radar_ingest.h"
#include "ld2450_parser.h"
#include "ld2410_parser.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "RADAR";

/* ============================================================================
 * Private State
 * ============================================================================ */

/* Module state */
static struct {
    bool initialized;
    radar_ingest_config_t config;
    TaskHandle_t task_handle;

    /* Parsers */
    ld2410_parser_t ld2410_parser;
    ld2450_parser_t ld2450_parser;

    /* Radar states */
    radar_state_t ld2410_state;
    radar_state_t ld2450_state;
    uint32_t ld2410_last_frame_ms;
    uint32_t ld2450_last_frame_ms;

    /* Statistics */
    radar_stats_t ld2410_stats;
    radar_stats_t ld2450_stats;

    /* Rolling average for targets */
    float ld2450_avg_targets;

    /* Callbacks */
    radar_detection_callback_t detection_cb;
    void *detection_cb_ctx;
    radar_presence_callback_t presence_cb;
    void *presence_cb_ctx;
    radar_state_callback_t state_cb;
    void *state_cb_ctx;

    /* Mutex for stats access */
    SemaphoreHandle_t mutex;
} s_radar;

/* UART buffer sizes */
#define UART_BUF_SIZE   256
#define UART_QUEUE_SIZE 10

/* Event queue handles */
static QueueHandle_t s_ld2410_queue;
static QueueHandle_t s_ld2450_queue;

/* ============================================================================
 * Private Functions
 * ============================================================================ */

/**
 * @brief Get current time in milliseconds
 */
static inline uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Check radar connection timeouts and update states
 */
static void check_radar_timeouts(void)
{
    uint32_t now = get_time_ms();

    /* Check LD2410 */
    if (s_radar.ld2410_state == RADAR_STATE_CONNECTED) {
        if ((now - s_radar.ld2410_last_frame_ms) > s_radar.config.disconnect_timeout_ms) {
            s_radar.ld2410_state = RADAR_STATE_DISCONNECTED;
            ESP_LOGW(TAG, "LD2410 disconnected (no frames for %lu ms)",
                     s_radar.config.disconnect_timeout_ms);

            if (s_radar.state_cb) {
                s_radar.state_cb(RADAR_SENSOR_LD2410, RADAR_STATE_DISCONNECTED,
                                s_radar.state_cb_ctx);
            }
        }
    }

    /* Check LD2450 (if enabled) */
    if (s_radar.config.ld2450_uart_num >= 0) {
        if (s_radar.ld2450_state == RADAR_STATE_CONNECTED) {
            if ((now - s_radar.ld2450_last_frame_ms) > s_radar.config.disconnect_timeout_ms) {
                s_radar.ld2450_state = RADAR_STATE_DISCONNECTED;
                ESP_LOGW(TAG, "LD2450 disconnected (no frames for %lu ms)",
                         s_radar.config.disconnect_timeout_ms);

                if (s_radar.state_cb) {
                    s_radar.state_cb(RADAR_SENSOR_LD2450, RADAR_STATE_DISCONNECTED,
                                    s_radar.state_cb_ctx);
                }
            }
        }
    }
}

/**
 * @brief Update radar state on frame received
 */
static void on_ld2410_frame(void)
{
    s_radar.ld2410_last_frame_ms = get_time_ms();

    if (s_radar.ld2410_state == RADAR_STATE_DISCONNECTED) {
        s_radar.ld2410_state = RADAR_STATE_CONNECTED;
        ESP_LOGI(TAG, "LD2410 connected");

        if (s_radar.state_cb) {
            s_radar.state_cb(RADAR_SENSOR_LD2410, RADAR_STATE_CONNECTED,
                            s_radar.state_cb_ctx);
        }
    }
}

static void on_ld2450_frame(void)
{
    s_radar.ld2450_last_frame_ms = get_time_ms();

    if (s_radar.ld2450_state == RADAR_STATE_DISCONNECTED) {
        s_radar.ld2450_state = RADAR_STATE_CONNECTED;
        ESP_LOGI(TAG, "LD2450 connected");

        if (s_radar.state_cb) {
            s_radar.state_cb(RADAR_SENSOR_LD2450, RADAR_STATE_CONNECTED,
                            s_radar.state_cb_ctx);
        }
    }
}

/**
 * @brief Apply filtering to LD2450 detection frame
 *
 * Reject targets outside configured range/speed limits.
 */
static void filter_detection_frame(radar_detection_frame_t *frame)
{
    uint8_t valid_count = 0;

    for (int i = 0; i < 3; i++) {
        radar_detection_t *t = &frame->targets[i];

        if (!t->valid) {
            continue;
        }

        /* Range filter */
        if (t->y_mm < (int16_t)s_radar.config.min_range_mm ||
            t->y_mm > (int16_t)s_radar.config.max_range_mm) {
            t->valid = false;
            continue;
        }

        /* X bounds check */
        if (t->x_mm < LD2450_X_MIN || t->x_mm > LD2450_X_MAX) {
            t->valid = false;
            continue;
        }

        /* Speed sanity check */
        int16_t abs_speed = t->speed_cm_s < 0 ? -t->speed_cm_s : t->speed_cm_s;
        if (abs_speed > (int16_t)s_radar.config.max_speed_cm_s) {
            t->valid = false;
            continue;
        }

        valid_count++;
    }

    frame->target_count = valid_count;
}

/**
 * @brief Process LD2410 UART data
 */
static void process_ld2410_data(const uint8_t *data, size_t len)
{
    radar_presence_frame_t frame;

    while (len > 0) {
        if (ld2410_parser_feed(&s_radar.ld2410_parser, data, len, &frame)) {
            on_ld2410_frame();

            /* Update stats */
            s_radar.ld2410_stats.frames_received++;
            s_radar.ld2410_stats.last_frame_ms = frame.timestamp_ms;

            /* Debug output */
            #ifdef CONFIG_RS1_DEBUG_RADAR_OUTPUT
            ESP_LOGI(TAG, "LD2410: state=%d mov=%dcm/%d%% stat=%dcm/%d%%",
                     frame.state,
                     frame.moving_distance_cm, frame.moving_energy,
                     frame.stationary_distance_cm, frame.stationary_energy);
            #endif

            /* Invoke callback */
            if (s_radar.presence_cb) {
                s_radar.presence_cb(&frame, s_radar.presence_cb_ctx);
            }
        }
        break; /* Feed processes all data at once */
    }

    s_radar.ld2410_stats.bytes_received += len;
}

/**
 * @brief Process LD2450 UART data
 */
static void process_ld2450_data(const uint8_t *data, size_t len)
{
    radar_detection_frame_t frame;

    while (len > 0) {
        if (ld2450_parser_feed(&s_radar.ld2450_parser, data, len, &frame)) {
            on_ld2450_frame();

            /* Apply filters */
            filter_detection_frame(&frame);

            /* Update stats */
            s_radar.ld2450_stats.frames_received++;
            s_radar.ld2450_stats.last_frame_ms = frame.timestamp_ms;

            /* Rolling average of target count */
            s_radar.ld2450_avg_targets =
                0.95f * s_radar.ld2450_avg_targets + 0.05f * frame.target_count;
            s_radar.ld2450_stats.avg_targets_per_frame = s_radar.ld2450_avg_targets;

            /* Debug output */
            #ifdef CONFIG_RS1_DEBUG_RADAR_OUTPUT
            if (frame.target_count > 0) {
                for (int i = 0; i < 3; i++) {
                    if (frame.targets[i].valid) {
                        ESP_LOGI(TAG, "LD2450 T%d: X=%d Y=%d V=%d Q=%d",
                                 i,
                                 frame.targets[i].x_mm,
                                 frame.targets[i].y_mm,
                                 frame.targets[i].speed_cm_s,
                                 frame.targets[i].signal_quality);
                    }
                }
            }
            #endif

            /* Invoke callback */
            if (s_radar.detection_cb) {
                s_radar.detection_cb(&frame, s_radar.detection_cb_ctx);
            }
        }
        break; /* Feed processes all data at once */
    }

    s_radar.ld2450_stats.bytes_received += len;
}

/**
 * @brief Initialize LD2410 to engineering mode
 */
static esp_err_t init_ld2410_engineering_mode(int uart_num)
{
    uint8_t cmd_buf[20];
    size_t cmd_len;

    ESP_LOGI(TAG, "Enabling LD2410 engineering mode...");

    /* Send enable config command */
    cmd_len = ld2410_build_enable_config(cmd_buf);
    uart_write_bytes(uart_num, cmd_buf, cmd_len);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Send enable engineering mode command */
    cmd_len = ld2410_build_enable_engineering_mode(cmd_buf);
    uart_write_bytes(uart_num, cmd_buf, cmd_len);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Send disable config command */
    cmd_len = ld2410_build_disable_config(cmd_buf);
    uart_write_bytes(uart_num, cmd_buf, cmd_len);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "LD2410 engineering mode enabled");
    return ESP_OK;
}

/**
 * @brief Main radar processing task
 *
 * Runs on Core 1 for time-critical processing.
 * Handles both LD2410 and LD2450 UART streams.
 */
static void radar_task(void *arg)
{
    uint8_t rx_buffer[UART_BUF_SIZE];
    uart_event_t event;

    ESP_LOGI(TAG, "Radar task started on core %d", xPortGetCoreID());

    /* Initialize LD2410 to engineering mode */
    init_ld2410_engineering_mode(s_radar.config.ld2410_uart_num);

    uint32_t last_timeout_check = get_time_ms();

    while (1) {
        /* Process LD2410 events */
        if (xQueueReceive(s_ld2410_queue, &event, 0)) {
            if (event.type == UART_DATA && event.size > 0) {
                int len = uart_read_bytes(s_radar.config.ld2410_uart_num,
                                         rx_buffer, event.size, 0);
                if (len > 0) {
                    process_ld2410_data(rx_buffer, len);
                }
            }
        }

        /* Process LD2450 events (if enabled) */
        if (s_radar.config.ld2450_uart_num >= 0 && s_ld2450_queue) {
            if (xQueueReceive(s_ld2450_queue, &event, 0)) {
                if (event.type == UART_DATA && event.size > 0) {
                    int len = uart_read_bytes(s_radar.config.ld2450_uart_num,
                                             rx_buffer, event.size, 0);
                    if (len > 0) {
                        process_ld2450_data(rx_buffer, len);
                    }
                }
            }
        }

        /* Periodic timeout check */
        uint32_t now = get_time_ms();
        if ((now - last_timeout_check) >= 500) {
            check_radar_timeouts();
            last_timeout_check = now;

            /* Update frame rate stats */
            static uint32_t last_ld2410_frames = 0;
            static uint32_t last_ld2450_frames = 0;
            static uint32_t last_rate_update = 0;

            if ((now - last_rate_update) >= 1000) {
                uint32_t ld2410_delta = s_radar.ld2410_stats.frames_received - last_ld2410_frames;
                uint32_t ld2450_delta = s_radar.ld2450_stats.frames_received - last_ld2450_frames;

                s_radar.ld2410_stats.frame_rate_hz = (float)ld2410_delta;
                s_radar.ld2450_stats.frame_rate_hz = (float)ld2450_delta;

                last_ld2410_frames = s_radar.ld2410_stats.frames_received;
                last_ld2450_frames = s_radar.ld2450_stats.frames_received;
                last_rate_update = now;
            }
        }

        /* Small delay to prevent tight loop */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * @brief Initialize UART for radar
 */
static esp_err_t init_uart(int uart_num, int rx_pin, int tx_pin, QueueHandle_t *queue)
{
    uart_config_t uart_config = {
        .baud_rate = 256000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret;

    ret = uart_driver_install(uart_num, UART_BUF_SIZE * 2, 0,
                              UART_QUEUE_SIZE, queue, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART%d driver: %s",
                 uart_num, esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(uart_num, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART%d: %s",
                 uart_num, esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(uart_num, tx_pin, rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART%d pins: %s",
                 uart_num, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "UART%d initialized: RX=%d TX=%d @ 256000 baud",
             uart_num, rx_pin, tx_pin);

    return ESP_OK;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

esp_err_t radar_ingest_init(const radar_ingest_config_t *config)
{
    if (s_radar.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Copy configuration */
    memcpy(&s_radar.config, config, sizeof(radar_ingest_config_t));

    /* Create mutex */
    s_radar.mutex = xSemaphoreCreateMutex();
    if (s_radar.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Initialize parsers */
    ld2410_parser_init(&s_radar.ld2410_parser);
    ld2450_parser_init(&s_radar.ld2450_parser);

    /* Initialize states */
    s_radar.ld2410_state = RADAR_STATE_DISCONNECTED;
    s_radar.ld2450_state = RADAR_STATE_DISCONNECTED;
    s_radar.ld2410_last_frame_ms = get_time_ms();
    s_radar.ld2450_last_frame_ms = get_time_ms();

    /* Initialize LD2410 UART */
    esp_err_t ret = init_uart(config->ld2410_uart_num,
                              config->ld2410_rx_pin,
                              config->ld2410_tx_pin,
                              &s_ld2410_queue);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Initialize LD2450 UART (if enabled) */
    if (config->ld2450_uart_num >= 0) {
        ret = init_uart(config->ld2450_uart_num,
                       config->ld2450_rx_pin,
                       config->ld2450_tx_pin,
                       &s_ld2450_queue);
        if (ret != ESP_OK) {
            uart_driver_delete(config->ld2410_uart_num);
            return ret;
        }
        ESP_LOGI(TAG, "LD2450 tracking radar enabled (Pro mode)");
    } else {
        ESP_LOGI(TAG, "LD2450 tracking radar disabled (Lite mode)");
    }

    /* Create radar task on specified core */
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        radar_task,
        "radar",
        config->task_stack_size,
        NULL,
        config->task_priority,
        &s_radar.task_handle,
        config->task_core
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create radar task");
        uart_driver_delete(config->ld2410_uart_num);
        if (config->ld2450_uart_num >= 0) {
            uart_driver_delete(config->ld2450_uart_num);
        }
        return ESP_FAIL;
    }

    s_radar.initialized = true;
    ESP_LOGI(TAG, "Radar ingest module initialized");

    return ESP_OK;
}

esp_err_t radar_ingest_deinit(void)
{
    if (!s_radar.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Stop task */
    if (s_radar.task_handle) {
        vTaskDelete(s_radar.task_handle);
        s_radar.task_handle = NULL;
    }

    /* Release UART */
    uart_driver_delete(s_radar.config.ld2410_uart_num);
    if (s_radar.config.ld2450_uart_num >= 0) {
        uart_driver_delete(s_radar.config.ld2450_uart_num);
    }

    /* Release mutex */
    if (s_radar.mutex) {
        vSemaphoreDelete(s_radar.mutex);
        s_radar.mutex = NULL;
    }

    s_radar.initialized = false;
    ESP_LOGI(TAG, "Radar ingest module deinitialized");

    return ESP_OK;
}

esp_err_t radar_ingest_register_detection_callback(radar_detection_callback_t callback,
                                                    void *user_ctx)
{
    s_radar.detection_cb = callback;
    s_radar.detection_cb_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t radar_ingest_register_presence_callback(radar_presence_callback_t callback,
                                                   void *user_ctx)
{
    s_radar.presence_cb = callback;
    s_radar.presence_cb_ctx = user_ctx;
    return ESP_OK;
}

esp_err_t radar_ingest_register_state_callback(radar_state_callback_t callback,
                                                void *user_ctx)
{
    s_radar.state_cb = callback;
    s_radar.state_cb_ctx = user_ctx;
    return ESP_OK;
}

radar_state_t radar_ingest_get_state(radar_sensor_t sensor)
{
    if (sensor == RADAR_SENSOR_LD2410) {
        return s_radar.ld2410_state;
    } else if (sensor == RADAR_SENSOR_LD2450) {
        return s_radar.ld2450_state;
    }
    return RADAR_STATE_DISCONNECTED;
}

bool radar_ingest_has_tracking(void)
{
    return s_radar.config.ld2450_uart_num >= 0;
}

esp_err_t radar_ingest_get_stats(radar_sensor_t sensor, radar_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_radar.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (sensor == RADAR_SENSOR_LD2410) {
            uint32_t parsed, invalid;
            ld2410_parser_get_stats(&s_radar.ld2410_parser, &parsed, &invalid);
            s_radar.ld2410_stats.frames_invalid = invalid;
            memcpy(stats, &s_radar.ld2410_stats, sizeof(radar_stats_t));
        } else if (sensor == RADAR_SENSOR_LD2450) {
            uint32_t parsed, invalid;
            ld2450_parser_get_stats(&s_radar.ld2450_parser, &parsed, &invalid);
            s_radar.ld2450_stats.frames_invalid = invalid;
            memcpy(stats, &s_radar.ld2450_stats, sizeof(radar_stats_t));
        }
        xSemaphoreGive(s_radar.mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}
