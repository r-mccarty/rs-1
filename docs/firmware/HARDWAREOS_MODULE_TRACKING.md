# HardwareOS Tracking Module Specification (M02)

Version: 0.2
Date: 2026-01-09
Owner: OpticWorks Firmware
Status: Draft

---

## 1. Purpose

Provide robust multi-target tracking under occlusion and multipath using a Kalman filter-based association, prediction, and smoothing pipeline. This module transforms raw detections from M01 into stable, persistent tracks that the Zone Engine (M03) can reliably evaluate for presence detection.

**This is the core intelligence of the presence detection system.** The quality of tracking directly affects false occupancy/vacancy rates.

---

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | LD2450 outputs at **33 Hz** (30ms frame interval) | All timing calculations, filter tuning |
| A2 | Maximum 3 targets per frame from LD2450 | Array sizing, association complexity |
| A3 | Coordinate range: X ±6000mm, Y 0-6000mm | Gate distance calculations |
| A4 | Typical human walking speed: 0.5-1.5 m/s | Gate distance, prediction tuning |
| A5 | Typical occlusions last < 2 seconds | Hold time, track retirement |
| A6 | ESP32-C3 single-core, 160 MHz | CPU budget constraints |
| A7 | Kalman filter is the mandated tracking algorithm | Implementation choice |

---

## 3. Data Structures

### 3.1 Track State

```c
typedef enum {
    TRACK_STATE_TENTATIVE,    // New track, awaiting confirmation
    TRACK_STATE_CONFIRMED,    // Confirmed track, actively reporting
    TRACK_STATE_OCCLUDED,     // Confirmed track, temporarily missing
    TRACK_STATE_RETIRED       // Track marked for removal
} track_state_t;

typedef struct {
    // Identity
    uint8_t track_id;                 // Unique ID (1-255, 0 = invalid)
    track_state_t state;              // Current lifecycle state

    // Position and velocity (Kalman state vector)
    int16_t x_mm;                     // Estimated X position
    int16_t y_mm;                     // Estimated Y position
    int16_t vx_mm_s;                  // Estimated X velocity (mm/s)
    int16_t vy_mm_s;                  // Estimated Y velocity (mm/s)

    // Kalman filter internals
    float P[4][4];                    // State covariance matrix
    float x_state[4];                 // State vector [x, y, vx, vy]

    // Confidence and timing
    uint8_t confidence;               // Track confidence (0-100)
    uint8_t consecutive_hits;         // Consecutive frames with match
    uint8_t consecutive_misses;       // Consecutive frames without match
    uint32_t first_seen_ms;           // Timestamp of track creation
    uint32_t last_seen_ms;            // Timestamp of last detection match

    // Association
    uint8_t last_detection_idx;       // Index of last matched detection
} track_t;

#define MAX_TRACKS 3

typedef struct {
    track_t tracks[MAX_TRACKS];       // Active tracks
    uint8_t active_count;             // Number of non-retired tracks
    uint8_t next_track_id;            // Next available track ID
    uint32_t frame_count;             // Total frames processed
} tracker_state_t;
```

### 3.2 Track Frame Output

```c
typedef struct {
    uint8_t track_id;                 // Track identifier
    int16_t x_mm;                     // Position X
    int16_t y_mm;                     // Position Y
    int16_t vx_mm_s;                  // Velocity X
    int16_t vy_mm_s;                  // Velocity Y
    uint8_t confidence;               // Confidence (0-100)
    track_state_t state;              // Current state
} track_output_t;

typedef struct {
    track_output_t tracks[MAX_TRACKS];
    uint8_t track_count;              // Confirmed + occluded tracks
    uint32_t timestamp_ms;            // Frame timestamp
    uint32_t frame_seq;               // Frame sequence number
} track_frame_t;
```

---

## 4. Track Lifecycle State Machine

