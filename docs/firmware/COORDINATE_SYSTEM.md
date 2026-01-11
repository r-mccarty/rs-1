# HardwareOS Coordinate System Specification

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks Firmware
Status: Draft

---

## 1. Purpose

This document defines the canonical coordinate system used throughout HardwareOS. All modules MUST use these conventions to ensure consistent behavior across the firmware stack.

**This is the authoritative reference for coordinate handling.** If any module specification conflicts with this document, this document takes precedence.

---

## 2. Canonical Units

| Property | Value | Rationale |
|----------|-------|-----------|
| **Linear unit** | Millimeters (mm) | Matches LD2450 native output, avoids floating-point |
| **Angular unit** | Degrees | Human-readable for zone configuration |
| **Velocity unit** | cm/s | LD2450 native output |
| **Time unit** | Milliseconds (ms) | ESP-IDF timer resolution |

### 2.1 Unit Conversion Policy

**All internal firmware operations use mm.** Unit conversion happens ONLY at system boundaries:

| Boundary | Conversion | Owner |
|----------|------------|-------|
| Zone Editor API input (JSON) | meters → mm | M11 (on receive) |
| Zone Editor API output (JSON) | mm → meters | M11 (on send) |
| Native API display | mm (no conversion) | M05 |
| Telemetry export | mm (no conversion) | M09 |

**Never perform floating-point division in the hot path.** Convert once at the edge, operate in integers.

---

## 3. Coordinate Frame

### 3.1 Origin and Axes

```
                    PHYSICAL SENSOR ORIENTATION
    ════════════════════════════════════════════════════════════

                         Room Interior
                              ▲
                              │ +Y (depth)
                              │
                              │
            ◄─────────────────┼─────────────────►
           -X                 │                 +X
         (left)         ┌─────┴─────┐         (right)
                        │  LD2450   │
                        │  SENSOR   │
                        │   ████    │  ◄── Radar face
                        └───────────┘
                              │
                        Mounting Surface
                         (wall/ceiling)

    ════════════════════════════════════════════════════════════

    OBSERVER POSITION: Standing in the room, facing the sensor.

    +X = Observer's RIGHT
    -X = Observer's LEFT
    +Y = AWAY from sensor (into the room)
    -Y = Invalid (behind sensor)
```

### 3.2 Axis Definitions

| Axis | Direction | Range | Description |
|------|-----------|-------|-------------|
| **X** | Horizontal | -6000 to +6000 mm | Lateral position relative to sensor centerline |
| **Y** | Depth | 0 to 6000 mm | Distance from sensor into room |
| **Z** | Vertical | N/A (2D MVP) | Reserved for future 3D support |

### 3.3 Reference Frame Disambiguation

**"Right" and "Left" are defined from the perspective of an observer standing in the room, facing the sensor.**

| Term | Direction | Coordinate |
|------|-----------|------------|
| Right | Observer's right hand | +X |
| Left | Observer's left hand | -X |
| Forward/Deep | Toward observer, away from sensor | +Y |
| Near | Close to sensor | Small +Y |

---

## 4. Coordinate Ranges

### 4.1 LD2450 Physical Limits

| Parameter | Minimum | Maximum | Unit |
|-----------|---------|---------|------|
| X position | -6000 | +6000 | mm |
| Y position | 0 | 6000 | mm |
| Effective range | 100 | 6000 | mm |
| Speed | -12800 | +12700 | cm/s |

### 4.2 Valid Detection Region

```
                        Detection Region (Top View)
    ════════════════════════════════════════════════════════════

              -6000 mm                           +6000 mm
                 │                                  │
                 ▼                                  ▼
            ─────┬──────────────────────────────────┬─────
                 │░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░│
                 │░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░│
                 │░░░░░░░░ VALID DETECTION ░░░░░░░░░│ ◄── Y = 6000mm
                 │░░░░░░░░░░░ REGION ░░░░░░░░░░░░░░░│
                 │░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░│
                 │░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░│
                 │░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░│
                 │░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░│ ◄── Y = 100mm (min)
            ─────┴──────────────────────────────────┴─────
                              │
                         ┌────┴────┐
                         │ SENSOR  │ ◄── Y = 0 (origin)
                         └─────────┘

    ════════════════════════════════════════════════════════════

    Gray region = Valid detection area
    Y < 100mm = Near-field noise, filtered by M01
    Y < 0 = Invalid (behind sensor)
```

