# HardwareOS Specification (RS-1)

Version: 0.2
Date: 2026-01-08
Owner: OpticWorks Firmware
Status: Draft

---

## 1. Overview

HardwareOS is the custom firmware stack for RS-1 that transforms raw LD2450 radar data into stable, zone-based presence events for Home Assistant. It provides a complete pipeline from hardware interface to cloud connectivity, with a focus on reliability, testability, and seamless smart home integration.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              HardwareOS                                      │
│                                                                              │
│    ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐  │
│    │ LD2450  │───▶│ Tracking│───▶│  Zone   │───▶│Smoothing│───▶│   HA    │  │
│    │ Radar   │    │ Engine  │    │ Engine  │    │ Filter  │    │ Native  │  │
│    │         │    │         │    │         │    │         │    │   API   │  │
│    └─────────┘    └─────────┘    └─────────┘    └─────────┘    └─────────┘  │
│         │                             │                             │        │
│         │              ┌──────────────┴──────────────┐              │        │
│         ▼              ▼                             ▼              ▼        │
│    ┌─────────────────────────────────────────────────────────────────────┐  │
│    │                      System Services Layer                          │  │
│    │   Config Store  │  Timebase  │  Logging  │  Security  │  OTA       │  │
│    └─────────────────────────────────────────────────────────────────────┘  │
│                                      │                                       │
│                                      ▼                                       │
│    ┌─────────────────────────────────────────────────────────────────────┐  │
│    │                         ESP-IDF / Hardware                          │  │
│    │         UART  │  NVS  │  Wi-Fi  │  TLS  │  Timers  │  Watchdog     │  │
│    └─────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Goals

| Goal | Description |
|------|-------------|
| **Reliable Presence** | Zone-based occupancy with low flicker, even during brief occlusions |
| **HA Integration** | ESPHome Native API compatibility for seamless Home Assistant discovery |
| **Local-First** | Full functionality without cloud; cloud enhances but isn't required |
| **Maintainable** | Clear module boundaries with testable interfaces |
| **Secure** | Signed firmware, encrypted transport, secure boot chain |

## 3. Non-Goals (MVP)

- Multi-sensor fusion (single radar only)
- Full mobile app (zone editor is web-based)
- Cloud analytics dashboard (telemetry hooks only)
- 3D presence detection (2D zones only)

---

## 4. System Architecture

### 4.1 Data Flow Pipeline

The core processing pipeline runs at radar frame rate (~33 Hz) with throttled output to Home Assistant (~10 Hz):

```
                                    CORE DATA PATH
    ════════════════════════════════════════════════════════════════════════

    ┌──────────────┐         ┌──────────────┐         ┌──────────────┐
    │   LD2450     │  UART   │     M01      │ detect  │     M02      │
    │   Sensor     │────────▶│ Radar Ingest │────────▶│   Tracking   │
    │              │ 33 Hz   │              │  frame  │              │
    └──────────────┘         └──────────────┘         └──────────────┘
                                    │                        │
                    ┌───────────────┘                        │
                    │  timestamp                             │ tracks
                    ▼                                        ▼
             ┌──────────────┐                        ┌──────────────┐
             │     M08      │                        │     M03      │
             │   Timebase   │                        │  Zone Engine │
             │              │                        │              │
             └──────────────┘                        └──────────────┘
                                                            │
                    ┌───────────────────────────────────────┘
                    │  zone occupancy (raw)
                    ▼
             ┌──────────────┐         ┌──────────────┐         ┌──────────┐
             │     M04      │ smooth  │     M05      │ protobuf│   Home   │
             │  Presence    │────────▶│  Native API  │────────▶│Assistant │
             │  Smoothing   │  state  │   Server     │  10 Hz  │          │
             └──────────────┘         └──────────────┘         └──────────┘

    ════════════════════════════════════════════════════════════════════════
```

### 4.2 Module Layers

