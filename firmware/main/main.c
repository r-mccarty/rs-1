/**
 * @file main.c
 * @brief RS-1 HardwareOS Main Entry Point
 *
 * This is the main entry point for the RS-1 presence sensor firmware.
 * It initializes all HardwareOS modules according to the boot sequence
 * defined in docs/firmware/BOOT_SEQUENCE.md.
 *
 * @version 0.1.0
 * @date 2026-02-02
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "rs1_config.h"
#include "radar_ingest.h"

static const char *TAG = "RS1_MAIN";

/**
 * @brief Initialize Non-Volatile Storage (NVS)
 *
 * NVS is required for Wi-Fi credentials, zone configuration, and device settings.
 * If NVS is corrupted or uninitialized, it will be erased and re-initialized.
 */
static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

/**
 * @brief Print device information at startup
 */
static void print_device_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, " RS-1 HardwareOS v%d.%d.%d",
             RS1_VERSION_MAJOR, RS1_VERSION_MINOR, RS1_VERSION_PATCH);
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "Variant: %s", RS1_VARIANT_NAME);
    ESP_LOGI(TAG, "ESP32 with %d CPU core(s), WiFi%s%s",
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    ESP_LOGI(TAG, "Silicon revision %d", chip_info.revision);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "==========================================");
}

/**
 * @brief Application entry point
 *
 * Boot sequence (from BOOT_SEQUENCE.md):
 * 1. Initialize NVS (M06 dependency)
 * 2. Initialize Logging (M09)
 * 3. Initialize Timebase (M08)
 * 4. Initialize Config Store (M06)
 * 5. Initialize Security (M10)
 * 6. Initialize Radar Ingest (M01)
 * 7. Initialize Tracking (M02) - Pro only
 * 8. Initialize Zone Engine (M03) - Pro only
 * 9. Initialize Presence Smoothing (M04)
 * 10. Initialize Native API (M05)
 * 11. Initialize OTA Manager (M07)
 * 12. Initialize Zone Editor Interface (M11)
 * 13. Initialize IAQ Module (M12) - if detected
 */
void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());

    print_device_info();

    /* Initialize M01 - Radar Ingest */
    ESP_LOGI(TAG, "Initializing M01 Radar Ingest...");

    radar_ingest_config_t radar_config = RADAR_INGEST_CONFIG_DEFAULT();

    /* Override with Kconfig values if available */
    #ifdef CONFIG_RS1_LD2410_RX_PIN
    radar_config.ld2410_rx_pin = CONFIG_RS1_LD2410_RX_PIN;
    #endif
    #ifdef CONFIG_RS1_LD2410_TX_PIN
    radar_config.ld2410_tx_pin = CONFIG_RS1_LD2410_TX_PIN;
    #endif

    #if RS1_VARIANT_PRO
        #ifdef CONFIG_RS1_LD2450_RX_PIN
        radar_config.ld2450_rx_pin = CONFIG_RS1_LD2450_RX_PIN;
        #endif
        #ifdef CONFIG_RS1_LD2450_TX_PIN
        radar_config.ld2450_tx_pin = CONFIG_RS1_LD2450_TX_PIN;
        #endif
    #else
        /* Lite variant - disable LD2450 */
        radar_config.ld2450_uart_num = -1;
    #endif

    #ifdef CONFIG_RS1_RADAR_DISCONNECT_TIMEOUT_MS
    radar_config.disconnect_timeout_ms = CONFIG_RS1_RADAR_DISCONNECT_TIMEOUT_MS;
    #endif

    esp_err_t ret = radar_ingest_init(&radar_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize radar ingest: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "M01 Radar Ingest initialized");
    }

    ESP_LOGI(TAG, "HardwareOS boot complete");

    /* Main loop - heap monitoring */
    uint32_t last_heap_log = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        #ifdef CONFIG_RS1_DEBUG_HEAP_MONITOR
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if ((now - last_heap_log) >= 10000) { /* Every 10 seconds */
            ESP_LOGI(TAG, "Heap: free=%lu min=%lu",
                     esp_get_free_heap_size(),
                     esp_get_minimum_free_heap_size());
            last_heap_log = now;
        }
        #endif
    }
}
