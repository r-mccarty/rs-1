# RS-1 Integration Test Specification

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks Firmware + QA
Status: Draft

---

## 1. Purpose

This document defines integration test scenarios that exercise interactions between multiple HardwareOS modules. These tests verify that the modules work together correctly as a system.

**This document addresses RFD-001 issue C15: No integration test scenarios.**

---

## 2. Test Categories

| Category | Description | Modules Involved |
|----------|-------------|------------------|
| Pipeline | Detection → Tracking → Zone → Smoothing → API | M01, M02, M03, M04, M05 |
| OTA | Update trigger → Download → Verify → Rollback | M07, M10, M08 |
| Configuration | Zone edit → Store → Apply → Verify | M06, M11, M03 |
| Recovery | Failure → Detection → Recovery → Resume | M01, M08, M09 |
| Cloud Sync | Device ↔ Cloud communication | M07, M09, M11, M05 |

---

## 3. Pipeline Integration Tests

### 3.1 Basic Presence Detection

**Scenario:** Single target enters detection zone and triggers occupancy.

```
Preconditions:
- Device booted and healthy
- One include zone configured ("zone_living")
- No targets currently detected

Test Steps:
1. Inject simulated radar frame with 1 target at (1000, 2000) mm
2. Wait 3 frames (90ms at 33 Hz)
3. Verify M02 creates confirmed track
4. Verify M03 reports zone_living occupied
5. Verify M04 reports smoothed occupancy=true
6. Verify M05 publishes binary_sensor state=ON to HA

Expected Results:
- Track created within 3 frames
- Zone occupancy within 1 frame of track confirmation
- HA entity update within 100ms of zone occupancy change
```

**Modules Tested:** M01 → M02 → M03 → M04 → M05

---

### 3.2 Multi-Zone Detection

**Scenario:** Target moves between two zones.

```
Preconditions:
- Two include zones configured ("zone_kitchen", "zone_dining")
- zone_kitchen: (0,0) to (2000,2000) mm
- zone_dining: (2000,0) to (4000,2000) mm

Test Steps:
1. Inject target at (1000, 1000) mm - inside zone_kitchen
2. Wait for zone_kitchen occupancy=true
3. Inject target moving to (3000, 1000) mm - inside zone_dining
4. Verify zone_kitchen occupancy transitions to false (after hold time)
5. Verify zone_dining occupancy transitions to true
6. Verify track ID remains consistent during movement

Expected Results:
- Both zones report correct occupancy
- Track ID preserved across zone boundary
- No duplicate tracks created
- Hold time respected for zone_kitchen exit
```

**Modules Tested:** M01 → M02 → M03 → M04

---

### 3.3 Occlusion Recovery

**Scenario:** Target becomes temporarily undetectable and reappears.

```
Preconditions:
- Target being tracked at (1500, 2000) mm
- Track state = CONFIRMED

Test Steps:
1. Stop injecting radar frames with target (simulate occlusion)
2. Wait 500ms
3. Verify track enters OCCLUDED state
4. Verify zone occupancy remains true (within hold time)
5. Re-inject target at (1600, 2100) mm
6. Verify track resumes CONFIRMED state
7. Verify same track_id maintained

Expected Results:
- Track not retired during 500ms occlusion
- Position prediction reasonably accurate
- Confidence degrades during occlusion
- No zone flicker during occlusion
```

**Modules Tested:** M02 → M03 → M04

---

### 3.4 Maximum Target Capacity

**Scenario:** System handles 3 simultaneous targets (LD2450 limit).

```
Preconditions:
- Large zone covering entire detection area
- No targets currently detected

Test Steps:
1. Inject 3 targets at distinct locations
2. Verify 3 tracks created and confirmed
3. Verify zone target_count = 3
4. Inject 4th target (should be ignored by radar, but test parser robustness)
5. Remove 1 target
6. Verify track retirement and target_count = 2

Expected Results:
- All 3 tracks maintained independently
- No track ID collisions
- Graceful handling of capacity limit
```

**Modules Tested:** M01 → M02 → M03

---

## 4. OTA Integration Tests

### 4.1 Successful OTA Update

**Scenario:** Device receives OTA trigger, downloads, verifies, and reboots to new firmware.

```
Preconditions:
- Device on firmware v1.0.0
- MQTT connected to broker
- Wi-Fi RSSI > -70 dBm

Test Steps:
1. Publish OTA trigger for v1.1.0 to MQTT topic
2. Verify device publishes status "downloading"
3. Wait for download (mock HTTP server returns test firmware)
4. Verify device publishes status "verifying"
5. Verify device publishes status "success"
6. Device reboots
7. After reboot, verify firmware version is 1.1.0

Expected Results:
- Status updates published at each stage
- No interruption to other device functions during download
- Clean reboot with new firmware active
- OTA partition marked valid after successful boot
```

**Modules Tested:** M07 → M10 (signature) → M08 (watchdog pause)

---

### 4.2 OTA Rollback on Failure

