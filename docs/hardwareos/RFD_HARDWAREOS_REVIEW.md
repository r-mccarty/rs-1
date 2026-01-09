# RFD: Systems Engineering Review of HardwareOS Firmware Modules (RS-1)

Status: Draft
Author: Codex (reviewer)
Date: 2026-01-XX
Scope: M01-M11 specs in docs/hardwareos

## 0. Intent

This is a requirements and systems architecture review of the HardwareOS module specs. It is intentionally blunt and critical. The goal is to surface gaps and contradictions before implementation hardens into something untestable and brittle.

## 1. Executive Summary (Why I Dislike This Design)

- The module set is internally inconsistent on timing, units, and interfaces. It reads like 11 documents authored by 11 people who never synced assumptions.
- The data contracts between modules are underspecified or contradictory. You cannot reliably implement or test this pipeline as written.
- The concurrency model is undefined. Several modules imply ISR paths, queues, and timers, yet no ownership or lifecycle rules exist.
- Security, OTA, and config storage specs are mutually incompatible in critical places (root of trust, key storage, rollback, schema versioning).
- The design ignores ugly but common edge cases (frame loss bursts, time wrap, zone edits mid-frame, network partition, multi-client access).

Bottom line: this will ship as a random collection of features, not a system. We need to correct the architecture and requirements before code is written.

## 2. Cross-Cutting Gaps and Contradictions

### 2.1 Timing and Rate Mismatch (Blocker)
- M01 assumes ~33 Hz input, M02 assumes 10 Hz input, M04 assumes 10 Hz output, M08 claims pipeline runs at 33 Hz but only publishes at 10 Hz.
- There is no definitive statement for where the 10 Hz throttle happens or who owns it.
- Tests, buffers, gating, and smoothing parameters are all built on different timing assumptions.

### 2.2 Units and Coordinate Conventions (Blocker)
- M01 and M03 use millimeters, M11 JSON uses meters, M02 does not specify units at all.
- Zone editor says include zones take precedence; M03 says exclude zones take precedence. These cannot both be true.
- The axis orientation is described in some modules and absent in others; calibration/rotation is defined in M06 but not applied anywhere.

### 2.3 Data Contract Drift
- M03 outputs fields (avg_confidence, raw_occupied) that are not produced by M02 as specified.
- M04 references confidence-weighted hold, but M03 does not define how confidence is computed across tracks.
- M05 builds a static entity registry at boot, while M11 and M06 expect zones to be updated at runtime.

### 2.4 Concurrency and Ownership
- ISR, ring buffers, queues, and timebase are implied but not scoped. There is no single-threaded vs multi-threaded model.
- No ownership model exists for config updates vs active processing. Atomic config write is not the same as safe live reload.
- Logging/telemetry may block hot paths; no async boundaries are defined.

### 2.5 Security Model is Not a Model
- M10 asserts secure boot, key management, and signed OTA, but M07 only checks SHA256 from a manifest.
- M06 stores keys in NVS and calls it "encrypted" even when flash encryption is optional.
- Certificate pinning has no rotation story; key rotation is hand-waved.

### 2.6 Reliability and Failure Modes
- No end-to-end definition of system behavior when frames stop, Wi-Fi drops, or NVS is full.
- No degradation mode when cloud services are offline but local functionality must continue.
- Watchdog behavior is defined but not integrated with OTA or long operations.

## 3. Module-by-Module Review

### M01 Radar Ingest (HARDWAREOS_MODULE_RADAR_INGEST.md)

Requirements/Architecture Critique:
- Ring buffer sized for two frames is dangerously small. Any ISR jitter or temporary task stall will drop frames.
- "Drop oldest frame" is a lazy policy that destroys temporal ordering and will confuse tracking.
- Frame validation is underspecified (checksum algorithm unknown, presence indicators not defined). This is not implementable.
- Signal quality is invented without definition. Downstream modules rely on it but it is not sourced.

Missing Edge Cases:
- Garbage bytes in the stream causing false header alignment.
- Short bursts of noise that include footer but not header.
- Back-to-back frames with no gaps (UART DMA overrun).
- Frame timestamps across overflow (esp_timer) or missed frames.
- Partial frame after UART glitch mid-frame.

