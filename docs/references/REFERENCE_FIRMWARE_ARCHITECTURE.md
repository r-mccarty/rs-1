# Reference: LD2450 Firmware Architecture

This document speculates on the firmware architecture for LD2450-based presence sensors like Sensy-One, based on public documentation and ESPHome capabilities. RS-1 firmware will likely have similar requirements.

---

## Overview

The LD2450 is a 24GHz mmWave radar that outputs raw target data over UART. The firmware's job is to:

1. Parse raw sensor data
2. Apply zone logic (point-in-polygon)
3. Expose meaningful entities to Home Assistant
4. Accept zone configuration updates

---

## LD2450 Raw Output

The LD2450 reports up to 3 targets at 30ms intervals via UART (256000 baud).

### Data Per Target

| Field | Type | Unit | Range |
|-------|------|------|-------|
| X position | int16 | mm | -6000 to +6000 |
| Y position | int16 | mm | 0 to 6000 |
| Speed | int16 | cm/s | -128 to +127 (negative = approaching) |
| Resolution | uint16 | mm | Distance resolution |

### Coordinate System

```
                    +Y (away from sensor)
                     │
                     │    Target
                     │      ●
                     │     /
                     │    /
                     │   /
        ─────────────┼─────────────── +X
        -6000mm      │  Sensor     +6000mm
                     │    ▼
                     │
                    0,0
```

- Origin (0,0) is at the sensor
- X is horizontal (left/right)
- Y is depth (distance from sensor)
- 120° horizontal FOV, 60° vertical FOV
- Max range: 6 meters

---

## Firmware Processing Pipeline

```
┌─────────────────────────────────────────────────────────────────┐
│                     Firmware Architecture                        │
│                                                                  │
│  ┌──────────┐    ┌──────────────┐    ┌─────────────────────┐   │
│  │  LD2450  │───▶│ UART Parser  │───▶│ Target Data Buffer  │   │
│  │  Sensor  │    │              │    │                     │   │
│  └──────────┘    └──────────────┘    │ target[0].x/y/speed │   │
│                                       │ target[1].x/y/speed │   │
│                                       │ target[2].x/y/speed │   │
│                                       └──────────┬──────────┘   │
│                                                  │               │
│                                                  ▼               │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                   Zone Processing                         │   │
│  │                                                           │   │
│  │  For each target:                                         │   │
│  │    For each zone:                                         │   │
│  │      if point_in_polygon(target.x, target.y, zone):       │   │
│  │        zone.target_count++                                │   │
│  │        zone.occupied = true                               │   │
│  │                                                           │   │
│  │  For exclusion zone:                                      │   │
│  │    Filter out targets inside exclusion                    │   │
│  │                                                           │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                  │               │
│                                                  ▼               │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                 Entity Publishing                         │   │
│  │                                                           │   │
│  │  Global:                                                  │   │
│  │    - binary_sensor.presence (any target detected)         │   │
│  │    - sensor.target_count (0-3)                            │   │
│  │                                                           │   │
│  │  Per-target (x3):                                         │   │
│  │    - sensor.target_N_x                                    │   │
│  │    - sensor.target_N_y                                    │   │
│  │    - sensor.target_N_speed                                │   │
│  │                                                           │   │
│  │  Per-zone (x3 + 1 exclusion):                             │   │
│  │    - binary_sensor.zone_N_occupied                        │   │
│  │    - sensor.zone_N_target_count                           │   │
│  │                                                           │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Zone Configuration

### Zone Data Structure

Each zone is a polygon with up to 8 vertices:

```cpp
struct Zone {
    uint8_t id;                    // Zone ID (0-3)
    uint8_t vertex_count;          // Number of vertices (3-8)
    int16_t vertices[8][2];        // X,Y coordinates in mm
    bool is_exclusion;             // Exclusion zone flag

    // Runtime state
    bool occupied;
    uint8_t target_count;
};
```

### Zone Storage Options

**Option A: Number Entities (Simple)**

Store each vertex as a number entity:
```yaml
number:
  - platform: template
    name: "Zone 1 X1"
    min_value: -6000
    max_value: 6000
    step: 10
    # ... repeat for all vertices
```

Pros: Works with stock ESPHome
Cons: Many entities (8 vertices × 2 coords × 4 zones = 64 numbers)

**Option B: Text Entity (JSON)**

Store zone config as JSON in a text entity:
```yaml
text:
  - platform: template
    name: "Zone Config"
    # Value: {"zones":[{"vertices":[[0,0],[1000,0],[1000,2000],[0,2000]]},...]}
