# MQTT Protocol Contract

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks Firmware + Cloud
Status: Draft

---

## 1. Purpose

This document defines the canonical MQTT topic schema and message formats for communication between RS-1 devices and OpticWorks cloud services. All firmware modules and cloud services MUST use topics and payloads as specified here.

**This is the single source of truth for MQTT topics.** Individual module specs (M07, M09, M11) should reference this document rather than defining their own topics.

---

## 2. Broker Configuration

### 2.1 Broker

- **Provider**: EMQX (Cloud or self-hosted on Cloudflare)
- **Port**: 8883 (TLS required)
- **Protocol**: MQTT 5.0 (fallback to 3.1.1 for compatibility)

### 2.2 Authentication

- **Method**: Username/password derived from device identity (see M10 Security)
- **Username**: Hex-encoded `device_id` (32 characters)
- **Password**: HMAC-SHA256 derived credential (base64-encoded)

### 2.3 TLS Requirements

- TLS 1.2 minimum
- Server certificate pinned to OpticWorks CA
- Client certificates optional (not required for MVP)

---

## 3. Topic Namespace

All topics use the following hierarchy:

```
opticworks/{device_id}/{category}/{action}
```

| Component | Format | Example |
|-----------|--------|---------|
| `device_id` | 32-char hex (from MAC) | `aabbccddeeff00112233445566778899` |
| `category` | Lowercase identifier | `ota`, `telemetry`, `config`, `diag` |
| `action` | Direction/purpose | `trigger`, `status`, `request`, `response` |

---

## 4. Topic Catalog

### 4.1 OTA Topics (M07)

| Topic | Direction | QoS | Retain | Purpose |
|-------|-----------|-----|--------|---------|
| `opticworks/{device_id}/ota/trigger` | Cloud → Device | 1 | No | Initiate firmware update |
| `opticworks/{device_id}/ota/status` | Device → Cloud | 1 | Yes | Report update progress/result |

### 4.2 Telemetry Topics (M09)

| Topic | Direction | QoS | Retain | Purpose |
|-------|-----------|-----|--------|---------|
| `opticworks/{device_id}/telemetry` | Device → Cloud | 0 | No | Periodic metrics and logs |

### 4.3 Diagnostics Topics (M09)

| Topic | Direction | QoS | Retain | Purpose |
|-------|-----------|-----|--------|---------|
| `opticworks/{device_id}/diag/request` | Cloud → Device | 1 | No | Request diagnostics dump |
| `opticworks/{device_id}/diag/response` | Device → Cloud | 1 | No | Diagnostics data |

### 4.4 Configuration Topics (M11)

| Topic | Direction | QoS | Retain | Purpose |
|-------|-----------|-----|--------|---------|
| `opticworks/{device_id}/config/update` | Cloud → Device | 1 | No | Push zone config to device |
| `opticworks/{device_id}/config/status` | Device → Cloud | 1 | Yes | Confirm config applied |

### 4.5 Target Stream Topics (M11)

| Topic | Direction | QoS | Retain | Purpose |
|-------|-----------|-----|--------|---------|
| `opticworks/{device_id}/targets/stream` | Device → Cloud | 0 | No | Live target positions (~10 Hz) |

**Privacy Note:** Target stream transmits real-time position data and is **disabled by default**. It is only enabled when:
1. User explicitly enables "Zone Editor Live View" in the mobile app, AND
2. User is actively viewing the Zone Editor interface

Target stream automatically stops after 5 minutes of inactivity or when the Zone Editor is closed. This ensures no persistent position tracking occurs without explicit user action.

### 4.6 Device State Topics

| Topic | Direction | QoS | Retain | Purpose |
|-------|-----------|-----|--------|---------|
| `opticworks/{device_id}/state` | Device → Cloud | 1 | Yes | Online/offline, firmware version |

### 4.7 Provisioning Topics

| Topic | Direction | QoS | Retain | Purpose |
|-------|-----------|-----|--------|---------|
| `opticworks/{device_id}/provision` | Device → Cloud | 1 | No | Device registration request |
| `opticworks/{device_id}/provision/response` | Cloud → Device | 1 | No | Registration result |

See `PROTOCOL_PROVISIONING.md` for complete provisioning flow.

---

## 5. Message Schemas

### 5.1 OTA Trigger (Cloud → Device)