### M02 Tracking (HARDWAREOS_MODULE_TRACKING.md)

Requirements/Architecture Critique:
- It assumes 10 Hz input without stating where the downsample occurs. This is a system-level bug.
- Unit conventions are absent; gating in meters vs mm is ambiguous.
- Track lifecycle logic is too vague to implement consistently ("confirm after N", "drop after M" without defaults tied to frame rate).
- ID stability is treated as optional, yet M03 uses track IDs for zone occupancy and events.

Missing Edge Cases:
- Two targets crossing paths (ID swap risk).
- Fast target > gating threshold (drops and re-creates track).
- Target disappears then reappears with same position (ghost track).
- Frame bursts after pause causing large dt and prediction blowup.
- Target count saturating at 3 with rapid turnover.

### M03 Zone Engine (HARDWAREOS_MODULE_ZONE_ENGINE.md)

Requirements/Architecture Critique:
- "Unlimited zones" vs max 16 zones is contradictory; memory budget assumes 16.
- Debounce logic is referenced but not defined (what is debounced: events or states?).
- Point-in-polygon uses integer math without overflow or boundary handling rules.
- Exclude zone precedence is stated here, but M11 claims include precedence.

Missing Edge Cases:
- Point on edge or vertex (define inside/outside policy).
- Self-intersection detection on colinear edges.
- Track with invalid coordinates (NaN or out of range).
- Zone config update mid-processing (double-evaluate or partial state).
- Empty zone list: should raw occupancy be exposed or suppressed?

### M04 Presence Smoothing (HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md)

Requirements/Architecture Critique:
- Uses 10 Hz frame assumption again. This cascades the timing mismatch.
- Depends on avg_confidence and has_moving but M03 does not guarantee those fields.
- Sensitivity mapping conflicts with min_hold_ms: mapping can produce 0ms while min is 100ms.
- "Home Assistant polls state" is wrong for ESPHome Native API (subscription-based).

Missing Edge Cases:
- Rapid flip between 0 and 1 targets (thrash across ENTERING/HOLDING).
- Zone deletion or renaming while occupied.
- Confidence spikes causing hold time to extend beyond max_hold_ms.
- Timebase wrap at ~49 days affecting timers.

### M05 Native API Server (HARDWAREOS_MODULE_NATIVE_API.md)

Requirements/Architecture Critique:
- Single-connection assumption is not safe. HA can reconnect frequently and may open multiple sessions.
- Entity registry built once at boot contradicts runtime zone updates (M11).
- Max message size 1024 bytes is unrealistic with 50 entities plus metadata; ESPHome uses chunked listings.
- Rebooting after no connection for 15 minutes is user-hostile and will create reboot loops in offline homes.

Missing Edge Cases:
- Zone config change after HA is connected: how are entities re-enumerated?
- Encryption key rotation: what happens to existing clients?
- State update backlog when Wi-Fi is congested.
- DNS/mDNS failure with no discovery fallback.

### M06 Device Config Store (HARDWAREOS_MODULE_CONFIG_STORE.md)

Requirements/Architecture Critique:
- "Atomic" is claimed, but the shadow/rename flow is not atomic in ESP-IDF NVS.
- Rollback exists for zones only; other config updates are not transactional.
- Schema migration is punted to an open question but OTA relies on stable schema.
- Encrypting keys in NVS without flash encryption is misleading security theater.

Missing Edge Cases:
- Power loss between writing zones_prev and zones.
- NVS full or corrupted partition (no read-only fallback).
- Multiple writers (zone editor + OTA + local API) colliding.
- Clock not synced but updated_at requires Unix time.

### M07 OTA (HARDWAREOS_MODULE_OTA.md)

Requirements/Architecture Critique:
- OTA validation is only SHA256 from a manifest. It ignores M10 signature verification.
- MQTT-triggered OTA without local fallback means "no cloud" violates the product goal.
- Power gating is based on "no brownout resets" which is not a real power signal.
- Rollback success criteria are vague ("radar responds" is undefined).

