# Mock Boundaries for Testing

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks
Status: Draft

---

## 1. Purpose

This document describes how to use the contract schemas to create mock boundaries between firmware and cloud components, enabling independent testing without real integrations.

---

## 2. Mock Boundary Strategy

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Test Isolation                                  │
│                                                                              │
│  ┌─────────────────────────┐         ┌─────────────────────────┐           │
│  │     Firmware Tests      │         │      Cloud Tests        │           │
│  │                         │         │                         │           │
│  │  ┌─────────────────┐    │         │    ┌─────────────────┐  │           │
│  │  │ Unit Tests      │    │         │    │ Unit Tests      │  │           │
│  │  │ (mock MQTT)     │    │         │    │ (mock devices)  │  │           │
│  │  └────────┬────────┘    │         │    └────────┬────────┘  │           │
│  │           │             │         │             │           │           │
│  │           ▼             │         │             ▼           │           │
│  │  ┌─────────────────┐    │         │    ┌─────────────────┐  │           │
│  │  │ Validate        │    │         │    │ Validate        │  │           │
│  │  │ outgoing msgs   │◄───┼─────────┼────► incoming msgs   │  │           │
│  │  └────────┬────────┘    │         │    └────────┬────────┘  │           │
│  │           │             │         │             │           │           │
│  └───────────┼─────────────┘         └─────────────┼───────────┘           │
│              │                                     │                        │
│              ▼                                     ▼                        │
│  ┌─────────────────────────────────────────────────────────────┐           │
│  │                    contracts/ (JSON Schemas)                 │           │
│  │                                                              │           │
│  │  SCHEMA_ZONE_CONFIG.json    SCHEMA_OTA_MANIFEST.json        │           │
│  │  SCHEMA_TELEMETRY.json      SCHEMA_DEVICE_STATE.json        │           │
│  │                                                              │           │
│  └─────────────────────────────────────────────────────────────┘           │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Key Principle:** Both firmware and cloud validate their messages against the shared schemas. If a message validates against the schema, it's a valid contract participant.

---

## 3. Firmware Testing

### 3.1 Mock MQTT Broker

Firmware tests should mock the MQTT connection and validate outgoing messages:

```c
// test_telemetry.c
void test_telemetry_message_format(void) {
    // Capture outgoing MQTT message
    mqtt_message_t msg = capture_next_publish("opticworks/+/telemetry");

    // Parse JSON
    cJSON *json = cJSON_Parse(msg.payload);

    // Validate required fields per SCHEMA_TELEMETRY.json
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(json, "device_id"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(json, "timestamp"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(json, "metrics"));

    // Validate device_id format (32 hex chars)
    const char *device_id = cJSON_GetStringValue(
        cJSON_GetObjectItem(json, "device_id"));
    TEST_ASSERT_EQUAL(32, strlen(device_id));

    cJSON_Delete(json);
}
```

### 3.2 Generate Test Fixtures from Schemas

Use the JSON schemas to generate valid and invalid test fixtures:

```python
# generate_fixtures.py
import json
from jsonschema import validate, ValidationError

def generate_valid_ota_trigger():
    """Generate a valid OTA trigger fixture."""
    return {
        "version": "1.2.0",
        "url": "https://fw.opticworks.io/rs1/1.2.0.bin",
        "sha256": "a" * 64,
        "rollout_id": "test-rollout-001",
        "issued_at": "2026-01-15T10:00:00Z"
    }

def generate_invalid_ota_trigger_missing_version():
    """Generate an invalid OTA trigger (missing required field)."""
    return {
        "url": "https://fw.opticworks.io/rs1/1.2.0.bin",
        "sha256": "a" * 64,
        "rollout_id": "test-rollout-001",
        "issued_at": "2026-01-15T10:00:00Z"
    }

def validate_fixture(fixture, schema_path):
    """Validate fixture against schema."""
    with open(schema_path) as f:
        schema = json.load(f)
    try:
        validate(instance=fixture, schema=schema)
        return True
    except ValidationError:
        return False
```

