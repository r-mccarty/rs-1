# HardwareOS Presence Smoothing Module Specification (M04)

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Apply hysteresis, hold timers, and confidence-based smoothing to raw zone occupancy from M03 Zone Engine. The goal is stable, flicker-free presence output suitable for Home Assistant automations, reducing false vacancy during brief occlusions.

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | Frame rate from M03 is ~10 Hz (after throttling) | Timer granularity, hold time precision |
| A2 | Occlusions typically last < 2 seconds | Default hold time selection |
| A3 | Users prefer false occupancy over false vacancy | Bias toward longer holds |
| A4 | Sensitivity is exposed as a single slider (0-100) | Parameter mapping complexity |
| A5 | Per-zone sensitivity is supported | Storage per zone |
| A6 | M02 Tracking provides confidence scores | Confidence-weighted smoothing |
| A7 | Home Assistant polls state, not events | State-based output design |

## 3. Inputs

### 3.1 From M03 Zone Engine

```c
typedef struct {
    char zone_id[16];
    bool raw_occupied;       // Instantaneous occupancy
    uint8_t target_count;    // Current track count in zone
    uint8_t avg_confidence;  // Average track confidence (0-100)
    bool has_moving;         // Any moving track in zone
    uint32_t timestamp_ms;
} zone_raw_state_t;
```

### 3.2 From M06 Config Store

```c
typedef struct {
    char zone_id[16];
    uint8_t sensitivity;        // 0 (max hold) to 100 (instant response)
    uint16_t hold_time_ms;      // Derived from sensitivity, or override
    uint16_t enter_delay_ms;    // Delay before confirming occupancy
} zone_smoothing_config_t;
```

## 4. Outputs

### 4.1 Smoothed Zone State

```c
typedef struct {
    char zone_id[16];
    bool occupied;              // Smoothed occupancy (stable)
    bool raw_occupied;          // Instantaneous (for debugging)
    uint8_t target_count;       // Current count
    uint32_t occupied_since_ms; // When occupancy started (0 if vacant)
    uint32_t vacant_since_ms;   // When vacancy started (0 if occupied)
    smoothing_state_t state;    // Internal state machine state
} zone_smoothed_state_t;
```

### 4.2 Published to M05 Native API

- `binary_sensor.<zone_id>_occupancy`: Smoothed occupancy.
- `sensor.<zone_id>_target_count`: Current target count.
- Optional: `binary_sensor.<zone_id>_raw`: Unsmoothed (debug only).

## 5. Smoothing Algorithm

### 5.1 State Machine

```
           ┌─────────────────────────────────────────┐
           │                                         │
           ▼                                         │
    ┌──────────────┐     raw=true      ┌─────────────────┐
    │              │ ───────────────▶  │                 │
    │   VACANT     │                   │  ENTERING       │
    │              │ ◀───────────────  │  (delay timer)  │
    └──────────────┘    timer expired  └─────────────────┘
           │            (no confirm)           │
           │                                   │ timer expired
           │                                   │ (confirmed)
           │                                   ▼
           │                          ┌─────────────────┐
           │          raw=false       │                 │
           │ ◀──────────────────────  │   OCCUPIED      │
           │     hold timer expired   │                 │
           │                          └─────────────────┘
           │                                   │
           │                                   │ raw=false
           │                                   ▼
           │                          ┌─────────────────┐
           │          raw=true        │                 │
           │                          │   HOLDING       │
           └──────────────────────────│  (hold timer)   │
                     (cancel hold)    └─────────────────┘
```

### 5.2 State Definitions

| State | Output | Description |
|-------|--------|-------------|
| `VACANT` | `occupied=false` | No presence, idle |
| `ENTERING` | `occupied=false` | Raw present, waiting for enter delay |
| `OCCUPIED` | `occupied=true` | Confirmed presence |
| `HOLDING` | `occupied=true` | Raw absent, holding for occlusion bridge |

### 5.3 Transitions

| From | To | Trigger |
|------|----|---------|
| VACANT | ENTERING | `raw_occupied=true` |
| ENTERING | VACANT | `raw_occupied=false` before enter delay |
| ENTERING | OCCUPIED | Enter delay timer expires with `raw_occupied=true` |
| OCCUPIED | HOLDING | `raw_occupied=false` |
| HOLDING | OCCUPIED | `raw_occupied=true` (cancel hold) |
| HOLDING | VACANT | Hold timer expires with `raw_occupied=false` |

