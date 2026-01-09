# RS-1: OpticWorks Presence Sensor

A custom-firmware presence sensor built on ESP32-C3 and LD2450 mmWave radar, designed for seamless Home Assistant integration.

---

## Overview

RS-1 is a zone-based presence sensor that runs **HardwareOS**, a custom firmware stack providing reliable occupancy detection with low flicker. Unlike typical ESPHome implementations, RS-1 includes multi-target tracking, occlusion prediction, and confidence-based smoothing for stable presence output.

```
┌─────────────────────────────────────────────────────────────────┐
│                         RS-1 System                             │
│                                                                 │
│   ┌─────────┐    ┌──────────────┐    ┌─────────────────────┐   │
│   │ LD2450  │───▶│  HardwareOS  │───▶│   Home Assistant    │   │
│   │  Radar  │    │   Firmware   │    │   (Native API)      │   │
│   └─────────┘    └──────────────┘    └─────────────────────┘   │
│                         │                                       │
│                         ▼                                       │
│                  ┌──────────────┐                               │
│                  │  Zone Editor │                               │
│                  │   (Web/App)  │                               │
│                  └──────────────┘                               │
└─────────────────────────────────────────────────────────────────┘
```

---

## Hardware

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32-C3-MINI-1 (RISC-V, 160MHz, 4MB Flash) |
| **Radar** | Hi-Link LD2450 (24GHz FMCW mmWave) |
| **Detection Range** | Up to 6 meters |
| **Field of View** | 120° horizontal, 60° vertical |
| **Targets Tracked** | Up to 3 simultaneously |
| **Update Rate** | ~33 Hz (30ms frames) |
| **Connectivity** | Wi-Fi 802.11 b/g/n |
| **Power** | USB-C, 5V |

---

## Features

| Feature | Description |
|---------|-------------|
| **Zone-Based Presence** | Define polygon zones; get per-zone occupancy |
| **Multi-Target Tracking** | Track up to 3 targets with position and velocity |
| **Occlusion Handling** | Predict through brief dropouts, reduce false vacancy |
| **Sensitivity Control** | Tune hold times and responsiveness per zone |
| **Home Assistant Native** | ESPHome-compatible API, auto-discovery |
| **Local-First** | Full functionality without cloud |
| **Secure OTA** | Signed firmware updates with rollback |
| **Zone Editor** | Visual zone configuration via web or mobile |

---

## Project Structure

```
rs-1/
├── README.md                 # This file
├── CLAUDE.md                 # Agent instructions (→ AGENTS.md)
├── LICENSE                   # MIT License
│
└── docs/
    ├── PRD_RS1.md            # Product requirements
    ├── PRODUCT_SPEC_RS1.md   # Product specification
    ├── TECH_REQUIREMENTS_RS1.md  # Technical requirements
    ├── VALIDATION_PLAN_RS1.md    # Testing and validation
    │
    ├── hardwareos/           # Firmware architecture
    │   ├── README.md         # HardwareOS overview + diagrams
    │   ├── HARDWAREOS_MODULE_RADAR_INGEST.md     # M01
    │   ├── HARDWAREOS_MODULE_TRACKING.md         # M02
    │   ├── HARDWAREOS_MODULE_ZONE_ENGINE.md      # M03
    │   ├── HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md # M04
    │   ├── HARDWAREOS_MODULE_NATIVE_API.md       # M05
    │   ├── HARDWAREOS_MODULE_CONFIG_STORE.md     # M06
    │   ├── HARDWAREOS_MODULE_OTA.md              # M07
    │   ├── HARDWAREOS_MODULE_TIMEBASE.md         # M08
    │   ├── HARDWAREOS_MODULE_LOGGING.md          # M09
    │   ├── HARDWAREOS_MODULE_SECURITY.md         # M10
    │   └── HARDWAREOS_MODULE_ZONE_EDITOR.md      # M11
    │
    ├── references/           # Research and reference docs
    │   ├── REFERENCE_FIRMWARE_ARCHITECTURE.md
    │   ├── REFERENCE_SENSY_ZONE_EDITOR.md
    │   └── WIRING.md
    │
    └── archived/             # Historical docs
        └── SPRINT.md
```

---

## HardwareOS Architecture

HardwareOS is an 11-module firmware stack. See [docs/hardwareos/README.md](docs/hardwareos/README.md) for full architecture documentation.

### Core Data Path

| Module | Name | Purpose |
|--------|------|---------|
| M01 | Radar Ingest | Parse LD2450 UART frames |
| M02 | Tracking | Multi-target tracking with occlusion prediction |
| M03 | Zone Engine | Point-in-polygon zone evaluation |
| M04 | Presence Smoothing | Hysteresis and hold logic |

### External Interfaces

| Module | Name | Purpose |
|--------|------|---------|
| M05 | Native API | ESPHome-compatible Home Assistant integration |
| M07 | OTA Manager | Secure firmware updates |
| M11 | Zone Editor | Visual zone configuration interface |

### System Services

| Module | Name | Purpose |
|--------|------|---------|
| M06 | Config Store | Persistent configuration with rollback |
| M08 | Timebase | Frame timing and task scheduling |
| M09 | Logging | Diagnostics and optional telemetry |
| M10 | Security | Secure boot, signing, TLS |

---

## Documentation

| Document | Description |
|----------|-------------|
| [PRD_RS1.md](docs/PRD_RS1.md) | Product requirements and user stories |
| [PRODUCT_SPEC_RS1.md](docs/PRODUCT_SPEC_RS1.md) | Detailed product specification |
| [TECH_REQUIREMENTS_RS1.md](docs/TECH_REQUIREMENTS_RS1.md) | Technical requirements |
| [VALIDATION_PLAN_RS1.md](docs/VALIDATION_PLAN_RS1.md) | Testing and validation plan |
| [HardwareOS README](docs/hardwareos/README.md) | Firmware architecture with diagrams |

---

## Development Status

| Phase | Status | Description |
|-------|--------|-------------|
| **Specification** | Complete | PRD, tech requirements, module specs |
| **Architecture** | Complete | HardwareOS 11-module design |
| **Firmware** | Not Started | ESP-IDF implementation |
| **Hardware** | Not Started | PCB design, enclosure |
| **Zone Editor** | Not Started | Web/mobile app |

---

## Getting Started

> **Note**: Firmware implementation is in progress. These instructions will be updated when builds are available.

### Prerequisites

- ESP-IDF 5.x
- Python 3.9+
- USB-to-Serial adapter (for initial flash)

### Building (Coming Soon)

```bash
# Clone the repo
git clone https://github.com/r-mccarty/rs-1.git
cd rs-1

# Build firmware
idf.py build

# Flash to device
idf.py -p /dev/ttyUSB0 flash
```

---

## Contributing

This is an OpticWorks internal project. See [CLAUDE.md](CLAUDE.md) for development guidelines.

---

## License

MIT License - See [LICENSE](LICENSE) for details.

---

## Links

- [OpticWorks](https://optic.works)
- [Home Assistant](https://home-assistant.io)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [LD2450 Datasheet](https://www.hlktech.net/index.php?id=1157)
