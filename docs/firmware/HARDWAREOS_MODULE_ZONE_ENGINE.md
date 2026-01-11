# HardwareOS Zone Engine Module Specification (M03)

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Map confirmed tracks from M02 Tracking to user-defined polygon zones and emit per-zone occupancy states. The Zone Engine is the bridge between raw tracking data and meaningful presence events for Home Assistant.

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | Zones are 2D polygons (no 3D/height) | Zone schema, algorithm choice |
| A2 | Maximum 8 vertices per zone | Memory allocation, point-in-polygon complexity |
| A3 | Unlimited zones in firmware (resource-constrained in practice) | Storage limits, iteration performance |
| A4 | Coordinates in mm, matching M01 output | Unit conversion requirements |
| A5 | Include zones take precedence over exclude zones | Zone priority logic |
| A6 | M02 provides confirmed tracks only (not tentative) | No additional filtering needed |
| A7 | Zone updates are atomic (full config replace) | No incremental zone editing |
| A8 | ESP32-C3 has sufficient RAM for ~16 zones | Memory budget |

## 3. Inputs

### 3.1 From M02 Tracking

```c
typedef struct {
    uint8_t track_id;       // Unique track identifier
    int16_t x_mm;           // Current X position
    int16_t y_mm;           // Current Y position
    int16_t vx_mm_s;        // X velocity
    int16_t vy_mm_s;        // Y velocity
    uint8_t confidence;     // Track confidence (0-100)
    bool confirmed;         // Track is confirmed (not tentative)
} track_t;

typedef struct {
    track_t tracks[3];      // Up to 3 confirmed tracks
    uint8_t track_count;    // Number of active tracks
    uint32_t timestamp_ms;  // Frame timestamp
} track_frame_t;
```

### 3.2 From M06 Config Store

```c
typedef struct {
    char id[16];            // Zone identifier (e.g., "zone_living")
    char name[32];          // Display name (e.g., "Living Room")
    zone_type_t type;       // ZONE_INCLUDE or ZONE_EXCLUDE
    int16_t vertices[8][2]; // Polygon vertices (x, y) in mm
    uint8_t vertex_count;   // Number of vertices (3-8)
    uint8_t sensitivity;    // Sensitivity preset (0-100)
} zone_config_t;

typedef struct {
    zone_config_t zones[16]; // Zone definitions
    uint8_t zone_count;      // Number of defined zones
    uint32_t version;        // Config version for sync
} zone_map_t;
```

## 4. Outputs

### 4.1 Zone Occupancy State

```c
typedef struct {
    char zone_id[16];       // Zone identifier
    bool occupied;          // Any track in zone
    uint8_t target_count;   // Number of tracks in zone (0-3)
    uint8_t track_ids[3];   // IDs of tracks in zone
    bool has_moving;        // Any track moving in zone
    uint32_t last_change_ms;// Timestamp of last occupancy change
} zone_state_t;

typedef struct {
    zone_state_t states[16]; // Per-zone states
    uint8_t zone_count;      // Number of zones
    uint32_t timestamp_ms;   // Frame timestamp
} zone_frame_t;
```

### 4.2 Occupancy Events (to M04 Presence Smoothing)

- `ZONE_ENTER`: Track entered zone.
- `ZONE_EXIT`: Track left zone.
- `ZONE_OCCUPIED`: Zone transitioned to occupied.
- `ZONE_VACANT`: Zone transitioned to vacant.

## 5. Processing Pipeline

### 5.1 Per-Frame Processing

For each track frame from M02:

```
1. For each confirmed track:
   a. For each exclude zone:
      - If track in exclude zone → mark track as excluded
   b. For each include zone:
      - If track not excluded AND track in zone:
        - Increment zone.target_count
        - Add track_id to zone.track_ids
        - If track.speed > threshold → zone.has_moving = true

2. For each zone:
   a. Determine new occupancy: occupied = (target_count > 0)
   b. If occupancy changed from previous frame:
      - Emit ZONE_ENTER/EXIT events for affected tracks
      - Emit ZONE_OCCUPIED/VACANT event
      - Update last_change_ms

3. Push zone_frame_t to M04 Presence Smoothing
```

### 5.2 Point-in-Polygon Algorithm

Use ray casting for O(n) complexity:

```c
bool point_in_polygon(int16_t x, int16_t y,
                      int16_t vertices[][2],
                      uint8_t vertex_count) {
    bool inside = false;
    for (int i = 0, j = vertex_count - 1; i < vertex_count; j = i++) {
        int16_t xi = vertices[i][0], yi = vertices[i][1];
        int16_t xj = vertices[j][0], yj = vertices[j][1];
        if (((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}
```

