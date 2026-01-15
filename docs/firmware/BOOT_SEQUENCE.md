# HardwareOS Boot Sequence Specification

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks Firmware
Status: Draft

---

## 1. Purpose

This document defines the module initialization order, dependencies, and failure handling during HardwareOS boot. It ensures deterministic startup behavior and prevents race conditions between modules.

**This document addresses RFD-001 issue C7: Boot sequence undefined.**

---

## 2. Boot Phases

### 2.1 Phase Overview

```
                    HARDWAREOS BOOT SEQUENCE
════════════════════════════════════════════════════════════════════════

  Time    Phase          Duration   Description
  ─────   ─────          ────────   ───────────
  0ms     ESP-IDF Init   ~500ms     Bootloader, flash, clocks
  500ms   Phase 1        ~50ms      Core services (no deps)
  550ms   Phase 2        ~100ms     Config and security
  650ms   Phase 3        ~200ms     Network (Wi-Fi connect)
  850ms   Phase 4        ~50ms      Radar and processing
  900ms   Phase 5        ~100ms     External interfaces
  1000ms  READY          -          System operational

════════════════════════════════════════════════════════════════════════
```

### 2.2 Phase Definitions

| Phase | Name | Modules | Max Duration |
|-------|------|---------|--------------|
| 0 | ESP-IDF | Bootloader, FreeRTOS, drivers | 500ms |
| 1 | Core Services | M08 Timebase, M09 Logging | 50ms |
| 2 | Configuration | M06 Config Store, M10 Security, Variant Detection | 100ms |
| 3 | Network | Wi-Fi, mDNS | 200ms |
| 4 | Processing | M01 Radar, M02 Tracking*, M03 Zone*, M04 Smoothing | 50ms |
| 5 | Interfaces | M05 Native API, M07 OTA, M11 Zone Editor | 100ms |

*M02/M03 only initialized for RS-1 Pro variant. RS-1 Lite skips these modules.

### 2.3 Variant-Aware Initialization

The boot sequence detects the product variant during Phase 2 and adjusts module initialization:

| Variant | Processing Pipeline | Skipped Modules |
|---------|---------------------|-----------------|
| **RS-1 Lite** | M01 → M04 → M05 | M02 Tracking, M03 Zone Engine |
| **RS-1 Pro** | M01 → M02 → M03 → M04 → M05 | None |

Variant detection reads from NVS configuration set during manufacturing/provisioning.

---

## 3. Module Dependency Graph

```
                    MODULE INITIALIZATION DAG
════════════════════════════════════════════════════════════════════════

    Level 0 (no deps)        Level 1              Level 2
    ─────────────────        ───────              ───────

         ┌───────┐
         │  M08  │ ◀───────────────────────────────────────────┐
         │Timebase│                                             │
         └───┬───┘                                              │
             │                                                  │
             ▼                                                  │
         ┌───────┐                                              │
         │  M09  │ ◀────────────────────────────────────┐      │
         │Logging│                                       │      │
         └───┬───┘                                       │      │
             │                                           │      │
             ├──────────────┬──────────────┐            │      │
             ▼              ▼              ▼            │      │
         ┌───────┐     ┌───────┐     ┌───────┐         │      │
         │  M06  │     │  M10  │     │Wi-Fi  │         │      │
         │Config │     │Security│    │Driver │         │      │
         └───┬───┘     └───┬───┘     └───┬───┘         │      │
             │             │             │             │      │
             │             │             │             │      │
             │     ┌───────┴───────┐     │             │      │
             │     │               │     │             │      │
             ▼     ▼               ▼     ▼             │      │
         ┌───────────┐        ┌───────────┐            │      │
         │    M01    │        │    M05    │────────────┤      │
         │Radar Ingest│       │Native API │            │      │
         └─────┬─────┘        └───────────┘            │      │
               │                    ▲                  │      │
               ▼                    │                  │      │
         ┌───────────┐              │                  │      │
         │    M02    │              │                  │      │
         │ Tracking  │              │                  │      │
         └─────┬─────┘              │                  │      │
               │                    │                  │      │
               ▼                    │                  │      │
         ┌───────────┐              │                  │      │
         │    M03    │──────────────┤                  │      │
         │Zone Engine│              │                  │      │
         └─────┬─────┘              │                  │      │
               │                    │                  │      │
               ▼                    │                  │      │
         ┌───────────┐              │                  │      │
         │    M04    │──────────────┘                  │      │
         │Smoothing  │                                 │      │
         └───────────┘                                 │      │
                                                       │      │
         ┌───────────┐     ┌───────────┐              │      │
         │    M07    │────▶│    M11    │──────────────┴──────┘
         │    OTA    │     │Zone Editor│
         └───────────┘     └───────────┘

════════════════════════════════════════════════════════════════════════

    Legend:
    ────▶  "must initialize after"
    All modules depend on M08 (Timebase) and M09 (Logging)
```

