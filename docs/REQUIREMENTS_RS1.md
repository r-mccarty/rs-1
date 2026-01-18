# RS-1 Requirements Specification

**Version:** 0.3
**Date:** 2026-01-18
**Owner:** OpticWorks Product + Engineering
**Status:** Draft

> This document consolidates product and technical requirements. For product vision, user journeys, and success metrics, see [PRD_RS1.md](PRD_RS1.md).

---

## 1. Product Summary

RS-1 is a prosumer-grade mmWave presence sensor family built on a single PCBA with selective population. It features mobile onboarding, native Home Assistant discovery via ESPHome-compatible Native API, and cloud-push OTA updates. The product prioritizes UX and software capability (unlimited zones, AR-assisted setup) over multi-sensor hardware complexity.

### 1.1 Product Variants

| Variant | Radar | Capabilities | Target Use | Retail |
|---------|-------|--------------|------------|--------|
| **RS-1 Lite** | LD2410 | Static presence detection | Utility rooms (bathrooms, hallways, closets) | $49 |
| **RS-1 Pro** | LD2410 + LD2450 | Dual radar fusion, zone tracking | Living spaces (living rooms, kitchens, bedrooms) | $89 |

### 1.2 Add-Ons

| Add-On | Description | Retail |
|--------|-------------|--------|
| **PoE** | Ethernet + Power via RMII PHY | +$30 |
| **IAQ** | ENS160 air quality via magnetic snap-on daughterboard | +$35 |

### 1.3 Platform Foundation

RS-1's single-PCBA architecture is designed for capability extension via OTA. The same infrastructure that delivers firmware updates can deliver new capabilities—activity classification, semantic understanding, direct device control—without requiring hardware changes. Today, RS-1 integrates with Home Assistant. Tomorrow, it can operate standalone or control devices directly via Matter/webhooks.

**Optional Subscription:** $3-5/month (remote access, AR scanning, analytics)

---

## 2. Scope

### 2.1 In Scope (MVP)

- Single PCBA with ESP32-WROOM-32E + CH340N
- RS-1 Lite variant (LD2410 static presence)
- RS-1 Pro variant (LD2410 + LD2450 dual radar fusion)
- Unlimited software-defined zones (Pro variant)
- ESPHome Native API compatibility
- Mobile app onboarding (QR + Wi-Fi setup)
- Manual zone editor (drag/resize/rename)
- Cloud-push OTA with local fallback
- Local-first operation (no cloud required for core function)
- PoE add-on option (via RMII PHY + SR8201F)
- IAQ add-on option (magnetic snap-on daughterboard)

### 2.2 Out of Scope (MVP)

- Camera-based detection (RS-Vision future product)
- Enterprise fleet management
- AR-assisted room scanning (post-beta)
- Consumer retail packaging

---

## 3. Hardware Requirements

### 3.1 Bill of Materials

| Component | Specification | Notes |
|-----------|---------------|-------|
| **MCU** | ESP32-WROOM-32E-N8 + CH340N | Xtensa LX6 dual-core, 240MHz, 8MB Flash, 520KB SRAM |
| **Radar** | Hi-Link LD2450 | 24GHz FMCW, 3 targets, 6m range, 120° H × 60° V |
| **Interface** | UART | 256000 baud |
| **Power** | USB-C, 5V | PoE add-on available |
| **Indicator** | Status LED | WS2812 RGB |
| **Enclosure** | Wall/ceiling mount | Form factor TBD |

### 3.2 Hardware Constraints

- Dual-core operation (ESP32-WROOM-32E is dual-core Xtensa LX6)
- 200KB heap budget for application
- 8MB flash: ~3.5MB per OTA partition, 16KB NVS, 64KB logs
- No hardware security module (software key storage only)
- Requires CH340N USB-UART bridge (no native USB)

---

## 4. Firmware Requirements (HardwareOS)

### 4.1 Radar Processing

| Requirement | Specification |
|-------------|---------------|
| Target tracking | Up to 3 concurrent targets from LD2450 |
| Coordinate system | X ±6000mm, Y 0-6000mm from sensor origin |
| Frame rate | ~33 Hz (30ms frames) |
| Zone evaluation | Point-in-polygon per zone per frame |
| Zone count | Unlimited (resource-constrained to ~16 practical) |

### 4.2 Presence Output

| Requirement | Specification |
|-------------|---------------|
| Output type | Per-zone binary occupancy + target count |
| Sensitivity | Configurable per zone (maps to hold time) |
| Smoothing | Hysteresis state machine to reduce flicker |
| Latency | Occupancy state updates within 1 second of movement |
| Stability | Minimal dropouts during micro-movements |

### 4.3 Home Assistant Integration

