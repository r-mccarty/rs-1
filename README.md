# RS-1: OpticWorks Presence Sensor

ESP32 + LD2450 mmWave presence sensor for Home Assistant.

## Overview

RS-1 is a compact presence sensor built on the Hi-Link LD2450 24GHz mmWave radar module. It detects up to 3 targets simultaneously with position (X/Y), speed, and movement data.

**Target market**: Home Assistant enthusiasts looking for reliable room presence detection.

## Hardware

| Component | Specification |
|-----------|---------------|
| Radar | Hi-Link LD2450 (24GHz mmWave) |
| MCU | ESP32 |
| Detection range | Up to 6 meters |
| Field of view | 120° horizontal, 60° vertical |
| Targets tracked | Up to 3 simultaneously |
| Update rate | 30ms |
| Connectivity | WiFi (ESPHome) |
| Power | USB-C, 5V |

## Features

- **Plug and play**: Works with Home Assistant out of the box via ESPHome
- **Multi-target tracking**: Position, speed, and movement for up to 3 targets
- **Zone support**: Native polygon zone detection
- **No cloud**: All processing on-device
- **OTA updates**: Update firmware wirelessly

## Roadmap

### v1.0 (January 2025)
- Basic LD2450 presence sensor
- ESPHome firmware
- Resin-printed enclosure
- Home Assistant auto-discovery

### v1.1 (Future)
- TUI visualization interface
- Terminal-based zone configuration
- Real-time target visualization

### v2.0 (RS-1 Full)
- Sensor fusion (camera + mmWave)
- On-device ML for occupancy classification
- Advanced presence analytics

## Project Structure

```
rs-1/
├── README.md
├── docs/
│   └── SPRINT.md          # Launch sprint plan
├── firmware/
│   └── esphome/           # ESPHome configuration
├── hardware/
│   └── enclosure/         # CAD files for enclosure
└── store/                 # Product listing assets
```

## Quick Start

```bash
# Clone the repo
git clone https://github.com/r-mccarty/rs-1.git
cd rs-1

# Flash firmware (requires ESPHome)
cd firmware/esphome
esphome run rs1-presence.yaml
```

## License

MIT License - See [LICENSE](LICENSE) for details.

## Links

- [OpticWorks Store](https://optic.works)
- [Home Assistant](https://home-assistant.io)
- [ESPHome LD2450 Docs](https://esphome.io/components/sensor/ld2450/)
