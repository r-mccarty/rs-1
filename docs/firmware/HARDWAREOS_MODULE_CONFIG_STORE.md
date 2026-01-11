# HardwareOS Device Config Store Module Specification (M06)

Version: 0.2
Date: 2026-01-09
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Provide persistent, atomic, and versioned storage for device configuration including zones, sensitivity settings, calibration data, and network credentials. This module abstracts NVS (Non-Volatile Storage) operations and ensures data integrity across power cycles and OTA updates.

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | ESP-IDF NVS library is the storage backend | API surface, wear leveling |
| A2 | ESP32-C3 has 4MB flash with ~16KB NVS partition | Storage limits |
| A3 | Zone config is < 4KB total | Single NVS blob vs chunking |
| A4 | Config updates are infrequent (< 10/day); NVS commits only on actual changes | Flash wear (~100K cycles = 27 years at 10/day) |
| A5 | Atomic updates are required (no partial writes) | Transaction implementation |
| A6 | Config versioning enables rollback | Version schema |
| A7 | Factory reset clears all config | Reset behavior |

## 3. Data Domains

### 3.1 Zone Configuration

```c
#define MAX_ZONES 16
#define MAX_VERTICES 8

typedef struct {
    char id[16];
    char name[32];
    zone_type_t type;           // INCLUDE, EXCLUDE
    int16_t vertices[MAX_VERTICES][2];
    uint8_t vertex_count;
    uint8_t sensitivity;
} zone_config_t;

typedef struct {
    uint32_t version;           // Incrementing version
    uint32_t updated_at;        // Unix timestamp
    zone_config_t zones[MAX_ZONES];
    uint8_t zone_count;
    uint16_t checksum;          // CRC16 for integrity
} zone_store_t;
```

**Storage Key**: `zones`
**Max Size**: ~4KB

### 3.2 Device Settings

```c
typedef struct {
    char device_name[32];       // mDNS name
    char friendly_name[48];     // Display name
    uint8_t default_sensitivity;// Global sensitivity (0-100)
    bool telemetry_enabled;     // Opt-in telemetry
    uint16_t state_throttle_ms; // API update throttle
} device_settings_t;
```

**Storage Key**: `device`
**Max Size**: ~256 bytes

### 3.3 Network Configuration

```c
typedef struct {
    char ssid[33];              // Wi-Fi SSID
    char password[65];          // Wi-Fi password (encrypted at rest)
    bool static_ip;             // Use static IP
    uint32_t ip_addr;           // Static IP (if enabled)
    uint32_t gateway;           // Gateway (if static)
    uint32_t subnet;            // Subnet mask (if static)
    uint32_t dns;               // DNS server (if static)
} network_config_t;
```

**Storage Key**: `network`
**Max Size**: ~256 bytes

### 3.4 Security Configuration

```c
typedef struct {
    char api_password[33];      // Legacy API password
    uint8_t encryption_key[32]; // Noise PSK
    bool encryption_enabled;    // Require encryption
    uint8_t pairing_token[16];  // Local pairing token
} security_config_t;
```

**Storage Key**: `security`
**Max Size**: ~128 bytes

### 3.5 Calibration Data

```c
typedef struct {
    int16_t x_offset_mm;        // Radar X offset correction
    int16_t y_offset_mm;        // Radar Y offset correction
    float rotation_deg;         // Rotation correction
    uint8_t mounting_type;      // WALL, CEILING, CUSTOM
    uint32_t calibrated_at;     // Calibration timestamp
} calibration_t;
```

**Storage Key**: `calibration`
**Max Size**: ~64 bytes

## 4. API Interface

### 4.1 Read Operations

```c
// Read zone configuration
esp_err_t config_get_zones(zone_store_t *out);

// Read specific zone by ID
esp_err_t config_get_zone(const char *zone_id, zone_config_t *out);

// Read device settings
esp_err_t config_get_device(device_settings_t *out);

// Read network config
esp_err_t config_get_network(network_config_t *out);

// Read calibration
esp_err_t config_get_calibration(calibration_t *out);
```

