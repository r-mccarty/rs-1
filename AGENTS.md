# RS-1 Agent Instructions

This file provides context for AI agents working on the RS-1 codebase.

---

## Project Overview

RS-1 is a presence sensor product from OpticWorks, built on:
- **MCU**: ESP32-C3-MINI-1
- **Radar**: LD2450 (24GHz mmWave, 3 targets, 6m range)
- **Firmware**: HardwareOS (custom ESP-IDF stack)
- **Integration**: ESPHome Native API for Home Assistant

---

## Repository Structure

```
rs-1/
├── README.md                 # Project overview
├── CLAUDE.md → AGENTS.md     # This file (symlinked)
├── LICENSE                   # MIT
│
└── docs/
    ├── PRD_RS1.md            # Product requirements
    ├── PRODUCT_SPEC_RS1.md   # Product specification
    ├── TECH_REQUIREMENTS_RS1.md
    ├── VALIDATION_PLAN_RS1.md
    │
    ├── hardwareos/           # Firmware module specs
    │   ├── README.md         # Architecture overview with diagrams
    │   └── HARDWAREOS_MODULE_*.md  # Per-module specifications
    │
    ├── references/           # Research docs
    └── archived/             # Historical docs
```

---

## HardwareOS Modules

The firmware is organized into 11 modules. Always reference the module specs when working on firmware:

| Module | File | Purpose |
|--------|------|---------|
| M01 | `HARDWAREOS_MODULE_RADAR_INGEST.md` | LD2450 UART parsing |
| M02 | `HARDWAREOS_MODULE_TRACKING.md` | Multi-target tracking |
| M03 | `HARDWAREOS_MODULE_ZONE_ENGINE.md` | Zone occupancy |
| M04 | `HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md` | Flicker reduction |
| M05 | `HARDWAREOS_MODULE_NATIVE_API.md` | Home Assistant API |
| M06 | `HARDWAREOS_MODULE_CONFIG_STORE.md` | Persistent config |
| M07 | `HARDWAREOS_MODULE_OTA.md` | Firmware updates |
| M08 | `HARDWAREOS_MODULE_TIMEBASE.md` | Timing/scheduling |
| M09 | `HARDWAREOS_MODULE_LOGGING.md` | Diagnostics |
| M10 | `HARDWAREOS_MODULE_SECURITY.md` | Secure boot/TLS |
| M11 | `HARDWAREOS_MODULE_ZONE_EDITOR.md` | Zone config interface |

---

## Key Technical Details

### LD2450 Radar
- UART: 256000 baud
- Frame rate: ~33 Hz (30ms)
- Max targets: 3
- Coordinate range: X ±6000mm, Y 0-6000mm
- Frame size: 40 bytes

### ESP32-C3-MINI-1
- Architecture: RISC-V single-core, 160MHz
- Flash: 4MB
- SRAM: 400KB
- Framework: ESP-IDF 5.x

### Home Assistant Integration
- Protocol: ESPHome Native API (Protobuf over TCP)
- Port: 6053
- Discovery: mDNS `_esphomelib._tcp.local`
- Encryption: Noise protocol (optional)

---

## Development Guidelines

### Documentation
- Each module has an **Assumptions** table - check these when requirements change
- Architecture diagrams are in `docs/hardwareos/README.md`
- Reference docs in `docs/references/` contain research and competitor analysis

### Code Style (When Firmware Exists)
- Follow ESP-IDF coding conventions
- Use ESP-IDF logging: `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()`
- Module prefixes: `radar_`, `track_`, `zone_`, etc.

### Testing
- Unit tests should mock hardware dependencies
- Integration tests use simulated radar data
- See `VALIDATION_PLAN_RS1.md` for test requirements

---

## Common Tasks

### Reading Module Specs
```bash
# View all module specs
ls docs/hardwareos/HARDWAREOS_MODULE_*.md

# Read a specific module
cat docs/hardwareos/HARDWAREOS_MODULE_TRACKING.md
```

### Updating Documentation
- Module specs follow a consistent format (Purpose, Assumptions, Inputs, Outputs, etc.)
- Always update the Assumptions table when requirements change
- Keep ASCII diagrams in sync with implementation

### Adding New Features
1. Check if feature fits existing module or needs new module
2. Update relevant module spec first
3. Update `docs/hardwareos/README.md` if architecture changes
4. Implement feature following spec

---

## Key Assumptions

These are critical assumptions. If any change, review affected modules:

| Assumption | Value | Affects |
|------------|-------|---------|
| LD2450 frame rate | ~33 Hz | M01, M02, M08 |
| Max targets | 3 | M01, M02, M03 |
| Coordinate range | X ±6000mm, Y 0-6000mm | M01, M03 |
| ESPHome API version | 1.9+ | M05 |
| Occlusion duration | < 2 seconds typical | M04 |

---

## Links

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [ESPHome Native API](https://esphome.io/components/api.html)
- [LD2450 Datasheet](https://www.hlktech.net/index.php?id=1157)
- [Noise Protocol](https://noiseprotocol.org/)