Missing Edge Cases:
- Stale or replayed MQTT triggers.
- OTA triggered while another update is in progress.
- Download completes but reboot fails repeatedly (boot loop).
- Firmware image too large for partition.
- Update while config schema changes (migrations).

### M08 Timebase/Scheduler (HARDWAREOS_MODULE_TIMEBASE.md)

Requirements/Architecture Critique:
- Conflicting assumptions: pipeline runs at 33 Hz, but M02/M04 are 10 Hz.
- Scheduler is FIFO with no preemption or budget enforcement; long tasks will delay everything.
- Uptime uses 32-bit milliseconds with overflow at ~49 days but no wrap logic is specified.
- Watchdog timeout is fixed at 5s yet OTA and NVS writes can exceed that.

Missing Edge Cases:
- Timebase or esp_timer overflow during long uptime.
- Frame jitter bursts triggering false "missed frame" spikes.
- NTP sync step changes causing negative deltas.
- Scheduler task unregister while running.

### M09 Logging/Diagnostics (HARDWAREOS_MODULE_LOGGING.md)

Requirements/Architecture Critique:
- Logging is synchronous by implication; there is no async queue or backpressure policy.
- "No PII" is contradicted by example logs and zone names in debug messages.
- JSON telemetry on ESP32-C3 is memory-heavy and slow unless carefully bounded.
- Flash logging without wear analysis or explicit compaction is risky.

Missing Edge Cases:
- Log storm during radar noise (buffer overflow behavior).
- Telemetry when MQTT down (queue growth and drop policy).
- Crash logging when RTC memory is already occupied.
- Logging from ISR context (unsafe if not deferred).

### M10 Security (HARDWAREOS_MODULE_SECURITY.md)

Requirements/Architecture Critique:
- Root of trust is unclear. If secure boot is optional, firmware signature verification is meaningless.
- Signature block includes the public key; verifying against a list embedded in firmware is circular unless keys are eFuse-backed.
- Certificate pinning without a rotation plan will brick devices when certs rotate.
- "Local HTTP no TLS" is not acceptable without strict auth and CSRF protections.

Missing Edge Cases:
- Secure boot disabled in factory: does the system refuse to operate?
- Device secret extraction if flash encryption is off.
- Key revocation after leak.
- Noise handshake failure and fallback behavior.

### M11 Zone Editor (HARDWAREOS_MODULE_ZONE_EDITOR.md)

Requirements/Architecture Critique:
- Zone schema uses meters while firmware expects mm, and includes a sensitivity shape unrelated to M04.
- Include zone precedence conflicts with M03 exclude-first rule.
- Local API endpoints are defined but not mapped to any existing server (M05 is native API only).
- Cloud architecture is mixed into firmware scope, which is not implementable on-device.

Missing Edge Cases:
- Zone update during occupancy: does state transition or reset?
- WebSocket stream when Wi-Fi signal is weak (frame drops and UI jitter).
- Version conflicts across cloud and local edits.
- Large floorplan assets exceeding device storage.

## 4. Required Decisions Before Implementation

1. Canonical timing model: define where 33 Hz becomes 10 Hz, and enforce it across all modules.
2. Canonical units and coordinate system: mm or meters, and a single conversion boundary.
3. Runtime config update contract: how modules hot-reload zones and update entities without reboot.
4. Root of trust: secure boot required or not; where trusted keys live.
5. Concurrency model: define task/queue ownership and ISR boundaries.

## 5. Suggested Refactors (Minimal to Unblock)

- Publish a single "HardwareOS Data Contracts" doc with structs, units, and rates for each module.
- Move zone editor REST/WebSocket into a dedicated local HTTP server module with explicit auth.
- Require secure boot in production and bind OTA signature verification to eFuse-trusted keys.
- Establish a single telemetry and logging queue with backpressure and drop policy.
- Define a standard error budget (frame drop %, update latency, reboot tolerance).

## 6. Open Questions for Discussion

- Is 10 Hz output a hard requirement? If yes, where is the decimator and how does it interact with tracking?
- Do we want to support multi-client access (HA + local UI) or not?
- What is the expected maximum zone count and how does that align with memory?
- Are we willing to ship without flash encryption given the stated security goals?

