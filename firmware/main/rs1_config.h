/**
 * @file rs1_config.h
 * @brief RS-1 HardwareOS Configuration Header
 *
 * Central configuration file for RS-1 firmware.
 * Contains hardware definitions, pin assignments, and compile-time configuration.
 *
 * Reference: docs/hardware/HARDWARE_SPEC.md Section 7 (Pin Assignments)
 */

#ifndef RS1_CONFIG_H
#define RS1_CONFIG_H

#include "sdkconfig.h"

/* Version Information */
#define RS1_VERSION_MAJOR 0
#define RS1_VERSION_MINOR 1
#define RS1_VERSION_PATCH 0

/* Product Variant Configuration
 * Set via menuconfig or sdkconfig.defaults
 * RS1_VARIANT_LITE: LD2410 only, binary presence
 * RS1_VARIANT_PRO:  LD2410 + LD2450, zone tracking
 */
#ifdef CONFIG_RS1_VARIANT_PRO
    #define RS1_VARIANT_PRO 1
    #define RS1_VARIANT_LITE 0
    #define RS1_VARIANT_NAME "RS-1 Pro"
#else
    #define RS1_VARIANT_PRO 0
    #define RS1_VARIANT_LITE 1
    #define RS1_VARIANT_NAME "RS-1 Lite"
#endif

/* ============================================================================
 * GPIO Pin Assignments
 * Reference: docs/hardware/HARDWARE_SPEC.md Section 7.1
 * ============================================================================ */

/* UART0 - USB Debug Console (CH340N) */
#define RS1_GPIO_UART0_TX       1
#define RS1_GPIO_UART0_RX       3

/* UART1 - LD2410 Static Presence Radar (Both variants) */
#define RS1_GPIO_LD2410_TX      4
#define RS1_GPIO_LD2410_RX      5
#define RS1_UART_LD2410_NUM     UART_NUM_1

/* UART2 - LD2450 Tracking Radar (Pro only) */
#if RS1_VARIANT_PRO
    #define RS1_GPIO_LD2450_TX  16
    #define RS1_GPIO_LD2450_RX  17
    #define RS1_UART_LD2450_NUM UART_NUM_2
#endif

/* I2C Bus - Sensors */
#define RS1_GPIO_I2C_SDA        21
#define RS1_GPIO_I2C_SCL        22

/* Status LED (WS2812) */
#define RS1_GPIO_LED            25

/* Reset Button */
#define RS1_GPIO_RESET_BTN      34

/* ============================================================================
 * UART Configuration
 * Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md Section 3
 * ============================================================================ */

/* LD2410 UART Settings */
#define RS1_LD2410_BAUD_RATE    256000
#define RS1_LD2410_DATA_BITS    UART_DATA_8_BITS
#define RS1_LD2410_PARITY       UART_PARITY_DISABLE
#define RS1_LD2410_STOP_BITS    UART_STOP_BITS_1
#define RS1_LD2410_FRAME_SIZE   39  /* Engineering mode frame size */
#define RS1_LD2410_FRAME_RATE   5   /* Approximate Hz */

/* LD2450 UART Settings (Pro only) */
#if RS1_VARIANT_PRO
    #define RS1_LD2450_BAUD_RATE    256000
    #define RS1_LD2450_DATA_BITS    UART_DATA_8_BITS
    #define RS1_LD2450_PARITY       UART_PARITY_DISABLE
    #define RS1_LD2450_STOP_BITS    UART_STOP_BITS_1
    #define RS1_LD2450_FRAME_SIZE   40  /* Target frame size */
    #define RS1_LD2450_FRAME_RATE   33  /* Hz */
#endif

/* ============================================================================
 * I2C Configuration
 * Reference: docs/hardware/HARDWARE_SPEC.md Section 7.2
 * ============================================================================ */

#define RS1_I2C_PORT            I2C_NUM_0
#define RS1_I2C_FREQ_HZ         400000  /* 400 kHz Fast Mode */

/* I2C Device Addresses */
#define RS1_I2C_ADDR_AHT20      0x38    /* Temperature/Humidity */
#define RS1_I2C_ADDR_LTR303     0x29    /* Ambient Light */
#define RS1_I2C_ADDR_ENS160     0x52    /* Air Quality (IAQ module) */