```
    ┌─────────────────────────────────────────────────────────────────────┐
    │                         EXTERNAL INTERFACES                         │
    │                                                                     │
    │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────┐ │
    │  │     M05     │  │     M07     │  │     M11     │  │    M09     │ │
    │  │ Native API  │  │     OTA     │  │ Zone Editor │  │  Logging   │ │
    │  │   Server    │  │   Manager   │  │  Interface  │  │ (Telemetry)│ │
    │  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └─────┬──────┘ │
    └─────────┼────────────────┼────────────────┼────────────────┼───────┘
              │                │                │                │
              │ entities       │ firmware       │ zones          │ metrics
              │                │                │                │
    ┌─────────┼────────────────┼────────────────┼────────────────┼───────┐
    │         │          CORE PROCESSING        │                │       │
    │         │                │                │                │       │
    │  ┌──────┴──────┐         │         ┌──────┴──────┐         │       │
    │  │     M04     │         │         │     M06     │         │       │
    │  │  Smoothing  │◀────────┼─────────│Config Store │◀────────┼───────│
    │  └──────┬──────┘         │         └──────┬──────┘         │       │
    │         │                │                │                │       │
    │  ┌──────┴──────┐         │         ┌──────┴──────┐         │       │
    │  │     M03     │         │         │     M10     │         │       │
    │  │ Zone Engine │         │         │  Security   │◀────────┘       │
    │  └──────┬──────┘         │         └─────────────┘                 │
    │         │                │                                         │
    │  ┌──────┴──────┐         │                                         │
    │  │     M02     │         │                                         │
    │  │  Tracking   │         │                                         │
    │  └──────┬──────┘         │                                         │
    │         │                │                                         │
    │  ┌──────┴──────┐  ┌──────┴──────┐  ┌─────────────┐                 │
    │  │     M01     │  │     M08     │  │     M09     │                 │
    │  │Radar Ingest │  │  Timebase   │  │   Logging   │                 │
    │  └─────────────┘  └─────────────┘  └─────────────┘                 │
    │                                                                     │
    └─────────────────────────────────────────────────────────────────────┘
              │                │                │
    ┌─────────┴────────────────┴────────────────┴────────────────────────┐
    │                          HARDWARE / ESP-IDF                         │
    │                                                                     │
    │     UART        NVS        Wi-Fi       TLS       Timers    WDT     │
    │   (LD2450)   (Config)    (Network)  (Crypto)   (Sched)   (Health) │
    └─────────────────────────────────────────────────────────────────────┘
```

### 4.3 Zone Processing Detail

```
                              ZONE PROCESSING FLOW
    ════════════════════════════════════════════════════════════════════════

    Input: Confirmed tracks from M02
    ┌────────────────────────────────────────────┐
    │  Track 1: (1200, 2400) conf=92 moving      │
    │  Track 2: (800, 1800)  conf=78 stationary  │
    │  Track 3: (---, ----)  invalid             │
    └────────────────────────────────────────────┘
                            │
                            ▼
    ┌────────────────────────────────────────────┐
    │           Exclude Zone Check               │
    │                                            │
    │   ┌──────────────┐                         │
    │   │   Exclude    │  Track in exclude?      │
    │   │    Zone      │  ───▶ Remove from list  │
    │   │  (window)    │                         │
    │   └──────────────┘                         │
    └────────────────────────────────────────────┘
                            │
                            ▼
    ┌────────────────────────────────────────────┐
    │         Point-in-Polygon (per zone)        │
    │                                            │
    │   Zone "kitchen"          Zone "hallway"   │
    │   ┌─────────────┐        ┌─────────────┐   │
    │   │  ●Track 1   │        │             │   │
    │   │      ●Track2│        │   ●Track 1  │   │
    │   │             │        │             │   │
    │   └─────────────┘        └─────────────┘   │
    │   occupied=true          occupied=true     │
    │   count=2                count=1           │
    └────────────────────────────────────────────┘
                            │
                            ▼
    ┌────────────────────────────────────────────┐
    │          Presence Smoothing (M04)          │
    │                                            │
    │   State Machine per Zone:                  │
    │                                            │
    │   VACANT ──▶ ENTERING ──▶ OCCUPIED         │
    │      ▲                        │            │
    │      │                        ▼            │
    │      └─────── HOLDING ◀───────┘            │
    │                                            │
    │   Output: Stable occupancy (no flicker)    │
    └────────────────────────────────────────────┘
                            │
                            ▼
    ┌────────────────────────────────────────────┐
    │              M05 Native API                │
    │                                            │
    │   binary_sensor.rs1_kitchen_occupancy: ON  │
    │   sensor.rs1_kitchen_target_count: 2       │
    │   binary_sensor.rs1_hallway_occupancy: ON  │
    │   sensor.rs1_hallway_target_count: 1       │
    └────────────────────────────────────────────┘
```

