# HardwareOS Specification (RS-1)

Version: 0.3
Date: 2026-01-15
Owner: OpticWorks Firmware
Status: Draft

---

## 1. Overview

HardwareOS is the custom firmware stack for RS-1 that transforms radar data into stable, zone-based presence events for Home Assistant. It supports two product variants (RS-1 Lite and RS-1 Pro) with different radar configurations and processing pipelines. The firmware provides a complete pipeline from hardware interface to cloud connectivity, with a focus on reliability, testability, and seamless smart home integration.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              HardwareOS                                      │
│                                                                              │
│    ┌─────────┐                                                               │
│    │ LD2410  │──┐                                                            │
│    │(presence)│  │   ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌───────┐ │
│    └─────────┘  ├──▶│ Radar   │───▶│ Tracking│───▶│Smoothing│───▶│  HA   │ │
│    ┌─────────┐  │   │ Ingest  │    │ + Zone  │    │ Filter  │    │Native │ │
│    │ LD2450  │──┘   │  (M01)  │    │(M02/M03)│    │  (M04)  │    │ (M05) │ │
│    │(tracking)│     └─────────┘    └─────────┘    └─────────┘    └───────┘ │
│    └─────────┘           │                             │              │      │
│     (Pro only)           │              ┌──────────────┴──────────────┘      │
│                          ▼              ▼                                    │
│    ┌─────────────────────────────────────────────────────────────────────┐  │
│    │                      System Services Layer                          │  │
│    │   Config Store  │  Timebase  │  Logging  │  Security  │  OTA       │  │
│    └─────────────────────────────────────────────────────────────────────┘  │
│                                      │                                       │
│                                      ▼                                       │
│    ┌─────────────────────────────────────────────────────────────────────┐  │
│    │                         ESP-IDF / Hardware                          │  │
│    │       UART×2  │  NVS  │  Wi-Fi  │  TLS  │  Timers  │  Watchdog     │  │
│    └─────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
    Lite: LD2410 only (M01→M04→M05)  |  Pro: LD2410+LD2450 dual-radar fusion
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

## 3. Product Variants

RS-1 ships in two variants with different radar configurations:

| Variant | Radar | Capabilities | Target Use |
|---------|-------|--------------|------------|
| **RS-1 Lite** | LD2410 | Binary presence detection | Utility rooms (bathrooms, hallways, closets) |
| **RS-1 Pro** | LD2410 + LD2450 | Dual radar fusion, zone tracking | Living spaces (living rooms, kitchens, bedrooms) |

### 3.1 Module Activation by Variant

| Module | Lite | Pro | Notes |
|--------|------|-----|-------|
| M01 Radar Ingest | LD2410 parser | LD2450 parser | Different protocols |
| M02 Tracking | **Disabled** | Full Kalman | Not compiled for Lite |
| M03 Zone Engine | **Disabled** | Full | Not compiled for Lite |
| M04 Smoothing | Basic | Full | Lite: simplified hold time |
| M05-M11 | Same | Same | Hardware-agnostic |

### 3.2 Lite Processing Pipeline

```
RS-1 Lite: M01 (LD2410) ──▶ M04 (Smoothing) ──▶ M05 (Native API)
                                │
                                └── Binary presence only, no zone tracking
```

### 3.3 Pro Processing Pipeline

RS-1 Pro uses **dual-radar fusion** with both LD2410 and LD2450 in time-division multiplexed mode:

```
             ┌──────────────┐
             │    LD2410    │  (presence confidence, ~5 Hz)
             │   (UART 1)   │
             └──────┬───────┘
                    │
                    ▼
RS-1 Pro: M01 (Dual Radar) ──▶ M02 (Tracking) ──▶ M03 (Zone Engine) ──▶ M04 ──▶ M05
                    ▲                  │
                    │                  └── Coordinates + presence confidence
             ┌──────┴───────┐
             │    LD2450    │  (target tracking, 33 Hz)
             │   (UART 2)   │
             └──────────────┘

Data Fusion Strategy:
• LD2450 provides X/Y coordinates and velocity for up to 3 targets
• LD2410 provides binary presence confidence as additional input to smoothing
• M01 multiplexes both UART streams into unified detection output
```

### 3.4 SMP Architecture (Dual-Core)

ESP32-WROOM-32E is dual-core. Tasks are pinned to cores for optimal performance:

| Core | Tasks | Rationale |
|------|-------|-----------|
| **Core 0** | Wi-Fi stack, M05 Native API, M07 OTA, M09 Logging | Network-bound operations |
| **Core 1** | M01 Radar Ingest, M02 Tracking, M04 Smoothing | Time-critical radar processing |

This prevents TLS handshakes (~500ms) from blocking radar frame processing (33 Hz).

---

## 4. Non-Goals (MVP)

- Full mobile app (zone editor is web-based)
- Cloud analytics dashboard (telemetry hooks only)
- 3D presence detection (2D zones only)
- LD2410 zone tracking (Lite variant uses binary presence only)

---

## 5. System Architecture

### 5.1 Data Flow Pipeline (RS-1 Pro)

