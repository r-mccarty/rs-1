# OpticWorks RS-1 PRD

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Product
Status: Draft

## 1. Problem Statement

Current mmWave presence sensors trade reliability for usability. Power users must tune multiple sensor entities, learn radar limitations, and work around short zone limits. RS-1 aims to deliver reliable zone-based presence with a consumer-grade setup experience while retaining full local control and native Home Assistant compatibility.

## 2. Goals and Non-Goals

### Goals

- Deliver a 60-second mobile setup experience for most users.
- Provide unlimited software-defined zones with reliable occupancy output.
- Provide ESPHome Native API compatibility for HA discovery.
- Enable cloud-push OTA with local fallback.

### Non-Goals (MVP)

- Multi-sensor fusion (LD2410, PIR, camera).
- Enterprise fleet management.
- Mandatory cloud dependency for core features.

## 3. Target Users

- Prosumer Home Assistant users seeking reliability without ESPHome complexity.
- Technical users constrained by competitor zone limits or manual update workflows.
- Future: mainstream smart home users once app maturity supports it.

## 4. User Journeys

### 4.1 Primary Journey: New Device Setup

1. User opens app and taps "Add Device."
2. App scans QR code to identify RS-1.
3. User provides Wi-Fi credentials (manual entry).
4. Device registers locally and appears in Home Assistant.
5. App prompts for zone creation (AR scan post-beta).
6. User reviews and edits zones, then saves.

### 4.2 Daily Use

1. User checks zone occupancy in Home Assistant.
2. Automations trigger lights/HVAC based on zone state.
3. App shows device health, zone status, and firmware version.

### 4.3 OTA Update

1. App notifies user of new firmware.
2. Update proceeds over cloud, with local fallback if cloud fails.
3. App confirms update and device returns to online state.

## 5. UX Flows

### 5.1 Onboarding Flow

- Entry: "Add Device"
- QR scan to identify device
- Wi-Fi credentials entry
- Device registration
- Manual zone creation (AR scan post-beta)
- Zone review/edit
- Finish + tips

### 5.2 Zone Editing Flow

- List of zones with status chips
- Select zone to edit name, shape, sensitivity
- Drag handles to resize, move
- Save and sync to device

### 5.3 Home Assistant Flow

- RS-1 appears in Integrations as ESPHome-compatible device
- Entities: per-zone occupancy + device health
- Avoid raw radar entities

## 6. Wireframes (ASCII)

### 6.1 Mobile Onboarding

```
+------------------------+
| Add RS-1               |
|                        |
| [ Scan QR ]            |
|                        |
| Aim camera at QR code  |
| to add device.         |
+------------------------+
```

```
+------------------------+
| Wi-Fi Setup            |
|                        |
| Network: [ MyWiFi   v] |
| Password: [ ****** ]   |
|                        |
|        (Connect)       |
+------------------------+
```

```
+------------------------+
| Create Zones           |
|                        |
| (Add Zone)             |
| (Edit Zones)           |
|                        |
| AR scan: post-beta     |
+------------------------+
```

### 6.2 Zone Editor

```
+------------------------+
| Living Room            |
|                        |
| +--------------------+ |
| |  [Zone A]          | |
| |        [Zone B]    | |
| +--------------------+ |
|                        |
| (Add Zone)  (Save)     |
+------------------------+
```

### 6.3 Device Status

```
+------------------------+
| RS-1 Status            |
|                        |
| Firmware: 0.9.2        |
| Health: OK             |
| Zones: 5               |
|                        |
| (Check Update)         |
+------------------------+
```

## 7. Functional Requirements (Product Level)

- Unlimited zones derived from software processing.
- Per-zone occupancy output with sensitivity control (z-score model).
- Local-only operation after provisioning.
- Cloud-push OTA with local fallback.
- HA discovery via ESPHome Native API.

## 8. Success Metrics

- 80% of users complete setup in <= 60 seconds.
- < 5% false occupancy rate in typical rooms.
- OTA success rate >= 99%.
- < 1% of users need manual firmware updates.

## 9. Risks and Mitigations

- Stationary occupant misses with single radar.
  - Mitigation: clear positioning and upgrade path to RS-1 Fusion.
- ESPHome API changes.
  - Mitigation: compatibility tests and version negotiation.
- AR scan quality variability (post-beta).
  - Mitigation: fallback to manual zone creation.

## 10. Open Questions / Decisions Needed

- Mobile app platforms and technology stack (native vs cross-platform).
- Exact AR scan approach and device support requirements (post-beta).
- OTA rollout cadence and rollback strategy.