| Requirement | Specification |
|-------------|---------------|
| Protocol | ESPHome Native API (Protobuf over TCP) |
| Port | 6053 (default) |
| Discovery | mDNS `_esphomelib._tcp.local` |
| Encryption | Noise protocol (optional, recommended) |
| Entities exposed | Per-zone occupancy, per-zone target count, device health, Wi-Fi RSSI |
| Entities NOT exposed | Raw radar coordinates, track IDs, zone vertices |

### 4.4 Configuration Storage

| Requirement | Specification |
|-------------|---------------|
| Backend | ESP-IDF NVS |
| Atomic writes | Yes, with rollback on failure |
| Versioning | Incrementing version per zone config update |
| Encryption | Sensitive fields (passwords, keys) encrypted at rest |

### 4.5 OTA Updates

| Requirement | Specification |
|-------------|---------------|
| Trigger | MQTT from cloud orchestrator |
| Delivery | HTTPS download from CDN |
| Verification | ECDSA P-256 signature + SHA-256 hash |
| Rollback | ESP-IDF dual OTA partitions, auto-rollback on failed boot |
| Local fallback | Serial flash via USB |

### 4.6 Security

| Requirement | Specification |
|-------------|---------------|
| Secure boot | ESP32 Secure Boot V2 |
| Flash encryption | Optional for MVP |
| Transport | TLS 1.2+ for MQTT and HTTPS |
| API auth | Noise protocol PSK or legacy password |
| Anti-rollback | eFuse-based version counter (32 versions max) |

### 4.7 Platform Enablers (Post-MVP)

These requirements establish architectural hooks for future platform capabilities. They are NOT MVP requirements but inform MVP architecture decisions.

| Capability | Version | Description | MVP Architectural Hook |
|------------|---------|-------------|------------------------|
| Activity Classification | v1.5 | Detect sitting, walking, sleeping patterns | Track velocity/movement patterns in M02 |
| Local Action Dispatch | v1.5 | Trigger webhooks/HTTP without HA | Reserved config schema fields |
| Matter Bridge | v2.0 | Direct device control via Matter | Protocol abstraction in M05 |
| Standalone Mode | v2.0 | Full operation without HA | Local automation rule engine placeholder |

**Technical Enablers:**

| Metric | Target | Rationale |
|--------|--------|-----------|
| Edge Processing Latency | <100ms detection to action | 5-10x faster than cloud roundtrip |
| ML Inference Budget | ~50KB heap, ~500ms classification | Fits within ESP32 constraints |
| Protocol Abstraction | Unified presence model | Serves ESPHome, Matter, webhooks from same data |

---

## 5. Mobile App Requirements

### 5.1 Platforms

| Requirement | Specification |
|-------------|---------------|
| Platforms | iOS, Android (technology stack TBD) |
| Minimum OS | iOS 15+, Android 10+ |

### 5.2 Onboarding Flow

1. Scan QR code on device (opens app or web fallback)
2. Connect to device AP (`OpticWorks-XXXX`)
3. Enter Wi-Fi credentials (via app or captive portal)
4. Device provisions, connects to WiFi, and registers with cloud
5. Create/edit zones
6. Confirm setup complete

**Target:** 80% of users complete in ≤60 seconds

See `docs/contracts/PROTOCOL_PROVISIONING.md` for detailed provisioning protocol.

### 5.3 QR Code Format

| Requirement | Specification |
|-------------|---------------|
| Scheme | `opticworks://setup?d={device_id}&ap={ap_ssid}` |
| Device ID | 12-char hex (last 6 bytes of MAC) |
| AP SSID | `OpticWorks-{XXXX}` (last 4 of device_id, uppercase) |
| Fallback | Web redirect to `https://setup.opticworks.io/` |

### 5.4 Device AP Mode

| Requirement | Specification |
|-------------|---------------|
| SSID | `OpticWorks-{XXXX}` (derived from device_id) |
| Security | Open (no password) |
| IP Address | `192.168.4.1` |
| Captive Portal | Yes, auto-redirects to provisioning UI |
| Timeout | 10 minutes (then deep sleep) |

### 5.5 Provisioning API (Local)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/device-info` | GET | Device identity and version |
| `/api/networks` | GET | Scan available WiFi networks |
| `/api/provision` | POST | Submit WiFi credentials |
| `/api/provision/status` | GET | Poll provisioning status |
| `/api/provision/ws` | WS | Real-time provisioning updates |

### 5.6 Zone Editor

| Feature | Specification |
|---------|---------------|
| Zone creation | Tap to add polygon vertices |
| Zone editing | Drag handles to resize/move |
| Zone properties | Name, type (include/exclude), sensitivity slider |
| Live preview | Target positions at ~10 Hz |
| Validation | Reject self-intersecting polygons |

### 5.7 Device Management

- View device status (firmware version, health, Wi-Fi signal)
- Trigger OTA update check
- View update progress
- Factory reset option

### 5.8 AR Scanning (Post-Beta)