### 3.3 Firmware Test Matrix

| Test Category | Mock | Validate Against |
|---------------|------|------------------|
| OTA handling | Incoming trigger | `SCHEMA_OTA_MANIFEST.json` |
| OTA status | Outgoing status | OTA status format in `PROTOCOL_MQTT.md` |
| Telemetry | Outgoing metrics | `SCHEMA_TELEMETRY.json` |
| Zone config | Incoming config | `SCHEMA_ZONE_CONFIG.json` |
| Device state | Outgoing state | `SCHEMA_DEVICE_STATE.json` |

---

## 4. Cloud Testing

### 4.1 Mock Device Messages

Cloud services should generate mock device messages that validate against schemas:

```typescript
// test/mocks/device.ts
import Ajv from 'ajv';
import deviceStateSchema from '../contracts/SCHEMA_DEVICE_STATE.json';

const ajv = new Ajv();
const validateDeviceState = ajv.compile(deviceStateSchema);

export function createMockDeviceState(overrides?: Partial<DeviceState>): DeviceState {
  const state: DeviceState = {
    online: true,
    firmware_version: "1.2.0",
    protocol_version: "1.0",
    uptime_sec: 3600,
    wifi_rssi: -55,
    last_seen: new Date().toISOString(),
    config_version: 1,
    zone_count: 2,
    radar_connected: true,
    ...overrides
  };

  // Validate against schema
  if (!validateDeviceState(state)) {
    throw new Error(`Invalid mock: ${JSON.stringify(validateDeviceState.errors)}`);
  }

  return state;
}

export function createMockTelemetry(deviceId: string): Telemetry {
  // Similar pattern...
}
```

### 4.2 Validate Outgoing Messages

Before publishing to MQTT, cloud services should validate:

```typescript
// services/ota-orchestrator.ts
import otaSchema from '../contracts/SCHEMA_OTA_MANIFEST.json';

export async function triggerOTA(deviceId: string, manifest: OTAManifest) {
  // Validate manifest against schema before publishing
  const valid = validateOTAManifest(manifest);
  if (!valid) {
    throw new ValidationError('Invalid OTA manifest', validateOTAManifest.errors);
  }

  await mqttClient.publish(
    `opticworks/${deviceId}/ota/trigger`,
    JSON.stringify(manifest)
  );
}
```

### 4.3 Cloud Test Matrix

| Test Category | Mock | Validate Against |
|---------------|------|------------------|
| Device registry | Incoming state | `SCHEMA_DEVICE_STATE.json` |
| Telemetry ingestion | Incoming telemetry | `SCHEMA_TELEMETRY.json` |
| OTA orchestrator | Outgoing trigger | `SCHEMA_OTA_MANIFEST.json` |
| Zone sync | Outgoing config | `SCHEMA_ZONE_CONFIG.json` |

---

## 5. CI Integration

### 5.1 Schema Validation in CI

Add schema validation to CI pipelines:

```yaml
# .github/workflows/contracts.yml
name: Contract Validation

on: [push, pull_request]

jobs:
  validate-schemas:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Validate JSON Schemas
        run: |
          npm install -g ajv-cli
          for schema in docs/contracts/SCHEMA_*.json; do
            ajv compile -s "$schema" --spec=draft2020
          done

      - name: Validate examples in schemas
        run: |
          for schema in docs/contracts/SCHEMA_*.json; do
            ajv validate -s "$schema" -d "$schema" --spec=draft2020
          done

  check-breaking-changes:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      - name: Check for breaking schema changes
        run: |
          # Compare schemas with main branch
          git diff origin/main -- docs/contracts/SCHEMA_*.json > schema_diff.txt

          # Check for removed required fields, changed types, etc.
          python scripts/check_schema_compat.py schema_diff.txt
```

