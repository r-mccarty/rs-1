# RS-1 Contract Validation

This directory contains JSON Schemas and validation tooling for firmware-cloud interface contracts.

## Directory Structure

```
contracts/
├── README.md                    # This file
├── PROTOCOL_MQTT.md             # MQTT topic and payload specification
├── PROTOCOL_PROVISIONING.md     # Device provisioning protocol
├── schemas/                     # JSON Schema definitions
│   ├── telemetry.schema.json
│   ├── device-state.schema.json
│   ├── zone-config.schema.json
│   └── ota-manifest.schema.json
├── examples/                    # Example payloads for validation
│   ├── telemetry/
│   │   ├── valid-minimal.json
│   │   └── invalid-missing-device-id.json
│   ├── zone-config/
│   │   ├── valid-minimal.json
│   │   └── invalid-missing-zones.json
│   └── ...
├── golden/                      # Production-captured payloads
│   ├── firmware-outputs/
│   └── cloud-outputs/
└── tooling/
    ├── validate.sh              # Run all validation layers
    └── check-breaking.py        # Detect breaking schema changes
```

## Validation Layers

### Layer 1: Schema Compilation
Schemas must compile without errors using JSON Schema Draft 2020-12.

### Layer 2: Example Validation
- Files matching `valid-*.json` must pass schema validation
- Files matching `invalid-*.json` must fail schema validation

### Layer 3: Golden File Regression
Production-captured payloads must remain valid across schema changes.

### Layer 4: Breaking Change Detection
Schema changes that would break existing producers/consumers are flagged.

## Running Validation

```bash
# Install dependencies (once)
npm install -g ajv-cli ajv-formats

# Run all validation
./tooling/validate.sh

# CI mode (fail on warnings)
./tooling/validate.sh --ci
```

## Schema Naming Convention

- Schemas use `.schema.json` suffix
- File names are kebab-case: `zone-config.schema.json`
- Schema `$id` matches canonical URL: `https://opticworks.io/schemas/zone-config.json`

## Adding a New Schema

1. Create `schemas/{name}.schema.json` following Draft 2020-12
2. Create `examples/{name}/valid-minimal.json` with minimal valid payload
3. Create `examples/{name}/invalid-*.json` with common error cases
4. Run `./tooling/validate.sh` to verify
5. Update `PROTOCOL_MQTT.md` if the schema defines an MQTT payload

## Breaking Change Policy

Breaking changes require major version bump and migration path:
- Adding required fields → breaking
- Removing enum values → breaking
- Tightening constraints (smaller max, larger min) → breaking
- Adding optional fields → non-breaking
- Loosening constraints → non-breaking

See `tooling/check-breaking.py` for automated detection.
