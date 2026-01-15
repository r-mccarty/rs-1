# HardwareOS IAQ Module Specification (M12)

Version: 0.1
Date: 2026-01-15
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Manage the optional IAQ (Indoor Air Quality) daughterboard module. This module handles:
- Auto-detection of the IAQ module via I2C bus polling
- ENS160 sensor initialization and calibration
- Periodic TVOC, eCO2, and AQI readings
- Publishing air quality data to the Native API (M05)
- Handling module hot-plug events (attach/detach during operation)

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | ENS160 uses I2C address 0x52 or 0x53 | Detection logic, address scanning |
| A2 | IAQ module is optional; device must function without it | Graceful degradation, null checks |
| A3 | I2C bus is shared with AHT20 (0x38) and LTR-303 (0x29) | Bus arbitration, timing constraints |
| A4 | ENS160 requires 48-hour conditioning for accurate readings | Initial accuracy warning, calibration state |
| A5 | Module can be attached/detached at runtime | Hot-plug detection, entity availability |
| A6 | OTA firmware unlock is required for IAQ features | Entitlement check before enabling sensors |
| A7 | ESP32-WROOM-32E is the target MCU | I2C peripheral, memory constraints |
| A8 | Reading interval: 1 second (sensor limitation) | Polling rate, power consumption |

## 3. Inputs

### 3.1 Hardware Interface

| Parameter | Value |
|-----------|-------|
| Interface | I2C (shared bus with environmental sensors) |
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| Speed | 100 kHz (standard) or 400 kHz (fast) |
| Pull-ups | External, on RS-1 main board |
| Address | 0x52 (default) or 0x53 (ADDR pin high) |

### 3.2 Sensor Registers (ENS160)

| Register | Address | Description |
|----------|---------|-------------|
| PART_ID | 0x00 | Part identification (expect 0x0160) |
| OPMODE | 0x10 | Operating mode register |
| DATA_TVOC | 0x22 | TVOC reading (2 bytes, ppb) |
| DATA_ECO2 | 0x24 | eCO2 reading (2 bytes, ppm) |
| DATA_AQI | 0x21 | Air Quality Index (1-5) |
| DATA_STATUS | 0x20 | Data status and validity flags |

### 3.3 Configuration Inputs

```c
typedef struct {
    uint8_t i2c_address;        // 0x52 or 0x53
    uint16_t poll_interval_ms;  // Reading interval (default 1000)
    bool auto_detect;           // Enable detection polling
    uint16_t detect_interval_s; // Detection poll interval (default 5)
} iaq_config_t;
```

## 4. Outputs

### 4.1 IAQ Data Structure

```c
typedef enum {
    IAQ_STATUS_NOT_DETECTED = 0,    // Module not present
    IAQ_STATUS_DETECTED,            // Module present, not licensed
    IAQ_STATUS_INITIALIZING,        // Warming up (3 min)
    IAQ_STATUS_CONDITIONING,        // First 48 hours
    IAQ_STATUS_READY,               // Normal operation
    IAQ_STATUS_ERROR                // Sensor error
} iaq_status_t;

typedef struct {
    uint16_t tvoc_ppb;          // Total Volatile Organic Compounds (0-65000)
    uint16_t eco2_ppm;          // Equivalent CO2 (400-65000)
    uint8_t aqi_uba;            // UBA Air Quality Index (1-5)
    uint8_t data_validity;      // ENS160 status register
    iaq_status_t status;        // Module/sensor status
    uint32_t timestamp_ms;      // Reading timestamp
    bool licensed;              // OTA entitlement verified
} iaq_reading_t;
```

### 4.2 AQI Interpretation

| AQI Value | Level | Description |
|-----------|-------|-------------|
| 1 | Excellent | Clean air, no action needed |
| 2 | Good | Acceptable air quality |
| 3 | Moderate | Some pollutants present |
| 4 | Poor | Health effects possible |
| 5 | Unhealthy | Immediate action recommended |

### 4.3 Home Assistant Entities

When IAQ module is detected and licensed:

| Entity | Type | Unit | Notes |
|--------|------|------|-------|
| `sensor.rs1_tvoc` | sensor | ppb | Total VOCs |
| `sensor.rs1_eco2` | sensor | ppm | Equivalent CO2 |
| `sensor.rs1_aqi` | sensor | - | 1-5 scale |
| `binary_sensor.rs1_iaq_module` | binary_sensor | - | Module presence |

