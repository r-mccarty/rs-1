# HardwareOS Degraded Modes Specification

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks Firmware
Status: Draft

---

## 1. Purpose

This document specifies how RS-1 devices behave when subsystems fail or external dependencies are unavailable. The goal is graceful degradation: maintain core functionality where possible and provide clear status to users.

**This document addresses RFD-001 issues C13 (Wi-Fi disconnect) and C16 (degraded mode specification).**

---

## 2. Design Principles

1. **Core functionality first**: Presence detection should work even when cloud/HA is unavailable
2. **No silent failures**: User should be able to determine device state
3. **Automatic recovery**: When failures resolve, resume normal operation
4. **No reboot loops**: Hardware failures should not cause infinite restarts
5. **Preserve data**: Buffer telemetry/logs during outages where possible

---

## 3. Degradation Matrix

| Failure | Core Detection | HA Integration | Cloud Features | Recovery |
|---------|----------------|----------------|----------------|----------|
| Wi-Fi down | YES | NO | NO | Automatic |
| MQTT down | YES | YES | NO | Automatic |
| HA disconnected | YES | NO | YES | Automatic |
| Radar disconnected | NO | YES (stale) | YES | Automatic |
| NVS corrupted | YES (defaults) | YES | YES | Manual |
| Flash full | YES | YES | NO (logs lost) | Automatic |

---

## 4. Wi-Fi Disconnection

### 4.1 Detection

- ESP-IDF Wi-Fi event: `WIFI_EVENT_STA_DISCONNECTED`
- M08 watchdog source removed from expected feeds
- M09 logs event

### 4.2 Behavior During Outage

| Module | Behavior |
|--------|----------|
| M01 Radar Ingest | Normal operation |
| M02 Tracking | Normal operation |
| M03 Zone Engine | Normal operation |
| M04 Presence Smoothing | Normal operation |
| M05 Native API | TCP listeners active but no connections |
| M07 OTA | Suspended |
| M09 Logging | Telemetry queued locally (max 100 entries) |
| M11 Zone Editor | Local API unavailable |

### 4.3 User Indicators

- Device LED: Slow blink (configurable)
- M05: No HA entities update
- Serial console: "WiFi: DISCONNECTED" status

### 4.4 Recovery

```c
void wifi_reconnect_handler(void) {
    // Automatic reconnection with backoff
    static int attempt = 0;
    int delay_ms = min(1000 * (1 << attempt), 60000);  // 1s, 2s, 4s... max 60s

    if (wifi_connect() == ESP_OK) {
        attempt = 0;
        log_event("wifi_reconnected");
        telemetry_flush_queued();  // Send buffered telemetry
        mqtt_reconnect();
    } else {
        attempt++;
        schedule_retry(delay_ms);
    }
}
```

### 4.5 Impact Summary

```
┌────────────────────────────────────────────────────────────────┐
│                    Wi-Fi Disconnected                          │
│                                                                │
│  Working:                    Not Working:                      │
│  ✓ Radar detection           ✗ Home Assistant updates          │
│  ✓ Zone occupancy            ✗ OTA updates                     │
│  ✓ Local presence state      ✗ Cloud telemetry                 │
│  ✓ Serial logging            ✗ Zone Editor (cloud mode)        │
│                              ✗ Remote diagnostics              │
└────────────────────────────────────────────────────────────────┘
```

---

## 5. MQTT Broker Unavailable

### 5.1 Detection

- MQTT connection timeout
- MQTT disconnect event
- Ping failure (keep-alive)

### 5.2 Behavior During Outage

| Module | Behavior |
|--------|----------|
| M05 Native API | Normal operation (HA works via local API) |
| M07 OTA | Cannot receive triggers |
| M09 Telemetry | Queued locally |
| M11 Zone Editor | Cloud sync disabled, local API works |

### 5.3 Recovery

- Automatic reconnection with exponential backoff
- Queued telemetry flushed on reconnect
- OTA trigger re-evaluation on reconnect

### 5.4 Impact Summary

```
┌────────────────────────────────────────────────────────────────┐
│                    MQTT Disconnected                           │
│                                                                │
│  Working:                    Not Working:                      │
│  ✓ Radar detection           ✗ OTA updates (no trigger)        │
│  ✓ Zone occupancy            ✗ Cloud telemetry                 │
│  ✓ Home Assistant (local)    ✗ Zone Editor (cloud sync)        │
│  ✓ Zone Editor (local)       ✗ Remote diagnostics              │
│  ✓ Serial logging                                              │
└────────────────────────────────────────────────────────────────┘
```

---

## 6. Home Assistant Disconnected

### 6.1 Detection

- Native API TCP connection closed
- No subscription refresh within timeout

