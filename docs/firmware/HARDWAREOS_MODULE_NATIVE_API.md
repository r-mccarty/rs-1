# HardwareOS Native API Server Module Specification (M05)

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Implement an ESPHome Native API-compatible server that enables seamless Home Assistant auto-discovery and entity integration. This module exposes curated presence entities while maintaining protocol compatibility with existing ESPHome tooling.

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | ESPHome Native API protocol version 1.9+ | Protocol implementation, encryption |
| A2 | Home Assistant 2024.1+ as target integration | Entity types, discovery behavior |
| A3 | Single simultaneous HA connection is sufficient for MVP | Connection handling complexity |
| A4 | Wi-Fi is the only network transport (no Ethernet/BLE) | Network initialization |
| A5 | mDNS is available for discovery | Discovery fallback requirements |
| A6 | API encryption is required in production | Noise protocol implementation |
| A7 | Entity count is < 50 for MVP | Memory for entity registry |
| A8 | State updates throttled to 10 Hz max | Network bandwidth, HA load |

## 3. Protocol Overview

### 3.1 ESPHome Native API

- **Transport**: TCP on port 6053 (default).
- **Framing**: Protobuf-based message framing.
- **Encryption**: Noise Protocol Framework (optional, recommended).
- **Discovery**: mDNS `_esphomelib._tcp.local`.

### 3.2 Message Types (Subset)

| Message | Direction | Purpose |
|---------|-----------|---------|
| `HelloRequest/Response` | Client→Device | Protocol handshake |
| `ConnectRequest/Response` | Client→Device | Authentication |
| `DeviceInfoRequest/Response` | Client→Device | Device metadata |
| `ListEntitiesRequest` | Client→Device | Entity enumeration |
| `*StateResponse` | Device→Client | Entity state updates |
| `SubscribeStatesRequest` | Client→Device | State subscription |

## 4. Inputs

### 4.1 From M04 Presence Smoothing

```c
typedef struct {
    char zone_id[16];
    bool occupied;
    uint8_t target_count;
    uint32_t occupied_since_ms;
} smoothed_zone_state_t;
```

### 4.2 From Internal Systems

- Device info (MAC, firmware version, uptime).
- Wi-Fi signal strength (RSSI).
- Health status from M08/M09.

## 5. Outputs

### 5.1 Entity Types Exposed

| Entity Type | Platform | Count | Purpose |
|-------------|----------|-------|---------|
| `binary_sensor` | presence | Per zone | Zone occupancy |
| `sensor` | count | Per zone | Target count in zone |
| `sensor` | signal_strength | 1 | Wi-Fi RSSI |
| `sensor` | uptime | 1 | Device uptime |
| `binary_sensor` | connectivity | 1 | Device online status |

### 5.2 Entity Naming Convention

```
<device_name>_<zone_id>_occupancy     # binary_sensor
<device_name>_<zone_id>_target_count  # sensor
<device_name>_signal_strength         # sensor
<device_name>_uptime                  # sensor
```

Example for device "rs1_living" with zone "kitchen":
- `binary_sensor.rs1_living_kitchen_occupancy`
- `sensor.rs1_living_kitchen_target_count`

## 6. Implementation Details

### 6.1 Connection Handling

```c
typedef struct {
    int socket_fd;
    bool authenticated;
    bool subscribed_states;
    uint32_t last_activity_ms;
    noise_state_t *noise;  // Encryption state
} api_connection_t;

// MVP: Single connection
// Future: Connection pool for multiple clients
api_connection_t connection;
```

### 6.2 State Publishing

```c
void publish_zone_state(const smoothed_zone_state_t *state) {
    if (!connection.subscribed_states) return;

    // Binary sensor state
    BinarySensorStateResponse binary_msg = {
        .key = hash(state->zone_id, "_occupancy"),
        .state = state->occupied,
        .missing_state = false
    };
    send_message(MSG_BINARY_SENSOR_STATE, &binary_msg);

    // Sensor state (target count)
    SensorStateResponse sensor_msg = {
        .key = hash(state->zone_id, "_target_count"),
        .state = (float)state->target_count,
        .missing_state = false
    };
    send_message(MSG_SENSOR_STATE, &sensor_msg);
}
```

### 6.3 Entity Registry

Static entity definitions (no runtime discovery of new entities):

```c
typedef struct {
    uint32_t key;           // Unique entity key (hash)
    entity_type_t type;     // BINARY_SENSOR, SENSOR, etc.
    char object_id[32];     // ESPHome object_id
    char name[48];          // Human-readable name
    char device_class[16];  // HA device class
    char unit[8];           // Unit of measurement
    char icon[24];          // MDI icon
} entity_def_t;

// Populated from zone config at startup
entity_def_t entity_registry[50];
uint8_t entity_count;
```

## 7. Device Info Response

```protobuf
message DeviceInfoResponse {
    string name = 1;                    // "rs1-living-room"
    string friendly_name = 2;           // "RS-1 Living Room"
    string mac_address = 3;             // "AA:BB:CC:DD:EE:FF"
    string esphome_version = 4;         // "2024.1.0" (compatibility)
    string compilation_time = 5;        // Build timestamp
    string model = 6;                   // "RS-1"
    string manufacturer = 7;            // "OpticWorks"
    bool has_deep_sleep = 8;            // false
    string project_name = 9;            // "opticworks.rs1"
    string project_version = 10;        // "1.0.0"
    uint32 webserver_port = 11;         // 80 or 0
    uint32 bluetooth_proxy_version = 12;// 0
}
```