## 5. Processing Pipeline

### 5.1 State Machine

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        IAQ Module State Machine                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│    ┌──────────────┐                                                     │
│    │   DISABLED   │◄─────────────────────────────────────┐              │
│    │  (No module) │                                      │              │
│    └──────┬───────┘                                      │              │
│           │                                              │              │
│           │ I2C scan finds ENS160                        │              │
│           ▼                                              │              │
│    ┌──────────────┐                                      │              │
│    │   DETECTED   │                                      │              │
│    │ (Unlicensed) │─────────────────────┐                │              │
│    └──────┬───────┘                     │                │              │
│           │                             │                │              │
│           │ Entitlement verified        │ Module removed │              │
│           ▼                             │                │              │
│    ┌──────────────┐                     │                │              │
│    │ INITIALIZING │                     │                │              │
│    │  (Warm-up)   │                     │                │              │
│    └──────┬───────┘                     │                │              │
│           │                             │                │              │
│           │ 3 minutes elapsed           │                │              │
│           ▼                             │                │              │
│    ┌──────────────┐                     │                │              │
│    │ CONDITIONING │                     │                │              │
│    │  (48 hours)  │                     │                │              │
│    └──────┬───────┘                     │                │              │
│           │                             │                │              │
│           │ Baseline established        │                │              │
│           ▼                             ▼                │              │
│    ┌──────────────┐              ┌──────────────┐        │              │
│    │    READY     │◄────────────►│    ERROR     │────────┴──────────────│
│    │  (Normal)    │              │              │      Module removed   │
│    └──────────────┘              └──────────────┘                       │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.2 Detection Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        Module Detection Flow                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   On Boot / Every 5 seconds:                                             │
│                                                                          │
│   1. Scan I2C for ENS160 at 0x52                                        │
│      ├── Found → Continue to step 2                                     │
│      └── Not found → Scan 0x53                                          │
│          ├── Found → Continue to step 2                                 │
│          └── Not found → Set status = NOT_DETECTED                      │
│                                                                          │
│   2. Read PART_ID register                                               │
│      ├── Returns 0x0160 → Module valid                                  │
│      └── Other value → Set status = ERROR                               │
│                                                                          │
│   3. Check entitlement (local cache or cloud)                           │
│      ├── Licensed → Set status = INITIALIZING                           │
│      │              Start warm-up timer (3 min)                          │
│      │              Trigger OTA check for IAQ firmware                  │
│      └── Not licensed → Set status = DETECTED (unlicensed)              │
│                         Log: "IAQ module detected, license required"    │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.3 Reading Flow

```c
// Called every 1 second when status == READY
void iaq_read_sensors(iaq_reading_t *reading) {
    // 1. Read status register
    uint8_t status = i2c_read_reg(ENS160_ADDR, REG_DATA_STATUS);

    // 2. Check if new data available
    if (!(status & DATA_VALID_FLAG)) {
        reading->data_validity = status;
        return; // No new data
    }

    // 3. Read TVOC (2 bytes, little-endian)
    reading->tvoc_ppb = i2c_read_reg16(ENS160_ADDR, REG_DATA_TVOC);

    // 4. Read eCO2 (2 bytes, little-endian)
    reading->eco2_ppm = i2c_read_reg16(ENS160_ADDR, REG_DATA_ECO2);

    // 5. Read AQI
    reading->aqi_uba = i2c_read_reg(ENS160_ADDR, REG_DATA_AQI);

    // 6. Timestamp
    reading->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // 7. Publish to M05 (Native API)
    native_api_publish_iaq(reading);
}
```

## 6. Entitlement and OTA Unlock