The core processing pipeline uses **dual-radar fusion** with time-division multiplexing. LD2450 runs at 33 Hz for tracking, LD2410 at ~5 Hz for presence confidence. **Note:** RS-1 Lite uses a simplified M01→M04→M05 path with LD2410 only (see Section 3).

```
                                    CORE DATA PATH (RS-1 Pro)
    ════════════════════════════════════════════════════════════════════════

    ┌──────────────┐                 ┌──────────────┐         ┌──────────────┐
    │   LD2410     │  UART 1         │              │ detect  │     M02      │
    │   Sensor     │──(~5 Hz)───────▶│     M01      │────────▶│   Tracking   │
    │  (presence)  │                 │ Radar Ingest │  frame  │              │
    └──────────────┘                 │              │         └──────────────┘
                                     │  (Dual-Radar │                │
    ┌──────────────┐                 │   TDM Mux)   │                │
    │   LD2450     │  UART 2         │              │                │ tracks
    │   Sensor     │──(33 Hz)───────▶│              │                ▼
    │  (tracking)  │                 └──────────────┘         ┌──────────────┐
    └──────────────┘                        │                 │     M03      │
                                            │                 │  Zone Engine │
                    ┌───────────────────────┘                 │              │
                    │  timestamp                              └──────────────┘
                    ▼                                                │
             ┌──────────────┐                                        │
             │     M08      │                                        │
             │   Timebase   │                                        │
             │              │                                        │
             └──────────────┘                                        │
                                     ┌───────────────────────────────┘
                                     │  zone occupancy (raw)
                                     ▼
             ┌──────────────┐         ┌──────────────┐         ┌──────────┐
             │     M04      │ smooth  │     M05      │ protobuf│   Home   │
             │  Presence    │────────▶│  Native API  │────────▶│Assistant │
             │  Smoothing   │  state  │   Server     │  10 Hz  │          │
             └──────────────┘         └──────────────┘         └──────────┘

    ════════════════════════════════════════════════════════════════════════

    Dual-Radar Fusion:
    • LD2450 (UART 2, 33 Hz): Primary tracking - provides X/Y/velocity for 3 targets
    • LD2410 (UART 1, ~5 Hz): Presence confidence - binary presence as smoothing input
    • M01 multiplexes both streams, outputs unified detection_frame_t to M02
```

### 5.2 Module Layers

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

### 5.3 Zone Processing Detail (RS-1 Pro)

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

## 6. Module Index

### 6.1 Core Data Path

These modules process radar data into presence events:

| Module | Name | Purpose | Spec |
|--------|------|---------|------|
| **M01** | Radar Ingest | Parse LD2450 UART frames into timestamped detections | [HARDWAREOS_MODULE_RADAR_INGEST.md](HARDWAREOS_MODULE_RADAR_INGEST.md) |
| **M02** | Tracking | Associate detections to tracks, predict through occlusion | [HARDWAREOS_MODULE_TRACKING.md](HARDWAREOS_MODULE_TRACKING.md) |
| **M03** | Zone Engine | Map tracks to polygon zones, determine occupancy | [HARDWAREOS_MODULE_ZONE_ENGINE.md](HARDWAREOS_MODULE_ZONE_ENGINE.md) |
| **M04** | Presence Smoothing | Apply hysteresis and hold logic for stable output | [HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md](HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md) |

### 6.2 External Interfaces

These modules handle communication with external systems:

| Module | Name | Purpose | Spec |
|--------|------|---------|------|
| **M05** | Native API Server | ESPHome-compatible API for Home Assistant | [HARDWAREOS_MODULE_NATIVE_API.md](HARDWAREOS_MODULE_NATIVE_API.md) |
| **M07** | OTA Manager | Cloud-triggered updates with rollback | [HARDWAREOS_MODULE_OTA.md](HARDWAREOS_MODULE_OTA.md) |
| **M11** | Zone Editor Interface | Zone config sync and live target streaming | [HARDWAREOS_MODULE_ZONE_EDITOR.md](HARDWAREOS_MODULE_ZONE_EDITOR.md) |

### 6.3 System Services

These modules provide infrastructure for all other modules:

| Module | Name | Purpose | Spec |
|--------|------|---------|------|
| **M06** | Device Config Store | Persistent, versioned storage for all configuration | [HARDWAREOS_MODULE_CONFIG_STORE.md](HARDWAREOS_MODULE_CONFIG_STORE.md) |
| **M08** | Timebase / Scheduler | Frame timing, task scheduling, watchdog | [HARDWAREOS_MODULE_TIMEBASE.md](HARDWAREOS_MODULE_TIMEBASE.md) |
| **M09** | Logging + Diagnostics | Local logs, metrics, optional cloud telemetry | [HARDWAREOS_MODULE_LOGGING.md](HARDWAREOS_MODULE_LOGGING.md) |
| **M10** | Security | Secure boot, firmware signing, transport security | [HARDWAREOS_MODULE_SECURITY.md](HARDWAREOS_MODULE_SECURITY.md) |

---

## 7. Module Responsibilities

### M01 Radar Ingest

