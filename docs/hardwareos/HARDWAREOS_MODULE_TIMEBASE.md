# HardwareOS Timebase / Scheduler Module Specification (M08)

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Provide stable timing services, frame synchronization, and task scheduling for the HardwareOS processing pipeline. This module ensures deterministic radar frame cadence, periodic housekeeping tasks, and watchdog integration for system reliability.

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | ESP-IDF FreeRTOS is the RTOS | Task primitives, timer APIs |
| A2 | LD2450 frame rate is ~33 Hz (30ms) | Frame timing expectations |
| A3 | Processing pipeline runs at 10 Hz (100ms throttled) | Scheduler tick rate |
| A4 | Single-core operation on ESP32-C3 | No SMP considerations |
| A5 | Watchdog timeout of 5 seconds is acceptable | Recovery time |
| A6 | NTP is available for wall-clock time (optional) | Timestamp accuracy |
| A7 | System uptime is tracked in milliseconds | Overflow at ~49 days |

## 3. Components

### 3.1 System Timer

High-resolution timer for frame timestamps and profiling.

```c
typedef struct {
    uint64_t boot_time_us;      // esp_timer since boot
    uint32_t uptime_ms;         // Derived, with overflow handling
    uint32_t unix_time;         // NTP-synced (0 if unavailable)
    bool ntp_synced;            // NTP sync status
} system_time_t;
```

### 3.2 Frame Timer

Radar frame cadence management.

```c
typedef struct {
    uint32_t expected_interval_ms;  // 30ms for LD2450
    uint32_t actual_interval_ms;    // Measured
    uint32_t jitter_ms;             // Max deviation
    uint32_t missed_frames;         // Frames not received in time
    uint32_t last_frame_ms;         // Last frame timestamp
} frame_timer_t;
```

### 3.3 Task Scheduler

Periodic task execution.

```c
typedef void (*task_callback_t)(void *arg);

typedef struct {
    const char *name;
    task_callback_t callback;
    void *arg;
    uint32_t interval_ms;
    uint32_t last_run_ms;
    uint32_t run_count;
    uint32_t max_duration_us;
    bool enabled;
} scheduled_task_t;

#define MAX_SCHEDULED_TASKS 16
```

### 3.4 Watchdog

System health monitoring.

```c
typedef struct {
    uint32_t timeout_ms;        // Watchdog timeout
    uint32_t last_feed_ms;      // Last watchdog feed
    uint8_t feed_sources;       // Bitmask of feeders
    bool triggered;             // Watchdog tripped
} watchdog_state_t;
```

## 4. API Interface

### 4.1 Time Functions

```c
// Get current system time
void timebase_get_time(system_time_t *time);

// Get uptime in milliseconds
uint32_t timebase_uptime_ms(void);

// Get monotonic timestamp (won't overflow in practical use)
uint64_t timebase_monotonic_us(void);

// Sync with NTP (non-blocking, result via callback)
void timebase_ntp_sync(void (*callback)(bool success));
```

### 4.2 Frame Timing

```c
// Notify frame received (called by M01)
void timebase_frame_received(uint32_t frame_seq);

// Get frame timing stats
void timebase_get_frame_stats(frame_timer_t *stats);

// Check if frame is late
bool timebase_frame_late(void);
```

### 4.3 Task Scheduling

```c
// Register a periodic task
esp_err_t scheduler_register(const char *name,
                             task_callback_t cb,
                             void *arg,
                             uint32_t interval_ms);

// Unregister a task
esp_err_t scheduler_unregister(const char *name);

// Enable/disable a task
esp_err_t scheduler_enable(const char *name, bool enabled);

// Get task stats
esp_err_t scheduler_get_stats(const char *name, scheduled_task_t *stats);
```

### 4.4 Watchdog

```c
// Initialize watchdog with timeout
void watchdog_init(uint32_t timeout_ms);

// Feed watchdog (must be called by registered sources)
void watchdog_feed(uint8_t source_id);

// Register a feed source
uint8_t watchdog_register_source(const char *name);

// Check watchdog status
bool watchdog_healthy(void);
```

## 5. Scheduled Tasks

### 5.1 Default Tasks

| Task | Interval | Purpose |
|------|----------|---------|
| `health_check` | 1000ms | Feed watchdog, check system health |
| `wifi_monitor` | 5000ms | Check Wi-Fi connectivity |
| `ntp_sync` | 3600000ms | Hourly NTP resync |
| `nvs_commit` | 60000ms | Commit pending NVS writes |
| `telemetry_flush` | 10000ms | Flush telemetry buffer (if enabled) |
| `memory_check` | 30000ms | Log heap stats |

### 5.2 Task Priority

Tasks are executed in registration order within a single scheduler tick. Long-running tasks should yield or be broken into smaller units.

| Priority | Tasks |
|----------|-------|
| Critical | `health_check`, `watchdog_feed` |
| Normal | `wifi_monitor`, `memory_check` |
| Low | `ntp_sync`, `telemetry_flush` |

## 6. Frame Synchronization

### 6.1 Frame Timing Monitor