### 6.1 Entitlement Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        IAQ Entitlement Flow                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   1. Module detected (ENS160 responds on I2C)                           │
│                                                                          │
│   2. Check local NVS for cached entitlement                             │
│      ├── Found + Valid → Skip to step 5                                 │
│      └── Not found / Expired → Continue to step 3                       │
│                                                                          │
│   3. Query cloud for IAQ entitlement                                    │
│      MQTT: device/{device_id}/entitlement/request                       │
│      Payload: { "feature": "iaq", "module_id": "<ENS160 serial>" }      │
│                                                                          │
│   4. Receive entitlement response                                        │
│      MQTT: device/{device_id}/entitlement/response                      │
│      Payload: { "feature": "iaq", "granted": true/false,                │
│                 "expires": "<ISO8601>", "firmware_url": "<url>" }       │
│                                                                          │
│   5. If granted:                                                         │
│      a. Cache entitlement in NVS                                        │
│      b. If firmware_url provided → Trigger M07 (OTA) for IAQ update    │
│      c. Enable sensor readings                                          │
│                                                                          │
│   6. If not granted:                                                     │
│      a. Log: "IAQ module not licensed"                                  │
│      b. Keep module in DETECTED state                                   │
│      c. Expose binary_sensor for module presence (allows purchase)      │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.2 Entitlement Cache

| Field | Type | Description |
|-------|------|-------------|
| feature | string | "iaq" |
| granted | bool | License valid |
| expires | uint32 | Unix timestamp (0 = perpetual) |
| checked_at | uint32 | Last cloud check timestamp |

Cache expiry: 7 days (re-check with cloud periodically)

## 7. Hot-Plug Handling

### 7.1 Module Attach (Runtime)

1. I2C scan detects ENS160 (previously not present)
2. Log: "IAQ module attached"
3. Check entitlement (may trigger OTA)
4. If licensed: Begin warm-up sequence
5. After warm-up: Start publishing readings
6. Home Assistant entities become available

### 7.2 Module Detach (Runtime)

1. I2C read fails (NACK or timeout)
2. Retry 3 times with 100ms delay
3. If still failing:
   - Log: "IAQ module detached"
   - Set status = NOT_DETECTED
   - Home Assistant entities become unavailable
   - Stop sensor reading task

### 7.3 Entity Availability

```yaml
# Home Assistant entity behavior
sensor.rs1_tvoc:
  availability: "{{ is_state('binary_sensor.rs1_iaq_module', 'on') }}"
```

## 8. Power Management

### 8.1 Power States

| State | ENS160 Mode | Power | Notes |
|-------|-------------|-------|-------|
| NOT_DETECTED | N/A | 0 mA | No module |
| DETECTED | Deep Sleep | 10 µA | Unlicensed, low power |
| INITIALIZING | Standard | 32 mA | Warm-up (3 min) |
| CONDITIONING | Standard | 32 mA | 48-hour calibration |
| READY | Standard | 32 mA | Normal operation |
| IDLE | Low Power | 10 mA | Between readings |

### 8.2 Power Sequencing

- IAQ module shares 3.3V rail with RS-1
- No separate power gating (module too low power to matter)
- Module draws power when attached; hot-plug safe

## 9. Error Handling

### 9.1 I2C Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| NACK on address | Module not present | Scan other address, retry later |
| NACK on data | Register read failed | Retry up to 3 times |
| Timeout | Bus hung | I2C bus reset, re-init |
| Invalid PART_ID | Wrong device | Log error, ignore device |

### 9.2 Sensor Errors

| Condition | Detection | Action |
|-----------|-----------|--------|
| Readings stuck | Same value for 60s | Log warning, continue |
| Out of range | TVOC > 65000 | Clamp value, log warning |
| Status invalid | Status register bits | Log, attempt re-init |

### 9.3 Degraded Mode

If IAQ module fails after being in READY state:

1. Log: "IAQ module error, entering degraded mode"
2. Set status = ERROR
3. Keep last-known readings (with stale flag)
4. Retry initialization every 60 seconds
5. Home Assistant entities show "unavailable" or stale timestamp

## 10. Telemetry

### 10.1 Metrics Published

| Metric | Type | Frequency |
|--------|------|-----------|
| iaq.tvoc | gauge | Every 1s |
| iaq.eco2 | gauge | Every 1s |
| iaq.aqi | gauge | Every 1s |
| iaq.status | enum | On change |
| iaq.uptime_hours | counter | Every 1h |

### 10.2 Diagnostic Events

