# HardwareOS Zone Editor Specification

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware + Cloud
Status: Draft

## 1. Purpose

Provide a zone editor experience comparable to Sensy One, without requiring a Home Assistant add-on. The zone editor configures polygon zones, walls/obstacles (optional), and renders live target positions for tuning.

## 2. Goals

- Visual zone creation and editing (polygon-based).
- Live target visualization at ~10 Hz.
- Local-first operation with a cloud-assisted option.
- Stable zone storage and versioning on device.

## 3. Non-Goals (MVP)

- Full 3D visualization.
- Cloud-only dependency for zone editing.
- Automated room scans (post-beta).

## 4. HardwareOS Module Scope (M11)

- Accept zone config updates and persist to M06 Device Config Store.
- Publish live target stream for UI visualization.
- Maintain config versioning and rollback on invalid configs.
- Provide a local API for direct LAN editing.

## 5. Data Model

### 5.1 Coordinate System

**User-Facing (Zone Editor UI/API):**
- X/Y in **meters** relative to radar origin
- Positive X/Y axes aligned to device coordinate frame
- Z ignored for MVP (2D)

**Internal Firmware:**
- All coordinates stored and processed in **millimeters (mm)**
- Conversion happens at M11 boundary per COORDINATE_SYSTEM.md
- `mm = (int16_t)(meters * 1000.0f)` on receive
- `meters = mm / 1000.0f` on send

### 5.2 Zone Schema (Device)

**Internal Storage (mm):**
```json
{
  "version": 3,
  "updated_at": "2026-01-15T12:00:00Z",
  "zones": [
    {
      "id": "zone_living",
      "name": "Living Room",
      "type": "include",
      "vertices": [[200, 400], [2000, 400], [2000, 3000], [200, 3000]],
      "sensitivity": 50
    }
  ]
}
```

**Note:** MQTT payloads use mm (see `../contracts/SCHEMA_ZONE_CONFIG.json`). The Zone Editor UI converts to/from meters for user display.

**Sensitivity:** Integer 0-100 per GLOSSARY.md:
- 0 = Maximum stability (5000ms hold time, minimal flicker)
- 50 = Balanced (default)
- 100 = Maximum responsiveness (instant, may flicker)

Constraints:

- Max vertices per zone: 8 (tunable)
- Max zones: 16 (resource-constrained)
- Zones may overlap; include zones take precedence
- Invalid polygons rejected with error

## 6. Local Device API (LAN)

### 6.1 REST Endpoints

- `GET /api/zones` -> returns zone config + version
- `POST /api/zones` -> update zone config (atomic)
- `GET /api/targets` -> last known targets (snapshot)

### 6.2 Streaming

- `WS /api/targets/stream` -> push target positions at frame rate.

### 6.3 Validation

- Reject self-intersecting polygons.
- Reject zones outside device max range.
- Reject updates with stale version (optimistic lock).

## 7. Cloud Architecture (Ideal)

### 7.1 Components

- **Zone Editor Web App**: Cloudflare Pages or Workers (UI).
- **Zone Editor API**: Cloudflare Workers (REST).
- **Firmware CDN**: Cloudflare R2 (optional for assets like floorplans).
- **Config Store**: Cloudflare D1 (zone configs, versions, audit trail).
- **MQTT Broker**: Managed broker (EMQX Cloud or HiveMQ) for device push.

### 7.2 Cloud Flow (Optional)

1. User opens web app and authenticates.
2. App loads zone config from API (D1).
3. User edits zones; API writes to D1 and publishes MQTT update.
4. Device receives config update, applies, and publishes ack/status.
5. App subscribes to status for confirmation.

### 7.3 Local-First Flow

1. App discovers device on LAN (mDNS or QR IP).
2. App writes config directly to device API.
3. Device stores config locally; no cloud needed.

### 7.4 Live Target Visualization

- Cloud mode: device publishes target stream to MQTT; UI subscribes via API proxy.
- Local mode: UI connects directly to device WebSocket.

## 8. Security

- Local API requires device pairing token.
- Cloud API requires user auth and per-device access control.
- MQTT ACLs restrict publish/subscribe to device and owner.

## 9. Telemetry (Optional)

- Zone config save success/failure.
- Target stream performance (frame drops).

## 10. Open Questions

- Final vertex limit and max zones per device.
- Whether walls/obstacles are needed for MVP.
- MQTT broker selection (EMQX vs HiveMQ).

