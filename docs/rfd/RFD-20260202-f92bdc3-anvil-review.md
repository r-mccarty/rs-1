# RFD: Anvil Review f92bdc3

**Commit:** f92bdc35bd2796f206d3c49edd0354c98747d9c6
**Date:** 2026-02-02
**Author:** Anvil Review

## Summary

This commit adds a full ESP-IDF firmware implementation with new components for all 12 HardwareOS modules, along with project scaffolding and parser tests. The code is large and modular, but the main entry point does not wire most modules together, and several security/OTA guarantees from the specs are not enforced yet.

## Findings

### 1. CRITICAL: Boot sequence does not initialize most modules

**Location:** `firmware/main/main.c:67-144`

**What:** `app_main` only initializes M01 (Radar Ingest) and then enters a heap-monitor loop. M02–M12 (tracking, zone engine, presence smoothing, native API, config store, security, OTA, logging, timebase, zone editor, IAQ) are never initialized or started.

**Impact:** The firmware boots without the core pipeline, persistence, OTA, security, or API surfaces. This is a functional regression against the boot sequence documented in comments and docs, and it prevents any end-to-end behavior from working beyond raw radar ingest.

**Recommendation:** Implement the documented boot sequence in `app_main`, including error handling and variant gating, and ensure each module registers the required callbacks (radar → tracking → zone engine → smoothing → API/telemetry).

### 2. HIGH: Firmware verification trusts any signing key when none configured

**Location:** `firmware/components/security/security.c:405-434`

**What:** `security_is_trusted_key` returns true for any public key if no trusted keys are configured.

**Impact:** In production builds without a populated trusted key list, any attacker with a generated key can sign firmware and pass signature verification. This defeats the signature trust model and makes OTA/package verification ineffective.

**Recommendation:** Gate this behavior behind a build-time dev flag (e.g., `CONFIG_RS1_ALLOW_UNTRUSTED_KEYS`) and default to rejecting untrusted keys in production. Add a hard failure when the trusted key store is empty unless an explicit dev override is set.

### 3. HIGH: OTA manifest SHA256 is parsed but never verified

**Location:** `firmware/components/ota_manager/ota_manager.c:575-851`

**What:** The manifest’s SHA256 is parsed into `manifest->sha256` but never compared to the downloaded image. `verify_firmware()` only logs and relies on ESP-IDF validation, and does not hash/compare the image against the manifest.

**Impact:** OTA integrity depends solely on transport and the ESP-IDF image header checks. A compromised MQTT payload or URL could supply a different image than the manifest intended without detection.

**Recommendation:** After download, compute the SHA256 of the downloaded image and compare it to `s_state.current_manifest.sha256`. If `verify_signature` is enabled, also verify the signature block against the image bytes.

### 4. MEDIUM: Zone editor config is not persisted to NVS

**Location:** `firmware/components/zone_editor/zone_editor.c:288-305`

**What:** `zone_editor_set_config` contains a TODO and does not save the updated zone config to M06 config store.

**Impact:** Zone edits are lost on reboot, and optimistic versioning will drift from the stored config. This breaks the expected editor workflow and persistence contract.

**Recommendation:** Integrate `config_store` to persist zone configs and load them on init. Add rollback behavior in case of invalid writes.

### 5. MEDIUM: WebSocket clients are never removed on disconnect

**Location:** `firmware/components/zone_editor/zone_editor.c:681-760` (broadcast) and `firmware/components/zone_editor/zone_editor.c:982-1013` (connect only)

**What:** WebSocket clients are added on connect but there is no handler to clear entries on close or error. `client_count` only increases, and stale client entries are left active.

**Impact:** Client slots can be exhausted over time and broadcasts continue to attempt sends to closed sockets, increasing CPU usage and dropping frames.

**Recommendation:** Handle `HTTPD_WS_TYPE_CLOSE` / socket errors to prune clients, decrement `client_count`, and free slots when a client disconnects.

## Recommended actions

1. Wire up the full module boot sequence in `app_main`, and add a smoke test that verifies all module init paths succeed on a supported target.
2. Make trusted key enforcement mandatory in production builds; add a CI guard to fail builds when trusted keys are empty.
3. Implement OTA integrity verification against the manifest SHA256 and, when enabled, signature verification over the downloaded image.
4. Persist zone editor config via M06 and add a small test that round-trips a config update through NVS.
5. Add WebSocket disconnect handling and a regression test for client slot reuse.