### 5.2.1 Point-on-Edge Behavior

**Definition:** Points exactly on a zone boundary (edge or vertex) are considered **INSIDE** the zone.

This policy prevents flickering when a target is at a zone boundary and ensures consistent behavior. See RFD-001 issue C11.

```c
// Edge case handling: shrink zone by 1mm for evaluation
// to ensure boundary points are considered inside
#define EDGE_MARGIN_MM 1

bool point_in_zone_with_margin(int16_t x, int16_t y,
                                const zone_config_t *zone) {
    // For include zones: expand slightly to capture edges
    // For exclude zones: shrink slightly to not capture edges
    int16_t margin = (zone->type == ZONE_INCLUDE) ? EDGE_MARGIN_MM : -EDGE_MARGIN_MM;

    // Apply margin to all vertices (simplified: actual impl uses proper polygon offset)
    return point_in_polygon(x, y, zone->vertices, zone->vertex_count);
}
```

**Rationale:**
- Prevents boundary flickering between adjacent zones
- Deterministic behavior regardless of floating-point precision
- User expectation: if target is "on the line", count as present

### 5.3 Zone Priority Rules

1. **Exclude zones first**: If a track is in any exclude zone, it is ignored for all include zones.
2. **Overlapping includes**: A track can be in multiple include zones simultaneously.
3. **Default behavior**: If no zones defined, global presence from M02 is the only signal.

## 6. Zone Types

| Type | Behavior | Use Case |
|------|----------|----------|
| `ZONE_INCLUDE` | Track inside → zone occupied | Define presence areas |
| `ZONE_EXCLUDE` | Track inside → ignored | Filter false positives (windows, fans) |

## 7. Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `max_zones` | uint8 | 16 | Maximum zones supported |
| `max_vertices` | uint8 | 8 | Maximum vertices per zone |
| `moving_threshold_cm_s` | uint16 | 10 | Speed threshold for "moving" |
| `debounce_frames` | uint8 | 2 | Frames before emitting change event |

## 8. Performance Requirements

| Metric | Requirement | Rationale |
|--------|-------------|-----------|
| Processing latency | < 500µs per frame | 3 tracks × 16 zones × 8 vertices = 384 checks |
| Memory (zones) | < 2KB | 16 zones × ~128 bytes each |
| Memory (state) | < 512 bytes | Per-zone state tracking |

## 9. Validation Rules

### 9.1 Zone Validation (on config update)

| Rule | Check | Action |
|------|-------|--------|
| Minimum vertices | vertex_count >= 3 | Reject zone |
| Maximum vertices | vertex_count <= 8 | Reject zone |
| Self-intersection | Edges must not cross | Reject zone |
| Range bounds | All vertices within sensor range | Warn, allow |
| Unique ID | No duplicate zone IDs | Reject update |

### 9.2 Self-Intersection Check

```c
bool is_simple_polygon(int16_t vertices[][2], uint8_t count) {
    // Check each edge against non-adjacent edges
    for (int i = 0; i < count; i++) {
        for (int j = i + 2; j < count; j++) {
            if (j == (i + count - 1) % count) continue; // Skip adjacent
            if (edges_intersect(vertices, i, j)) return false;
        }
    }
    return true;
}
```

## 10. Telemetry (M09 Integration)

| Metric | Type | Description |
|--------|------|-------------|
| `zone.evaluations_per_sec` | Gauge | Zone checks per second |
| `zone.occupancy_changes` | Counter | Total occupancy transitions |
| `zone.tracks_excluded` | Counter | Tracks filtered by exclude zones |
| `zone.processing_us` | Gauge | Per-frame processing time |

## 11. Testing Strategy

### 11.1 Unit Tests

- Point-in-polygon with convex/concave polygons.
- Edge cases: point on vertex, point on edge.
- Exclude zone filtering logic.
- Zone overlap handling.

### 11.2 Integration Tests

- Simulated track movement across zones.
- Zone entry/exit event timing.
- Config update while tracking active.

### 11.3 Stress Tests

- Maximum zones (16) with maximum vertices (8 each).
- Rapid track movement across zone boundaries.

## 12. Dependencies

| Module | Purpose |
|--------|---------|
| M02 Tracking | Provides confirmed tracks |
| M04 Presence Smoothing | Consumes zone occupancy |
| M06 Device Config Store | Provides zone definitions |

## 13. Open Questions

- Should zone sensitivity affect point-in-polygon (margin expansion)?
- How to handle track straddling zone boundary (centroid vs any part)?
- Define maximum practical zone count for ESP32-C3 memory budget.