### 6.2 Behavior During Outage

| Module | Behavior |
|--------|----------|
| M01-M04 | Normal operation |
| M05 Native API | Awaiting new connection |
| M07 OTA | Normal (cloud-triggered) |
| M09 Telemetry | Normal (cloud) |

### 6.3 Recovery

- HA initiates reconnection (device passive)
- State subscription resumes
- All current states published on reconnect

### 6.4 Impact Summary

```
┌────────────────────────────────────────────────────────────────┐
│                    Home Assistant Disconnected                 │
│                                                                │
│  Working:                    Not Working:                      │
│  ✓ Radar detection           ✗ HA entity updates               │
│  ✓ Zone occupancy            ✗ HA automations                  │
│  ✓ Cloud telemetry                                             │
│  ✓ OTA updates                                                 │
│  ✓ Zone Editor                                                 │
└────────────────────────────────────────────────────────────────┘
```

---

## 7. Radar Disconnected

### 7.1 Detection

- No frames received for 3 seconds (configurable)
- M01 transitions to `RADAR_STATE_DISCONNECTED`

### 7.2 Behavior During Outage

| Module | Behavior |
|--------|----------|
| M01 Radar Ingest | No data, state = DISCONNECTED |
| M02 Tracking | All tracks retired after timeout |
| M03 Zone Engine | All zones report unoccupied |
| M04 Presence Smoothing | Reports no presence (after hold time) |
| M05 Native API | Reports radar_connected = false |
| M08 Watchdog | Radar removed from expected feeds |

### 7.3 Recovery

```c
void radar_on_frame_received(void) {
    if (radar_state == RADAR_STATE_DISCONNECTED) {
        radar_state = RADAR_STATE_CONNECTED;
        watchdog_set_radar_disconnected(false);
        log_event("radar_reconnected");

        // Clear stale track state
        tracking_reset();
    }
    last_frame_ms = timebase_uptime_ms();
}
```

### 7.4 Impact Summary

```
┌────────────────────────────────────────────────────────────────┐
│                    Radar Disconnected                          │
│                                                                │
│  Working:                    Not Working:                      │
│  ✓ Wi-Fi connectivity        ✗ Presence detection              │
│  ✓ Home Assistant connection ✗ Zone occupancy                  │
│  ✓ OTA updates               ✗ Target tracking                 │
│  ✓ Zone Editor (config only)                                   │
│  ✓ Telemetry (reports error)                                   │
│                                                                │
│  Status: Device is functional but cannot detect presence       │
└────────────────────────────────────────────────────────────────┘
```

### 7.5 Diagnostic Output

Device provides clear indication of radar status:

```
# M05 exposes sensor:
binary_sensor.rs1_radar_connected = OFF

# M09 logs:
[00003000][W][RADAR] Radar disconnected (no frames for 3000ms)

# Serial console:
RADAR: DISCONNECTED - check cable/power
```

---

## 8. NVS Corruption

### 8.1 Detection

- `nvs_flash_init()` returns `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND`
- CRC check failure on config read

### 8.2 Behavior

```c
esp_err_t config_store_init(void) {
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        log_event("nvs_corrupted", "erasing");

        // Erase and reinitialize
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();

        // Load factory defaults
        config_load_defaults();
        config_commit();

        log_event("nvs_recovered", "defaults_loaded");
    }

    return err;
}
```

### 8.3 Recovery

- Automatic: NVS erased and factory defaults loaded
- User must reconfigure zones via Zone Editor
- Device ID preserved (from eFuse, not NVS)

### 8.4 Impact Summary

```
┌────────────────────────────────────────────────────────────────┐
│                    NVS Corrupted (Recovered)                   │
│                                                                │
│  Working:                    Lost:                             │
│  ✓ Radar detection           ✗ Custom zone configuration       │
│  ✓ Basic operation           ✗ Wi-Fi credentials (may need     │
│  ✓ Device identity             reprovisioning)                 │
│                              ✗ API encryption key              │
│                                                                │
│  Action Required: Reconfigure device via Zone Editor           │
└────────────────────────────────────────────────────────────────┘
```

---

## 9. Flash Storage Full

### 9.1 Detection

- `esp_spiffs_info()` shows < 1KB free
- Write operations return `ESP_ERR_NO_MEM`

### 9.2 Behavior

| Component | Behavior |
|-----------|----------|
| Persistent logs | Oldest logs deleted (circular) |
| Config updates | Rejected with error |
| Crash logs | Overwrite oldest |

### 9.3 Recovery

- Automatic log rotation
- User can clear logs via diagnostic command
- OTA update can resize partitions

---

## 10. Boot Failure Recovery

