# RS-1 Agent Instructions

This file provides context for AI agents working on the RS-1 codebase.

---

## Project Overview

RS-1 is a presence sensor product from OpticWorks, built on:
- **MCU**: ESP32-WROOM-32E + CH340N USB-UART (single PCBA, multi-variant population)
- **Radar**: LD2450 (24GHz mmWave, 3 targets, 6m range) and/or LD2410 (presence)
- **Firmware**: HardwareOS (custom ESP-IDF stack)
- **Cloud**: Cloudflare Workers + D1 + R2, EMQX for MQTT
- **Integration**: ESPHome Native API for Home Assistant

### Product Variants

| Variant | Radar | Features | Target Retail |
|---------|-------|----------|---------------|
| RS-1 Lite | LD2410 | Static presence detection | $49 |
| RS-1 Pro | LD2410 + LD2450 | Dual radar fusion, zone tracking | $89 |

**Add-ons:** PoE (+$30), IAQ Air Quality (+$35)

**Positioning:**
- **RS-1 Lite:** "I exist" - Utility rooms (bathrooms, hallways, closets)
- **RS-1 Pro:** "I live here" - Living spaces (living rooms, kitchens, bedrooms)

---

## Repository Structure

```
rs-1/
├── README.md                 # Project overview
├── CLAUDE.md -> AGENTS.md    # This file (symlinked)
├── LICENSE                   # MIT
│
└── docs/
    ├── PRD_RS1.md            # Product requirements
    ├── REQUIREMENTS_RS1.md   # Functional requirements
    │
    ├── firmware/             # Device firmware specs (11 modules)
    │   ├── README.md         # Architecture overview with diagrams
    │   ├── HARDWAREOS_MODULE_*.md  # Per-module specifications
    │   ├── BOOT_SEQUENCE.md  # Module initialization order
    │   ├── COORDINATE_SYSTEM.md  # Sensor coordinate system (mm canonical)
    │   ├── MEMORY_BUDGET.md  # Resource constraints (~250KB heap)
    │   ├── DEGRADED_MODES.md # Failure handling behaviors
    │   └── GLOSSARY.md       # Canonical term definitions
    │
    ├── cloud/                # Backend service specs
    │   ├── README.md         # Cloud architecture overview
    │   ├── SERVICE_OTA_ORCHESTRATOR.md   # Staged firmware rollouts
    │   ├── SERVICE_DEVICE_REGISTRY.md    # Device identity/enrollment
    │   ├── SERVICE_TELEMETRY.md          # Metrics ingestion
    │   └── INFRASTRUCTURE.md # Cloudflare + EMQX configuration
    │
    ├── contracts/            # Firmware-cloud agreements
    │   ├── PROTOCOL_MQTT.md  # MQTT topics and payloads
    │   ├── SCHEMA_ZONE_CONFIG.json    # Zone configuration schema
    │   ├── SCHEMA_OTA_MANIFEST.json   # OTA trigger schema
    │   ├── SCHEMA_TELEMETRY.json      # Telemetry payload schema
    │   ├── SCHEMA_DEVICE_STATE.json   # Device state schema
    │   └── MOCK_BOUNDARIES.md  # Contract-based testing strategy
    │
    ├── hardware/             # Hardware specifications
    │   ├── HARDWARE_SPEC.md  # Formal hardware requirements & BOM
    │   ├── RS-1_Unified_BOM.md  # Detailed bill of materials
    │   ├── POE_IMPLEMENTATION.md  # Complete PoE power architecture
    │   └── hardware-concept-evolution.md  # Architecture decisions
    │
    ├── testing/              # Test specifications
    │   ├── INTEGRATION_TESTS.md  # Cross-module test scenarios
    │   └── VALIDATION_PLAN_RS1.md  # Overall validation plan
    │
    ├── reviews/              # Architecture reviews
    │   └── RFD_001_HARDWAREOS_ARCHITECTURE_REVIEW.md
    │
    ├── references/           # Research docs
    │   ├── REFERENCE_FIRMWARE_ARCHITECTURE.md
    │   ├── REFERENCE_SENSY_ZONE_EDITOR.md
    │   └── WIRING.md
    │
    └── archived/             # Historical docs
```

---

## HardwareOS Firmware Modules

The firmware is organized into 12 modules. Always reference the module specs when working on firmware:

| Module | File | Purpose |
|--------|------|---------|
| M01 | `HARDWAREOS_MODULE_RADAR_INGEST.md` | LD2450 UART parsing, disconnect detection |
| M02 | `HARDWAREOS_MODULE_TRACKING.md` | Multi-target Kalman tracking (state machine) |
| M03 | `HARDWAREOS_MODULE_ZONE_ENGINE.md` | Zone occupancy, point-in-polygon |
| M04 | `HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md` | Flicker reduction, hold time |
| M05 | `HARDWAREOS_MODULE_NATIVE_API.md` | ESPHome Native API for Home Assistant |
| M06 | `HARDWAREOS_MODULE_CONFIG_STORE.md` | NVS persistence, commit policy |
| M07 | `HARDWAREOS_MODULE_OTA.md` | Firmware updates via MQTT |
| M08 | `HARDWAREOS_MODULE_TIMEBASE.md` | Timing, scheduling, watchdog |
| M09 | `HARDWAREOS_MODULE_LOGGING.md` | Diagnostics, telemetry |
| M10 | `HARDWAREOS_MODULE_SECURITY.md` | Secure boot, TLS, pairing |
| M11 | `HARDWAREOS_MODULE_ZONE_EDITOR.md` | Zone config interface (cloud) |
| M12 | `HARDWAREOS_MODULE_IAQ.md` | IAQ module detection, ENS160 driver, OTA unlock |

---

## Cloud Services

Backend services run on Cloudflare with EMQX for MQTT:

| Service | Document | Purpose |
|---------|----------|---------|
| OTA Orchestrator | `SERVICE_OTA_ORCHESTRATOR.md` | Staged rollouts, abort on failure |
| Device Registry | `SERVICE_DEVICE_REGISTRY.md` | Device identity, ownership, state |
| Telemetry | `SERVICE_TELEMETRY.md` | Metrics ingestion, alerting |
| Infrastructure | `INFRASTRUCTURE.md` | Workers, D1, R2, EMQX config |

### Cloud Stack
- **Compute**: Cloudflare Workers (TypeScript)
- **Database**: Cloudflare D1 (SQLite at edge)
- **Storage**: Cloudflare R2 (firmware, telemetry archives)
- **MQTT**: EMQX Cloud (device communication)

---

## Hardware Documentation

Hardware specifications are in `docs/hardware/`. Key documents:

| Document | Purpose |
|----------|---------|
| `HARDWARE_SPEC.md` | Formal hardware requirements, BOM, electrical specs |
| `RS-1_Unified_BOM.md` | Detailed bill of materials with part numbers |
| `POE_IMPLEMENTATION.md` | Complete PoE power architecture specification |
| `IAQ_MODULE_SPEC.md` | IAQ daughterboard hardware specification |
| `hardware-concept-evolution.md` | Architecture decisions (C3→32E migration) |

### Hardware Architecture

Single PCBA design with selective population for two product variants:

```
┌─────────────────────────────────────────────────────────────┐
│                     RS-1 Platform                           │
├─────────────────────────────────────────────────────────────┤
│  Core (All Variants):                                       │
│  • ESP32-WROOM-32E (MCU) + CH340N (USB-UART)               │
│  • USB-C (Power + Data via CH340N)                         │
│  • AHT20 (Temp/Humidity), LTR-303 (Lux), WS2812 (LED)      │
├─────────────────────────────────────────────────────────────┤
│  Variant Population:                                        │
│  • Lite: LD2410 radar (static presence)                     │
│  • Pro:  LD2410 + LD2450 (dual radar fusion)               │
├─────────────────────────────────────────────────────────────┤
│  Add-On Options:                                            │
│  • PoE: SR8201F RMII PHY + DP1435-5V PD module             │
│  • IAQ: Separate module (magnetic snap-on via pogo pins)   │
└─────────────────────────────────────────────────────────────┘
```

---

## Key Technical Details

### LD2450 Radar
- UART: 256000 baud
- Frame rate: **33 Hz** (30ms period)
- Max targets: 3
- Coordinate range: X ±6000mm, Y 0-6000mm
- Frame size: 40 bytes

### ESP32-WROOM-32E
- Architecture: Xtensa LX6 dual-core, 240MHz
- Flash: 8MB (N8 variant)
- SRAM: 520KB
- GPIO: 34 programmable (sufficient for dual radar + RMII Ethernet)
- Native USB: No (requires CH340N USB-UART bridge)
- EMAC/RMII: Yes (enables low-cost Ethernet PHY)
- Framework: ESP-IDF 5.x

**Note:** ESP32-WROOM-32E selected over ESP32-S3 for native EMAC/RMII support, enabling low-cost Ethernet PHY for PoE variants. See `docs/hardware/hardware-concept-evolution.md` for rationale.