---

## 5. Module Index

### 5.1 Core Data Path

These modules process radar data into presence events:

| Module | Name | Purpose | Spec |
|--------|------|---------|------|
| **M01** | Radar Ingest | Parse LD2450 UART frames into timestamped detections | [HARDWAREOS_MODULE_RADAR_INGEST.md](HARDWAREOS_MODULE_RADAR_INGEST.md) |
| **M02** | Tracking | Associate detections to tracks, predict through occlusion | [HARDWAREOS_MODULE_TRACKING.md](HARDWAREOS_MODULE_TRACKING.md) |
| **M03** | Zone Engine | Map tracks to polygon zones, determine occupancy | [HARDWAREOS_MODULE_ZONE_ENGINE.md](HARDWAREOS_MODULE_ZONE_ENGINE.md) |
| **M04** | Presence Smoothing | Apply hysteresis and hold logic for stable output | [HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md](HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md) |

### 5.2 External Interfaces

These modules handle communication with external systems:

| Module | Name | Purpose | Spec |
|--------|------|---------|------|
| **M05** | Native API Server | ESPHome-compatible API for Home Assistant | [HARDWAREOS_MODULE_NATIVE_API.md](HARDWAREOS_MODULE_NATIVE_API.md) |
| **M07** | OTA Manager | Cloud-triggered updates with rollback | [HARDWAREOS_MODULE_OTA.md](HARDWAREOS_MODULE_OTA.md) |
| **M11** | Zone Editor Interface | Zone config sync and live target streaming | [HARDWAREOS_MODULE_ZONE_EDITOR.md](HARDWAREOS_MODULE_ZONE_EDITOR.md) |

### 5.3 System Services

These modules provide infrastructure for all other modules:

| Module | Name | Purpose | Spec |
|--------|------|---------|------|
| **M06** | Device Config Store | Persistent, versioned storage for all configuration | [HARDWAREOS_MODULE_CONFIG_STORE.md](HARDWAREOS_MODULE_CONFIG_STORE.md) |
| **M08** | Timebase / Scheduler | Frame timing, task scheduling, watchdog | [HARDWAREOS_MODULE_TIMEBASE.md](HARDWAREOS_MODULE_TIMEBASE.md) |
| **M09** | Logging + Diagnostics | Local logs, metrics, optional cloud telemetry | [HARDWAREOS_MODULE_LOGGING.md](HARDWAREOS_MODULE_LOGGING.md) |
| **M10** | Security | Secure boot, firmware signing, transport security | [HARDWAREOS_MODULE_SECURITY.md](HARDWAREOS_MODULE_SECURITY.md) |

---

## 6. Module Responsibilities

### M01 Radar Ingest

**Input**: LD2450 UART frames (40 bytes @ 33 Hz)
**Output**: `detection_frame_t` with up to 3 targets

- Parse binary UART frames per LD2450 protocol
- Validate frame checksums and reject malformed data
- Normalize coordinates to consistent reference frame
- Apply basic filtering (range gate, speed sanity)
- Timestamp frames for downstream synchronization

### M02 Tracking

**Input**: Detection frames from M01
**Output**: Confirmed tracks with position, velocity, confidence

- Maintain track state using Kalman or alpha-beta filter
- Associate detections to tracks via gated nearest-neighbor
- Predict track positions through brief occlusions
- Manage track lifecycle (tentative → confirmed → retired)
- Provide confidence scores for downstream smoothing

### M03 Zone Engine

**Input**: Confirmed tracks from M02, zone config from M06
**Output**: Per-zone occupancy state (raw)

- Evaluate point-in-polygon for each track against each zone
- Handle exclude zones (filter false positives from windows, fans)
- Support overlapping include zones
- Emit zone entry/exit events for M04

### M04 Presence Smoothing

**Input**: Raw zone occupancy from M03
**Output**: Stable, flicker-free occupancy state

- Implement state machine: VACANT → ENTERING → OCCUPIED → HOLDING
- Apply configurable hold times based on sensitivity setting
- Use track confidence to extend hold during likely occlusions
- Throttle output rate for M05 publishing

### M05 Native API Server

**Input**: Smoothed state from M04
**Output**: ESPHome Native API messages to Home Assistant

