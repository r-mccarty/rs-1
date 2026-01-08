# HardwareOS Logging + Diagnostics Module Specification (M09)

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Provide comprehensive local logging, optional cloud telemetry, and diagnostic interfaces for debugging, monitoring, and troubleshooting RS-1 devices. This module supports development, field debugging, and opt-in usage analytics.

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | ESP-IDF logging (esp_log) is the base framework | Log format, levels |
| A2 | Log buffer size is ~8KB in RAM | Buffer overflow policy |
| A3 | UART is available for serial console | Debug output method |
| A4 | Cloud telemetry requires explicit user opt-in | Consent flow |
| A5 | Telemetry uses MQTT (same as OTA) | Protocol choice |
| A6 | No PII is collected in telemetry | Privacy compliance |
| A7 | Flash storage for persistent logs is optional | NVS/SPIFFS usage |

## 3. Components

### 3.1 Log Levels

| Level | Macro | Purpose |
|-------|-------|---------|
| Error | `LOG_E` | Critical failures requiring attention |
| Warning | `LOG_W` | Recoverable issues, degraded operation |
| Info | `LOG_I` | Normal operational events |
| Debug | `LOG_D` | Detailed debugging (development) |
| Verbose | `LOG_V` | Trace-level (rarely used) |

### 3.2 Log Targets

| Target | Description | Enabled By |
|--------|-------------|------------|
| Serial | UART console output | Always (default) |
| Buffer | RAM ring buffer | Always |
| Flash | Persistent storage | Config flag |
| Cloud | MQTT telemetry | User opt-in |

### 3.3 Telemetry

```c
typedef struct {
    char metric_name[32];
    metric_type_t type;         // COUNTER, GAUGE, HISTOGRAM
    union {
        uint32_t counter;
        float gauge;
        struct {
            float sum;
            uint32_t count;
            float buckets[8];
        } histogram;
    } value;
    uint32_t timestamp_ms;
} telemetry_metric_t;
```

## 4. API Interface

### 4.1 Logging

```c
// Log macros (module-tagged)
#define LOG_E(tag, fmt, ...) log_write(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) log_write(LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) log_write(LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) log_write(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_V(tag, fmt, ...) log_write(LOG_LEVEL_VERBOSE, tag, fmt, ##__VA_ARGS__)

// Core log function
void log_write(log_level_t level, const char *tag, const char *fmt, ...);

// Set log level per tag
void log_set_level(const char *tag, log_level_t level);

// Flush logs to flash (if enabled)
void log_flush(void);
```

### 4.2 Log Buffer Access

```c
// Get recent log entries
typedef struct {
    uint32_t timestamp_ms;
    log_level_t level;
    char tag[16];
    char message[128];
} log_entry_t;

// Read from ring buffer (returns count)
int log_read_recent(log_entry_t *entries, int max_count);

// Clear log buffer
void log_clear(void);
```

### 4.3 Telemetry

```c
// Increment counter
void telemetry_counter_inc(const char *name);
void telemetry_counter_add(const char *name, uint32_t value);

// Set gauge value
void telemetry_gauge_set(const char *name, float value);

// Record histogram sample
void telemetry_histogram_observe(const char *name, float value);

// Enable/disable telemetry
void telemetry_enable(bool enabled);

// Force flush telemetry to cloud
void telemetry_flush(void);
```

### 4.4 Diagnostics

```c
// System diagnostics
typedef struct {
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t uptime_ms;
    int8_t wifi_rssi;
    uint32_t radar_frames_total;
    uint32_t radar_frames_dropped;
    uint8_t active_tracks;
    uint8_t zones_occupied;
    float cpu_usage_percent;
    uint32_t watchdog_resets;
} system_diagnostics_t;

void diagnostics_get(system_diagnostics_t *diag);

// Dump diagnostics to log
void diagnostics_dump(void);
```

## 5. Log Format

### 5.1 Serial Output

```
[timestamp_ms][LEVEL][TAG] Message
[00012345][I][RADAR] Frame received, 3 targets
[00012346][D][TRACK] Track 1 updated: x=1234 y=2345 conf=87
[00012400][W][ZONE] Zone "kitchen" occupancy flicker detected
```

### 5.2 Buffer Format (Binary)

```c
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint8_t level;
    uint8_t tag_len;
    uint8_t msg_len;
    // followed by: tag[tag_len], message[msg_len]
} log_buffer_entry_t;
```

### 5.3 Cloud Format (JSON)

```json
{
    "device_id": "rs1-aabbccdd",
    "timestamp": 1704912345,
    "logs": [
        {"ts": 12345, "lvl": "W", "tag": "ZONE", "msg": "Flicker detected"}
    ],
    "metrics": {
        "radar.frames_received": 12345,
        "zone.occupancy_changes": 42,
        "system.free_heap": 45678
    }
}
```

## 6. Telemetry Metrics