```
                    TRACK LIFECYCLE STATE MACHINE
════════════════════════════════════════════════════════════════════════

                         ┌─────────────────────────────────┐
                         │         NEW DETECTION           │
                         │    (unmatched to any track)     │
                         └─────────────┬───────────────────┘
                                       │
                                       ▼
                         ┌─────────────────────────────────┐
                         │          TENTATIVE              │
                         │   consecutive_hits < CONFIRM_N  │
                         └─────────────┬───────────────────┘
                                       │
                    ┌──────────────────┼──────────────────┐
                    │                  │                  │
                    ▼                  ▼                  ▼
            ┌───────────┐      ┌───────────┐      ┌───────────┐
            │   MATCH   │      │   MISS    │      │ MISS × M  │
            │  hits++   │      │ misses++  │      │ threshold │
            └─────┬─────┘      └─────┬─────┘      └─────┬─────┘
                  │                  │                  │
                  │                  │                  ▼
                  │                  │          ┌───────────────┐
                  │                  │          │    RETIRED    │
                  │                  │          │ (remove track)│
                  │                  │          └───────────────┘
                  │                  │
                  ▼                  │
        ┌─────────────────┐         │
        │ hits >= CONFIRM │         │
        │       N         │         │
        └────────┬────────┘         │
                 │                  │
                 ▼                  │
    ┌────────────────────────┐      │
    │       CONFIRMED        │◀─────┘
    │  actively tracked      │
    │  reported to M03       │
    └────────────┬───────────┘
                 │
    ┌────────────┼────────────┐
    │            │            │
    ▼            ▼            ▼
┌───────┐  ┌──────────┐  ┌──────────┐
│ MATCH │  │   MISS   │  │ MISS × M │
│reset  │  │ misses++ │  │threshold │
│misses │  └────┬─────┘  └────┬─────┘
└───┬───┘       │             │
    │           ▼             ▼
    │    ┌─────────────┐  ┌───────────┐
    │    │  OCCLUDED   │  │  RETIRED  │
    │    │ (predicted) │  │  (final)  │
    │    └──────┬──────┘  └───────────┘
    │           │
    │           ▼
    │    ┌─────────────┐
    │    │ MATCH found │
    │    │ (recovered) │
    │    └──────┬──────┘
    │           │
    └───────────┴────────▶ CONFIRMED

════════════════════════════════════════════════════════════════════════
```

### 4.1 State Transitions

| From | Event | To | Condition |
|------|-------|-----|-----------|
| (new) | New detection | TENTATIVE | Unmatched detection spawns track |
| TENTATIVE | Match | TENTATIVE | consecutive_hits < CONFIRM_THRESHOLD |
| TENTATIVE | Match | CONFIRMED | consecutive_hits >= CONFIRM_THRESHOLD |
| TENTATIVE | Miss | TENTATIVE | consecutive_misses < TENTATIVE_DROP |
| TENTATIVE | Miss | RETIRED | consecutive_misses >= TENTATIVE_DROP |
| CONFIRMED | Match | CONFIRMED | Reset consecutive_misses to 0 |
| CONFIRMED | Miss | OCCLUDED | consecutive_misses >= 1 |
| OCCLUDED | Match | CONFIRMED | Detection recovered |
| OCCLUDED | Miss | OCCLUDED | consecutive_misses < OCCLUSION_TIMEOUT |
| OCCLUDED | Miss | RETIRED | consecutive_misses >= OCCLUSION_TIMEOUT |

### 4.2 Configuration Constants

| Constant | Value | Unit | Description |
|----------|-------|------|-------------|
| `CONFIRM_THRESHOLD` | 2 | frames | Hits to confirm tentative track |
| `TENTATIVE_DROP` | 3 | frames | Misses to drop tentative track |
| `OCCLUSION_TIMEOUT` | 66 | frames | ~2 seconds at 33 Hz |
| `GATE_DISTANCE` | 600 | mm | Max association distance |
| `MAX_TRACKS` | 3 | - | LD2450 hardware limit |

---

## 5. Kalman Filter Implementation

### 5.1 State Model

**State Vector:** `x = [x, y, vx, vy]ᵀ` (position and velocity in mm and mm/s)

**Process Model (Constant Velocity):**
```
x(k+1) = F · x(k) + w(k)

    ┌                    ┐
    │ 1  0  dt  0        │
F = │ 0  1  0   dt       │    where dt = 0.030s (30ms frame interval)
    │ 0  0  1   0        │
    │ 0  0  0   1        │
    └                    ┘
```

**Measurement Model:**
```
z(k) = H · x(k) + v(k)

    ┌          ┐
H = │ 1 0 0 0  │    (we only observe position, not velocity)
    │ 0 1 0 0  │
    └          ┘
```

### 5.2 Noise Parameters

| Parameter | Symbol | Value | Unit | Rationale |
|-----------|--------|-------|------|-----------|
| Process noise (position) | σ_pos | 50 | mm | Account for non-constant velocity |
| Process noise (velocity) | σ_vel | 200 | mm/s | Human acceleration variance |
| Measurement noise | σ_meas | 100 | mm | LD2450 position uncertainty |

