# HardwareOS Glossary

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks Firmware
Status: Draft

---

## Purpose

This glossary defines canonical meanings for terms used across HardwareOS modules. When a term appears in any module specification, its meaning MUST match the definition in this document.

**This document addresses RFD-001 issue C6: Sensitivity has three different meanings.**

---

## Terms

### Confidence

**Definition:** A numeric score (0-100) indicating the reliability of a track estimate.

**Produced by:** M02 Tracking
**Consumed by:** M03 Zone Engine, M04 Presence Smoothing

| Range | Meaning | Usage |
|-------|---------|-------|
| 80-100 | High confidence | Track is well-established, recently seen |
| 50-79 | Medium confidence | Track is valid but may be occluded |
| 20-49 | Low confidence | Track is stale or uncertain |
| 0-19 | Very low | Track about to be retired |

**Calculation:** Based on consecutive hits/misses, track age, and filter covariance.

---

### Detection

**Definition:** A single target measurement from the LD2450 radar sensor in one frame.

**Produced by:** LD2450 hardware
**Parsed by:** M01 Radar Ingest

**Structure:**
- Position (x_mm, y_mm)
- Speed (cm/s)
- Resolution (mm)
- Valid flag

**Note:** A detection is NOT the same as a track. Multiple detections may correspond to one track, or a detection may be noise.

---

### Frame

**Definition:** One complete data packet from the LD2450 sensor, containing up to 3 target detections.

**Rate:** 33 Hz (30ms intervals)
**Size:** 40 bytes

---

### Hold Time

**Definition:** The duration (in milliseconds) that a zone maintains "occupied" status after all tracks leave, to bridge brief occlusions.

**Configured by:** Sensitivity setting
**Range:** 0-5000ms
**Default:** 1500ms (at sensitivity=50)

**Formula:** `hold_time_ms = (100 - sensitivity) * 50`

---

### Occlusion

**Definition:** A temporary loss of radar detection for a target that is still physically present.

**Causes:**
- Person turning sideways (reduced radar cross-section)
- Furniture or obstacles blocking line-of-sight
- Multipath interference

**Handling:** M02 Tracking predicts through occlusion; M04 Smoothing holds occupancy state.

**Typical duration:** < 2 seconds

---

### Occupancy

**Definition:** The state of a zone having one or more human targets present.

**Types:**
- **Raw occupancy:** Instantaneous (from M03 Zone Engine)
- **Smoothed occupancy:** Stable, with hysteresis (from M04 Presence Smoothing)

**Published to Home Assistant:** Smoothed occupancy only.

---

### Sensitivity

**Definition:** A user-facing configuration value (0-100) that controls the tradeoff between responsiveness and stability.

**Canonical interpretation:**
- **0 = Maximum stability:** Longest hold times, slowest response, minimal flicker
- **100 = Maximum responsiveness:** No hold times, instant response, may flicker

**Affected parameters:**

| Sensitivity | Hold Time | Enter Delay | Behavior |
|-------------|-----------|-------------|----------|
| 0 | 5000ms | 500ms | Very stable, slow |
| 25 | 3750ms | 375ms | Stable |
| 50 | 2500ms | 250ms | Balanced (default) |
| 75 | 1250ms | 125ms | Responsive |
| 100 | 0ms | 0ms | Instant, no smoothing |

**Formulas:**
```c
hold_time_ms = (100 - sensitivity) * 50;     // 0-5000ms
enter_delay_ms = (100 - sensitivity) * 5;    // 0-500ms
```

**Storage:** Per-zone as `uint8_t sensitivity` (0-100)

**Deprecated interpretations:**
- ~~z_score threshold~~ (removed - use sensitivity only)
- ~~unused field~~ (now has canonical meaning)

---

### Target

**Definition:** A potential human detected by the radar sensor.

**Disambiguation:**
- **Raw target:** Detection from M01 (may be noise)
- **Confirmed target:** Track from M02 with state=CONFIRMED

**Maximum:** 3 simultaneous targets (LD2450 hardware limit)

---

### Track

**Definition:** A persistent identity assigned to a moving target, maintained across frames using Kalman filtering.

**Lifecycle states:**
1. TENTATIVE - New, awaiting confirmation
2. CONFIRMED - Actively tracked
3. OCCLUDED - Temporarily missing, predicted
4. RETIRED - Removed from tracking

**Properties:**
- track_id (unique identifier)
- position (x_mm, y_mm)
- velocity (vx_mm_s, vy_mm_s)
- confidence (0-100)
- state (lifecycle state)

---

### Zone

**Definition:** A user-defined polygonal region where presence is detected.

**Types:**
- **Include zone:** Tracks inside are counted as present
- **Exclude zone:** Tracks inside are ignored (e.g., windows, fans)

**Properties:**
- id (unique identifier)
- name (display name)
- type (include/exclude)
- vertices (3-8 points in mm)
- sensitivity (0-100)

---

## Deprecated Terms

These terms should NOT be used in new specifications:

| Deprecated Term | Use Instead |
|-----------------|-------------|
| z_score | sensitivity |
| detection confidence | (removed - use track confidence) |
| raw presence | raw occupancy |

---

## Module Term Usage

| Term | M01 | M02 | M03 | M04 | M05 | M06 | M11 |
|------|-----|-----|-----|-----|-----|-----|-----|
| Detection | Produces | Consumes | - | - | - | - | - |
| Track | - | Produces | Consumes | Consumes | - | - | - |
| Confidence | - | Produces | Passes | Uses | - | - | - |
| Sensitivity | - | - | Reads | Uses | - | Stores | Config |
| Occupancy | - | - | Produces | Smooths | Publishes | - | - |
| Zone | - | - | Uses | Uses | Uses | Stores | Config |

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-09 | OpticWorks | Initial glossary. Addresses RFD-001 issue C6 (sensitivity ambiguity). |
