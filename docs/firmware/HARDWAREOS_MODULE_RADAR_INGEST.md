# HardwareOS Radar Ingest Module Specification (M01)

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Parse raw LD2450 UART frames into normalized detection data and deliver timestamped detections to the Tracking module (M02). This module is the sole interface between hardware and the processing pipeline.

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | LD2450 outputs at ~33 Hz (30ms frame interval) | Frame timing, buffer sizing, downstream rate assumptions |
| A2 | UART baud rate is 256000 | Serial configuration, parse timing |
| A3 | Maximum 3 targets per frame | Buffer allocation, data structures |
| A4 | Coordinate range: X ±6000mm, Y 0–6000mm | Normalization bounds, zone engine constraints |
| A5 | Speed range: -128 to +127 cm/s | Velocity filtering thresholds |
| A6 | ESP32-C3-MINI-1 is the target MCU | Memory constraints, UART peripheral config |
| A7 | Single LD2450 sensor per device | No multi-sensor fusion in this module |

## 3. Inputs

### 3.1 Hardware Interface

- **UART RX**: LD2450 TX pin connected to ESP32-C3 UART RX.
- **Baud Rate**: 256000.
- **Frame Format**: Binary frames per LD2450 protocol specification.

### 3.2 Frame Structure (LD2450 Protocol)

| Byte Offset | Field | Size | Description |
|-------------|-------|------|-------------|
| 0–3 | Header | 4 | Frame start marker `0xAA 0xFF 0x03 0x00` |
| 4–5 | Target 1 X | 2 | Signed int16, mm |
| 6–7 | Target 1 Y | 2 | Signed int16, mm |
| 8–9 | Target 1 Speed | 2 | Signed int16, cm/s |
| 10–11 | Target 1 Resolution | 2 | uint16, mm |
| 12–23 | Target 2 | 12 | Same structure |
| 24–35 | Target 3 | 12 | Same structure |
| 36–37 | Checksum | 2 | Frame validation |
| 38–39 | Footer | 2 | Frame end marker `0x55 0xCC` |

Total frame size: 40 bytes.

## 4. Outputs

### 4.1 Detection Structure

```c
typedef struct {
    int16_t x_mm;           // X position in mm (-6000 to +6000)
    int16_t y_mm;           // Y position in mm (0 to 6000)
    int16_t speed_cm_s;     // Speed in cm/s (negative = approaching)
    uint16_t resolution_mm; // Distance resolution
    uint8_t signal_quality; // Derived quality metric (0-100)
    bool valid;             // Target present in this slot
} detection_t;

typedef struct {
    detection_t targets[3]; // Up to 3 targets
    uint8_t target_count;   // Number of valid targets (0-3)
    uint32_t timestamp_ms;  // Frame timestamp (system tick)
    uint32_t frame_seq;     // Monotonic frame sequence number
} detection_frame_t;
```

### 4.2 Coordinate Normalization

- Raw LD2450 coordinates are preserved in mm.
- Origin (0,0) is at the sensor.
- X-axis: horizontal, positive right.
- Y-axis: depth, positive away from sensor.
- Optional: Convert to meters for downstream modules (configurable).

## 5. Processing Pipeline

### 5.1 Frame Reception

1. UART ISR buffers incoming bytes.
2. Ring buffer holds up to 2 complete frames (80 bytes minimum).
3. Frame boundary detection via header/footer markers.

### 5.2 Frame Parsing

1. Validate header and footer markers.
2. Verify checksum (if non-zero per LD2450 spec).
3. Extract 3 target slots.
4. Mark targets as valid based on presence indicators.

### 5.3 Filtering (Pre-Tracking)

| Filter | Purpose | Default |
|--------|---------|---------|
| Range gate | Reject targets outside max range | Y > 6000mm |
| Minimum distance | Reject near-field noise | Y < 100mm |
| Speed sanity | Reject physically impossible speeds | \|speed\| > 500 cm/s |

### 5.4 Timestamping

- Attach `esp_timer_get_time() / 1000` as timestamp_ms.
- Increment frame_seq for each valid frame.

### 5.5 Delivery

- Push `detection_frame_t` to M02 Tracking via function call or queue.
- Non-blocking: if queue full, drop oldest frame and log warning.

## 6. Error Handling

