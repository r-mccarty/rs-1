# OpticWorks RS-1 Technical Requirements

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Product + Engineering
Status: Draft

## 1. System Overview

RS-1 is a single-radar presence sensor that exposes zone-based occupancy via an ESPHome Native API compatible server, supports mobile onboarding, and uses cloud-push OTA updates with local fallback.

## 2. Hardware Requirements

- Radar: LD2450 tracking radar module.
- MCU: ESP32-C3-MINI-1 (custom firmware, Wi-Fi, OTA, sufficient RAM for zone processing).
- Power: USB-C for base model; PoE variant is out of scope for MVP.
- LED: Optional status LED (TBD).
- Physical: Form factor TBD, but must support wall or ceiling mount.

## 3. Firmware Requirements (HardwareOS)

### 3.1 Radar Processing

- Parse LD2450 target coordinates for up to 3 targets.
- Maintain a software zone map; unlimited zone count in firmware.
- Output per-zone occupancy with configurable sensitivity based on z-score analysis.
- Provide smoothing/holding logic to reduce target dropouts.

### 3.2 ESPHome Native API Compatibility

- Implement a Native API server compatible with HA auto-discovery.
- Support standard device info and entity state updates.
- Expose curated entities only (zone occupancy, device health).

### 3.3 Local Web UI (Optional)

- Provide minimal LAN UI for device status and local update fallback.

### 3.4 OTA

- Cloud-triggered OTA with signed firmware.
- Local update mechanism for offline users.
- Rollback strategy aligned with ESP-IDF/ESP32 approved OTA update flows.

## 4. Mobile App Requirements

- QR code-based device identification + Wi-Fi credential entry.
- Zone setup and editing (drag/resize/rename).
- AR-assisted room scanning (post-beta; iOS RoomPlan API).
- OTA update control and progress status.
- Show device health and firmware version.

## 5. Cloud Services Requirements

- Device registry and authentication.
- OTA hosting and staged rollout controls.
- Optional analytics and remote access (subscription).
- Data retention and privacy policy definition (TBD).

## 6. Security Requirements

- Signed firmware verification on device.
- Secure OTA transport (TLS).
- Device pairing and local discovery must be authenticated.
- No mandatory cloud dependency for presence detection or HA integration.

## 7. Integration Requirements

- Home Assistant discovery should match ESPHome device behavior.
- Entities: per-zone occupancy, device health, signal quality (optional).
- Avoid raw radar entities to preserve unified UX.

## 8. Telemetry and Diagnostics (Optional)

- Local logs for debug.
- Cloud telemetry opt-in with clear user consent.
- Metrics: setup time, update success/fail, zone accuracy (if enabled).

## 9. Constraints and Assumptions

- Single-radar MVP; fusion is out of scope.
- HA integration must work without ESPHome runtime (Native API only).
- Unlimited zones are constrained by MCU resources; limits must be documented.

## 10. Open Technical Decisions

- AR scanning method and minimum device hardware support (post-beta).
- OTA rollback mechanism and storage layout (ESP-IDF guidance).
- Zone sensitivity model details and configuration format (z-score mapping).