**Process Noise Covariance Q:**
```
    ┌                          ┐
Q = │ σ_pos²  0      0     0   │
    │ 0       σ_pos² 0     0   │
    │ 0       0      σ_vel² 0  │
    │ 0       0      0   σ_vel²│
    └                          ┘
```

**Measurement Noise Covariance R:**
```
    ┌                  ┐
R = │ σ_meas²  0       │
    │ 0        σ_meas² │
    └                  ┘
```

### 5.3 Filter Algorithm

```c
// Constants
#define DT_SEC 0.030f  // 30ms frame interval (33 Hz)

// State transition matrix F
static const float F[4][4] = {
    {1, 0, DT_SEC, 0},
    {0, 1, 0, DT_SEC},
    {0, 0, 1, 0},
    {0, 0, 0, 1}
};

// Measurement matrix H
static const float H[2][4] = {
    {1, 0, 0, 0},
    {0, 1, 0, 0}
};

void kalman_predict(track_t *track) {
    // x_pred = F * x
    float x_pred[4];
    x_pred[0] = track->x_state[0] + DT_SEC * track->x_state[2];
    x_pred[1] = track->x_state[1] + DT_SEC * track->x_state[3];
    x_pred[2] = track->x_state[2];
    x_pred[3] = track->x_state[3];

    // P_pred = F * P * F^T + Q
    // (simplified: diagonal Q, symmetric P)
    float P_pred[4][4];
    matrix_mult_FPFt(track->P, F, P_pred);
    add_process_noise(P_pred, Q);

    // Update track state
    memcpy(track->x_state, x_pred, sizeof(x_pred));
    memcpy(track->P, P_pred, sizeof(P_pred));

    // Update position estimates
    track->x_mm = (int16_t)track->x_state[0];
    track->y_mm = (int16_t)track->x_state[1];
    track->vx_mm_s = (int16_t)(track->x_state[2] * 1000);  // m/s to mm/s
    track->vy_mm_s = (int16_t)(track->x_state[3] * 1000);
}

void kalman_update(track_t *track, int16_t z_x, int16_t z_y) {
    // Innovation: y = z - H * x_pred
    float y[2];
    y[0] = z_x - track->x_state[0];
    y[1] = z_y - track->x_state[1];

    // Innovation covariance: S = H * P * H^T + R
    float S[2][2];
    compute_innovation_cov(track->P, H, R, S);

    // Kalman gain: K = P * H^T * S^-1
    float K[4][2];
    compute_kalman_gain(track->P, H, S, K);

    // State update: x = x + K * y
    for (int i = 0; i < 4; i++) {
        track->x_state[i] += K[i][0] * y[0] + K[i][1] * y[1];
    }

    // Covariance update: P = (I - K * H) * P
    update_covariance(track->P, K, H);

    // Update position estimates
    track->x_mm = (int16_t)track->x_state[0];
    track->y_mm = (int16_t)track->x_state[1];
    track->vx_mm_s = (int16_t)(track->x_state[2] * 1000);
    track->vy_mm_s = (int16_t)(track->x_state[3] * 1000);
}
```

---

## 6. Association Algorithm

### 6.1 Gated Nearest-Neighbor

For each frame, associate detections to existing tracks:

```c
#define GATE_DISTANCE_MM 600  // Maximum association distance

void associate_detections(
    tracker_state_t *tracker,
    const detection_frame_t *detections,
    bool assigned_tracks[MAX_TRACKS],
    bool assigned_detections[3]
) {
    // Build cost matrix: distance between each track prediction and detection
    float cost[MAX_TRACKS][3];
    for (int t = 0; t < MAX_TRACKS; t++) {
        if (tracker->tracks[t].state == TRACK_STATE_RETIRED) {
            continue;
        }
        for (int d = 0; d < detections->target_count; d++) {
            if (!detections->targets[d].valid) continue;

            float dx = tracker->tracks[t].x_mm - detections->targets[d].x_mm;
            float dy = tracker->tracks[t].y_mm - detections->targets[d].y_mm;
            float dist = sqrtf(dx*dx + dy*dy);

            // Apply gate
            if (dist > GATE_DISTANCE_MM) {
                cost[t][d] = INFINITY;
            } else {
                cost[t][d] = dist;
            }
        }
    }

    // Greedy nearest-neighbor assignment
    // (Hungarian algorithm not needed for N <= 3)
    memset(assigned_tracks, 0, sizeof(bool) * MAX_TRACKS);
    memset(assigned_detections, 0, sizeof(bool) * 3);

    for (int iter = 0; iter < MAX_TRACKS; iter++) {
        float min_cost = INFINITY;
        int best_track = -1, best_det = -1;

        // Find minimum cost unassigned pair
        for (int t = 0; t < MAX_TRACKS; t++) {
            if (assigned_tracks[t]) continue;
            if (tracker->tracks[t].state == TRACK_STATE_RETIRED) continue;

            for (int d = 0; d < detections->target_count; d++) {
                if (assigned_detections[d]) continue;
                if (cost[t][d] < min_cost) {
                    min_cost = cost[t][d];
                    best_track = t;
                    best_det = d;
                }
            }
        }

        if (best_track < 0 || min_cost == INFINITY) break;

        assigned_tracks[best_track] = true;
        assigned_detections[best_det] = true;
        tracker->tracks[best_track].last_detection_idx = best_det;
    }
}
```

