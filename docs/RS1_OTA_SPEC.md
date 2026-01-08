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

```json
{"version": "1.2.0", "url": "https://fw.opticworks.io/rs1/1.2.0.bin", "sha256": "...", "min_rssi": -70, "rollout_id": "2026-01-15-a"}
```

## Status Payload Schema

```json
{"status": "success|failed|downloading", "version": "1.2.0", "error": null, "progress": 100, "rollout_id": "2026-01-15-a"}
```

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

## Partition Layout (ESP32-C3-MINI-1)

- ESP32-C3FH4: 4 MB flash, 384 kB ROM, 408 kB SRAM.
- Dual OTA partitions sized for the target max firmware size.
- Recommended initial target: <= 1.5 MB app size per OTA partition.
- Reserve NVS and OTA data partitions per ESP-IDF guidelines.

## Open Questions

- Finalize partition layout size targets with actual firmware image size.