---

## 4. Initialization Order

### 4.1 Ordered Module List

| Order | Module | Dependencies | Init Function | Timeout |
|-------|--------|--------------|---------------|---------|
| 1 | M08 Timebase | None | `timebase_init()` | 10ms |
| 2 | M09 Logging | M08 | `logging_init()` | 20ms |
| 3 | M06 Config Store | M09 | `config_init()` | 50ms |
| 4 | M10 Security | M09 | `security_init()` | 30ms |
| 5 | Wi-Fi | M06, M09 | `wifi_init()` | 200ms |
| 6 | M01 Radar Ingest | M08, M09 | `radar_init()` | 20ms |
| 7 | M02 Tracking | M01, M08, M09 | `tracking_init()` | 10ms |
| 8 | M03 Zone Engine | M02, M06 | `zone_engine_init()` | 10ms |
| 9 | M04 Smoothing | M03, M06 | `smoothing_init()` | 10ms |
| 10 | M05 Native API | M04, M10, Wi-Fi | `native_api_init()` | 50ms |
| 11 | M07 OTA | M06, M10, Wi-Fi | `ota_init()` | 30ms |
| 12 | M11 Zone Editor | M06, M10, Wi-Fi | `zone_editor_init()` | 30ms |

### 4.2 Initialization Code

```c
typedef esp_err_t (*init_fn_t)(void);

typedef struct {
    const char *name;
    init_fn_t init;
    uint32_t timeout_ms;
    bool required;          // System won't start without this
} init_stage_t;

static const init_stage_t init_sequence[] = {
    // Phase 1: Core services
    {"M08 Timebase",    timebase_init,      10,  true},
    {"M09 Logging",     logging_init,       20,  true},

    // Phase 2: Configuration
    {"M06 Config",      config_init,        50,  true},
    {"M10 Security",    security_init,      30,  true},

    // Phase 3: Network
    {"Wi-Fi",           wifi_init,          200, false},  // Can work offline

    // Phase 4: Processing pipeline
    {"M01 Radar",       radar_init,         20,  true},
    {"M02 Tracking",    tracking_init,      10,  true},
    {"M03 Zone Engine", zone_engine_init,   10,  true},
    {"M04 Smoothing",   smoothing_init,     10,  true},

    // Phase 5: External interfaces
    {"M05 Native API",  native_api_init,    50,  false},  // Can work without HA
    {"M07 OTA",         ota_init,           30,  false},
    {"M11 Zone Editor", zone_editor_init,   30,  false},
};

void app_main(void) {
    esp_err_t err;

    for (int i = 0; i < ARRAY_SIZE(init_sequence); i++) {
        const init_stage_t *stage = &init_sequence[i];

        ESP_LOGI(TAG, "Initializing %s...", stage->name);
        uint32_t start = timebase_uptime_ms();

        err = stage->init();

        uint32_t elapsed = timebase_uptime_ms() - start;

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s init failed: %s", stage->name, esp_err_to_name(err));
            if (stage->required) {
                ESP_LOGE(TAG, "Required module failed, rebooting...");
                esp_restart();
            }
            // Non-required: continue in degraded mode
            continue;
        }

        if (elapsed > stage->timeout_ms) {
            ESP_LOGW(TAG, "%s init slow: %dms > %dms",
                     stage->name, elapsed, stage->timeout_ms);
        }

        ESP_LOGI(TAG, "%s initialized in %dms", stage->name, elapsed);
    }

    ESP_LOGI(TAG, "HardwareOS boot complete");
    system_state = SYSTEM_STATE_READY;
}
```

---

## 5. Failure Handling

### 5.1 Required vs Optional Modules

| Module | Required | Fallback Behavior |
|--------|----------|-------------------|
| M08 Timebase | Yes | Cannot continue |
| M09 Logging | Yes | Cannot continue |
| M06 Config Store | Yes | Cannot continue (no config) |
| M10 Security | Yes | Cannot continue (no auth) |
| Wi-Fi | No | Offline mode, no HA/cloud |
| M01 Radar | Yes | No presence detection possible |
| M02-M04 Pipeline | Yes | No presence detection possible |
| M05 Native API | No | No HA integration |
| M07 OTA | No | No remote updates |
| M11 Zone Editor | No | No zone configuration |

### 5.2 Failure Recovery