```c
void timebase_frame_received(uint32_t frame_seq) {
    uint32_t now = timebase_uptime_ms();
    uint32_t interval = now - frame_timer.last_frame_ms;

    // Update jitter tracking
    int32_t deviation = interval - frame_timer.expected_interval_ms;
    if (abs(deviation) > frame_timer.jitter_ms) {
        frame_timer.jitter_ms = abs(deviation);
    }

    // Detect missed frames
    if (interval > frame_timer.expected_interval_ms * 2) {
        frame_timer.missed_frames += (interval / frame_timer.expected_interval_ms) - 1;
        log_warn("Missed %d radar frames", ...);
    }

    frame_timer.actual_interval_ms = interval;
    frame_timer.last_frame_ms = now;
}
```

### 6.2 Pipeline Throttling

The full pipeline (M01 → M02 → M03 → M04) runs at radar frame rate (~33 Hz), but state publishing to M05 is throttled:

```c
#define PUBLISH_INTERVAL_MS 100  // 10 Hz to Home Assistant

void pipeline_tick(void) {
    // Process at frame rate
    detection_frame_t detections;
    radar_ingest_process(&detections);
    tracking_process(&detections);
    zone_engine_process();
    presence_smoothing_process();

    // Throttle publishing
    static uint32_t last_publish = 0;
    if (timebase_uptime_ms() - last_publish >= PUBLISH_INTERVAL_MS) {
        native_api_publish_states();
        last_publish = timebase_uptime_ms();
    }
}
```

## 7. Watchdog Integration

### 7.1 Feed Sources

| Source ID | Component | Feed Interval |
|-----------|-----------|---------------|
| 0 | Main loop | Every tick |
| 1 | Radar ingest | Every frame |
| 2 | Wi-Fi task | Every 1s |

### 7.2 Watchdog Policy

```c
void health_check_task(void *arg) {
    // Check all sources have fed recently
    uint8_t expected = 0x07;  // Sources 0, 1, 2
    if ((watchdog_state.feed_sources & expected) != expected) {
        log_error("Watchdog: missing feeds from sources 0x%02X",
                  expected & ~watchdog_state.feed_sources);
    }

    // Feed system watchdog
    esp_task_wdt_reset();

    // Clear feed sources for next interval
    watchdog_state.feed_sources = 0;
}
```

### 7.3 Recovery Behavior

On watchdog timeout:
1. Log panic info to RTC memory.
2. System reset via hardware watchdog.
3. On boot, check RTC memory for panic info.
4. Increment boot counter, log to telemetry.

## 8. Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `frame_expected_ms` | uint16 | 30 | Expected radar frame interval |
| `publish_throttle_ms` | uint16 | 100 | State publish throttle |
| `watchdog_timeout_ms` | uint32 | 5000 | Watchdog timeout |
| `ntp_server` | string | "pool.ntp.org" | NTP server |
| `ntp_sync_interval_ms` | uint32 | 3600000 | NTP resync interval |

## 9. Performance Requirements

| Metric | Requirement | Rationale |
|--------|-------------|-----------|
| Timer resolution | 1ms | Adequate for presence detection |
| Scheduler overhead | < 100µs per tick | Minimal impact on pipeline |
| Jitter tolerance | ±10ms | Acceptable for 33 Hz input |
| Watchdog latency | < 100ms | Fast fault detection |

## 10. NTP Synchronization

### 10.1 Sync Flow

```
1. On Wi-Fi connect: trigger initial NTP sync
2. Query pool.ntp.org (UDP 123)
3. On success: set system time, mark synced
4. On failure: retry with backoff (1m, 5m, 15m)
5. Periodic resync every hour
```

### 10.2 Time Usage

| Use Case | Time Source |
|----------|-------------|
| Frame timestamps | Monotonic uptime (always available) |
| Config timestamps | Unix time (NTP, if synced) |
| Log timestamps | Unix time (fallback to uptime) |
| OTA cooldown | Monotonic uptime |

## 11. Telemetry (M09 Integration)

| Metric | Type | Description |
|--------|------|-------------|
| `timebase.uptime_sec` | Gauge | System uptime |
| `timebase.frame_jitter_ms` | Gauge | Max frame jitter |
| `timebase.missed_frames` | Counter | Total missed radar frames |
| `scheduler.tasks_run` | Counter | Scheduled task executions |
| `watchdog.resets` | Counter | Watchdog-triggered resets |

## 12. Testing Strategy

### 12.1 Unit Tests

- Timer accuracy with mocked esp_timer.
- Scheduler task registration and execution order.
- Watchdog feed tracking logic.

### 12.2 Integration Tests

- Frame timing with actual LD2450.
- NTP sync with network.
- Watchdog recovery after simulated hang.

### 12.3 Stress Tests

- Scheduler with 16 tasks at various intervals.
- Frame timing stability under CPU load.

## 13. Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| ESP-IDF esp_timer | 5.x | High-resolution timing |
| ESP-IDF esp_task_wdt | 5.x | Task watchdog |
| ESP-IDF SNTP | 5.x | NTP sync |
| FreeRTOS | 10.x | Task scheduling |

## 14. Open Questions

- Should scheduler support task priorities beyond execution order?
- Optimal watchdog timeout for OTA (may need longer)?
- Handle daylight saving time changes?