---

## 5. Module-Specific Requirements

### 5.1 M01 Radar Ingest

- Output: `detection_t` with `x_mm`, `y_mm` in millimeters
- No conversion from LD2450 native format
- Filter detections outside valid region (Y < 100mm or Y > 6000mm)

### 5.2 M02 Tracking

- Input/Output: All coordinates in mm
- Velocity: cm/s (LD2450 native)
- Prediction calculations use mm and ms

### 5.3 M03 Zone Engine

- Zone vertices: mm (int16_t)
- Point-in-polygon: mm coordinates
- No floating-point operations in zone evaluation

### 5.4 M11 Zone Editor Interface

**Critical: M11 is the only module that handles unit conversion.**

| API Direction | User Format | Firmware Format | Conversion |
|---------------|-------------|-----------------|------------|
| Receive zone config | meters (float) | mm (int16) | `mm = (int16_t)(meters * 1000.0f)` |
| Send zone config | meters (float) | mm (int16) | `meters = mm / 1000.0f` |
| Target stream | meters (float) | mm (int16) | `meters = mm / 1000.0f` |

**Validation after conversion:**
- Reject zones with any vertex outside [-6000, +6000] mm on X
- Reject zones with any vertex outside [0, 6000] mm on Y
- Warn (but allow) zones extending beyond typical effective range

### 5.5 M06 Config Store

- Zones stored in mm (int16_t) after M11 conversion
- No unit information stored; mm is assumed

---

## 6. Data Type Requirements

### 6.1 Coordinate Storage

```c
// Canonical coordinate types
typedef int16_t coord_mm_t;    // Range: -32768 to +32767 (sufficient for ±6m)
typedef int16_t speed_cm_s_t;  // Range: -32768 to +32767

// Point structure
typedef struct {
    coord_mm_t x;  // X coordinate in mm
    coord_mm_t y;  // Y coordinate in mm
} point_mm_t;

// Zone vertex array
typedef point_mm_t zone_vertices_t[8];  // Max 8 vertices
```

### 6.2 Conversion Functions

```c
// Convert meters (from Zone Editor JSON) to mm
static inline coord_mm_t meters_to_mm(float meters) {
    return (coord_mm_t)(meters * 1000.0f);
}

// Convert mm to meters (for Zone Editor JSON output)
static inline float mm_to_meters(coord_mm_t mm) {
    return (float)mm / 1000.0f;
}
```

---

## 7. Installation Guidance

### 7.1 Sensor Mounting

```
    RECOMMENDED SENSOR PLACEMENT
    ════════════════════════════════════════════════════════════

    Wall Mount (facing into room):

                    ┌─────────────────────────┐
                    │                         │
                    │         ROOM            │
                    │                         │
                    │                         │
                    │    ▲ +Y                 │
                    │    │                    │
                    │    │                    │
                    │    │                    │
                    │ -X ┼───► +X             │
                    │ [SENSOR]                │ ◄── Mount height: 1.2-2.0m
    ────────────────┴─────────────────────────┴────────────────
                         WALL

    ════════════════════════════════════════════════════════════

    - Mount sensor facing the area to be monitored
    - Avoid pointing at windows, mirrors, or HVAC vents
    - +X should align with room's natural "right" for intuitive zone editing
```

### 7.2 Zone Editor Coordinate Display

When displaying coordinates in the Zone Editor UI:

| UI Label | Coordinate | Example |
|----------|------------|---------|
| "Distance" or "Depth" | Y | "2.5 m from sensor" |
| "Left/Right" | X | "1.2 m right of center" |

---

## 8. Validation Checklist

Use this checklist when reviewing module specifications:

- [ ] All distances expressed in mm (not m, cm, or inches)
- [ ] Coordinate variables named with `_mm` suffix (e.g., `x_mm`, `y_mm`)
- [ ] No floating-point in hot path (zone evaluation, tracking)
- [ ] Unit conversion only at M11 boundary
- [ ] Range validation against [-6000, +6000] X and [0, 6000] Y
- [ ] Coordinate system diagram consistent with this document

---

## 9. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-09 | OpticWorks | Initial specification, addresses RFD-001 issues C3/C10 |