### 6.1 System Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `system.uptime_sec` | Gauge | Device uptime |
| `system.free_heap` | Gauge | Free heap bytes |
| `system.wifi_rssi` | Gauge | Wi-Fi signal strength |
| `system.cpu_usage` | Gauge | CPU utilization % |
| `system.boot_count` | Counter | Total boots |
| `system.watchdog_resets` | Counter | Watchdog resets |

### 6.2 Radar Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `radar.frames_received` | Counter | Total frames parsed |
| `radar.frames_invalid` | Counter | Parse failures |
| `radar.frame_rate_hz` | Gauge | Actual frame rate |
| `radar.targets_detected` | Histogram | Targets per frame distribution |

### 6.3 Tracking Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `tracking.active_tracks` | Gauge | Current track count |
| `tracking.id_switches` | Counter | Track ID reassignments |
| `tracking.occlusion_events` | Counter | Tracks entering hold |
| `tracking.occlusion_duration_ms` | Histogram | Hold time distribution |

### 6.4 Zone Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `zone.occupancy_changes` | Counter | Total transitions |
| `zone.false_vacant_events` | Counter | Quick re-occupancy (flicker) |
| `zone.processing_us` | Histogram | Per-frame processing time |

### 6.5 API Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `api.connections` | Counter | HA connections |
| `api.messages_sent` | Counter | State updates sent |
| `api.auth_failures` | Counter | Authentication failures |

## 7. Log Levels by Module

| Module | Default Level | Production Level |
|--------|---------------|------------------|
| MAIN | Info | Info |
| RADAR | Info | Warning |
| TRACK | Debug | Warning |
| ZONE | Debug | Warning |
| SMOOTH | Debug | Warning |
| API | Info | Warning |
| CONFIG | Info | Info |
| OTA | Info | Info |
| WIFI | Info | Warning |

## 8. Flash Logging

### 8.1 Storage Layout

- Partition: `logs` (SPIFFS or LittleFS)
- Size: 64KB (configurable)
- Rotation: Oldest logs deleted when full

### 8.2 Log Files

```
/logs/current.log    # Active log file
/logs/boot_001.log   # Previous boot log
/logs/crash.log      # Last panic/crash info
```

### 8.3 Crash Logging

On panic/reset:
1. Save last 1KB of log buffer to RTC memory.
2. On next boot, write RTC buffer to `/logs/crash.log`.
3. Include stack trace if available.

## 9. Cloud Telemetry

### 9.1 Consent and Privacy

```c
typedef struct {
    bool telemetry_enabled;     // User opt-in
    bool include_logs;          // Include error logs (not just metrics)
    uint32_t upload_interval_ms;// Batch interval
} telemetry_config_t;
```

### 9.2 Data NOT Collected

| Data | Reason |
|------|--------|
| Zone names | User-defined, potentially PII |
| Wi-Fi SSID | Network identity |
| Location | Privacy |
| Target positions | Not useful, privacy |

### 9.3 MQTT Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `opticworks/{device_id}/telemetry` | Device→Cloud | Metrics and logs |
| `opticworks/{device_id}/diag/request` | Cloud→Device | Request diagnostics |
| `opticworks/{device_id}/diag/response` | Device→Cloud | Diagnostics response |

## 10. Diagnostic Endpoints

### 10.1 Local HTTP (Debug Build)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/logs` | GET | Recent log entries |
| `/api/metrics` | GET | Current metrics |
| `/api/diag` | GET | System diagnostics |
| `/api/config` | GET | Current configuration |

### 10.2 Serial Commands

| Command | Description |
|---------|-------------|
| `log level <tag> <level>` | Set log level |
| `diag` | Dump diagnostics |
| `heap` | Show heap stats |
| `tasks` | List FreeRTOS tasks |
| `reset` | Soft reset |

## 11. Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `log_level_default` | uint8 | INFO | Global log level |
| `log_buffer_size` | uint16 | 8192 | RAM buffer size |
| `log_to_flash` | bool | false | Enable flash logging |
| `flash_log_size` | uint32 | 65536 | Flash partition size |
| `telemetry_enabled` | bool | false | Cloud telemetry opt-in |
| `telemetry_interval_ms` | uint32 | 60000 | Upload interval |

## 12. Performance Requirements

| Metric | Requirement | Rationale |
|--------|-------------|-----------|
| Log write latency | < 100µs | Non-blocking in hot path |
| Buffer memory | 8KB | Reasonable for ESP32-C3 |
| Telemetry upload | < 1KB/min | Minimal bandwidth |

## 13. Testing Strategy

### 13.1 Unit Tests

- Log format parsing.
- Ring buffer overflow behavior.
- Telemetry metric aggregation.

### 13.2 Integration Tests

- Serial output verification.
- Flash log persistence across reboot.
- Telemetry MQTT delivery.

### 13.3 Privacy Tests

- Verify no PII in telemetry payload.
- Verify telemetry disabled by default.

## 14. Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| ESP-IDF esp_log | 5.x | Base logging |
| SPIFFS/LittleFS | 5.x | Flash storage |
| cJSON | 1.x | JSON formatting |

## 15. Open Questions

- Remote log level adjustment via cloud?
- Log export via USB for support cases?
- Anomaly detection in telemetry (device-side)?