```

Pros: Single entity, flexible
Cons: Requires JSON parsing on ESP32

**Option C: HA State Polling (Sensy approach)**

Pull zone config from HA state entity created by zone editor:
```cpp
// Poll HA API for zone_editor_floorplan.floor_1
// Parse JSON response
// Update local zone configs
```

Pros: Decouples firmware from editor
Cons: Requires HA API polling, more complex

---

## Point-in-Polygon Algorithm

The core algorithm for zone detection. Ray casting is the standard approach:

```cpp
/**
 * Check if point (x,y) is inside polygon
 * Uses ray casting algorithm
 *
 * @param x Target X coordinate (mm)
 * @param y Target Y coordinate (mm)
 * @param vertices Array of polygon vertices
 * @param vertex_count Number of vertices
 * @return true if point is inside polygon
 */
bool point_in_polygon(int16_t x, int16_t y,
                      int16_t vertices[][2],
                      uint8_t vertex_count) {
    bool inside = false;

    for (int i = 0, j = vertex_count - 1; i < vertex_count; j = i++) {
        int16_t xi = vertices[i][0], yi = vertices[i][1];
        int16_t xj = vertices[j][0], yj = vertices[j][1];

        // Check if ray from point crosses this edge
        if (((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }

    return inside;
}
```

Complexity: O(n) where n = vertex count. With max 8 vertices and 3 targets × 4 zones, this runs 96 checks per frame (every 30ms). Trivial for ESP32.

---

## ESPHome Implementation

### Basic LD2450 (No Zones)

ESPHome has native LD2450 support:

```yaml
ld2450:
  id: ld2450_radar
  uart_id: uart_ld2450
  throttle: 100ms

binary_sensor:
  - platform: ld2450
    has_target:
      name: "Presence"

sensor:
  - platform: ld2450
    target_count:
      name: "Target Count"
    target_1:
      x:
        name: "Target 1 X"
      y:
        name: "Target 1 Y"
      speed:
        name: "Target 1 Speed"
```

### Adding Zone Support

Zone support requires custom component or lambda:

```yaml
# Custom component approach
external_components:
  - source: github://r-mccarty/rs1-zone-engine

rs1_zones:
  ld2450_id: ld2450_radar
  zones:
    - id: zone_1
      name: "Zone 1"
      vertices: !lambda 'return {{0,0}, {2000,0}, {2000,3000}, {0,3000}};'
    - id: zone_2
      name: "Zone 2"
      # ...

binary_sensor:
  - platform: rs1_zones
    zone_id: zone_1
    name: "Zone 1 Occupied"
```

Or using lambdas with existing LD2450 component:

```yaml
binary_sensor:
  - platform: template
    name: "Zone 1 Occupied"
    lambda: |-
      // Get target positions from LD2450
      auto targets = id(ld2450_radar).get_targets();

      // Define zone polygon (could be from global variable)
      int16_t zone1[][2] = {{0,0}, {2000,0}, {2000,3000}, {0,3000}};
      int vertex_count = 4;

      // Check each target
      for (auto &target : targets) {
        if (target.valid && point_in_polygon(target.x, target.y, zone1, vertex_count)) {
          return true;
        }
      }
      return false;
```

---

## Estimated Firmware Complexity

### Base LD2450 Support (ESPHome native)

| Component | LOC | Notes |
|-----------|-----|-------|
| UART config | ~10 | Standard ESPHome |
| LD2450 component | ~20 | Native ESPHome support |
| Basic sensors | ~30 | Target X/Y/speed entities |
| **Subtotal** | **~60** | Stock ESPHome, no custom code |

### Zone Processing (Custom)

| Component | LOC | Notes |
|-----------|-----|-------|
| Zone data structures | ~30 | Structs for zones, vertices |
| Point-in-polygon | ~25 | Ray casting algorithm |
| Zone processing loop | ~50 | Per-target, per-zone checks |
| Zone config storage | ~50-100 | Number entities or JSON parsing |
| Zone binary sensors | ~40 | Per-zone occupied sensors |
| Exclusion zone logic | ~20 | Filter targets in exclusion |
| **Subtotal** | **~215-265** | Custom code |

### Optional Enhancements

| Component | LOC | Notes |
|-----------|-----|-------|
| HA state polling | ~100 | Pull zone config from HA |
| Debouncing/hysteresis | ~50 | Prevent flapping |
| Speed-based filtering | ~30 | Ignore stationary vs moving |
| Zone entry/exit events | ~40 | Trigger on transitions |
| **Subtotal** | **~220** | Nice-to-have |

### Total Estimated LOC

| Tier | LOC | Features |
|------|-----|----------|
| **Minimal** | ~280 | Base + zones |
| **Standard** | ~400 | + debouncing, events |
| **Full** | ~500 | + HA polling, all features |

---

## Entity List (Full Implementation)

### Global Entities

| Entity | Type | Description |
|--------|------|-------------|
| `binary_sensor.presence` | Binary | Any target detected |
| `sensor.target_count` | Sensor | Number of targets (0-3) |

### Per-Target Entities (×3)

| Entity | Type | Unit |
|--------|------|------|
| `sensor.target_N_x` | Sensor | mm |
| `sensor.target_N_y` | Sensor | mm |
| `sensor.target_N_speed` | Sensor | cm/s |
| `sensor.target_N_distance` | Sensor | mm (derived) |
| `sensor.target_N_angle` | Sensor | degrees (derived) |

### Per-Zone Entities (×3 detection + 1 exclusion)

| Entity | Type | Description |
|--------|------|-------------|
| `binary_sensor.zone_N_occupied` | Binary | Any target in zone |
| `sensor.zone_N_target_count` | Sensor | Targets in zone (0-3) |
| `binary_sensor.zone_N_moving` | Binary | Moving target in zone |

### Zone Configuration Entities

**Option A: Individual numbers (verbose)**
- 64 number entities for vertices
- 4 number entities for vertex counts

**Option B: JSON text (compact)**
- 1 text entity with full config
- Service to update zones

---

## Performance Considerations

### ESP32 Capabilities

| Resource | Available | Zone Processing Need |
|----------|-----------|---------------------|
| CPU | 240 MHz dual-core | <1% utilization |
| RAM | 320 KB | ~1-2 KB for zones |
| Flash | 4-16 MB | ~50 KB firmware |

Zone processing is trivial for ESP32. The LD2450 updates at 30ms; even with 4 zones and 3 targets, processing completes in <1ms.

### Update Rate

```
LD2450 output:     30ms  (33 Hz)
Zone processing:   <1ms
Recommended throttle: 100-200ms for HA updates
```

Throttling HA updates reduces network traffic without losing responsiveness.

---

## Comparison: Raw vs Processed

### Raw Data Only (No Zone Processing)

```
Firmware → HA: target_1_x=1234, target_1_y=2345, ...
HA automation must: Check if (1234, 2345) in zone polygon
```

Pros: Simple firmware
Cons: Complex automations, HA does math every update

### On-Device Zone Processing (Sensy/RS-1 approach)

```
Firmware: Check targets against zones internally
Firmware → HA: zone_1_occupied=true, zone_1_targets=2
HA automation: Simple binary triggers
```

Pros: Simple automations, less HA load, works without zone editor
Cons: More complex firmware

**Recommendation**: On-device processing. It's minimal code and dramatically simplifies user automations.

---

## RS-1 Firmware Roadmap

### v1.0 (MVP - Jan 19 target)

- [x] Basic LD2450 support (ESPHome native)
- [x] Global presence binary sensor
- [x] Per-target X/Y/speed sensors
- [ ] Simple distance-based zone (min/max distance)

### v1.1 (Zone Support)

- [ ] Polygon zone data structures
- [ ] Point-in-polygon algorithm
- [ ] Per-zone binary sensors
- [ ] Zone config via number entities

### v1.2 (Zone Editor Integration)

- [ ] JSON-based zone config
- [ ] HA state polling for zone updates
- [ ] TUI/Web editor support

### v2.0 (Advanced)

- [ ] Exclusion zones
- [ ] Zone entry/exit events
- [ ] Speed-based filtering
- [ ] Multi-sensor coordination

---

## References

- [ESPHome LD2450 Component](https://esphome.io/components/sensor/ld2450/)
- [LD2450 Datasheet](https://www.hlktech.net/index.php?id=1157)
- [Point-in-Polygon Algorithm](https://en.wikipedia.org/wiki/Point_in_polygon)
- [Sensy-One S1 Pro](https://github.com/sensy-one/S1-Pro-Multi-Sense)