- Implement ESPHome Native API protocol (Protobuf over TCP)
- Support Noise protocol encryption
- Advertise via mDNS for auto-discovery
- Publish curated entities only (no raw radar data by default)

### M06 Device Config Store

**Input**: Config updates from M11, API, or OTA
**Output**: Persistent configuration for all modules

- Store zones, sensitivity, network, security settings
- Provide atomic writes with rollback on failure
- Version configs for sync with zone editor
- Encrypt sensitive data (passwords, keys) at rest

### M07 OTA Manager

**Input**: MQTT trigger from cloud
**Output**: Updated firmware with verification

- Receive OTA triggers via MQTT
- Download firmware via HTTPS with progress reporting
- Validate signature before applying (M10 integration)
- Support automatic rollback on failed boot

### M08 Timebase / Scheduler

**Input**: Hardware timers
**Output**: Timing services for all modules

- Maintain monotonic system time
- Track radar frame timing and detect missed frames
- Schedule periodic tasks (health check, telemetry flush)
- Integrate with hardware watchdog for fault recovery

### M09 Logging + Diagnostics

**Input**: Log calls from all modules
**Output**: Serial console, RAM buffer, optional cloud

- Multi-level logging (Error, Warning, Info, Debug, Verbose)
- Maintain ring buffer for recent log access
- Optional persistent logging to flash
- Collect and publish telemetry metrics (opt-in)

### M10 Security

**Input**: Firmware images, network connections
**Output**: Verified firmware, encrypted transport

- Validate firmware signatures (ECDSA P-256)
- Enforce secure boot chain
- Provide TLS for MQTT and HTTPS
- Manage device identity and API authentication

### M11 Zone Editor Interface

**Input**: Zone config from mobile/web editor
**Output**: Updated zones in M06, live target stream

- Accept zone updates via local REST API or cloud MQTT
- Stream live target positions via WebSocket
- Validate zone geometry before applying
- Support local-first and cloud-assisted modes

---

## 7. Inter-Module Communication

```
    ┌─────────────────────────────────────────────────────────────────────┐
    │                    MODULE DEPENDENCY GRAPH                          │
    │                                                                     │
    │                          ┌───────┐                                  │
    │                          │  M05  │◀──────────────┐                  │
    │                          │Native │               │                  │
    │                          │  API  │               │                  │
    │                          └───┬───┘               │                  │
    │                              │                   │                  │
    │                              ▼                   │                  │
    │    ┌───────┐            ┌───────┐            ┌───────┐              │
    │    │  M01  │───────────▶│  M02  │───────────▶│  M03  │──────┐       │
    │    │ Radar │  detects   │ Track │   tracks   │ Zone  │      │       │
    │    │Ingest │            │       │            │Engine │      │       │
    │    └───┬───┘            └───────┘            └───┬───┘      │       │
    │        │                                         │          │       │
    │        │                                         ▼          ▼       │
    │        │                    ┌───────┐        ┌───────┐  ┌───────┐   │
    │        │                    │  M06  │◀───────│  M04  │  │  M11  │   │
    │        │                    │Config │ config │Smooth │  │ Zone  │   │
    │        │                    │ Store │        │       │  │Editor │   │
    │        │                    └───┬───┘        └───────┘  └───┬───┘   │
    │        │                        │                           │       │
    │        ▼                        ▼                           │       │
    │    ┌───────┐            ┌───────┐            ┌───────┐      │       │
    │    │  M08  │            │  M10  │◀───────────│  M07  │◀─────┘       │
    │    │ Time  │            │ Secur │  verify    │  OTA  │              │
    │    │ base  │            │  ity  │            │       │              │
    │    └───┬───┘            └───────┘            └───────┘              │
    │        │                                                            │
    │        └────────────────────┐                                       │
    │                             ▼                                       │
    │                         ┌───────┐                                   │
    │                         │  M09  │◀─── all modules log here          │
    │                         │ Log   │                                   │
    │                         └───────┘                                   │
    │                                                                     │
    └─────────────────────────────────────────────────────────────────────┘

    Legend:
    ────▶  Data flow
    ◀────  Configuration/services
```

---

## 8. Hardware Platform

### 8.1 Target Hardware

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32-C3-MINI-1 (RISC-V, 160MHz, 400KB SRAM, 4MB Flash) |
| **Radar** | HiLink LD2450 (24GHz FMCW, 3 targets, 6m range) |
| **Interface** | UART @ 256000 baud |
| **Power** | USB-C, 5V |

