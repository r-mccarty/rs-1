# RS-1 OTA Specification

Version: 0.1
Status: Draft

## Overview

MQTT triggers OTA; HTTPS delivers firmware. ESP-IDF handles download, validation, and rollback.

## Components

- MQTT Broker: Receives trigger commands, publishes status updates.
- OTA Orchestrator (Cloud): Publishes triggers, enforces rollout policy, monitors status.
- Firmware CDN: Static HTTPS endpoint serving signed binaries (Cloudflare R2 preferred).
- Device: Subscribes to commands, executes OTA via `esp_https_ota()`.

## Cloud Server Responsibilities

- Host MQTT broker and authentication (device identity + ACLs).
- Run OTA Orchestrator (Cloudflare Workers preferred) to:
  - Select target cohorts and publish trigger messages.
  - Generate short-lived signed URLs to firmware in R2.
  - Enforce rollout stages and cooldown windows.
  - Collect status telemetry and surface failures.
- Store firmware binaries and manifest metadata.

## MQTT Topics

| Topic                                | Direction | Purpose              |
|--------------------------------------|-----------|----------------------|
| `opticworks/{device_id}/ota/trigger` | Cloud→Dev | OTA manifest push    |
| `opticworks/{device_id}/ota/status`  | Dev→Cloud | Result reporting     |

## Trigger Payload Schema

Per `../contracts/SCHEMA_OTA_MANIFEST.json`:

```json
{
  "version": "1.2.0",
  "url": "https://fw.opticworks.io/rs1/1.2.0.bin?sig=abc123",
  "sha256": "a1b2c3d4e5f6789012345678901234567890123456789012345678901234abcd",
  "min_rssi": -70,
  "rollout_id": "2026-01-15-a",
  "issued_at": "2026-01-15T10:00:00Z"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `version` | string | Yes | Target firmware version (semver) |
| `url` | string | Yes | HTTPS URL to signed firmware binary |
| `sha256` | string | Yes | SHA-256 hash of firmware (64 hex chars) |
| `min_rssi` | integer | No | Minimum Wi-Fi RSSI to proceed (default: -70) |
| `rollout_id` | string | Yes | Unique rollout identifier for tracking |
| `issued_at` | string | Yes | ISO 8601 timestamp of trigger issuance |

## Status Payload Schema

```json
{
  "status": "downloading",
  "version": "1.2.0",
  "progress": 45,
  "error": null,
  "rollout_id": "2026-01-15-a",
  "timestamp": "2026-01-15T10:05:30Z"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `status` | enum | `pending`, `downloading`, `verifying`, `success`, `failed` |
| `version` | string | Firmware version being updated |
| `progress` | integer | Download progress (0-100) |
| `error` | string | Error message if status is `failed` |
| `rollout_id` | string | Rollout identifier from trigger |
| `timestamp` | string | ISO 8601 timestamp |

## Device Flow

1. Receive trigger on MQTT.
2. Validate: version > current, RSSI >= min_rssi, power stable, not within cooldown window.
3. Publish `{"status": "downloading", "progress": 0}`.
4. Call `esp_https_ota()` with URL and SHA256.
5. On success: publish status, reboot.
6. On failure: publish error, abort.

### Power/RSSI Gating

- RSSI threshold default: -70 dBm (tunable per manifest).
- Power stable definition:
  - No brownout resets in last 5 minutes.
  - Reset history only (no ADC power measurement).

### Retry and Backoff

- Retry up to 3 times on download failure.
- Exponential backoff: 1 min, 5 min, 30 min.
- After final failure, report `failed` and wait for next trigger.

## Rollback Strategy

- ESP-IDF dual OTA partitions (ota_0, ota_1).
- First boot: partition marked "pending verification."
- App calls `esp_ota_mark_app_valid_cancel_rollback()` after: Wi-Fi connects, radar responds, config loads.
- Crash before validation -> bootloader reverts on next boot.

Rollback triggers:

- Watchdog reset during first 2 boots after update.
- Failure to reach "ready" state within 30 seconds after boot.

## Security

- Firmware URL uses signed URL or device client cert (no public enumeration).
- SHA256 in manifest validated before boot flag set.
- TLS 1.2+ required for MQTT and HTTPS.

## Authentication and Provisioning

- Each device is provisioned with a unique device ID and client credentials.
- MQTT ACLs restrict publish/subscribe to per-device topics.
- Signed firmware URLs are short-lived (<= 15 minutes) and scoped to device/rollout.
- Credential rotation via OTA update or secured provisioning flow.

## Rollout Policy

- Cohort-based staged rollout: 1% -> 10% -> 50% -> 100%.
- Cohort selection by hash of device_id for stable assignment.
- Halt or rollback if failure rate exceeds threshold (e.g., 2%).
- Enforce per-device cooldown (default 24 hours) to avoid repeated updates.

## Local Fallback

- Serial flashing via USB-to-serial as the manual recovery path.
- Fallback path available when MQTT or cloud is unreachable.

## Partition Layout (ESP32-WROOM-32E)

- ESP32-WROOM-32E-N8: 8 MB flash, 520 KB SRAM.
- Dual OTA partitions sized for the target max firmware size.
- Recommended initial target: <= 3 MB app size per OTA partition.
- Reserve NVS and OTA data partitions per ESP-IDF guidelines.

```
Flash Layout (8MB)
┌──────────────────────────┐ 0x000000
│     Bootloader (32KB)    │
├──────────────────────────┤ 0x008000
│   Partition Table (4KB)  │
├──────────────────────────┤ 0x009000
│       NVS (16KB)         │
├──────────────────────────┤ 0x00D000
│    OTA Data (8KB)        │
├──────────────────────────┤ 0x00F000
│      App OTA_0 (3MB)     │  ◀── Active firmware
├──────────────────────────┤ 0x30F000
│      App OTA_1 (3MB)     │  ◀── Update partition
├──────────────────────────┤ 0x60F000
│    SPIFFS/Logs (256KB)   │
├──────────────────────────┤ 0x64F000
│      Reserved            │
└──────────────────────────┘ 0x800000
```

## Open Questions

- Finalize partition layout size targets with actual firmware image size.