### 5.2 Breaking Change Detection

Detect breaking changes in schemas:

```python
# scripts/check_schema_compat.py
"""
Breaking changes to detect:
- Removed required fields
- Changed field types
- Tightened constraints (stricter patterns, reduced maxLength, etc.)
- Removed enum values

Non-breaking changes (allowed):
- Added optional fields
- Loosened constraints
- Added enum values
"""

def is_breaking_change(old_schema, new_schema):
    # Compare required fields
    old_required = set(old_schema.get('required', []))
    new_required = set(new_schema.get('required', []))

    if new_required - old_required:
        return True, f"New required fields: {new_required - old_required}"

    # Compare property types
    for prop, old_def in old_schema.get('properties', {}).items():
        if prop in new_schema.get('properties', {}):
            new_def = new_schema['properties'][prop]
            if old_def.get('type') != new_def.get('type'):
                return True, f"Type changed for {prop}"

    return False, None
```

---

## 6. Contract Versioning

### 6.1 Version Strategy

| Change Type | Version Bump | Example |
|-------------|--------------|---------|
| New optional field | Patch | Add `expires_at` to OTA manifest |
| New required field | Major | Add `min_version` to OTA manifest |
| Remove field | Major | Remove `progress` from OTA status |
| Change type | Major | Change `version` from string to object |
| Loosen constraint | Minor | Increase `maxLength` |
| Tighten constraint | Major | Decrease `maxLength` |

### 6.2 Version in Schema

Each schema includes a version in its `$id`:

```json
{
  "$id": "https://opticworks.io/schemas/v1/zone-config.json"
}
```

Major version changes create new schema files:
- `SCHEMA_ZONE_CONFIG.json` → `SCHEMA_ZONE_CONFIG_V2.json`

---

## 7. Test Fixture Library

### 7.1 Standard Fixtures

| Fixture | File | Purpose |
|---------|------|---------|
| Valid zone config | `fixtures/zone_config_valid.json` | Happy path testing |
| Complex zone config | `fixtures/zone_config_complex.json` | Max zones, max vertices |
| Invalid zone config | `fixtures/zone_config_invalid.json` | Error handling |
| OTA trigger | `fixtures/ota_trigger.json` | OTA flow testing |
| Telemetry batch | `fixtures/telemetry_batch.json` | Telemetry ingestion |
| Device state online | `fixtures/device_state_online.json` | Registry testing |
| Device state offline | `fixtures/device_state_lwt.json` | LWT handling |

### 7.2 Generating Fixtures

```bash
# Generate fixture from schema example
jq '.examples[0]' docs/contracts/SCHEMA_ZONE_CONFIG.json > fixtures/zone_config_valid.json

# Validate all fixtures
for fixture in fixtures/*.json; do
  schema=$(echo $fixture | sed 's/fixtures/docs\/contracts\/SCHEMA/' | sed 's/_valid\|_invalid\|_.*/.json/')
  ajv validate -s "$schema" -d "$fixture" --spec=draft2020
done
```

---

## 8. Integration Test Scenarios

Use contract schemas to define integration test scenarios:

| Scenario | Firmware Action | Cloud Action | Contract |
|----------|-----------------|--------------|----------|
| OTA Happy Path | Receive trigger, update, report success | Publish trigger, verify status | OTA manifest + status |
| Zone Config Sync | Receive config, apply, report status | Publish config, verify ack | Zone config + status |
| Telemetry Upload | Collect metrics, publish batch | Receive batch, store | Telemetry |
| Device Offline | Disconnect (LWT fires) | Update registry | Device state |

---

## 9. References

| Document | Purpose |
|----------|---------|
| `PROTOCOL_MQTT.md` | Full MQTT topic and message documentation |
| `SCHEMA_*.json` | JSON Schema definitions |
| `../testing/INTEGRATION_TESTS.md` | Integration test scenarios |