**Scenario:** New firmware fails to boot properly, triggering automatic rollback.

```
Preconditions:
- Device on firmware v1.0.0
- OTA to v1.1.0 (corrupted/invalid)

Test Steps:
1. Trigger OTA with intentionally broken firmware
2. Verify signature validation fails OR
3. If signature passes, verify boot fails (panic/watchdog)
4. Verify device rolls back to v1.0.0
5. Verify device reports rollback event in telemetry

Expected Results:
- Rollback within 2 boot attempts
- Device returns to stable v1.0.0
- Rollback logged to M09
- Device does not re-attempt same OTA without new trigger
```

**Modules Tested:** M07 → M08 (rollback detection)

---

### 4.3 OTA Gating by RSSI

**Scenario:** OTA is deferred when Wi-Fi signal is too weak.

```
Preconditions:
- Device RSSI = -75 dBm
- OTA trigger with min_rssi = -70 dBm

Test Steps:
1. Publish OTA trigger
2. Verify device does NOT start download
3. Verify device publishes status "deferred" with reason "rssi"
4. Improve RSSI to -65 dBm
5. Verify device starts download

Expected Results:
- RSSI check performed before download
- Clear deferral reason in status
- Automatic retry when conditions improve
```

**Modules Tested:** M07 → M05 (RSSI from system)

---

## 5. Configuration Integration Tests

### 5.1 Zone Config Update (Local)

**Scenario:** User updates zone configuration via local API.

```
Preconditions:
- Device has zone config v1 with 2 zones
- Valid session token obtained via pairing

Test Steps:
1. POST new zone config v2 with 3 zones to /api/zones
2. Verify HTTP 200 response
3. Verify M06 persists config to NVS
4. Verify M03 uses new zone definitions immediately
5. Verify target in new zone triggers occupancy
6. Reboot device
7. Verify config v2 persists across reboot

Expected Results:
- Config applied atomically
- No presence flicker during config update
- Config survives reboot
- Version number incremented
```

**Modules Tested:** M11 → M06 → M03

---

### 5.2 Zone Config Conflict Resolution

**Scenario:** Config update rejected due to version conflict.

```
Preconditions:
- Device has zone config v5
- Two clients have stale config v4

Test Steps:
1. Client A posts config with "expected_version": 4
2. Verify rejection with "version_conflict" error
3. Client A fetches current config v5
4. Client A posts config with "expected_version": 5
5. Verify config accepted, now v6

Expected Results:
- Optimistic locking prevents data loss
- Clear error message for conflict
- Retry with fresh version succeeds
```

**Modules Tested:** M11 → M06

---

### 5.3 Invalid Zone Rejection

**Scenario:** Self-intersecting polygon is rejected.

```
Preconditions:
- Device has valid zone config

Test Steps:
1. POST zone config with self-intersecting polygon
2. Verify HTTP 400 response with validation error
3. Verify existing config unchanged

Expected Results:
- Invalid polygon detected
- Specific error message (self-intersection)
- Existing zones not affected
```

**Modules Tested:** M11 → M03 (validation)

---

## 6. Recovery Integration Tests

### 6.1 Radar Disconnect Recovery

**Scenario:** Radar cable disconnected and reconnected.

```
Preconditions:
- Device operating normally with radar data

Test Steps:
1. Stop injecting radar frames (simulate disconnect)
2. Wait 3 seconds
3. Verify radar_state = DISCONNECTED
4. Verify watchdog does NOT reset device
5. Verify M09 logs "radar_disconnected" event
6. Resume injecting radar frames
7. Verify radar_state = CONNECTED
8. Verify tracking resumes

Expected Results:
- No watchdog reset loop
- Clear state transition logged
- Graceful resume when radar returns
- Zones report no occupancy during disconnect
```

**Modules Tested:** M01 → M08 → M09

---

### 6.2 Wi-Fi Disconnect Recovery

**Scenario:** Wi-Fi connection lost and restored.

```
Preconditions:
- Device connected to Wi-Fi and MQTT

Test Steps:
1. Disconnect Wi-Fi (mock network failure)
2. Verify local presence detection continues
3. Verify telemetry queued locally
4. Wait 30 seconds
5. Restore Wi-Fi
6. Verify MQTT reconnects
7. Verify queued telemetry flushed

Expected Results:
- Core presence detection unaffected
- HA connection lost but device stable
- Automatic reconnection
- No data loss (within buffer limits)
```

**Modules Tested:** M05 → M09 → (Wi-Fi stack)

---

### 6.3 NVS Corruption Recovery

**Scenario:** NVS partition corrupted, device recovers with defaults.

```
Preconditions:
- Device has custom zone config

Test Steps:
1. Corrupt NVS partition (simulate flash wear)
2. Reboot device
3. Verify NVS initialization detects corruption
4. Verify device boots with factory defaults
5. Verify device logs "nvs_recovery" event
6. User can reconfigure zones

Expected Results:
- Device boots despite NVS corruption
- Factory defaults applied
- User data lost but device functional
- Recovery event logged
```

