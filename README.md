# RS-1: OpticWorks Presence Sensor

A custom-firmware presence sensor built on ESP32-C3 and LD2450 mmWave radar, designed for seamless Home Assistant integration.

---

## Overview

RS-1 is a zone-based presence sensor that runs **HardwareOS**, a custom firmware stack providing reliable occupancy detection with low flicker. Unlike typical ESPHome implementations, RS-1 includes multi-target Kalman tracking, occlusion prediction, and confidence-based smoothing for stable presence output.

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
│   ┌─────────────────────────────────────────────────────────┐   │
│   │                   OpticWorks Cloud                       │   │
│   │  OTA Orchestrator │ Device Registry │ Telemetry │ MQTT  │   │
│   └─────────────────────────────────────────────────────────┘   │
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
| **Update Rate** | 33 Hz (30ms frames) |
| **Connectivity** | Wi-Fi 802.11 b/g/n |
| **Power** | USB-C, 5V |

---

## Features

| Feature | Description |
|---------|-------------|
| **Zone-Based Presence** | Define polygon zones; get per-zone occupancy |
| **Multi-Target Tracking** | Kalman filter tracking with position and velocity |
| **Occlusion Handling** | Predict through brief dropouts, reduce false vacancy |
| **Sensitivity Control** | Tune hold times and responsiveness per zone |
| **Home Assistant Native** | ESPHome-compatible API, auto-discovery |
| **Local-First** | Full functionality without cloud |
| **Secure OTA** | Signed firmware updates with staged rollouts |
| **Zone Editor** | Visual zone configuration via web or mobile |
| **Cloud Telemetry** | Optional device health monitoring and alerts |

---

## Project Structure

```
rs-1/
├── README.md                 # This file
├── CLAUDE.md                 # Agent instructions (-> AGENTS.md)
├── LICENSE                   # MIT License
│
└── docs/
    ├── PRD_RS1.md            # Product requirements
    ├── REQUIREMENTS_RS1.md   # Functional requirements
    │
    ├── firmware/             # Device firmware specs (11 modules)
    │   ├── README.md         # HardwareOS architecture + diagrams
    │   ├── HARDWAREOS_MODULE_*.md  # Per-module specifications
    │   ├── BOOT_SEQUENCE.md  # Module initialization order
    │   ├── COORDINATE_SYSTEM.md  # Sensor coordinate system
    │   ├── MEMORY_BUDGET.md  # Resource constraints
    │   ├── DEGRADED_MODES.md # Failure handling behaviors
    │   └── GLOSSARY.md       # Term definitions
    │
    ├── cloud/                # Backend service specs
    │   ├── README.md         # Cloud architecture overview
    │   ├── SERVICE_OTA_ORCHESTRATOR.md   # Staged rollouts
    │   ├── SERVICE_DEVICE_REGISTRY.md    # Device management
    │   ├── SERVICE_TELEMETRY.md          # Metrics ingestion
    │   └── INFRASTRUCTURE.md # Cloudflare + EMQX config
    │
    ├── contracts/            # Firmware-cloud agreements
    │   ├── PROTOCOL_MQTT.md  # MQTT topics and payloads
    │   ├── SCHEMA_*.json     # JSON schemas for payloads
    │   └── MOCK_BOUNDARIES.md  # Testing strategy
    │
    ├── testing/              # Test specifications
    │   ├── INTEGRATION_TESTS.md  # Cross-module scenarios
    │   └── VALIDATION_PLAN_RS1.md  # Validation plan
    │
    ├── reviews/              # Architecture reviews
    │   └── RFD_001_HARDWAREOS_ARCHITECTURE_REVIEW.md
    │
    ├── references/           # Research docs
    └── archived/             # Historical docs
```

---

## Architecture

### Firmware (HardwareOS)

HardwareOS is an 11-module firmware stack. See [docs/firmware/README.md](docs/firmware/README.md) for full documentation.

| Layer | Modules | Purpose |
|-------|---------|---------|
| **Data Path** | M01, M02, M03, M04 | Radar → Tracking → Zones → Presence |
| **Interfaces** | M05, M07, M11 | Home Assistant, OTA, Zone Editor |
| **Services** | M06, M08, M09, M10 | Config, Timing, Logging, Security |

### Cloud Services

Backend runs on Cloudflare (Workers, D1, R2) with EMQX for MQTT. See [docs/cloud/README.md](docs/cloud/README.md) for full documentation.

| Service | Purpose |
|---------|---------|
| **OTA Orchestrator** | Staged firmware rollouts with automatic abort |
| **Device Registry** | Device identity, ownership, state tracking |
| **Telemetry** | Metrics ingestion, health monitoring, alerts |

### Contracts

Firmware and cloud communicate via MQTT with JSON payloads. Schemas in [docs/contracts/](docs/contracts/) define the interface and enable independent testing.

---

## Documentation

| Document | Description |
|----------|-------------|
| [PRD_RS1.md](docs/PRD_RS1.md) | Product requirements and user stories |
| [REQUIREMENTS_RS1.md](docs/REQUIREMENTS_RS1.md) | Functional requirements |
| [Firmware README](docs/firmware/README.md) | HardwareOS architecture |
| [Cloud README](docs/cloud/README.md) | Cloud services architecture |
| [MQTT Protocol](docs/contracts/PROTOCOL_MQTT.md) | Firmware-cloud communication |
| [Integration Tests](docs/testing/INTEGRATION_TESTS.md) | Cross-module test scenarios |

---

## Development Status

| Phase | Status | Description |
|-------|--------|-------------|
| **Specification** | Complete | PRD, requirements, module specs |
| **Architecture** | Complete | Firmware (11 modules) + Cloud (4 services) |
| **Contracts** | Complete | MQTT protocol, JSON schemas |
| **Firmware** | Not Started | ESP-IDF implementation |
| **Cloud** | Not Started | Cloudflare Workers implementation |
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
- [Cloudflare Workers](https://developers.cloudflare.com/workers/)