### Coordinate System
- **Canonical unit**: millimeters (mm)
- **Origin**: Sensor center
- **+X**: Observer's right when facing sensor
- **+Y**: Away from sensor (into room)
- **Conversion**: Zone Editor uses meters, converted at M11 boundary

### Home Assistant Integration
- Protocol: ESPHome Native API (Protobuf over TCP)
- Port: 6053
- Discovery: mDNS `_esphomelib._tcp.local`
- Encryption: Noise protocol (optional)

---

## Development Guidelines

### Documentation First
- Each module has an **Assumptions** table - check these when requirements change
- Architecture diagrams are in `docs/firmware/README.md`
- MQTT protocol is defined in `docs/contracts/PROTOCOL_MQTT.md`
- Cloud services are defined in `docs/cloud/`

### Code Style (When Firmware Exists)
- Follow ESP-IDF coding conventions
- Use ESP-IDF logging: `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()`
- Module prefixes: `radar_`, `track_`, `zone_`, etc.

### Testing
- Unit tests should mock hardware dependencies
- Integration tests use simulated radar data
- Contract schemas in `docs/contracts/` define mock boundaries
- See `docs/testing/VALIDATION_PLAN_RS1.md` for test requirements
- See `docs/testing/INTEGRATION_TESTS.md` for cross-module scenarios

---

## Common Tasks

### Reading Documentation
```bash
# Firmware module specs
ls docs/firmware/HARDWAREOS_MODULE_*.md

# Cloud service specs
ls docs/cloud/SERVICE_*.md

# Contract schemas
ls docs/contracts/SCHEMA_*.json
```

### Updating Documentation
- Module specs follow a consistent format (Purpose, Assumptions, Inputs, Outputs, etc.)
- Always update the Assumptions table when requirements change
- Keep ASCII diagrams in sync with implementation
- Update contracts/ if firmware-cloud interface changes

### Adding New Features
1. Check if feature fits existing module or needs new module
2. Update relevant module spec first (firmware or cloud)
3. Update `docs/firmware/README.md` or `docs/cloud/README.md` if architecture changes
4. Update MQTT contract if communication affected
5. Implement feature following spec

---

## Key Assumptions

These are critical assumptions. If any change, review affected modules:

| Assumption | Value | Affects |
|------------|-------|---------|
| LD2450 frame rate | **33 Hz** | M01, M02, M04, M08 |
| Max targets | 3 | M01, M02, M03 |
| Coordinate unit | **mm** (canonical) | M01, M02, M03, M11 |
| Coordinate range | X ±6000mm, Y 0-6000mm | M01, M03 |
| Heap budget | ~250KB available | All modules |
| TLS memory | ~33KB per connection | M05, M07, M10 |
| NVS commits | On change only (<10/day) | M06, M08 |
| Sensitivity | `hold_time_ms = (100 - sensitivity) * 50` | M03, M04, M11 |

---

## Key Documents

| Document | Purpose |
|----------|---------|
| `docs/hardware/HARDWARE_SPEC.md` | **Hardware requirements, BOM, electrical specs** |
| `docs/hardware/RS-1_Unified_BOM.md` | **Detailed bill of materials** |
| `docs/hardware/POE_IMPLEMENTATION.md` | **Complete PoE power architecture** |
| `docs/hardware/IAQ_MODULE_SPEC.md` | **IAQ daughterboard hardware specification** |
| `docs/firmware/GLOSSARY.md` | Canonical term definitions |
| `docs/firmware/COORDINATE_SYSTEM.md` | Sensor coordinate system |
| `docs/firmware/MEMORY_BUDGET.md` | Resource constraints |
| `docs/firmware/BOOT_SEQUENCE.md` | Module initialization DAG |
| `docs/firmware/DEGRADED_MODES.md` | Failure handling behaviors |
| `docs/firmware/HARDWAREOS_MODULE_IAQ.md` | M12 IAQ firmware module specification |
| `docs/contracts/PROTOCOL_MQTT.md` | Cloud communication protocol |
| `docs/cloud/README.md` | Cloud architecture overview |
| `docs/testing/INTEGRATION_TESTS.md` | Cross-module test scenarios |

---

## Links

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [ESPHome Native API](https://esphome.io/components/api.html)
- [LD2450 Datasheet](https://www.hlktech.net/index.php?id=1157)
- [Noise Protocol](https://noiseprotocol.org/)
- [Cloudflare Workers](https://developers.cloudflare.com/workers/)
- [EMQX Documentation](https://docs.emqx.com/)
