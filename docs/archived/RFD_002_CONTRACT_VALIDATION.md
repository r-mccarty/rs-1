# RFD-002: Contract Validation Strategy

**Status:** Review
**Author:** Systems Engineering
**Date:** 2026-01-16
**Reviewers:** OpticWorks Firmware, Cloud, QA

---

## Summary

This RFD proposes a comprehensive contract validation strategy for RS-1, replacing the current `MOCK_BOUNDARIES.md` approach. The existing document describes *aspirations* but provides no actionable infrastructure. This RFD is critical of that approach and proposes a concrete, enforceable system that prevents contract violations from reaching production.

**Key Position:** Contracts are only as useful as the tooling that enforces them. An unenforced contract is documentation, not a contract.

---

## Table of Contents

1. [Problem Statement](#1-problem-statement)
2. [Critique of Current Approach](#2-critique-of-current-approach)
3. [Industry Context](#3-industry-context)
4. [Proposed Architecture](#4-proposed-architecture)
5. [Implementation Plan](#5-implementation-plan)
6. [Tooling Requirements](#6-tooling-requirements)
7. [Migration Path](#7-migration-path)
8. [Open Questions](#8-open-questions)

---

## 1. Problem Statement

RS-1 has two independently developed systems that must communicate perfectly:

```
┌─────────────────┐         MQTT/HTTPS         ┌─────────────────┐
│    Firmware     │ ◄─────────────────────────► │      Cloud      │
│  (ESP-IDF / C)  │                            │  (CF Workers)   │
└─────────────────┘                            └─────────────────┘
```

**Failure modes when contracts drift:**

| Scenario | Impact | Detection Time |
|----------|--------|----------------|
| Firmware sends `timestamp` as epoch int, cloud expects ISO string | Silent data loss | Days (telemetry gaps) |
| Cloud sends zone coords in meters, firmware expects mm | Zones 1000x wrong size | User reports (weeks) |
| New required field added to OTA manifest | 100% OTA failure | Production rollout |
| Schema allows null, parser doesn't | Device crashes | Field failures |

The current `MOCK_BOUNDARIES.md` acknowledges these risks but provides no mechanism to prevent them.

---

## 2. Critique of Current Approach

### 2.1 MOCK_BOUNDARIES.md Analysis

The document has the right ideas but critical implementation gaps:

| What It Says | Reality | Problem |
|--------------|---------|---------|
| "Validate outgoing messages against schemas" | No validation code exists | Aspirational only |
| "CI integration with ajv-cli" | No `.github/workflows/` in repo | No CI at all |
| `fixtures/zone_config_valid.json` | File doesn't exist | References phantom infrastructure |
| `scripts/check_schema_compat.py` | File doesn't exist | Pseudocode, not implementation |
| "Generate test fixtures from schemas" | Manual process described | Not automated |

**Verdict:** The document is a design sketch masquerading as a specification.

### 2.2 Specific Technical Issues

#### 2.2.1 Breaking Change Detection is Naive

The proposed `is_breaking_change()` function misses critical cases:

```python
# What MOCK_BOUNDARIES.md checks:
- New required fields
- Changed property types

# What it MISSES:
- additionalProperties: true → false (breaks extensibility)
- Enum value removal
- Pattern constraint tightening ("^[a-f0-9]+$" → "^[a-f0-9]{32}$")
- minItems/maxItems changes
- $ref target changes
- oneOf/anyOf reordering (can change validation behavior)
- Default value changes (affects omitted fields)
- Format additions (string → string+format:date-time)
```

A device sending valid v1 payloads could fail validation against v1.0.1 with these "non-breaking" changes.

#### 2.2.2 Unidirectional Validation

The document only considers:
- Firmware validates what it *sends*
- Cloud validates what it *receives*

It ignores:
- Firmware validating what it *receives* (OTA triggers, zone configs)
- Cloud validating what it *sends* (must be consumable by firmware)

**The consumer's constraints matter as much as the producer's.**

Example: Cloud schema allows `expires_at: null`. Firmware parser does:
```c
time_t expires = parse_iso8601(json_get_string(obj, "expires_at"));
// Crashes on null
```

Schema validation passes. Device crashes.

#### 2.2.3 No Consumer-Driven Contract Testing

Industry standard for distributed systems: **the consumer defines what it can accept**, not just what the producer claims to send.

```
WRONG (current approach):
  Cloud: "Here's what I send" (SCHEMA_OTA_MANIFEST.json)
  Firmware: "I'll parse whatever you send"

RIGHT (consumer-driven):
  Firmware: "Here's the OTA triggers I can parse" (firmware-ota-consumer.json)
  Cloud: "Here's the OTA triggers I produce" (cloud-ota-producer.json)
  CI: "Do these overlap correctly?" (contract test)
```

#### 2.2.4 Version Strategy Creates Maintenance Nightmare

From MOCK_BOUNDARIES.md:
> Major version changes create new schema files:
> `SCHEMA_ZONE_CONFIG.json` → `SCHEMA_ZONE_CONFIG_V2.json`

This means:
- Every consumer must know which version to import
- Multiple schemas in production simultaneously
- No clear deprecation path
- Test matrices explode (firmware v1.2 × schema v1, v2, v3...)

#### 2.2.5 No Local Developer Workflow

The document provides no answer to:
- "I changed a schema, how do I know I broke something?"
- "I'm writing firmware, how do I validate my JSON output?"
- "I'm writing cloud code, how do I test against realistic device messages?"

Developers will skip validation if it requires CI round-trips.

---

## 3. Industry Context

### 3.1 How This Problem Is Typically Solved

| Approach | Tools | Best For |
|----------|-------|----------|
| **Schema Validation** | JSON Schema + ajv/jsonschema | Simple producer/consumer |
| **Consumer-Driven Contracts** | Pact, Spring Cloud Contract | Microservices, APIs |
| **Protocol Buffers** | protoc, buf | High-performance, strict typing |
| **OpenAPI/Swagger** | openapi-generator | REST APIs |
| **Golden File Testing** | Custom scripts | Regression detection |
| **Snapshot Testing** | Jest, pytest-snapshot | UI, complex outputs |

### 3.2 Recommended Approach for RS-1

Given:
- Two primary systems (firmware, cloud)
- JSON over MQTT (not REST)
- Resource-constrained firmware (can't run full validators)
- Small team (must be low-maintenance)

**Recommendation: Layered validation with golden files**

```
Layer 1: Schema compilation (catches syntax errors)
Layer 2: Example validation (catches schema logic errors)
Layer 3: Golden file regression (catches unintentional changes)
Layer 4: Cross-system contract tests (catches drift)
Layer 5: Runtime validation in cloud (catches production issues)
```

---

## 4. Proposed Architecture

### 4.1 Contract Repository Structure

```
docs/contracts/
├── schemas/                    # JSON Schema definitions
│   ├── telemetry.schema.json
│   ├── device-state.schema.json
│   ├── ota-manifest.schema.json
│   └── zone-config.schema.json
│
├── examples/                   # Validated example payloads
│   ├── telemetry/
│   │   ├── valid-minimal.json
│   │   ├── valid-full.json
│   │   ├── valid-with-logs.json
│   │   └── invalid-missing-device-id.json
│   └── ... (per schema)
│
├── golden/                     # Regression baselines
│   ├── firmware-outputs/       # What firmware actually produces
│   │   ├── telemetry-boot.json
│   │   ├── telemetry-hourly.json
│   │   └── device-state-online.json
│   └── cloud-outputs/          # What cloud actually produces
│       ├── ota-trigger-normal.json
│       └── zone-config-3zones.json
│
├── consumers/                  # Consumer capability declarations
│   ├── firmware-consumes.json  # What firmware can parse
│   └── cloud-consumes.json     # What cloud can parse
│
└── tooling/
    ├── validate.sh             # Local validation script
    ├── check-breaking.py       # Breaking change detector
    └── generate-fixtures.py    # Example generator
```

### 4.2 Validation Layers

#### Layer 1: Schema Compilation

```bash
# Every schema must compile without errors
ajv compile --spec=draft2020 -s schemas/*.schema.json
```

**Catches:** Syntax errors, invalid JSON Schema constructs, circular references.

#### Layer 2: Example Validation

```bash
# Every example must validate against its schema
for schema in schemas/*.schema.json; do
  name=$(basename "$schema" .schema.json)
  ajv validate -s "$schema" -d "examples/$name/valid-*.json"

  # Invalid examples must FAIL validation
  for invalid in examples/$name/invalid-*.json; do
    ajv validate -s "$schema" -d "$invalid" && exit 1  # Should fail
  done
done
```

**Catches:** Schema logic errors, overly permissive schemas, missing constraints.

#### Layer 3: Golden File Regression

```python
# Golden files are checked into git
# Any change to schema that would invalidate golden files = breaking change

def test_golden_files_still_valid():
    for golden_file in glob("golden/**/*.json"):
        schema = infer_schema_from_path(golden_file)
        assert validate(golden_file, schema), f"Golden file no longer valid: {golden_file}"
```

**Catches:** Unintentional breaking changes, constraint tightening.

#### Layer 4: Cross-System Contract Tests

```python
# Firmware declares what it can consume
firmware_consumes = {
    "ota-manifest": {
        "required_fields": ["version", "url", "sha256"],
        "max_url_length": 256,  # Buffer size in firmware
        "nullable_fields": [],  # Firmware crashes on nulls
    }
}

# Cloud declares what it produces
cloud_produces = {
    "ota-manifest": {
        "always_present": ["version", "url", "sha256", "issued_at"],
        "sometimes_null": ["expires_at", "min_rssi"],
    }
}

def test_cloud_output_consumable_by_firmware():
    # expires_at is sometimes null, but firmware can't handle nulls
    # THIS TEST SHOULD FAIL until we fix the mismatch
    assert cloud_produces["ota-manifest"]["sometimes_null"].isdisjoint(
        firmware_consumes["ota-manifest"]["nullable_fields"]
    )
```

**Catches:** Producer/consumer mismatches before deployment.

#### Layer 5: Runtime Validation (Cloud Only)

```typescript
// Cloud validates incoming device messages AT RUNTIME
// Not for blocking - for observability

export async function onDeviceTelemetry(msg: unknown) {
  const valid = validateTelemetry(msg);
  if (!valid) {
    metrics.increment("contract.violation.telemetry");
    logger.warn("Contract violation", {
      errors: validateTelemetry.errors,
      device_id: msg?.device_id
    });
    // Still process - devices may be on old firmware
  }
  // ... process message
}
```

**Catches:** Drift in production, firmware bugs, malformed messages.

### 4.3 Breaking Change Detection (Proper Implementation)

```python
#!/usr/bin/env python3
"""
Comprehensive breaking change detector for JSON Schema.

Unlike the naive version in MOCK_BOUNDARIES.md, this catches:
- All constraint tightening (not just type changes)
- Enum value removal
- additionalProperties changes
- $ref target changes
- And more...
"""

BREAKING_CHANGES = [
    # Field requirements
    ("required", "added", "New required field"),
    ("required", "field_removed_from_optional", "Optional field removed entirely"),

    # Type changes
    ("type", "changed", "Type changed"),
    ("type", "narrowed", "Type options reduced"),

    # Constraint tightening
    ("minLength", "increased", "Minimum length increased"),
    ("maxLength", "decreased", "Maximum length decreased"),
    ("minimum", "increased", "Minimum value increased"),
    ("maximum", "decreased", "Maximum value decreased"),
    ("minItems", "increased", "Minimum items increased"),
    ("maxItems", "decreased", "Maximum items decreased"),
    ("pattern", "changed", "Pattern constraint changed"),

    # Enum changes
    ("enum", "value_removed", "Enum value removed"),

    # Structure changes
    ("additionalProperties", "false_added", "additionalProperties set to false"),
    ("additionalProperties", "schema_tightened", "additionalProperties schema tightened"),

    # Composition changes
    ("oneOf", "option_removed", "oneOf option removed"),
    ("anyOf", "option_removed", "anyOf option removed"),
    ("allOf", "schema_added", "Additional allOf constraint"),
]

NON_BREAKING_CHANGES = [
    # Field additions
    ("properties", "optional_added", "New optional property"),
    ("$defs", "added", "New definition added"),

    # Constraint loosening
    ("minLength", "decreased", "Minimum length decreased"),
    ("maxLength", "increased", "Maximum length increased"),
    ("enum", "value_added", "New enum value"),

    # Documentation
    ("description", "changed", "Description updated"),
    ("title", "changed", "Title updated"),
    ("examples", "changed", "Examples updated"),
]

def check_breaking_changes(old_schema: dict, new_schema: dict) -> list[str]:
    """
    Returns list of breaking change descriptions, empty if compatible.
    """
    issues = []

    # Check required fields
    old_required = set(old_schema.get("required", []))
    new_required = set(new_schema.get("required", []))
    added_required = new_required - old_required
    if added_required:
        issues.append(f"BREAKING: New required fields: {added_required}")

    # Check each property
    old_props = old_schema.get("properties", {})
    new_props = new_schema.get("properties", {})

    for prop_name, old_def in old_props.items():
        if prop_name not in new_props:
            issues.append(f"BREAKING: Property '{prop_name}' removed")
            continue

        new_def = new_props[prop_name]
        prop_issues = check_property_breaking(prop_name, old_def, new_def)
        issues.extend(prop_issues)

    # Check additionalProperties
    old_additional = old_schema.get("additionalProperties", True)
    new_additional = new_schema.get("additionalProperties", True)
    if old_additional is True and new_additional is False:
        issues.append("BREAKING: additionalProperties changed from true to false")

    # Check enum at root level
    if "enum" in old_schema and "enum" in new_schema:
        removed = set(old_schema["enum"]) - set(new_schema["enum"])
        if removed:
            issues.append(f"BREAKING: Enum values removed: {removed}")

    return issues


def check_property_breaking(name: str, old_def: dict, new_def: dict) -> list[str]:
    """Check a single property for breaking changes."""
    issues = []

    # Type changes
    old_type = old_def.get("type")
    new_type = new_def.get("type")
    if old_type != new_type:
        # Allow widening (string -> [string, null])
        if not is_type_widening(old_type, new_type):
            issues.append(f"BREAKING: '{name}' type changed from {old_type} to {new_type}")

    # String constraints
    if old_def.get("maxLength", float("inf")) > new_def.get("maxLength", float("inf")):
        issues.append(f"BREAKING: '{name}' maxLength decreased")
    if old_def.get("minLength", 0) < new_def.get("minLength", 0):
        issues.append(f"BREAKING: '{name}' minLength increased")

    # Numeric constraints
    if old_def.get("maximum", float("inf")) > new_def.get("maximum", float("inf")):
        issues.append(f"BREAKING: '{name}' maximum decreased")
    if old_def.get("minimum", float("-inf")) < new_def.get("minimum", float("-inf")):
        issues.append(f"BREAKING: '{name}' minimum increased")

    # Pattern changes (any change is potentially breaking)
    if old_def.get("pattern") != new_def.get("pattern"):
        issues.append(f"BREAKING: '{name}' pattern changed")

    # Enum changes
    if "enum" in old_def and "enum" in new_def:
        removed = set(old_def["enum"]) - set(new_def["enum"])
        if removed:
            issues.append(f"BREAKING: '{name}' enum values removed: {removed}")

    return issues
```

### 4.4 Developer Workflow

#### Local Validation Script

```bash
#!/bin/bash
# contracts/tooling/validate.sh
# Run this before committing any contract changes

set -e

CONTRACTS_DIR="$(dirname "$0")/.."
cd "$CONTRACTS_DIR"

echo "=== Layer 1: Schema Compilation ==="
for schema in schemas/*.schema.json; do
    echo "  Compiling: $schema"
    npx ajv compile --spec=draft2020 -s "$schema" -o /dev/null
done

echo "=== Layer 2: Example Validation ==="
for schema in schemas/*.schema.json; do
    name=$(basename "$schema" .schema.json)
    echo "  Validating examples for: $name"

    for valid in examples/$name/valid-*.json; do
        [ -f "$valid" ] || continue
        npx ajv validate -s "$schema" -d "$valid" --spec=draft2020
    done

    for invalid in examples/$name/invalid-*.json; do
        [ -f "$invalid" ] || continue
        if npx ajv validate -s "$schema" -d "$invalid" --spec=draft2020 2>/dev/null; then
            echo "    ERROR: $invalid should have failed validation"
            exit 1
        fi
    done
done

echo "=== Layer 3: Golden File Regression ==="
for golden in golden/**/*.json; do
    [ -f "$golden" ] || continue
    schema=$(python3 tooling/infer-schema.py "$golden")
    echo "  Checking: $golden against $schema"
    npx ajv validate -s "$schema" -d "$golden" --spec=draft2020
done

echo "=== Layer 4: Breaking Change Detection ==="
if git rev-parse --verify HEAD~1 >/dev/null 2>&1; then
    python3 tooling/check-breaking.py
fi

echo ""
echo "All contract validations passed."
```

#### Pre-Commit Hook

```bash
#!/bin/bash
# .git/hooks/pre-commit (or via husky/pre-commit framework)

if git diff --cached --name-only | grep -q "^docs/contracts/"; then
    echo "Contract files modified, running validation..."
    ./docs/contracts/tooling/validate.sh
fi
```

---

## 5. Implementation Plan

### Phase 1: Foundation (Week 1)

1. Restructure `docs/contracts/` to proposed layout
2. Rename existing schemas to `.schema.json` suffix
3. Create `tooling/validate.sh` script
4. Create initial example files from schema `examples` arrays
5. Add npm/pip dependencies for ajv-cli, jsonschema

**Deliverable:** `./tooling/validate.sh` runs and passes locally.

### Phase 2: Golden Files (Week 1-2)

1. Define golden file set (realistic device outputs)
2. Create golden files manually (will be replaced by real captures later)
3. Add golden file validation to `validate.sh`
4. Document golden file update process

**Deliverable:** Any schema change that breaks golden files is detected.

### Phase 3: CI Integration (Week 2)

1. Create `.github/workflows/contracts.yml`
2. Run on all PRs touching `docs/contracts/`
3. Block merge on validation failure
4. Add breaking change detection with PR comments

**Deliverable:** PRs cannot merge with invalid contracts.

### Phase 4: Consumer Declarations (Week 2-3)

1. Document firmware's parsing constraints (buffer sizes, null handling, etc.)
2. Document cloud's production guarantees
3. Create consumer capability test
4. Add to CI pipeline

**Deliverable:** Producer/consumer mismatches detected before merge.

### Phase 5: Runtime Observability (Week 3+)

1. Add schema validation to cloud message handlers
2. Create contract violation metrics
3. Alert on violation rate thresholds
4. Dashboard for contract health

**Deliverable:** Production contract drift is visible and alertable.

---

## 6. Tooling Requirements

### Required Dependencies

| Tool | Purpose | Install |
|------|---------|---------|
| ajv-cli | JSON Schema validation | `npm install -g ajv-cli ajv-formats` |
| jsonschema | Python validation | `pip install jsonschema` |
| jq | JSON manipulation | System package manager |

### CI Environment

```yaml
# .github/workflows/contracts.yml
name: Contract Validation

on:
  push:
    paths: ['docs/contracts/**']
  pull_request:
    paths: ['docs/contracts/**']

jobs:
  validate:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0  # For breaking change detection

      - uses: actions/setup-node@v4
        with:
          node-version: '20'

      - uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Install dependencies
        run: |
          npm install -g ajv-cli ajv-formats
          pip install jsonschema

      - name: Run contract validation
        run: ./docs/contracts/tooling/validate.sh

      - name: Check for breaking changes
        if: github.event_name == 'pull_request'
        run: |
          python3 docs/contracts/tooling/check-breaking.py \
            --base origin/${{ github.base_ref }} \
            --head HEAD \
            --output-format github
```

---

## 7. Migration Path

### From Current State

| Current | Proposed | Migration |
|---------|----------|-----------|
| `SCHEMA_ZONE_CONFIG.json` | `schemas/zone-config.schema.json` | Rename, extract examples |
| `SCHEMA_OTA_MANIFEST.json` | `schemas/ota-manifest.schema.json` | Rename, extract examples |
| `SCHEMA_TELEMETRY.json` | `schemas/telemetry.schema.json` | Rename, extract examples |
| `SCHEMA_DEVICE_STATE.json` | `schemas/device-state.schema.json` | Rename, extract examples |
| `MOCK_BOUNDARIES.md` | `README.md` | Archive, replace with new README |

### Deprecation

`MOCK_BOUNDARIES.md` should be moved to `docs/archived/` with a note pointing to this RFD and the new `docs/contracts/README.md`.

---

## 8. Open Questions

### 8.1 Schema Versioning Strategy

**Options:**

1. **Single schema, backward compatible only** - Never break, only add optional fields
2. **Versioned URLs** - `schemas/v1/telemetry.schema.json`, `schemas/v2/telemetry.schema.json`
3. **Version field in payload** - `{"schema_version": "1.2", ...}` with runtime dispatch

**Recommendation:** Option 1 for simplicity. If we need breaking changes, do a major firmware version bump and support both versions in cloud for transition period.

### 8.2 Firmware Validation

Should firmware validate incoming JSON against schemas?

- **Pro:** Catches malformed cloud messages early
- **Con:** JSON Schema validation is heavy for ESP32 (~30KB+ code)

**Recommendation:** No runtime schema validation in firmware. Instead:
1. Firmware declares its parsing constraints (consumer declaration)
2. Cloud validates it only produces consumable messages
3. Firmware does basic sanity checks (field presence, type coercion)

### 8.3 Error Message Contracts

Should we standardize error response formats?

Example: When zone config validation fails, what does cloud send back?

```json
{
  "error": "validation_failed",
  "details": [
    {"field": "zones[0].polygon", "message": "Polygon must have 3-8 vertices"}
  ]
}
```

**Recommendation:** Yes, add `schemas/error-response.schema.json` for cloud→device error messages.

### 8.4 Ownership

Who owns contract changes?

- **Option A:** Firmware team owns all schemas (cloud adapts)
- **Option B:** Cloud team owns all schemas (firmware adapts)
- **Option C:** Joint ownership with mandatory review from both teams

**Recommendation:** Option C. Any PR touching `docs/contracts/` requires approval from both firmware and cloud reviewers.

---

## 9. Decision Summary

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Validation approach | Layered (5 layers) | Defense in depth, catches different failure modes |
| Breaking change detection | Comprehensive script | Naive approach misses critical cases |
| Consumer contracts | Yes | Prevents producer/consumer drift |
| Golden files | Yes | Regression detection without full CDC complexity |
| Runtime validation | Cloud only | Firmware too constrained |
| Schema versioning | Single + backward compat | Simpler than multi-version support |
| CI blocking | Yes | Contracts are useless if not enforced |

---

## Appendix A: Comparison with MOCK_BOUNDARIES.md

| Aspect | MOCK_BOUNDARIES.md | This RFD |
|--------|-------------------|----------|
| Schema validation | Mentioned | Implemented with script |
| CI integration | Pseudocode | Full workflow provided |
| Breaking change detection | Naive (2 checks) | Comprehensive (15+ checks) |
| Consumer contracts | Not mentioned | Core requirement |
| Golden files | Not mentioned | Layer 3 of validation |
| Runtime observability | Not mentioned | Layer 5 of validation |
| Developer workflow | Not mentioned | Pre-commit hook + local script |
| Tooling | Fictional paths | Actual implementation plan |

---

## Appendix B: Example Consumer Declaration

```json
{
  "$schema": "https://opticworks.io/meta/consumer-declaration.json",
  "consumer": "rs1-firmware",
  "version": "1.0.0",
  "consumes": {
    "ota-manifest": {
      "required_fields": ["version", "url", "sha256"],
      "constraints": {
        "url": {
          "max_length": 256,
          "must_be_https": true
        },
        "sha256": {
          "exact_length": 64,
          "pattern": "^[a-f0-9]+$"
        },
        "version": {
          "pattern": "^\\d+\\.\\d+\\.\\d+$"
        }
      },
      "nullable_fields": [],
      "ignored_fields": ["rollout_id", "notes"]
    },
    "zone-config": {
      "required_fields": ["version", "zones"],
      "constraints": {
        "zones": {
          "max_items": 16
        },
        "zones[*].polygon": {
          "min_vertices": 3,
          "max_vertices": 8,
          "coordinate_unit": "mm",
          "coordinate_range": {
            "x": [-6000, 6000],
            "y": [0, 6000]
          }
        }
      }
    }
  }
}
```

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-16 | Systems Engineering | Initial draft |