### 6.2 Gate Distance Scaling

The gate distance can be scaled based on velocity to handle fast-moving targets:

```c
float compute_gate_distance(const track_t *track) {
    float base_gate = GATE_DISTANCE_MM;

    // Scale gate with velocity magnitude
    float speed = sqrtf(track->vx_mm_s * track->vx_mm_s +
                        track->vy_mm_s * track->vy_mm_s);

    // Allow larger gate for faster targets
    // 1 m/s walking → +100mm gate
    float velocity_scale = speed / 1000.0f * 100.0f;

    return fminf(base_gate + velocity_scale, 1000.0f);  // Cap at 1m
}
```

---

## 7. Main Processing Pipeline

### 7.1 Per-Frame Processing

```c
void tracker_process_frame(
    tracker_state_t *tracker,
    const detection_frame_t *detections,
    track_frame_t *output
) {
    // 1. PREDICT: Advance all tracks using Kalman prediction
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (tracker->tracks[i].state != TRACK_STATE_RETIRED) {
            kalman_predict(&tracker->tracks[i]);
        }
    }

    // 2. ASSOCIATE: Match detections to predicted track positions
    bool assigned_tracks[MAX_TRACKS];
    bool assigned_detections[3];
    associate_detections(tracker, detections, assigned_tracks, assigned_detections);

    // 3. UPDATE: Apply Kalman update for matched tracks
    for (int t = 0; t < MAX_TRACKS; t++) {
        track_t *track = &tracker->tracks[t];
        if (track->state == TRACK_STATE_RETIRED) continue;

        if (assigned_tracks[t]) {
            // Track matched: update with detection
            uint8_t d = track->last_detection_idx;
            kalman_update(track, detections->targets[d].x_mm,
                                 detections->targets[d].y_mm);
            track_handle_match(track, detections->timestamp_ms);
        } else {
            // Track not matched: handle miss
            track_handle_miss(track);
        }
    }

    // 4. SPAWN: Create new tracks for unassigned detections
    for (int d = 0; d < detections->target_count; d++) {
        if (!assigned_detections[d] && detections->targets[d].valid) {
            spawn_track(tracker, &detections->targets[d], detections->timestamp_ms);
        }
    }

    // 5. CLEANUP: Remove retired tracks
    cleanup_retired_tracks(tracker);

    // 6. OUTPUT: Build output frame
    build_output_frame(tracker, detections->timestamp_ms, output);

    tracker->frame_count++;
}
```

### 7.2 Track Event Handlers

```c
void track_handle_match(track_t *track, uint32_t timestamp_ms) {
    track->consecutive_hits++;
    track->consecutive_misses = 0;
    track->last_seen_ms = timestamp_ms;

    // Update confidence (increase on match)
    track->confidence = MIN(100, track->confidence + 5);

    // State transitions
    switch (track->state) {
        case TRACK_STATE_TENTATIVE:
            if (track->consecutive_hits >= CONFIRM_THRESHOLD) {
                track->state = TRACK_STATE_CONFIRMED;
                ESP_LOGI(TAG, "Track %d confirmed", track->track_id);
            }
            break;
        case TRACK_STATE_OCCLUDED:
            track->state = TRACK_STATE_CONFIRMED;
            ESP_LOGI(TAG, "Track %d recovered from occlusion", track->track_id);
            break;
        case TRACK_STATE_CONFIRMED:
            // Stay confirmed
            break;
        default:
            break;
    }
}

void track_handle_miss(track_t *track) {
    track->consecutive_misses++;

    // Decay confidence (decrease on miss)
    track->confidence = MAX(0, track->confidence - 10);

    // State transitions
    switch (track->state) {
        case TRACK_STATE_TENTATIVE:
            if (track->consecutive_misses >= TENTATIVE_DROP) {
                track->state = TRACK_STATE_RETIRED;
                ESP_LOGD(TAG, "Tentative track %d retired", track->track_id);
            }
            break;
        case TRACK_STATE_CONFIRMED:
            track->state = TRACK_STATE_OCCLUDED;
            ESP_LOGD(TAG, "Track %d now occluded", track->track_id);
            break;
        case TRACK_STATE_OCCLUDED:
            if (track->consecutive_misses >= OCCLUSION_TIMEOUT) {
                track->state = TRACK_STATE_RETIRED;
                ESP_LOGI(TAG, "Track %d retired after occlusion timeout",
                         track->track_id);
            }
            break;
        default:
            break;
    }
}
```

