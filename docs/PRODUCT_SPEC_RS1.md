# OpticWorks RS-1 Product Specification

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Product
Status: Draft

## 1. Summary

RS-1 is a prosumer-grade mmWave presence sensor focused on a simple, reliable user experience. The product delivers zone-based occupancy using a single LD2450 tracking radar, with a mobile onboarding flow and native Home Assistant discovery via an ESPHome-compatible Native API. RS-1 prioritizes UX and software capability (unlimited zones, cloud-push OTA, AR-assisted setup) over multi-sensor hardware complexity.

## 2. Goals

- Deliver a "one device, one app, 60 seconds" setup flow.
- Provide unlimited software-defined zones with reliable occupancy state per zone.
- Maintain full local operation with Home Assistant integration.
- Offer cloud-push OTA updates and optional subscription services.
- Achieve a $70-80 price point for the base device.

## 3. Non-Goals (MVP)

- Multi-sensor fusion (LD2410, PIR, camera).
- Enterprise fleet management.
- Mandatory cloud dependency for core functions.
- Consumer retail packaging or big-box distribution.

## 4. Target Users and Segments

- Primary: Prosumers using Home Assistant who want reliability without deep ESPHome configuration.
- Secondary: Technical users limited by competitor zone counts or manual update workflows.
- Future: Mainstream smart home users (dependent on mobile app maturity).

## 5. Key User Stories

- As a Home Assistant user, I can add RS-1 and see it discovered like any ESPHome device.
- As a homeowner, I can scan my room and quickly define zones without manual coordinates.
- As a user, I can set a zone's sensitivity with a simple slider and trust the output.
- As a user, I receive OTA firmware updates without manual downloads.
- As a user who avoids the cloud, I can run RS-1 fully locally with LAN access only.

## 6. Functional Requirements

### 6.1 Detection and Zones

- Track up to 3 concurrent targets from LD2450.
- Produce occupancy state per software-defined zone.
- Support unlimited zones through firmware-level processing (not limited by radar native zones).
- Provide configurable zone sensitivity based on z-score analysis.
- Expose a single, unified presence signal per zone (no raw sensor disagreements).

### 6.2 Home Assistant Integration

- Implement ESPHome Native API server compatible with HA auto-discovery.
- Expose curated entities: per-zone occupancy, optional signal quality, device health.
- Avoid exposing raw radar entities or multi-sensor confusion.

### 6.3 Mobile Setup

- Mobile onboarding with QR-based device identification and Wi-Fi credentials entry.
- AR-assisted room scanning to propose zone boundaries (post-beta).
- Manual zone editing in app (drag, resize, name).

### 6.4 OTA Updates

- Cloud-push OTA with staged rollout controls.
- Manual local update fallback (LAN).

### 6.5 Local-First Operation

- Device works without internet after setup (local HA + local UI).
- Cloud features are optional and additive.

## 7. Non-Functional Requirements

- Latency: occupancy state updates within 1 second of target movement.
- Reliability: stable occupancy for micro-movements with minimal dropouts.
- Security: encrypted OTA delivery, signed firmware verification.
- Privacy: no cloud requirement for presence detection or HA integration.
- Availability: local operation unaffected by cloud outages.

## 8. UX Principles

- Hide hardware complexity; present a single confident presence answer.
- Minimize entities in Home Assistant.
- Make setup feel consumer-grade (app-first, guided steps).
- Provide sensible defaults; advanced settings are optional.

## 9. Product Positioning

- "The presence sensor that doesn't require a PhD to set up."
- Differentiation: UX-first, unlimited zones, cloud-push OTA, HA-native.

## 10. Pricing and Packaging

- Base RS-1 MSRP: $70-80.
- Optional subscription: $3-5/month for remote access, AR scanning, analytics.
- RS-1 PoE and fusion variants planned after MVP validation.

## 11. Technical Architecture (High Level)

- Hardware: LD2450 radar, ESP32-C3-MINI-1 MCU running HardwareOS.
- Firmware: Custom Native API server compatible with ESPHome.
- Mobile App: Setup, AR scanning, zone configuration, OTA control.
- Cloud Services: OTA hosting, device registry, optional analytics.

## 12. Metrics and Success Criteria

- Setup completion within 60 seconds for 80% of users.
- Zone accuracy: < 5% false occupancy rate in typical rooms.
- OTA success rate > 99%.
- < 1% of users require manual firmware updates.

## 13. Risks and Mitigations

- Risk: Single radar misses fully stationary occupants.
  - Mitigation: Position RS-1 for active spaces; offer RS-1 Fusion upgrade.
- Risk: ESPHome API changes.
  - Mitigation: Maintain protocol compatibility tests and version negotiation.
- Risk: Competitors copy unified UX.
  - Mitigation: First-mover advantage on app and AR-based setup.

## 14. Milestones (Target)

- M0: Architecture validation and Native API compatibility (Dec 2025).
- M1: Mobile onboarding MVP with zone editor (Jan 2026).
- M2: OTA infrastructure and staged rollout (Feb 2026).
- M3: Beta launch (Jan 2026 planned; adjust if M1/M2 slip).