**Lite Input**: LD2410 UART frames (~5 Hz, binary presence)
**Pro Input**: LD2410 + LD2450 UART frames (time-division multiplexed)
**Output**: `detection_frame_t` with up to 3 targets (Pro) or binary presence (Lite)

- Parse binary UART frames per radar protocol (LD2410 and/or LD2450)
- For Pro: multiplex both UART streams with time-division handling
- Validate frame checksums and reject malformed data
- Normalize coordinates to consistent reference frame (Pro only)
- Apply basic filtering (range gate, speed sanity)
- Timestamp frames for downstream synchronization
- For Pro: fuse LD2410 presence confidence with LD2450 tracking data

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

## 8. Inter-Module Communication

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

## 9. Hardware Platform

### 9.1 Target Hardware

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32-WROOM-32E (Xtensa LX6 dual-core, 240MHz, 520KB SRAM, 8MB Flash) |
| **Radar (Lite)** | HiLink LD2410 (24GHz FMCW, binary presence) |
| **Radar (Pro)** | HiLink LD2410 + LD2450 (dual-radar fusion, time-division multiplexed) |
| **Interface** | UART×2: LD2450 @ 256000 baud, LD2410 @ 115200 baud |
| **USB** | CH340N USB-UART bridge |
| **Power** | USB-C, 5V |
| **Ethernet** | Optional RMII PHY (SR8201F) for PoE variant |

### 9.2 Resource Budget

| Resource | Budget | Usage |
|----------|--------|-------|
| Flash (App) | 3 MB | Firmware image |
| Flash (NVS) | 16 KB | Configuration |
| Flash (OTA) | 3 MB | OTA partition |
| Flash (Logs) | 256 KB | Persistent logs |
| RAM (Heap) | ~250 KB | Runtime allocations |
| RAM (Stack) | 16 KB | Task stacks |

### 9.3 Memory Map

```
    Flash Layout (8MB)
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
    │      (3MB)               │  ◀── Active firmware
    ├──────────────────────────┤ 0x30F000
    │      App OTA_1           │
    │      (3MB)               │  ◀── Update partition
    ├──────────────────────────┤ 0x60F000
    │    SPIFFS/Logs (256KB)   │  ◀── M09 persistent logs
    ├──────────────────────────┤ 0x64F000
    │      Reserved            │
    └──────────────────────────┘ 0x800000
```

---

## 10. Key Assumptions

These assumptions underpin the module specifications. If any change, review the affected modules.

| ID | Assumption | Affects |
|----|------------|---------|
| A1 | LD2450 frame rate is ~33 Hz (30ms interval) | M01, M02, M08 (Pro only) |
| A2 | LD2450 reports max 3 targets per frame | M01, M02, M03 (Pro only) |
| A3 | Coordinate range: X ±6000mm, Y 0-6000mm | M01, M03 (Pro only) |
| A4 | ESP32-WROOM-32E dual-core with task pinning | M08 (Core 0: network, Core 1: radar) |
| A5 | ESPHome Native API v1.9+ | M05 |
| A6 | Home Assistant 2024.1+ | M05 |
| A7 | MQTT for cloud (OTA, telemetry) | M07, M09 |
| A8 | Typical occlusions < 2 seconds | M04 (Pro only) |
| A9 | User prefers false occupancy over false vacancy | M04 |
| A10 | Product variant: Lite (LD2410) or Pro (LD2410+LD2450 dual-radar) | M01, M02, M03, M04 |
| A11 | LD2410 frame rate ~5 Hz, UART 115200 baud | M01 (Both variants) |
| A12 | Pro uses time-division multiplexed dual-radar fusion | M01 (Pro only) |

---

## 11. External Dependencies

| Dependency | Version | Modules | Purpose |
|------------|---------|---------|---------|
| ESP-IDF | 5.x | All | Framework, drivers, networking |
| mbedTLS | 3.x | M05, M07, M10 | TLS, crypto |
| Protobuf (nanopb) | latest | M05 | Native API encoding |
| Noise-C | latest | M05 | API encryption |
| lwIP | ESP-IDF | M05, M07 | TCP/IP stack |
| FreeRTOS | 10.x | M08 | Task scheduling |

---

## 12. Testing Strategy

### 12.1 Test Levels

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

### 12.2 CI/CD Integration

- Unit tests run on every commit
- Integration tests run on PR merge
- Hardware-in-loop tests on release candidates

---

## 13. Open Questions

| Question | Owner | Status |
|----------|-------|--------|
| Confirm actual LD2450 frame rate and jitter | Firmware | Open |
| Define max practical zone count for memory budget | Firmware | Open |
| Finalize zone sensitivity UX (per-zone vs global) | Product | Open |
| Flash encryption for production units | Security | Open |
| Multi-connection support for Native API | Firmware | Open |

---

## 14. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-XX | OpticWorks | Initial draft |
| 0.2 | 2026-01-08 | OpticWorks | Added ASCII diagrams, module links, architecture details |
| 0.3 | 2026-01-15 | OpticWorks | Updated MCU to ESP32-WROOM-32E, added Lite/Pro variant strategy, SMP architecture |
