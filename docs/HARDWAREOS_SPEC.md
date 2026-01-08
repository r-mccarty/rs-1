# HardwareOS Specification (RS-1)

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware
Status: Draft

## 1. Scope

HardwareOS is the custom firmware stack for RS-1 that provides radar processing, target tracking, zone occupancy, and an ESPHome Native API compatible interface. This document defines module boundaries and responsibilities.

## 2. Goals

- Reliable zone-based presence with low flicker under occlusion.
- ESPHome Native API compatibility for Home Assistant.
- Local-first operation with optional cloud OTA.
- Maintainable, testable module boundaries.

## 3. Non-Goals (MVP)

- Multi-sensor fusion.
- Full mobile app implementation.
- Cloud analytics and remote access (firmware side only provides hooks).

## 4. System Overview

HardwareOS consumes LD2450 detections at a fixed frame rate (assumed 10 Hz pending confirmation), produces tracked targets, and emits per-zone occupancy state through a curated entity interface.

## 5. Module Index

### 5.1 Core Data Path

- M01 Radar Ingest
  - Parses LD2450 frames into detections.
- M02 Tracking
  - Associates detections to tracks, predicts through occlusion.
- M03 Zone Engine
  - Maps tracks to software-defined zones and outputs occupancy.
- M04 Presence Smoothing
  - Hysteresis and hold logic for stable occupancy.

### 5.2 Interfaces

- M05 Native API Server
  - ESPHome-compatible device and entity interface.
- M06 Device Config Store
  - Persistent storage for zones, sensitivity, and calibration.
- M07 OTA Manager
  - Cloud-triggered OTA, local update fallback, rollback hooks.

### 5.3 System Services

- M08 Timebase / Scheduler
  - Frame timing, task scheduling, watchdog integration.
- M09 Logging + Diagnostics
  - Local logs and optional telemetry hooks.
- M10 Security
  - Firmware signature validation, transport security.

## 6. Module Responsibilities

### M01 Radar Ingest

- Parse LD2450 UART frames into detections.
- Normalize to a consistent coordinate frame.
- Provide timestamped detections to Tracking.

### M02 Tracking

- Maintain target tracks with position/velocity state.
- Assign detections to tracks using gated association.
- Predict track positions through short occlusions.
- Retire stale tracks after timeout.

### M03 Zone Engine

- Maintain an unlimited zone map in firmware.
- Determine track-to-zone membership.
- Emit occupancy updates per zone.

### M04 Presence Smoothing

- Apply hysteresis/hold to zone occupancy.
- Provide tunable parameters (exposed as sensitivity).

### M05 Native API Server

- Expose curated entities to Home Assistant.
- Provide device info and health status.
- Avoid raw radar entities by default.

### M06 Device Config Store

- Persist zones, sensitivity, and calibration.
- Provide atomic read/write and versioning.

### M07 OTA Manager

- Execute OTA updates using ESP-IDF workflows.
- Support rollback on failed boot.
- See `docs/RS1_OTA_SPEC.md` for OTA interfaces and flow.

### M08 Timebase / Scheduler

- Keep radar frame cadence stable.
- Schedule periodic tasks (health check, housekeeping).

### M09 Logging + Diagnostics

- Provide local logs for debugging.
- Optional telemetry hooks for opt-in metrics.

### M10 Security

- Validate firmware signatures.
- Enforce secure transport for OTA.

## 7. Dependencies

- ESP-IDF (OTA, networking, storage).
- LD2450 protocol specification (UART).
- ESPHome Native API protocol spec.

## 8. Open Questions

- Confirm actual LD2450 frame rate and timing jitter.
- Define memory budget for track/zone limits on ESP32-C3-MINI-1.
- Define configuration schema for zones and sensitivity.