/* ============================================================================
 * Radar Detection Limits
 * Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md Section 5.3
 * ============================================================================ */

/* LD2450 Coordinate Range (mm) */
#define RS1_RADAR_X_MIN         (-6000)
#define RS1_RADAR_X_MAX         6000
#define RS1_RADAR_Y_MIN         0
#define RS1_RADAR_Y_MAX         6000

/* Filtering Thresholds */
#define RS1_RADAR_MIN_RANGE_MM  100     /* Near-field rejection */
#define RS1_RADAR_MAX_RANGE_MM  6000    /* Max valid Y distance */
#define RS1_RADAR_MAX_SPEED_CMS 500     /* Speed sanity check */

/* LD2410 Energy Threshold */
#define RS1_LD2410_MIN_ENERGY   10      /* Minimum energy for presence */

/* ============================================================================
 * Timing Configuration
 * Reference: docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md Section 6.1
 * ============================================================================ */

/* Radar disconnect detection */
#define RS1_RADAR_DISCONNECT_TIMEOUT_MS 3000

/* Frame timing tolerances */
#define RS1_FRAME_TIMEOUT_MS    100     /* Log warning after this */

/* ============================================================================
 * Zone Configuration
 * Reference: docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md
 * ============================================================================ */

#define RS1_MAX_ZONES           16
#define RS1_MAX_VERTICES        8       /* Per zone */
#define RS1_MAX_TARGETS         3       /* LD2450 hardware limit */

/* ============================================================================
 * FreeRTOS Task Configuration
 * Reference: docs/firmware/MEMORY_BUDGET.md Section 6.1
 * ============================================================================ */

/* Core affinity (SMP architecture) */
#define RS1_CORE_NETWORK        0       /* Wi-Fi, Native API, OTA, Logging */
#define RS1_CORE_RADAR          1       /* Radar Ingest, Tracking, Smoothing */

/* Task priorities */
#define RS1_TASK_PRIORITY_RADAR     (configMAX_PRIORITIES - 1)
#define RS1_TASK_PRIORITY_NETWORK   (configMAX_PRIORITIES - 2)
#define RS1_TASK_PRIORITY_NORMAL    (configMAX_PRIORITIES / 2)
#define RS1_TASK_PRIORITY_LOW       (tskIDLE_PRIORITY + 1)

/* Stack sizes (words, not bytes) */
#define RS1_TASK_STACK_RADAR        (2048)
#define RS1_TASK_STACK_NATIVE_API   (2048)
#define RS1_TASK_STACK_OTA          (4096)
#define RS1_TASK_STACK_MAIN         (4096)

/* ============================================================================
 * Memory Budget Limits
 * Reference: docs/firmware/MEMORY_BUDGET.md
 * ============================================================================ */

#define RS1_HEAP_WARNING_KB     30      /* Log warning below this */
#define RS1_HEAP_CRITICAL_KB    10      /* Reboot below this */
#define RS1_OTA_MIN_FREE_HEAP_KB 20     /* Abort OTA below this */

/* Log buffer */
#define RS1_LOG_BUFFER_SIZE     8192    /* Default log ring buffer */
#define RS1_LOG_BUFFER_OTA      2048    /* Reduced during OTA */

/* ============================================================================
 * Native API Configuration
 * Reference: docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md
 * ============================================================================ */

#define RS1_NATIVE_API_PORT     6053
#define RS1_NATIVE_API_MAX_CONN 3       /* Max simultaneous connections */

/* mDNS Service */
#define RS1_MDNS_SERVICE_TYPE   "_esphomelib._tcp"
#define RS1_MDNS_SERVICE_NAME   "rs1"

/* ============================================================================
 * MQTT Configuration
 * Reference: docs/contracts/PROTOCOL_MQTT.md
 * ============================================================================ */

#define RS1_MQTT_PORT           8883    /* TLS */
#define RS1_MQTT_KEEPALIVE_SEC  60
#define RS1_MQTT_QOS_TELEMETRY  0       /* QoS 0 for telemetry */
#define RS1_MQTT_QOS_OTA        1       /* QoS 1 for OTA triggers */

/* Topic prefix: opticworks/{device_id}/ */
#define RS1_MQTT_TOPIC_PREFIX   "opticworks"

#endif /* RS1_CONFIG_H */
