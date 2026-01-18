# HardwareOS Radar Ingest Module Specification (M01)

Version: 0.2
Date: 2026-01-17
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Parse raw radar UART frames into normalized detection data and deliver timestamped detections to downstream modules. This module is the sole interface between radar hardware and the processing pipeline.

- **RS-1 Lite**: Parse LD2410 frames → binary presence output to M04
- **RS-1 Pro**: Parse LD2410 + LD2450 frames (time-division multiplexed) → fused detection output to M02

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | LD2450 outputs at ~33 Hz (30ms frame interval) | Frame timing, buffer sizing, downstream rate assumptions (Pro only) |
| A2 | LD2450 UART baud rate is 256000 | Serial configuration (Pro, UART 2) |
| A3 | Maximum 3 targets per frame (LD2450) | Buffer allocation, data structures (Pro only) |
| A4 | Coordinate range: X ±6000mm, Y 0–6000mm | Normalization bounds, zone engine constraints (Pro only) |
| A5 | Speed range: -128 to +127 cm/s | Velocity filtering thresholds (Pro only) |
| A6 | ESP32-WROOM-32E is the target MCU | Memory constraints, dual UART peripheral config |
| A7 | Product variant: Lite (LD2410) or Pro (LD2410+LD2450) | Different parsers, dual-radar for Pro |
| A8 | LD2410 UART baud rate is 256000 | Serial configuration (Lite UART 1, Pro UART 1) |
| A9 | LD2410 outputs at ~5 Hz (200ms frame interval) | Frame timing for presence updates |
| A10 | Pro uses time-division multiplexed dual-radar | M01 handles two UART streams concurrently |

## 3. Inputs

### 3.1 Hardware Interface

#### RS-1 Lite (Single Radar)
- **UART 1 RX**: LD2410 TX → ESP32-WROOM-32E GPIO (UART1 RX)
- **Baud Rate**: 256000
- **Frame Format**: LD2410 Engineering Mode frames (see Section 3.3)
- **Core Affinity**: Pinned to Core 1 for time-critical processing

#### RS-1 Pro (Dual Radar, Time-Division Multiplexed)
- **UART 1 RX**: LD2410 TX → ESP32-WROOM-32E GPIO (UART1 RX) @ 256000 baud
- **UART 2 RX**: LD2450 TX → ESP32-WROOM-32E GPIO (UART2 RX) @ 256000 baud
- **Frame Format**: Both protocols parsed concurrently
- **Core Affinity**: Pinned to Core 1 for time-critical processing

### 3.1.1 Variant Differences

| Aspect | RS-1 Lite | RS-1 Pro |
|--------|-----------|----------|
| **Radar(s)** | LD2410 only | LD2410 + LD2450 (dual-radar) |
| **UART Ports** | 1 (UART1) | 2 (UART1 + UART2) |
| **Frame Rates** | ~5 Hz (LD2410) | ~5 Hz (LD2410) + 33 Hz (LD2450) |
| **Output** | Binary presence + energy levels | Fused: coordinates + presence confidence |
| **Downstream** | M01 → M04 → M05 | M01 → M02 → M03 → M04 → M05 |

### 3.1.2 Dual-Radar Fusion Strategy (Pro Only)

In RS-1 Pro, both radars contribute complementary data:

| Radar | Contribution | Use |
|-------|--------------|-----|
| **LD2450** | X/Y coordinates, velocity for up to 3 targets | Primary tracking input to M02 |
| **LD2410** | Binary presence, moving/stationary energy levels | Confidence boost for M04 smoothing |

**Fusion Logic:**
- LD2450 provides primary detection frames at 33 Hz → M02 Tracking
- LD2410 presence state used as confidence input to M04 Smoothing
- If LD2450 reports 0 targets but LD2410 reports presence → extend hold time
- If both agree on no presence → faster transition to vacant state

### 3.2 Frame Structure (LD2450 Protocol - Pro Only)

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

### 3.3 Frame Structure (LD2410 Protocol - Lite and Pro)

The LD2410 operates in Engineering Mode to provide energy level data beyond simple binary presence.

#### 3.3.1 LD2410 Engineering Mode Frame