### 4.2 Write Operations

```c
// Write zone configuration (atomic, versioned)
esp_err_t config_set_zones(const zone_store_t *zones);

// Write device settings
esp_err_t config_set_device(const device_settings_t *settings);

// Write network config
esp_err_t config_set_network(const network_config_t *network);

// Write calibration
esp_err_t config_set_calibration(const calibration_t *calibration);
```

### 4.3 Version Operations

```c
// Get current zone config version
uint32_t config_get_zone_version(void);

// Rollback zones to previous version (if stored)
esp_err_t config_rollback_zones(void);

// Check if rollback is available
bool config_has_zone_rollback(void);
```

### 4.4 Maintenance Operations

```c
// Factory reset (erase all config)
esp_err_t config_factory_reset(void);

// Erase specific domain
esp_err_t config_erase(const char *key);

// Get storage statistics
esp_err_t config_get_stats(config_stats_t *stats);
```

## 5. NVS Commit Policy

**Critical:** NVS commits MUST only occur on actual configuration changes. Never use automatic periodic commits.

### 5.1 Flash Wear Analysis

| Commit Strategy | Commits/Day | Flash Lifetime |
|-----------------|-------------|----------------|
| Periodic (60s) | 1,440 | **69 days** (unacceptable) |
| Periodic (1hr) | 24 | 11.4 years |
| On-change only | < 10 | **27+ years** (target) |

### 5.2 Commit Triggers

NVS is committed only when:
- User saves zone configuration (via M11)
- User changes device settings
- Network credentials are updated
- OTA completes successfully
- Factory reset is initiated

### 5.3 Commit-on-Change Implementation

```c
// CORRECT: Commit only after actual write
esp_err_t config_set_zones(const zone_store_t *zones) {
    // ... validation and write ...
    nvs_commit(nvs_handle);  // Commit immediately after change
    return ESP_OK;
}

// INCORRECT: Never do periodic commits
// void nvs_periodic_commit(void) {  // DO NOT IMPLEMENT
//     nvs_commit(nvs_handle);
// }
```

---

## 6. Atomic Writes

### 6.1 Transaction Flow

```
1. Validate incoming config (schema, bounds, checksum)
2. Write to shadow key (e.g., "zones_new")
3. Verify shadow write (read back and compare)
4. Copy current to backup (e.g., "zones_prev")
5. Rename shadow to primary
6. Commit NVS
```

### 5.2 Recovery on Boot

```c
void config_init(void) {
    // Check for incomplete transaction
    if (nvs_key_exists("zones_new")) {
        // Interrupted write - discard shadow
        nvs_erase_key("zones_new");
    }

    // Validate primary config
    if (!config_validate("zones")) {
        // Corrupted - attempt rollback
        if (config_has_zone_rollback()) {
            config_rollback_zones();
        } else {
            // No backup - initialize defaults
            config_init_defaults();
        }
    }
}
```

## 6. Versioning

### 6.1 Version Increment

Every zone config write increments the version:

```c
esp_err_t config_set_zones(const zone_store_t *zones) {
    zone_store_t versioned = *zones;
    versioned.version = config_get_zone_version() + 1;
    versioned.updated_at = time(NULL);
    versioned.checksum = crc16(&versioned, offsetof(zone_store_t, checksum));
    // ... atomic write
}
```

### 6.2 Rollback Storage

Keep one previous version for rollback:

| Key | Purpose |
|-----|---------|
| `zones` | Current active config |
| `zones_prev` | Previous version (one level) |
| `zones_new` | Shadow during write (transient) |

## 7. Validation Rules

### 7.1 Zone Validation

| Field | Rule | Error |
|-------|------|-------|
| `id` | Non-empty, alphanumeric + underscore | `ERR_INVALID_ID` |
| `name` | Non-empty, <= 32 chars | `ERR_INVALID_NAME` |
| `vertex_count` | 3 <= count <= 8 | `ERR_VERTEX_COUNT` |
| `vertices` | Within sensor range | `ERR_OUT_OF_RANGE` |
| `checksum` | CRC16 matches | `ERR_CHECKSUM` |

