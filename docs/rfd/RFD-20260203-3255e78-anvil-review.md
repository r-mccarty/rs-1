# RFD: Anvil Review 3255e78

**Commit:** 3255e782eb24d79f38b68c1aa1e6b74d549269a7
**Date:** 2026-02-03
**Author:** Anvil Review

## Summary

This commit introduces the initial Cloudflare Worker implementation for RS-1 cloud services. The main risks are around unauthenticated ingestion endpoints, device identity generation that does not match the documented SHA-256 behavior, and telemetry aggregation math that can become permanently null once a device skips metrics.

## Findings

### 1. HIGH: Webhook endpoints accept unauthenticated events, enabling spoofed device/OTA state

**Location:** `cloud/src/routes/webhooks.ts:30`, `cloud/src/routes/webhooks.ts:66`, `cloud/src/routes/webhooks.ts:104`, `cloud/src/routes/webhooks.ts:198`, `cloud/src/routes/webhooks.ts:229`

**What:** All EMQX webhook handlers trust the incoming JSON payload without any shared secret, signature verification, or source validation. The handlers update device online state, write device events, update OTA status, and even update firmware versions on successful OTA status.

**Impact:** Anyone who can reach these endpoints can spoof device connectivity, force OTA status updates (including triggering auto-abort), or advance firmware versions. This undermines device state integrity and could disrupt rollouts.

**Recommendation:** Require an HMAC signature/header or EMQX webhook secret (and validate it), and reject unauthenticated requests. Consider restricting by source IP or an allowlist and validating `device_id`/`username` against known devices before writing state.

### 2. HIGH: Device ID generation contradicts documented SHA-256 privacy model and is reversible

**Location:** `cloud/src/utils/crypto.ts:16`, `cloud/src/routes/devices.ts:95`

**What:** `generateDeviceId` uses a simple non-cryptographic hash and concatenates the normalized MAC address into the ID, despite the header comment stating SHA-256 and privacy preservation.

**Impact:** Device IDs leak MAC addresses, enabling device fingerprinting and enumeration. The weak hash also raises collision risks in larger fleets and breaks the stated assumption of SHA-256-derived IDs.

**Recommendation:** Replace `generateDeviceId` with the SHA-256-based async path (or a synchronous SHA-256 implementation) and ensure the ID contains only the hash output. Update registration to await the async version if needed.

### 3. MEDIUM: Telemetry averages become permanently null when a metric is missing once

**Location:** `cloud/src/routes/telemetry.ts:43-52`

**What:** The upsert formula uses `avg_heap_kb * message_count` and `avg_wifi_rssi * message_count` without null guards. When a payload omits these metrics, `excluded.avg_*` is null, and the computed average becomes null, staying null for the rest of the day.

**Impact:** One missing metric wipes daily averages, degrading monitoring dashboards and analytics accuracy.

**Recommendation:** Use `COALESCE` guards and only update averages when the excluded value is non-null, or store separate counts for each metric to compute averages safely.

### 4. MEDIUM: OTA rollouts can be created with an “unknown” firmware hash

**Location:** `cloud/src/routes/ota.ts:72-76`

**What:** If R2 object metadata lacks a checksum, the rollout is stored with `firmware_sha256 = 'unknown'` and continues.

**Impact:** Devices expecting a hash for verification may reject or skip updates, and the backend can no longer attest to firmware integrity for that rollout.

**Recommendation:** Require a valid SHA-256 checksum when creating rollouts (fail the request if missing) or compute it on upload and store it consistently.

## Recommended actions

1. Add webhook authentication (HMAC/signature validation) and device existence checks before mutating state.
2. Replace device ID generation with true SHA-256 hashing and remove MAC leakage.
3. Fix telemetry aggregation to handle missing metrics without nulling averages; add tests for mixed payloads.
4. Enforce firmware checksum availability before creating OTA rollouts.
5. Add coverage for webhook auth, device registration ID format, telemetry upserts, and OTA rollout creation.