## 6. Sensitivity Mapping

Sensitivity (0-100) maps to hold/delay parameters:

| Sensitivity | Hold Time | Enter Delay | Use Case |
|-------------|-----------|-------------|----------|
| 0 (Min) | 5000ms | 500ms | Maximum stability, slow response |
| 25 | 3000ms | 300ms | Balanced, low flicker |
| 50 (Default) | 1500ms | 200ms | Balanced, moderate response |
| 75 | 500ms | 100ms | Fast response, some flicker |
| 100 (Max) | 0ms | 0ms | Instant, no smoothing |

### 6.1 Mapping Formula

```c
uint16_t hold_time_ms = (100 - sensitivity) * 50;   // 0-5000ms
uint16_t enter_delay_ms = (100 - sensitivity) * 5;  // 0-500ms
```

## 7. Confidence-Weighted Hold

When M02 provides track confidence, extend hold time for high-confidence tracks:

```c
uint16_t effective_hold_ms = base_hold_ms;
if (last_avg_confidence > 80) {
    effective_hold_ms = base_hold_ms * 1.5;  // 50% longer hold
} else if (last_avg_confidence < 30) {
    effective_hold_ms = base_hold_ms * 0.5;  // Shorter hold
}
```

Rationale: A track that was being confidently tracked is more likely to reappear after brief occlusion.

## 8. Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `default_sensitivity` | uint8 | 50 | Global sensitivity (0-100) |
| `min_hold_ms` | uint16 | 100 | Minimum hold even at sensitivity=100 |
| `max_hold_ms` | uint16 | 10000 | Maximum configurable hold |
| `use_confidence_weighting` | bool | true | Enable confidence-weighted hold |
| `confidence_boost_threshold` | uint8 | 80 | Confidence for extended hold |

## 9. Performance Requirements

| Metric | Requirement | Rationale |
|--------|-------------|-----------|
| State update latency | < 1ms per zone | Simple state machine |
| Timer precision | ±50ms | Human-perceptible threshold |
| Memory per zone | ~32 bytes | State + timers |

## 10. Edge Cases

### 10.1 Rapid Flicker

If raw occupancy toggles rapidly (>5 times/second):

- Enter delay prevents false OCCUPIED.
- Hold timer prevents false VACANT.
- Hysteresis resets on each raw=true during HOLDING.

### 10.2 Very Long Absence

If holding exceeds `max_hold_ms`:

- Force transition to VACANT.
- Log extended hold event for diagnostics.

### 10.3 Zone Config Change

On zone sensitivity update:

- Apply new parameters immediately.
- Do not reset current state (avoid artificial transitions).

## 11. Telemetry (M09 Integration)

| Metric | Type | Description |
|--------|------|-------------|
| `smoothing.state_changes` | Counter | Total state transitions |
| `smoothing.hold_extensions` | Counter | Times hold was extended by confidence |
| `smoothing.false_occupancy_prevented` | Counter | Enter delay prevented flicker |
| `smoothing.current_hold_ms` | Gauge | Active hold timer value |

## 12. Testing Strategy

### 12.1 Unit Tests

- State machine transitions for all paths.
- Timer behavior with mocked time.
- Sensitivity-to-parameter mapping.

### 12.2 Simulation Tests

- Replay recorded occlusion patterns → verify flicker reduction.
- Compare raw vs smoothed output over time.

### 12.3 User Acceptance

- Define acceptable false vacancy rate (< 5% during normal activity).
- Define acceptable lag on vacancy detection (< 3 seconds).

## 13. Dependencies

| Module | Purpose |
|--------|---------|
| M03 Zone Engine | Provides raw zone occupancy |
| M05 Native API | Publishes smoothed state to HA |
| M06 Device Config Store | Provides sensitivity settings |
| M08 Timebase | Timer scheduling |

## 14. Open Questions

- Should sensitivity be per-zone or global only for MVP?
- Optimal default hold time based on real-world occlusion data.
- Should "moving" vs "stationary" targets have different hold times?