- iOS RoomPlan API integration
- Auto-propose zone boundaries from room geometry
- Manual refinement after scan

---

## 6. Cloud Services Requirements

### 6.1 Device Registry

| Requirement | Specification |
|-------------|---------------|
| Identity | Derived from ESP32 eFuse MAC |
| Authentication | Device secret + HMAC-based MQTT credentials |
| Provisioning | Manufacturing-time secret injection |

### 6.2 OTA Orchestrator

| Requirement | Specification |
|-------------|---------------|
| Rollout stages | 1% → 10% → 50% → 100% |
| Cohort selection | Hash of device_id for stable assignment |
| Halt condition | >2% failure rate triggers pause |
| Cooldown | 24-hour per-device minimum between updates |

### 6.3 Telemetry (Opt-In)

| Requirement | Specification |
|-------------|---------------|
| Consent | Explicit user opt-in required |
| Transport | MQTT to cloud ingest |
| Data collected | System metrics, error counts, no PII |
| Data NOT collected | Zone names, Wi-Fi SSID, target positions |

### 6.4 Optional Subscription Features

- Remote device access (outside LAN)
- AR room scanning
- Analytics dashboard
- Priority support

---

## 7. Non-Functional Requirements

### 7.1 Performance

| Metric | Requirement |
|--------|-------------|
| Radar parse latency | < 1ms per frame |
| Zone evaluation | < 500µs per frame (3 targets × 16 zones) |
| State update latency | < 50ms from detection to HA |
| Memory (heap) | < 200KB total |
| CPU utilization | < 50% sustained |

### 7.2 Reliability

| Metric | Requirement |
|--------|-------------|
| Uptime | Continuous operation without reboot (except OTA) |
| Watchdog recovery | < 10 seconds from hang to operational |
| OTA success rate | > 99% |
| False vacancy rate | < 5% during normal activity |

### 7.3 Security

| Metric | Requirement |
|--------|-------------|
| Firmware signing | All production firmware signed |
| Transport encryption | TLS 1.2+ for all cloud connections |
| Credential storage | Encrypted at rest |
| No cloud dependency | Core presence detection works offline |

### 7.4 Compatibility

| Metric | Requirement |
|--------|-------------|
| Home Assistant | 2024.1+ |
| ESPHome protocol | Native API v1.9+ |
| Wi-Fi | 802.11 b/g/n (2.4GHz) |

---

## 8. UX Principles

1. **Hide complexity** - Present single confident presence answer, not raw sensor data
2. **Minimize entities** - Curated HA entities, no entity sprawl
3. **Consumer-grade setup** - App-first, guided steps, no YAML editing
4. **Sensible defaults** - Works out of box, advanced settings optional
5. **Local-first** - Full functionality without cloud account
6. **Progressive disclosure** - Basic users see simple presence on/off; power users can unlock semantic intelligence (activity types, patterns) via app settings

---

## 9. Constraints and Assumptions

### 9.1 Constraints

| Constraint | Impact |
|------------|--------|
| Single radar | Cannot detect fully stationary occupants reliably |
| 8MB flash | OTA partition size ~3.5MB each (dual partitions) |
| eFuse anti-rollback | Maximum 32 security-critical updates over device lifetime |
| No native USB | Requires CH340N USB-UART bridge |

### 9.2 Assumptions

| Assumption | If Changed |
|------------|------------|
| LD2450 frame rate ~33 Hz | Tracking timing, buffer sizing |
| LD2450 max 3 targets | Data structures, zone evaluation |
| Typical occlusions < 2 seconds | Hold time defaults |
| User prefers false occupancy over false vacancy | Smoothing bias |

---

## 10. Milestones

### 10.1 MVP Milestones

| Milestone | Target | Deliverables |
|-----------|--------|--------------|
| M0 | Dec 2025 | Architecture validation, Native API compatibility |
| M1 | Jan 2026 | Mobile onboarding MVP, zone editor |
| M2 | Feb 2026 | OTA infrastructure, staged rollout |
| M3 | Mar 2026 | Beta launch |

### 10.2 Platform Milestones (Post-MVP)

| Milestone | Target | Deliverables |
|-----------|--------|--------------|
| M4 | Q2 2026 | Edge intelligence SDK, activity classification beta |
| M5 | Q3 2026 | Direct control (HA API bypass), Matter research |
| M6 | Q4 2026 | Standalone mode, Matter MVP |

---

## 11. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-XX | OpticWorks | Initial draft (split docs) |
| 0.2 | 2026-01-09 | OpticWorks | Consolidated PRODUCT_SPEC + TECH_REQUIREMENTS |
| 0.3 | 2026-01-18 | OpticWorks | Added platform foundation; platform enablers (post-MVP); progressive disclosure UX principle; post-MVP milestones M4-M6; aligned with hardware variants (Lite/Pro) and add-ons (PoE/IAQ) |