See: `SCHEMA_OTA_MANIFEST.json`

```json
{
  "version": "1.2.0",
  "url": "https://fw.opticworks.io/rs1/1.2.0.bin",
  "sha256": "abc123...",
  "min_rssi": -70,
  "rollout_id": "2026-01-15-a",
  "issued_at": "2026-01-15T10:00:00Z"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `version` | string | Yes | Target firmware version (semver) |
| `url` | string | Yes | HTTPS URL to signed firmware binary |
| `sha256` | string | Yes | SHA-256 hash of firmware (hex, 64 chars) |
| `min_rssi` | integer | No | Minimum Wi-Fi RSSI to proceed (default: -70) |
| `rollout_id` | string | Yes | Unique rollout identifier for tracking |
| `issued_at` | string | Yes | ISO 8601 timestamp of trigger issuance |

### 5.2 OTA Status (Device → Cloud)

```json
{
  "status": "downloading",
  "version": "1.2.0",
  "progress": 45,
  "error": null,
  "rollout_id": "2026-01-15-a",
  "timestamp": "2026-01-15T10:05:30Z"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `status` | enum | Yes | `pending`, `downloading`, `verifying`, `success`, `failed` |
| `version` | string | Yes | Firmware version being updated |
| `progress` | integer | Yes | Download progress (0-100) |
| `error` | string | No | Error message if status is `failed` |
| `rollout_id` | string | Yes | Rollout identifier from trigger |
| `timestamp` | string | Yes | ISO 8601 timestamp |

### 5.3 Telemetry (Device → Cloud)

See: `SCHEMA_TELEMETRY.json`

```json
{
  "device_id": "aabbccddeeff...",
  "timestamp": "2026-01-15T10:00:00Z",
  "metrics": {
    "system.uptime_sec": 86400,
    "system.free_heap": 45000,
    "system.wifi_rssi": -55,
    "radar.frames_received": 2880000,
    "zone.occupancy_changes": 142
  },
  "logs": [
    {"ts": 12345, "lvl": "W", "tag": "ZONE", "msg": "Flicker detected"}
  ]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `device_id` | string | Yes | 32-char hex device identifier |
| `timestamp` | string | Yes | ISO 8601 timestamp of batch |
| `metrics` | object | Yes | Key-value metric pairs |
| `logs` | array | No | Error/warning log entries (opt-in only) |

### 5.4 Config Update (Cloud → Device)

See: `SCHEMA_ZONE_CONFIG.json`

```json
{
  "version": 3,
  "updated_at": "2026-01-15T12:00:00Z",
  "zones": [
    {
      "id": "zone_living",
      "name": "Living Room",
      "type": "include",
      "vertices": [[200, 400], [2000, 400], [2000, 3000], [200, 3000]],
      "sensitivity": 50
    }
  ]
}
```

**Coordinate Conversion Ownership:**

Per `../firmware/COORDINATE_SYSTEM.md`, M11 (Zone Editor module) is the **sole owner** of coordinate conversion between user-facing meters and internal millimeters:

- **Cloud Zone Editor → Device:** Cloud sends meters; M11 converts to mm before storage
- **Device → Cloud:** M11 converts mm to meters before publishing
- **Internal firmware:** All modules (M02 Tracking, M03 Zone Engine) use mm exclusively

This single conversion point prevents drift and rounding errors from multiple conversions.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `version` | integer | Yes | Config version (monotonic) |
| `updated_at` | string | Yes | ISO 8601 timestamp |
| `zones` | array | Yes | Array of zone definitions |
| `zones[].id` | string | Yes | Zone identifier (alphanumeric, max 16 chars) |
| `zones[].name` | string | Yes | Display name (max 32 chars) |
| `zones[].type` | enum | Yes | `include` or `exclude` |
| `zones[].vertices` | array | Yes | Array of [x, y] coordinate pairs in mm |
| `zones[].sensitivity` | integer | Yes | Sensitivity 0-100 (see GLOSSARY.md) |

### 5.5 Config Status (Device → Cloud)

```json
{
  "version": 3,
  "status": "applied",
  "zone_count": 4,
  "error": null,
  "timestamp": "2026-01-15T12:00:05Z"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `version` | integer | Yes | Config version received |
| `status` | enum | Yes | `applied`, `rejected`, `pending` |
| `zone_count` | integer | Yes | Number of zones in config |
| `error` | string | No | Error message if rejected |
| `timestamp` | string | Yes | ISO 8601 timestamp |

### 5.6 Target Stream (Device → Cloud)

```json
{
  "ts": 1704912345678,
  "targets": [
    {"x": 1234, "y": 2345, "speed": 15, "conf": 85},
    {"x": -500, "y": 1800, "speed": 0, "conf": 72}
  ]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `ts` | integer | Yes | Millisecond timestamp (Unix epoch) |
| `targets` | array | Yes | Array of active targets (max 3) |
| `targets[].x` | integer | Yes | X position in mm |
| `targets[].y` | integer | Yes | Y position in mm |
| `targets[].speed` | integer | Yes | Speed in cm/s |
| `targets[].conf` | integer | Yes | Confidence 0-100 |

### 5.7 Device State (Device → Cloud)

See: `SCHEMA_DEVICE_STATE.json`

```json
{
  "online": true,
  "firmware_version": "1.2.0",
  "uptime_sec": 86400,
  "wifi_rssi": -55,
  "last_seen": "2026-01-15T10:00:00Z"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `online` | boolean | Yes | Device connectivity status |
| `firmware_version` | string | Yes | Current firmware version (semver) |
| `uptime_sec` | integer | Yes | Device uptime in seconds |
| `wifi_rssi` | integer | Yes | Wi-Fi signal strength (dBm) |
| `last_seen` | string | Yes | ISO 8601 timestamp of last message |

### 5.8 Diagnostics Request (Cloud → Device)

```json
{
  "request_id": "diag-123456",
  "include": ["heap", "tasks", "config", "logs"]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `request_id` | string | Yes | Unique request identifier |
| `include` | array | No | Sections to include (default: all) |

### 5.9 Diagnostics Response (Device → Cloud)

```json
{
  "request_id": "diag-123456",
  "timestamp": "2026-01-15T10:00:00Z",
  "diagnostics": {
    "heap": {
      "free": 45000,
      "min_free": 32000,
      "largest_block": 28000
    },
    "tasks": [
      {"name": "main", "stack_free": 2048, "priority": 5}
    ],
    "config": {
      "zone_count": 4,
      "config_version": 3
    },
    "logs": [
      {"ts": 12345, "lvl": "I", "tag": "MAIN", "msg": "Boot complete"}
    ]
  }
}
```

### 5.10 Provisioning Request (Device → Cloud)

```json
{
  "device_id": "a1b2c3d4e5f600112233445566778899",
  "mac_address": "AA:BB:CC:D4:E5:F6",
  "firmware_version": "1.0.0",
  "timestamp": "2026-01-13T10:00:00Z"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `device_id` | string | Yes | 32-char hex device identifier (SHA-256 derived from MAC) |
| `mac_address` | string | Yes | Full MAC address (AA:BB:CC:DD:EE:FF) |
| `firmware_version` | string | Yes | Current firmware version (semver) |
| `timestamp` | string | Yes | ISO 8601 timestamp |

Cloud uses `mac_address` to lookup owner in purchase records database.

### 5.11 Provisioning Response (Cloud → Device)

**Success (owner found via purchase records):**
```json
{
  "status": "registered",
  "device_id": "a1b2c3d4e5f600112233445566778899",
  "owner": "user_abc123",
  "timestamp": "2026-01-13T10:00:01Z"
}
```

**Success (no owner - device not in purchase records):**
```json
{
  "status": "registered",
  "device_id": "a1b2c3d4e5f600112233445566778899",
  "owner": null,
  "timestamp": "2026-01-13T10:00:01Z"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `status` | enum | Yes | `registered` (provisioning always succeeds) |
| `device_id` | string | Yes | 32-char hex device identifier |
| `owner` | string | No | User ID if found in purchase records (null otherwise) |
| `timestamp` | string | Yes | ISO 8601 timestamp |

**Note:** Provisioning no longer fails. Unowned devices work locally and can be claimed later via support.

---

## 6. QoS and Retention Policy

| Topic Category | QoS | Retain | Rationale |
|----------------|-----|--------|-----------|
| OTA trigger | 1 | No | Must be delivered exactly once |
| OTA status | 1 | Yes | Latest status queryable |
| Telemetry | 0 | No | Loss acceptable, high frequency |
| Diagnostics | 1 | No | Request-response pattern |
| Config update | 1 | No | Must be delivered |
| Config status | 1 | Yes | Latest status queryable |
| Target stream | 0 | No | Real-time, loss acceptable |
| Device state | 1 | Yes | LWT and status queryable |
| Provisioning | 1 | No | Must be delivered, one-time |

---

## 7. Last Will and Testament (LWT)

Devices MUST configure LWT on connect:

- **Topic**: `opticworks/{device_id}/state`
- **Payload**:
  ```json
  {
    "online": false,
    "firmware_version": "1.2.0",
    "last_seen": "2026-01-15T10:00:00Z"
  }
  ```
- **QoS**: 1
- **Retain**: Yes

The LWT payload MUST include `firmware_version` to match the device state schema. This ensures the cloud can track which firmware version went offline.

This ensures the cloud knows when a device disconnects unexpectedly.

---

## 8. ACL Rules

MQTT broker MUST enforce the following ACLs:

| Principal | Allowed Topics | Actions |
|-----------|----------------|---------|
| Device `{id}` | `opticworks/{id}/#` | Subscribe, Publish |
| Cloud services | `opticworks/+/#` | Subscribe, Publish |
| Zone Editor (user) | `opticworks/{owned_devices}/#` | Subscribe only |

Devices MUST NOT be able to:
- Subscribe to other devices' topics
- Publish to `opticworks/+/ota/trigger` (cloud only)
- Publish to `opticworks/+/config/update` (cloud only)

---

## 9. Rate Limits

| Topic | Max Rate | Action on Exceed |
|-------|----------|------------------|
| Target stream | 15 msg/sec | Drop oldest |
| Telemetry | 1 msg/min | Batch locally |
| OTA status | 10 msg/update | Throttle |
| Diagnostics | 1 req/10sec | Reject |

### 9.1 Rate Limit Enforcement

Rate limits are enforced **on the device side** using a token bucket algorithm:

```c
typedef struct {
    uint32_t tokens;           // Current token count
    uint32_t max_tokens;       // Bucket capacity
    uint32_t refill_rate_ms;   // Time to add one token
    uint32_t last_refill_ms;   // Last refill timestamp
} rate_limiter_t;

bool rate_limit_allow(rate_limiter_t *rl) {
    uint32_t now = timebase_uptime_ms();
    uint32_t elapsed = now - rl->last_refill_ms;
    uint32_t new_tokens = elapsed / rl->refill_rate_ms;

    if (new_tokens > 0) {
        rl->tokens = MIN(rl->max_tokens, rl->tokens + new_tokens);
        rl->last_refill_ms = now;
    }

    if (rl->tokens > 0) {
        rl->tokens--;
        return true;  // Allow message
    }
    return false;  // Rate limited
}
```

**Rationale:** Device-side enforcement prevents unnecessary network traffic and broker load. The broker MAY additionally enforce limits as a safety net.

---

## 10. Versioning

### 10.1 Protocol Version

This document defines Protocol Version **1.0**. Future breaking changes will increment the major version.

### 10.2 Backward Compatibility

- New optional fields MAY be added without version change
- Removed fields MUST trigger major version increment
- Devices SHOULD ignore unknown fields

### 10.3 Version Negotiation

Devices publish protocol version in device state:

```json
{
  "online": true,
  "protocol_version": "1.0",
  "firmware_version": "1.2.0"
}
```

Cloud services MUST support protocol versions for at least 2 major firmware versions.

---

## 11. References

| Document | Purpose |
|----------|---------|
| `PROTOCOL_PROVISIONING.md` | Complete provisioning protocol |
| `SCHEMA_ZONE_CONFIG.json` | JSON Schema for zone configuration |
| `SCHEMA_OTA_MANIFEST.json` | JSON Schema for OTA triggers |
| `SCHEMA_TELEMETRY.json` | JSON Schema for telemetry payloads |
| `SCHEMA_DEVICE_STATE.json` | JSON Schema for device state |
| `MOCK_BOUNDARIES.md` | Testing with contract schemas |
| `../firmware/HARDWAREOS_MODULE_SECURITY.md` | MQTT authentication details |

---

## 12. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-09 | OpticWorks | Initial draft. Consolidates topics from M07, M09, M11. Addresses RFD-001 issue C14. |