```c
typedef enum {
    BOOT_FAIL_RESTART,      // Required module: restart
    BOOT_FAIL_DEGRADED,     // Optional module: continue without
    BOOT_FAIL_RETRY,        // Transient failure: retry
} boot_fail_action_t;

boot_fail_action_t handle_init_failure(const char *module, esp_err_t err) {
    if (strcmp(module, "Wi-Fi") == 0) {
        if (err == ESP_ERR_WIFI_NOT_STARTED) {
            return BOOT_FAIL_RETRY;  // Retry once
        }
        return BOOT_FAIL_DEGRADED;   // Continue offline
    }

    if (strcmp(module, "M05 Native API") == 0 ||
        strcmp(module, "M07 OTA") == 0 ||
        strcmp(module, "M11 Zone Editor") == 0) {
        return BOOT_FAIL_DEGRADED;   // Non-essential
    }

    return BOOT_FAIL_RESTART;        // Essential module
}
```

### 5.3 Boot Loop Prevention

```c
#define MAX_BOOT_FAILURES 3
#define BOOT_FAILURE_RESET_SEC 300  // Reset counter after 5 min uptime

void check_boot_loop(void) {
    uint32_t boot_count = nvs_get_boot_count();

    if (boot_count > MAX_BOOT_FAILURES) {
        ESP_LOGE(TAG, "Boot loop detected (%d failures), entering safe mode",
                 boot_count);
        enter_safe_mode();
        return;
    }

    nvs_set_boot_count(boot_count + 1);

    // Schedule boot counter reset after stable uptime
    scheduler_register("boot_counter_reset", reset_boot_counter,
                       NULL, BOOT_FAILURE_RESET_SEC * 1000);
}

void enter_safe_mode(void) {
    // Minimal boot: only Wi-Fi and OTA
    // Allows recovery via firmware update
    wifi_init();
    ota_init();
    // Do NOT init radar/presence pipeline
}
```

---

## 6. Ready State Validation

### 6.1 Ready Criteria

System is "ready" when:

```c
typedef struct {
    bool timebase_ok;       // M08 initialized
    bool logging_ok;        // M09 initialized
    bool config_ok;         // M06 initialized, config valid
    bool security_ok;       // M10 initialized
    bool radar_ok;          // M01 receiving frames
    bool pipeline_ok;       // M02-M04 processing
    bool wifi_connected;    // Wi-Fi associated (optional)
    bool native_api_ok;     // M05 listening (optional)
} ready_state_t;

bool system_is_ready(void) {
    return state.timebase_ok &&
           state.logging_ok &&
           state.config_ok &&
           state.security_ok &&
           state.radar_ok &&
           state.pipeline_ok;
}
```

### 6.2 Ready Timeout

If system doesn't reach ready state within 30 seconds:

1. Log detailed diagnostics
2. If OTA pending, mark as failed (triggers rollback)
3. Reboot and retry

---

## 7. OTA Boot Validation

### 7.1 First Boot After OTA

```c
void validate_ota_boot(void) {
    if (!esp_ota_check_rollback_is_possible()) {
        return;  // Not an OTA boot
    }

    // Must reach ready state within 30 seconds
    scheduler_register("ota_validation", ota_validation_check,
                       NULL, 30000);
}

void ota_validation_check(void *arg) {
    if (system_is_ready()) {
        ESP_LOGI(TAG, "OTA validation passed, marking firmware valid");
        esp_ota_mark_app_valid_cancel_rollback();
    } else {
        ESP_LOGE(TAG, "OTA validation failed, will rollback on next boot");
        // Intentionally don't mark valid - bootloader will rollback
        esp_restart();
    }
}
```

---

## 8. Timing Budget

### 8.1 Boot Time Targets

| Target | Time | Measurement |
|--------|------|-------------|
| First log message | < 600ms | M09 initialized |
| Config loaded | < 700ms | M06 initialized |
| Radar receiving | < 900ms | First frame from M01 |
| Ready state | < 1200ms | All required modules |
| HA discovery | < 2000ms | mDNS advertised |

### 8.2 Measurement Points

```c
void log_boot_timing(const char *checkpoint) {
    static uint32_t boot_start = 0;
    if (boot_start == 0) {
        boot_start = timebase_uptime_ms();
    }
    ESP_LOGI(TAG, "BOOT: %s at +%dms", checkpoint,
             timebase_uptime_ms() - boot_start);
}
```

---

## 9. Telemetry

| Metric | Type | Description |
|--------|------|-------------|
| `boot.duration_ms` | Gauge | Time to ready state |
| `boot.failures` | Counter | Init failures |
| `boot.count` | Counter | Total boots |
| `boot.safe_mode` | Counter | Safe mode entries |

---

## 10. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-09 | OpticWorks | Initial specification. Addresses RFD-001 issue C7. |