**Modules Tested:** M06 → M08 → M09

---

## 7. Cloud Sync Integration Tests

### 7.1 Telemetry Upload

**Scenario:** Device uploads periodic telemetry to cloud.

```
Preconditions:
- Telemetry enabled (user opt-in)
- MQTT connected

Test Steps:
1. Trigger telemetry flush
2. Verify MQTT publish to opticworks/{device_id}/telemetry
3. Verify payload matches SCHEMA_TELEMETRY.json
4. Verify metrics include expected keys
5. Verify no PII in payload

Expected Results:
- Valid JSON payload
- Schema-compliant format
- Privacy-safe content
- Correct device_id in topic
```

**Modules Tested:** M09 → (MQTT)

---

### 7.2 Remote Diagnostics

**Scenario:** Cloud requests diagnostics, device responds.

```
Preconditions:
- Device connected to MQTT

Test Steps:
1. Publish diag request to opticworks/{device_id}/diag/request
2. Wait for response on opticworks/{device_id}/diag/response
3. Verify response contains heap, tasks, config info
4. Verify response_id matches request_id

Expected Results:
- Timely response (< 5 seconds)
- Complete diagnostic data
- Request correlation maintained
```

**Modules Tested:** M09 → (MQTT)

---

## 8. End-to-End Test Scenarios

### 8.1 First-Time Setup

**Scenario:** Device is set up for first time by user.

```
Test Steps:
1. Power on new device
2. Device broadcasts AP for provisioning
3. User connects and configures Wi-Fi
4. Device connects to network
5. User opens Zone Editor app
6. App discovers device via mDNS
7. User pairs with device (6-digit code)
8. User draws zones in app
9. Device applies zone config
10. HA discovers device and creates entities
11. User sees presence in HA

Expected Results:
- Complete setup in < 5 minutes
- No errors or manual intervention
- Presence working immediately
```

**Modules Tested:** All

---

### 8.2 Continuous Operation (24-hour)

**Scenario:** Device operates continuously for 24 hours under normal conditions.

```
Test Steps:
1. Deploy device in test room
2. Generate realistic presence patterns
3. Monitor for 24 hours
4. Collect telemetry and logs

Expected Results:
- No memory leaks (heap stable)
- No crashes or watchdog resets
- Presence accuracy maintained
- HA connection stable
- Uptime counter accurate
```

**Modules Tested:** All (soak test)

---

## 9. Test Infrastructure

### 9.1 Simulation Framework

```c
// Radar frame simulator
void inject_target(int16_t x_mm, int16_t y_mm, int16_t speed_cm_s) {
    detection_frame_t frame = {
        .target_count = 1,
        .targets[0] = {
            .x_mm = x_mm,
            .y_mm = y_mm,
            .speed_cm_s = speed_cm_s,
            .valid = true
        },
        .timestamp_ms = timebase_uptime_ms(),
        .frame_seq = next_frame_seq++
    };
    radar_inject_frame(&frame);
}
```

### 9.2 Test Fixtures

| Fixture | Purpose |
|---------|---------|
| `fixtures/zone_config_simple.json` | Single zone |
| `fixtures/zone_config_complex.json` | Multiple overlapping zones |
| `fixtures/radar_walk_path.csv` | Simulated person walking |
| `fixtures/radar_stationary.csv` | Stationary target |
| `fixtures/ota_manifest_valid.json` | Valid OTA trigger |

### 9.3 Test Harness

```bash
# Run integration tests on real hardware
./test/integration/run_tests.sh --target esp32c3 --port /dev/ttyUSB0

# Run integration tests in simulator
./test/integration/run_tests.sh --target simulator
```

---

## 10. Test Coverage Matrix

| Module | Unit Tests | Integration Tests | Hardware Tests |
|--------|------------|-------------------|----------------|
| M01 Radar Ingest | Frame parsing | Pipeline flow | LD2450 actual |
| M02 Tracking | Kalman filter | Multi-target | Long-term |
| M03 Zone Engine | Point-in-polygon | Zone transitions | - |
| M04 Smoothing | Hold time | Occlusion bridge | - |
| M05 Native API | Protobuf encoding | HA discovery | HA actual |
| M06 Config Store | NVS read/write | Config sync | Flash wear |
| M07 OTA | Signature verify | Update + rollback | Real OTA |
| M08 Timebase | Timer accuracy | Watchdog | Long-term |
| M09 Logging | Format, buffer | Telemetry upload | - |
| M10 Security | Crypto | Pairing flow | - |
| M11 Zone Editor | API validation | Local + cloud | App E2E |

---

## 11. References

| Document | Purpose |
|----------|---------|
| `contracts/MOCK_BOUNDARIES.md` | Test mocking strategy |
| `contracts/SCHEMA_*.json` | Message validation |
| `firmware/VALIDATION_PLAN_RS1.md` | Overall validation plan |
