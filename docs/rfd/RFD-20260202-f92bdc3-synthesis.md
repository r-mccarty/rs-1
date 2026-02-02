# RFD: Forge Synthesis f92bdc3

**Commit:** f92bdc35bd2796f206d3c49edd0354c98747d9c6
**Date:** 2026-02-02
**Sources:** anvil

## Executive Summary

This commit introduces the complete ESP-IDF firmware implementation for all 12 HardwareOS modules (~16K lines), but the main entry point only initializes M01 (Radar Ingest), leaving modules M02-M12 unwired. Critical security gaps exist in firmware signature verification and OTA integrity checking that must be addressed before production deployment.

## Actionable Findings (Score >= 0.6)

### 1. Boot sequence does not initialize most modules
- **Severity:** Critical
- **Sources:** anvil only
- **Score:** 0.70
  - Severity: 1.0 × 0.40 = 0.40
  - Consensus: 0.5 × 0.25 = 0.125
  - Critical Path: 1.0 × 0.20 = 0.20 (main.c is boot-critical)
  - Recurrence: 0 × 0.15 = 0.00
- **Location:** `firmware/main/main.c:67-144`
- **Summary:** `app_main` only initializes M01 (Radar Ingest) and enters a heap-monitor loop. Modules M02-M12 (tracking, zone engine, presence smoothing, native API, config store, security, OTA, logging, timebase, zone editor, IAQ) are never initialized. The firmware boots without the core pipeline, persistence, OTA, security, or API surfaces.
- **Action:** Implement the documented boot sequence in `app_main` per `docs/firmware/BOOT_SEQUENCE.md`, including error handling and variant gating. Ensure callback chain: radar -> tracking -> zone engine -> smoothing -> API/telemetry.

### 2. Firmware verification trusts any signing key when none configured
- **Severity:** High
- **Sources:** anvil only
- **Score:** 0.62
  - Severity: 0.8 × 0.40 = 0.32
  - Consensus: 0.5 × 0.25 = 0.125
  - Critical Path: 1.0 × 0.20 = 0.20 (security module)
  - Recurrence: 0 × 0.15 = 0.00
- **Location:** `firmware/components/security/security.c:405-434`
- **Summary:** `security_is_trusted_key` returns true for any public key if no trusted keys are configured. In production builds without a populated trusted key list, any attacker-generated key can pass signature verification, defeating the trust model.
- **Action:** Gate permissive behavior behind `CONFIG_RS1_ALLOW_UNTRUSTED_KEYS` dev flag. Default to rejecting untrusted keys in production. Add hard failure when trusted key store is empty without explicit dev override. Add CI guard to fail builds when trusted keys are empty in release config.

### 3. OTA manifest SHA256 is parsed but never verified
- **Severity:** High
- **Sources:** anvil only
- **Score:** 0.62
  - Severity: 0.8 × 0.40 = 0.32
  - Consensus: 0.5 × 0.25 = 0.125
  - Critical Path: 1.0 × 0.20 = 0.20 (OTA security)
  - Recurrence: 0 × 0.15 = 0.00
- **Location:** `firmware/components/ota_manager/ota_manager.c:575-851`
- **Summary:** The manifest's SHA256 is parsed into `manifest->sha256` but never compared to the downloaded image. `verify_firmware()` only logs and relies on ESP-IDF validation. A compromised MQTT payload or URL could supply a different image than the manifest intended without detection.
- **Action:** After download, compute SHA256 of the downloaded image and compare to `s_state.current_manifest.sha256`. When signature verification is enabled, also verify the signature block against image bytes.

## Deferred Findings (Score < 0.6)

- **Zone editor config not persisted to NVS** (Medium, score 0.38) - `firmware/components/zone_editor/zone_editor.c:288-305` - Zone edits lost on reboot due to missing M06 config store integration. Contains TODO marker.
- **WebSocket clients never removed on disconnect** (Medium, score 0.32) - `firmware/components/zone_editor/zone_editor.c:681-760, 982-1013` - Client slots exhaust over time; broadcasts attempt sends to closed sockets. Handle `HTTPD_WS_TYPE_CLOSE` to prune clients.

## Cross-Repo Pattern Analysis

No recurring patterns detected across repositories.

## Recommended Actions

1. **Immediate:**
   - Wire up full module boot sequence in `app_main` following documented initialization DAG
   - Add trusted key enforcement for production builds with CI guard
   - Implement OTA SHA256 integrity verification against manifest

2. **Near-term:**
   - Persist zone editor config via M06 config store
   - Add WebSocket disconnect handling for client slot cleanup

3. **Backlog:**
   - Add smoke test verifying all module init paths succeed
   - Add regression test for WebSocket client slot reuse
   - Add round-trip test for zone config through NVS