### 8.2 Resource Budget

| Resource | Budget | Usage |
|----------|--------|-------|
| Flash (App) | 1.5 MB | Firmware image |
| Flash (NVS) | 16 KB | Configuration |
| Flash (OTA) | 1.5 MB | OTA partition |
| RAM (Heap) | 200 KB | Runtime allocations |
| RAM (Stack) | 16 KB | Task stacks |

### 8.3 Memory Map

```
    Flash Layout (4MB)
    ┌──────────────────────────┐ 0x000000
    │     Bootloader (32KB)    │
    ├──────────────────────────┤ 0x008000
    │   Partition Table (4KB)  │
    ├──────────────────────────┤ 0x009000
    │       NVS (16KB)         │  ◀── M06 Config Store
    ├──────────────────────────┤ 0x00D000
    │    OTA Data (8KB)        │  ◀── M07 OTA state
    ├──────────────────────────┤ 0x00F000
    │      App OTA_0           │
    │      (1.5MB)             │  ◀── Active firmware
    ├──────────────────────────┤ 0x18F000
    │      App OTA_1           │
    │      (1.5MB)             │  ◀── Update partition
    ├──────────────────────────┤ 0x30F000
    │    SPIFFS/Logs (64KB)    │  ◀── M09 persistent logs
    ├──────────────────────────┤ 0x31F000
    │      Reserved            │
    └──────────────────────────┘ 0x400000
```

---

## 9. Key Assumptions

These assumptions underpin the module specifications. If any change, review the affected modules.

| ID | Assumption | Affects |
|----|------------|---------|
| A1 | LD2450 frame rate is ~33 Hz (30ms interval) | M01, M02, M08 |
| A2 | LD2450 reports max 3 targets per frame | M01, M02, M03 |
| A3 | Coordinate range: X ±6000mm, Y 0-6000mm | M01, M03 |
| A4 | ESP32-C3 single-core operation | M08 (no SMP) |
| A5 | ESPHome Native API v1.9+ | M05 |
| A6 | Home Assistant 2024.1+ | M05 |
| A7 | MQTT for cloud (OTA, telemetry) | M07, M09 |
| A8 | Typical occlusions < 2 seconds | M04 |
| A9 | User prefers false occupancy over false vacancy | M04 |

---

## 10. External Dependencies

| Dependency | Version | Modules | Purpose |
|------------|---------|---------|---------|
| ESP-IDF | 5.x | All | Framework, drivers, networking |
| mbedTLS | 3.x | M05, M07, M10 | TLS, crypto |
| Protobuf (nanopb) | latest | M05 | Native API encoding |
| Noise-C | latest | M05 | API encryption |
| lwIP | ESP-IDF | M05, M07 | TCP/IP stack |
| FreeRTOS | 10.x | M08 | Task scheduling |

---

## 11. Testing Strategy

### 11.1 Test Levels

```
    ┌─────────────────────────────────────────────────────────────────────┐
    │                         TEST PYRAMID                                │
    │                                                                     │
    │                           /\                                        │
    │                          /  \                                       │
    │                         / E2E\     Field tests with real hardware   │
    │                        /______\                                     │
    │                       /        \                                    │
    │                      /Integration\   Cross-module, simulated radar  │
    │                     /______________\                                │
    │                    /                \                               │
    │                   /    Unit Tests    \   Per-module, mocked deps    │
    │                  /____________________\                             │
    │                                                                     │
    └─────────────────────────────────────────────────────────────────────┘
```

### 11.2 CI/CD Integration

- Unit tests run on every commit
- Integration tests run on PR merge
- Hardware-in-loop tests on release candidates

---

## 12. Open Questions

| Question | Owner | Status |
|----------|-------|--------|
| Confirm actual LD2450 frame rate and jitter | Firmware | Open |
| Define max practical zone count for memory budget | Firmware | Open |
| Finalize zone sensitivity UX (per-zone vs global) | Product | Open |
| Flash encryption for production units | Security | Open |
| Multi-connection support for Native API | Firmware | Open |

---

## 13. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-XX | OpticWorks | Initial draft |
| 0.2 | 2026-01-08 | OpticWorks | Added ASCII diagrams, module links, architecture details |