### 7.2 Network Validation

| Field | Rule | Error |
|-------|------|-------|
| `ssid` | Non-empty, <= 32 chars | `ERR_INVALID_SSID` |
| `password` | 8-64 chars (if WPA) | `ERR_INVALID_PASSWORD` |
| `ip_addr` | Valid IPv4 (if static) | `ERR_INVALID_IP` |

## 8. Encryption at Rest

### 8.1 Sensitive Fields

| Field | Storage |
|-------|---------|
| `network.password` | Encrypted with device key |
| `security.api_password` | Encrypted with device key |
| `security.encryption_key` | Encrypted with device key |

### 8.2 Device Key

- Derived from eFuse unique ID.
- Never leaves device.
- Used for AES-128 encryption of sensitive blobs.

```c
void derive_device_key(uint8_t key[16]) {
    uint8_t efuse_id[6];
    esp_efuse_mac_get_default(efuse_id);
    // HKDF or similar derivation
    hkdf_sha256(efuse_id, 6, "config_key", key, 16);
}
```

## 9. Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `nvs_namespace` | string | "rs1" | NVS namespace |
| `max_zone_size` | uint16 | 4096 | Max zone blob size |
| `enable_rollback` | bool | true | Keep previous zone version |
| `encrypt_sensitive` | bool | true | Encrypt passwords/keys |

## 10. Performance Requirements

| Metric | Requirement | Rationale |
|--------|-------------|-----------|
| Read latency | < 10ms | Fast boot config load |
| Write latency | < 100ms | Acceptable for config saves |
| Boot init time | < 50ms | Config validation on boot |
| Flash writes/day | < 100 | NVS wear (~100K cycles) |

## 11. Storage Budget

| Domain | Max Size | Notes |
|--------|----------|-------|
| Zones | 4KB | 16 zones × ~256 bytes |
| Zones (prev) | 4KB | Rollback copy |
| Device | 256B | Settings |
| Network | 256B | Wi-Fi credentials |
| Security | 128B | Keys and passwords |
| Calibration | 64B | Mounting offsets |
| **Total** | ~9KB | Well within 16KB NVS |

## 12. Error Codes

```c
typedef enum {
    CONFIG_OK = 0,
    CONFIG_ERR_NOT_FOUND,       // Key doesn't exist
    CONFIG_ERR_INVALID,         // Validation failed
    CONFIG_ERR_CHECKSUM,        // CRC mismatch
    CONFIG_ERR_FULL,            // NVS full
    CONFIG_ERR_FLASH,           // Flash write failed
    CONFIG_ERR_ROLLBACK_UNAVAIL,// No previous version
} config_err_t;
```

## 13. Telemetry (M09 Integration)

| Metric | Type | Description |
|--------|------|-------------|
| `config.writes_total` | Counter | Total config writes |
| `config.rollbacks` | Counter | Rollback operations |
| `config.validation_failures` | Counter | Rejected config updates |
| `config.nvs_used_bytes` | Gauge | NVS usage |

## 14. Testing Strategy

### 14.1 Unit Tests

- Serialize/deserialize all config types.
- Validation logic for all rules.
- CRC16 computation.

### 14.2 Integration Tests

- Power-cycle during write → verify recovery.
- Fill NVS → verify graceful failure.
- Rollback flow end-to-end.

### 14.3 Stress Tests

- 10,000 write cycles → verify no corruption.
- Random power interrupts during writes.

## 15. Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| ESP-IDF NVS | 5.x | Storage backend |
| mbedTLS | 3.x | Encryption (AES-128) |

## 16. Open Questions

- Should we support multiple rollback levels (>1 previous)?
- Cloud backup/restore of zone configuration?
- Migration path when schema changes between firmware versions?

---

## 17. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-XX | OpticWorks | Initial draft |
| 0.2 | 2026-01-09 | OpticWorks | Added NVS commit policy section, clarified assumption A4. Addresses RFD-001 issue C5 (NVS wear). |