| Error | Detection | Action |
|-------|-----------|--------|
| Invalid header/footer | Marker mismatch | Discard frame, resync |
| Checksum failure | Computed != received | Discard frame, increment error counter |
| Frame timeout | No frame in 100ms | Log warning, notify M09 |
| Buffer overflow | Ring buffer full | Discard oldest bytes, log |
| Radar disconnect | No frames for 3s | Enter DISCONNECTED state, notify M08 |

### 6.1 Radar Disconnect Handling

**Critical:** If the radar stops sending frames (hardware failure, cable disconnect), the system must handle gracefully without entering a watchdog reset loop.

**State Machine:**
```
    CONNECTED ──(no frame 3s)──▶ DISCONNECTED
        ▲                              │
        │                              │
        └────(frame received)──────────┘
```

**Implementation:**
```c
typedef enum {
    RADAR_STATE_DISCONNECTED,
    RADAR_STATE_CONNECTED
} radar_state_t;

static radar_state_t radar_state = RADAR_STATE_DISCONNECTED;
static uint32_t last_frame_ms = 0;

#define RADAR_DISCONNECT_TIMEOUT_MS 3000

void radar_check_connection(void) {
    uint32_t now = timebase_uptime_ms();

    if (radar_state == RADAR_STATE_CONNECTED) {
        if (now - last_frame_ms > RADAR_DISCONNECT_TIMEOUT_MS) {
            radar_state = RADAR_STATE_DISCONNECTED;
            ESP_LOGW(TAG, "Radar disconnected (no frames for %dms)",
                     RADAR_DISCONNECT_TIMEOUT_MS);
            // Notify watchdog that radar silence is expected
            watchdog_set_radar_disconnected(true);
            // Notify telemetry
            log_event("radar_disconnected");
        }
    }
}

void radar_on_frame_received(void) {
    last_frame_ms = timebase_uptime_ms();

    if (radar_state == RADAR_STATE_DISCONNECTED) {
        radar_state = RADAR_STATE_CONNECTED;
        ESP_LOGI(TAG, "Radar reconnected");
        watchdog_set_radar_disconnected(false);
        log_event("radar_reconnected");
    }
}

// Public API for M08 watchdog
bool radar_is_connected(void) {
    return radar_state == RADAR_STATE_CONNECTED;
}
```

**M08 Watchdog Integration:**
- When radar is disconnected, M08 should NOT expect radar frame feeds
- System continues operating in degraded mode (no presence detection)
- Watchdog accepts DISCONNECTED as a valid state

## 7. Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `uart_baud` | uint32 | 256000 | UART baud rate |
| `uart_rx_pin` | uint8 | GPIO_XX | RX pin assignment |
| `min_range_mm` | uint16 | 100 | Minimum valid Y distance |
| `max_range_mm` | uint16 | 6000 | Maximum valid Y distance |
| `max_speed_cm_s` | uint16 | 500 | Maximum valid speed |
| `output_meters` | bool | false | Output in meters vs mm |

## 8. Performance Requirements

| Metric | Requirement | Rationale |
|--------|-------------|-----------|
| Parse latency | < 1ms per frame | Must not block 33 Hz input |
| Memory (static) | < 512 bytes | Ring buffer + frame structs |
| CPU utilization | < 2% | Leave headroom for tracking |

## 9. Telemetry (M09 Integration)

| Metric | Type | Description |
|--------|------|-------------|
| `radar.frames_received` | Counter | Total frames parsed |
| `radar.frames_invalid` | Counter | Checksum/format failures |
| `radar.targets_per_frame` | Gauge | Rolling average target count |
| `radar.frame_interval_ms` | Gauge | Actual frame timing |

## 10. Testing Strategy

### 10.1 Unit Tests

- Parse known-good frame bytes → verify detection values.
- Parse malformed frames → verify graceful rejection.
- Verify timestamp monotonicity across frames.

### 10.2 Integration Tests

- Feed recorded LD2450 captures → verify pipeline throughput.
- Stress test with continuous frames at 33 Hz.

### 10.3 Hardware Tests

- Verify frame reception with actual LD2450 module.
- Measure real frame rate and jitter.

## 11. Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| ESP-IDF UART driver | 5.x | Hardware UART |
| esp_timer | 5.x | Timestamping |

## 12. Open Questions

- Confirm LD2450 checksum algorithm (sum vs CRC).
- Determine if resolution field is useful for tracking confidence.
- Validate actual frame rate under various target conditions.