| Event | Trigger | Payload |
|-------|---------|---------|
| `iaq.module_attached` | Detection | `{ "address": 0x52 }` |
| `iaq.module_detached` | Loss of contact | `{}` |
| `iaq.entitlement_checked` | Cloud query | `{ "granted": true }` |
| `iaq.calibration_complete` | 48h elapsed | `{ "baseline_tvoc": 100 }` |

## 11. Configuration

### 11.1 NVS Keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `iaq.enabled` | bool | true | Enable IAQ detection |
| `iaq.i2c_addr` | uint8 | 0x52 | Primary I2C address |
| `iaq.poll_ms` | uint16 | 1000 | Reading interval |
| `iaq.detect_s` | uint16 | 5 | Detection poll interval |
| `iaq.entitlement` | blob | - | Cached entitlement data |

### 11.2 Runtime Configuration

No runtime configuration changes supported. All config via NVS or cloud.

## 12. Integration with Other Modules

### 12.1 Module Dependencies

```
            ┌──────────┐
            │   M12    │
            │   IAQ    │
            └────┬─────┘
                 │
        ┌────────┼────────┐
        ▼        ▼        ▼
   ┌────────┐ ┌────────┐ ┌────────┐
   │  M05   │ │  M06   │ │  M07   │
   │ Native │ │ Config │ │  OTA   │
   │  API   │ │ Store  │ │        │
   └────────┘ └────────┘ └────────┘
```

| Dependency | Direction | Purpose |
|------------|-----------|---------|
| M05 (Native API) | M12 → M05 | Publish sensor entities |
| M06 (Config Store) | M12 → M06 | Read/write entitlement cache |
| M07 (OTA) | M12 → M07 | Trigger firmware update |
| M08 (Timebase) | M12 → M08 | Timers for polling, warm-up |
| M09 (Logging) | M12 → M09 | Diagnostic logging |

### 12.2 API Surface

```c
// Initialize IAQ module
esp_err_t iaq_init(const iaq_config_t *config);

// Start detection polling
esp_err_t iaq_start_detection(void);

// Get current reading (returns last cached reading)
esp_err_t iaq_get_reading(iaq_reading_t *reading);

// Get module status
iaq_status_t iaq_get_status(void);

// Force entitlement re-check
esp_err_t iaq_check_entitlement(void);

// Shutdown (called on deep sleep or OTA)
esp_err_t iaq_shutdown(void);
```

## 13. Testing

### 13.1 Unit Tests

| Test | Description |
|------|-------------|
| `test_iaq_detection_found` | Verify detection with mock I2C |
| `test_iaq_detection_not_found` | Verify graceful handling when absent |
| `test_iaq_reading_valid` | Verify correct parsing of sensor data |
| `test_iaq_hotplug_attach` | Verify state transition on attach |
| `test_iaq_hotplug_detach` | Verify state transition on detach |
| `test_iaq_entitlement_granted` | Verify licensed flow |
| `test_iaq_entitlement_denied` | Verify unlicensed flow |

### 13.2 Integration Tests

| Test | Description |
|------|-------------|
| `test_iaq_e2e_publish` | Full flow: detect → license → publish |
| `test_iaq_ha_entities` | Verify Home Assistant sees entities |
| `test_iaq_ota_trigger` | Verify OTA triggered on license grant |

## 14. Open Items

### 14.1 Resolved

- [x] Sensor selection: ENS160
- [x] Interface: I2C shared bus
- [x] Detection: Polling at 5s interval
- [x] Licensing: Cloud-based entitlement

### 14.2 Pending

- [ ] Define MQTT topics for entitlement (coordinate with M07/cloud)
- [ ] Determine if 1.8V LDO needed on IAQ module (or 3.3V operation)
- [ ] Implement calibration persistence across power cycles
- [ ] Define telemetry schema for ENS160 readings
- [ ] Coordinate with M05 for entity registration

## 15. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-15 | Firmware Engineering | Initial draft |

## 16. References

| Document | Description |
|----------|-------------|
| ENS160 Datasheet | ScioSense sensor specification |
| IAQ_MODULE_SPEC.md | Hardware module specification |
| HARDWAREOS_MODULE_NATIVE_API.md | M05 - Entity publishing |
| HARDWAREOS_MODULE_OTA.md | M07 - Firmware updates |
| HARDWAREOS_MODULE_CONFIG_STORE.md | M06 - NVS persistence |