### 10.1 Detection

- M08 tracks boot count in RTC memory
- 3 consecutive failed boots triggers safe mode

### 10.2 Safe Mode Behavior

```c
typedef struct {
    bool wifi_disabled;         // No network initialization
    bool minimal_logging;       // Serial only
    bool factory_config;        // Ignore stored config
    bool ota_only;              // Accept OTA via serial
} safe_mode_config_t;

void enter_safe_mode(void) {
    safe_mode_config_t safe = {
        .wifi_disabled = true,
        .minimal_logging = true,
        .factory_config = true,
        .ota_only = true
    };

    log_write(LOG_ERROR, "MAIN", "Entering safe mode after 3 failed boots");

    // Initialize only essential components
    uart_init();
    serial_ota_init();

    // Wait for recovery
    while (1) {
        serial_ota_poll();
        vTaskDelay(100);
    }
}
```

### 10.3 Recovery

- Serial console provides recovery options
- Manual OTA via serial (`esptool.py`)
- Factory reset command available

---

## 11. Degraded Mode Indicators

### 11.1 LED Patterns

| State | Pattern | Description |
|-------|---------|-------------|
| Normal | Solid on | All systems operational |
| Wi-Fi disconnected | Slow blink (1 Hz) | Network issue |
| Radar disconnected | Fast blink (4 Hz) | Sensor issue |
| Safe mode | Double blink | Recovery mode |
| OTA in progress | Breathing | Updating |

### 11.2 Home Assistant Sensors

| Sensor | Purpose |
|--------|---------|
| `binary_sensor.rs1_radar_connected` | Radar cable status |
| `binary_sensor.rs1_cloud_connected` | MQTT connection |
| `sensor.rs1_device_status` | Overall health text |

### 11.3 Telemetry Events

| Event | Payload |
|-------|---------|
| `wifi_disconnected` | `{"reason": "..."}` |
| `wifi_reconnected` | `{"downtime_sec": N}` |
| `radar_disconnected` | `{}` |
| `radar_reconnected` | `{}` |
| `nvs_corrupted` | `{"action": "erased"}` |
| `safe_mode_entered` | `{"boot_failures": 3}` |

---

## 12. Module-Specific Degraded Behavior

### 12.1 M05 Native API

**Wi-Fi Down:**
```c
void native_api_wifi_down_handler(void) {
    // Close any active connections
    api_close_all_connections();

    // Keep listener socket open for when Wi-Fi returns
    // (will fail but that's expected)

    // State updates continue internally but not published
    presence_state_update_internal_only = true;
}
```

### 12.2 M07 OTA

**MQTT Down:**
```c
void ota_mqtt_down_handler(void) {
    // Cannot receive triggers, but if update was in progress:
    if (ota_state == OTA_DOWNLOADING) {
        // Continue download (uses HTTPS, not MQTT)
        // Status updates queued for later
    }

    // Set flag to re-check for updates on reconnect
    ota_check_pending = true;
}
```

### 12.3 M09 Logging

**Cloud Unavailable:**
```c
#define TELEMETRY_QUEUE_MAX 100

void telemetry_cloud_down_handler(void) {
    // Queue telemetry locally
    if (telemetry_queue_count < TELEMETRY_QUEUE_MAX) {
        telemetry_queue_add(current_metrics);
    } else {
        // Drop oldest
        telemetry_queue_shift();
        telemetry_queue_add(current_metrics);
        telemetry_counter_inc("telemetry.dropped");
    }
}
```

### 12.4 M11 Zone Editor

**Cloud Down:**
```c
void zone_editor_cloud_down_handler(void) {
    // Local API continues to work
    // Cloud sync disabled
    cloud_sync_enabled = false;

    // Notify connected clients
    ws_broadcast_status("cloud_disconnected");
}
```

---

## 13. Testing Degraded Modes

See `testing/INTEGRATION_TESTS.md` for degraded mode test scenarios:

- 6.1 Radar Disconnect Recovery
- 6.2 Wi-Fi Disconnect Recovery
- 6.3 NVS Corruption Recovery

---

## 14. Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `radar_disconnect_timeout_ms` | uint32 | 3000 | Time before declaring radar disconnected |
| `wifi_reconnect_max_backoff_ms` | uint32 | 60000 | Max reconnect delay |
| `mqtt_reconnect_max_backoff_ms` | uint32 | 60000 | Max reconnect delay |
| `telemetry_queue_size` | uint16 | 100 | Max queued telemetry entries |
| `boot_failure_threshold` | uint8 | 3 | Failures before safe mode |

---

## 15. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-09 | OpticWorks | Initial draft. Addresses RFD-001 issues C13 and C16. |