---

## 8. Confidence Calculation

Track confidence reflects the reliability of the track estimate:

```c
uint8_t compute_confidence(const track_t *track) {
    uint8_t conf = 50;  // Base confidence

    // Bonus for consecutive hits
    conf += MIN(30, track->consecutive_hits * 5);

    // Penalty for consecutive misses
    conf -= MIN(40, track->consecutive_misses * 8);

    // Bonus for track age (stable tracks are more reliable)
    uint32_t age_sec = (track->last_seen_ms - track->first_seen_ms) / 1000;
    conf += MIN(20, age_sec * 2);

    return CLAMP(conf, 0, 100);
}
```

**Confidence interpretation:**

| Range | Meaning | M03/M04 Behavior |
|-------|---------|------------------|
| 80-100 | High confidence | Use for presence decisions |
| 50-79 | Medium confidence | Use with hold time |
| 20-49 | Low confidence | May be noise or leaving |
| 0-19 | Very low | About to retire |

---

## 9. Configuration Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `confirm_threshold` | uint8 | 2 | 1-5 | Frames to confirm tentative |
| `tentative_drop` | uint8 | 3 | 2-10 | Misses to drop tentative |
| `occlusion_timeout_frames` | uint8 | 66 | 33-99 | ~1-3 seconds at 33 Hz |
| `gate_distance_mm` | uint16 | 600 | 300-1000 | Association gate |
| `process_noise_pos` | uint16 | 50 | 10-200 | Kalman Q position (mm) |
| `process_noise_vel` | uint16 | 200 | 50-500 | Kalman Q velocity (mm/s) |
| `measurement_noise` | uint16 | 100 | 50-300 | Kalman R (mm) |

---

## 10. Performance Requirements

| Metric | Requirement | Rationale |
|--------|-------------|-----------|
| Processing latency | < 500 µs per frame | 33 Hz = 30 ms budget |
| Memory (static) | < 640 bytes | See MEMORY_BUDGET.md |
| CPU utilization | < 5% | Leave headroom for other modules |
| Track ID stability | > 95% | Minimize ID switches |

---

## 11. Telemetry (M09 Integration)

| Metric | Type | Description |
|--------|------|-------------|
| `track.active_count` | Gauge | Current confirmed + occluded tracks |
| `track.confirmations` | Counter | Tracks promoted to confirmed |
| `track.retirements` | Counter | Tracks retired |
| `track.id_switches` | Counter | Track ID changes (undesirable) |
| `track.occlusion_duration_ms` | Histogram | Occlusion durations |
| `track.processing_us` | Gauge | Per-frame processing time |

---

## 12. Testing Strategy

### 12.1 Unit Tests

- Kalman filter convergence on constant-velocity target
- Association with overlapping detections
- State machine transitions (all edges)
- Confidence calculation bounds

### 12.2 Integration Tests

- Track stability under occlusion (1-2 second gaps)
- Multi-target crossing scenarios
- ID persistence through temporary loss
- Throughput at 33 Hz with 3 targets

### 12.3 Dataset Tests

- Recorded LD2450 captures with ground truth
- Edge cases: targets entering/leaving, overlapping paths
- Noise rejection: stationary clutter, multipath

---

## 13. Dependencies

| Module | Purpose |
|--------|---------|
| M01 Radar Ingest | Provides detection frames |
| M03 Zone Engine | Consumes track frames |
| M08 Timebase | Frame timing, timestamps |
| M09 Logging | Telemetry, debug logs |

---

## 14. Open Questions

- Tune Kalman parameters from real-world datasets
- Evaluate need for IMM (Interacting Multiple Model) for varying motion patterns
- Consider track re-identification after long occlusions

---

## 15. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-XX | OpticWorks | Initial draft |
| 0.2 | 2026-01-09 | OpticWorks | Complete rewrite: added state machine, Kalman filter details, data structures, pseudocode. Addresses RFD-001 issue C2. Frame rate corrected to 33 Hz. |