| Byte Offset | Field | Size | Description |
|-------------|-------|------|-------------|
| 0–3 | Header | 4 | Frame start marker `0xF4 0xF3 0xF2 0xF1` |
| 4–5 | Data Length | 2 | Payload length (little-endian) |
| 6 | Data Type | 1 | `0x01` = Engineering mode data |
| 7 | Head | 1 | `0xAA` frame head |
| 8 | Target State | 1 | `0x00`=No target, `0x01`=Moving, `0x02`=Stationary, `0x03`=Both |
| 9 | Moving Target Distance | 2 | Distance in cm (little-endian) |
| 11 | Moving Target Energy | 1 | 0-100 signal strength |
| 12 | Stationary Target Distance | 2 | Distance in cm (little-endian) |
| 14 | Stationary Target Energy | 1 | 0-100 signal strength |
| 15 | Detection Distance | 2 | Max detection distance in cm |
| 17–24 | Moving Energy Gates | 8 | Energy per gate (0-8) for moving targets |
| 25–32 | Stationary Energy Gates | 8 | Energy per gate (0-8) for stationary targets |
| 33 | Tail | 1 | `0x55` frame tail |
| 34 | Check | 1 | `0x00` |
| 35–38 | Footer | 4 | Frame end marker `0xF8 0xF7 0xF6 0xF5` |

Total frame size: 39 bytes (Engineering Mode).

#### 3.3.2 LD2410 Target States

| Value | State | Description |
|-------|-------|-------------|
| `0x00` | NO_TARGET | No presence detected |
| `0x01` | MOVING | Moving target only |
| `0x02` | STATIONARY | Stationary target only |
| `0x03` | MOVING_AND_STATIONARY | Both moving and stationary targets |

#### 3.3.3 LD2410 Configuration

Before use, LD2410 must be configured to Engineering Mode:

```
Command: Enable Engineering Mode
TX: FD FC FB FA 04 00 62 00 00 00 04 03 02 01
RX: FD FC FB FA 04 00 62 01 00 00 04 03 02 01
```

This is performed during M01 initialization.

## 4. Outputs

### 4.1 Detection Structures

#### 4.1.1 LD2450 Detection (Pro - Target Tracking)

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

#### 4.1.2 LD2410 Presence (Lite and Pro)

```c
typedef enum {
    LD2410_NO_TARGET = 0x00,
    LD2410_MOVING = 0x01,
    LD2410_STATIONARY = 0x02,
    LD2410_MOVING_AND_STATIONARY = 0x03
} ld2410_target_state_t;

typedef struct {
    ld2410_target_state_t state;    // Target state enum
    uint16_t moving_distance_cm;    // Moving target distance
    uint8_t moving_energy;          // Moving target energy (0-100)
    uint16_t stationary_distance_cm;// Stationary target distance
    uint8_t stationary_energy;      // Stationary target energy (0-100)
    uint8_t moving_gates[9];        // Per-gate energy (gates 0-8)
    uint8_t stationary_gates[9];    // Per-gate energy (gates 0-8)
    uint32_t timestamp_ms;          // Frame timestamp
    uint32_t frame_seq;             // Monotonic frame sequence
} presence_frame_t;
```

#### 4.1.3 Fused Output (Pro Only)

For RS-1 Pro, M01 outputs both structures:
- `detection_frame_t` from LD2450 → M02 Tracking (33 Hz)
- `presence_frame_t` from LD2410 → M04 Smoothing as confidence input (~5 Hz)

For RS-1 Lite, M01 outputs only:
- `presence_frame_t` from LD2410 → M04 Smoothing (~5 Hz)

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

### 7.1 UART Configuration

| Parameter | Type | Default (Lite) | Default (Pro) | Description |
|-----------|------|----------------|---------------|-------------|
| `uart1_baud` | uint32 | 256000 | 256000 | LD2410 UART baud rate |
| `uart1_rx_pin` | uint8 | GPIO_XX | GPIO_XX | LD2410 RX pin |
| `uart2_baud` | uint32 | N/A | 256000 | LD2450 UART baud rate (Pro only) |
| `uart2_rx_pin` | uint8 | N/A | GPIO_YY | LD2450 RX pin (Pro only) |

### 7.2 Filtering Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `min_range_mm` | uint16 | 100 | Minimum valid Y distance (LD2450) |
| `max_range_mm` | uint16 | 6000 | Maximum valid Y distance (LD2450) |
| `max_speed_cm_s` | uint16 | 500 | Maximum valid speed (LD2450) |
| `output_meters` | bool | false | Output in meters vs mm |
| `ld2410_min_energy` | uint8 | 10 | Minimum energy threshold for presence |

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
- Optimal fusion strategy: How much weight should LD2410 presence have vs LD2450 tracking?
- LD2410 gate energy thresholds: What energy levels indicate reliable presence?
- Dual-radar timing: Does LD2410 polling interfere with LD2450 frame timing on Pro?