## 8. Authentication

### 8.1 API Password (Legacy)

- Simple password in `ConnectRequest`.
- Stored in M06 Config Store.
- Not recommended for production.

### 8.2 Noise Protocol (Recommended)

- Noise_NNpsk0 protocol.
- Pre-shared key derived from device setup.
- Full encryption of all messages after handshake.

```c
typedef struct {
    uint8_t psk[32];        // Pre-shared key
    bool encryption_enabled;
} api_auth_config_t;
```

## 9. mDNS Advertisement

### 9.1 Service Registration

```
Service: _esphomelib._tcp.local
Instance: rs1-{mac_suffix}
Port: 6053
TXT Records:
  - version=1.9
  - mac=AABBCCDDEEFF
  - address=192.168.1.100
  - project_name=opticworks.rs1
  - project_version=1.0.0
```

### 9.2 Unique Instance Naming

To avoid mDNS collisions when multiple RS-1 devices are on the same network, each device MUST use a unique instance name derived from its MAC address:

```c
// Instance name format: rs1-{last 6 hex chars of MAC}
// Example: MAC AA:BB:CC:DD:EE:FF → "rs1-ddeeff"

void mdns_init_service(void) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    char instance_name[16];
    snprintf(instance_name, sizeof(instance_name), "rs1-%02x%02x%02x",
             mac[3], mac[4], mac[5]);

    mdns_service_add(instance_name, "_esphomelib", "_tcp", 6053, txt_records, txt_count);

    ESP_LOGI(TAG, "mDNS registered: %s._esphomelib._tcp.local", instance_name);
}
```

**Full mDNS name:** `rs1-ddeeff._esphomelib._tcp.local`

This ensures Home Assistant can discover and distinguish multiple RS-1 devices on the same network.

## 10. Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `api_port` | uint16 | 6053 | TCP port for API |
| `api_password` | string | "" | Legacy password (optional) |
| `encryption_key` | bytes[32] | random | Noise PSK |
| `reboot_timeout_ms` | uint32 | 0 | Reboot if no connection (0 = disabled) |
| `state_throttle_ms` | uint16 | 100 | Min interval between state updates |

### 10.1 Reboot Timeout Behavior

**Default: Disabled (0)**

The `reboot_timeout_ms` parameter controls automatic device reboot when no Home Assistant connection is established. This is **disabled by default** to prevent infinite reboot loops.

**Why disabled by default:**
- Devices may operate without Home Assistant (standalone mode)
- First-time setup has no HA connection until user configures
- Network issues should not cause continuous reboots
- See RFD-001 issue C12

**When to enable:**
```c
// Only enable after HA is successfully configured and connection verified
config.reboot_timeout_ms = 900000;  // 15 minutes
```

**Behavior when enabled:**
- Timer starts on boot
- Timer resets on successful HA connection
- If timer expires without connection → system reboot
- Log warning at 50% and 90% of timeout

## 11. Performance Requirements

| Metric | Requirement | Rationale |
|--------|-------------|-----------|
| Connection latency | < 500ms | Fast HA discovery |
| State update latency | < 50ms | Real-time presence |
| Memory for API | < 8KB | Protobuf + connection state |
| Max message size | 1024 bytes | Entity list fits in single message |

## 12. Error Handling

| Error | Detection | Action |
|-------|-----------|--------|
| Connection dropped | Socket error | Clean up, await new connection |
| Auth failure | Invalid password/PSK | Disconnect, log attempt |
| Message parse error | Protobuf decode failure | Disconnect, log |
| Entity overflow | Registry full | Reject zone config, log |

## 13. Entities NOT Exposed (By Design)

To maintain a clean UX and avoid confusion:

| Excluded | Reason |
|----------|--------|
| Raw target X/Y/speed | Too low-level for typical automations |
| Track IDs | Internal implementation detail |
| Zone vertices | Configuration, not state |
| Debug metrics | Use M09 Logging instead |

These can be exposed via a separate debug interface if needed.

## 14. Telemetry (M09 Integration)

| Metric | Type | Description |
|--------|------|-------------|
| `api.connections_total` | Counter | Total connections since boot |
| `api.auth_failures` | Counter | Failed auth attempts |
| `api.messages_sent` | Counter | Outbound messages |
| `api.state_updates_sec` | Gauge | State updates per second |

## 15. Testing Strategy

### 15.1 Unit Tests

- Protobuf message encoding/decoding.
- Entity key generation consistency.
- Auth flow state machine.

### 15.2 Integration Tests

- Connect with ESPHome dashboard → verify discovery.
- Connect with Home Assistant → verify entities appear.
- State change propagation timing.

### 15.3 Compatibility Tests

- Test against ESPHome 2024.1, 2024.6, 2024.12.
- Test against Home Assistant 2024.1+.

## 16. Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| Protobuf-c or nanopb | latest | Message encoding |
| Noise-C | latest | Encryption (optional) |
| lwIP/ESP-IDF TCP | 5.x | Network stack |
| mDNS (ESP-IDF) | 5.x | Service discovery |

## 17. Open Questions

- Support multiple simultaneous HA connections?
- Expose firmware update progress as sensor?
- Add "last motion" timestamp sensor for zones?

---

## 18. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-XX | OpticWorks | Initial draft |
| 0.2 | 2026-01-09 | OpticWorks | Disabled reboot_timeout_ms by default (0) to prevent infinite reboot loops when HA is not configured. Added section 10.1. Addresses RFD-001 issue C12. |
