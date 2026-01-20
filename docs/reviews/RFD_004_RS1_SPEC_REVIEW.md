# RS-1 Specification Review (RFD_004)

Version: 0.1
Date: 2026-01-20
Owner: Embedded Systems Review
Status: Draft

---

## 1. Executive Summary (Top 5 Critical Issues Before M1)

1) Identity format mismatch (12 vs 32 hex) across QR/provisioning/MQTT/schemas will break auth, ACLs, and registry mapping.
2) "Local-first" promise is incompatible with cloud-dependent provisioning/config/target streaming flows, blocking offline setup and edits.
3) Provisioning security is insufficient: open AP + HTTP exposes Wi-Fi credentials and user token despite mitigation claims.
4) Resource and timing requirements conflict (200KB heap/4MB flash vs TLS/mDNS/OTA; <50ms vs 1s latency), making firmware infeasible as written.
5) Contract/schema gaps (LWT invalid vs schema, QoS 1 semantics, telemetry schema bug, missing schemas) will cause integration failures.

---

## 2. Document-by-Document Findings

### PRD_RS1.md

#### High
1) Location: `docs/PRD_RS1.md:35`, `docs/contracts/SCHEMA_ZONE_CONFIG.json:21`
   Issue: PRD promises "unlimited" zones while schema caps zones at 16.
   Why it matters: Product promise diverges from implementation limits and UX.
   Suggested resolution: Define explicit max zones in PRD/requirements and align schema.
   Effort: small

2) Location: `docs/PRD_RS1.md:37`, `docs/REQUIREMENTS_RS1.md:111`
   Issue: PRD promises cloud-optional OTA but requirements define cloud-push OTA with serial fallback only.
   Why it matters: Offline update expectation is unmet, leading to support gaps.
   Suggested resolution: Add LAN OTA path or revise PRD claim.
   Effort: medium

3) Location: `docs/PRD_RS1.md:128`, `docs/REQUIREMENTS_RS1.md:261`
   Issue: False vacancy target <1% in PRD conflicts with <5% in requirements.
   Why it matters: Tuning and verification will be inconsistent.
   Suggested resolution: Set a single target and update both docs.
   Effort: small

#### Medium
4) Location: `docs/PRD_RS1.md:118`
   Issue: "Manual IP entry" for HA discovery is promised but not specified elsewhere.
   Why it matters: Failure-mode UX cannot be implemented or tested.
   Suggested resolution: Add a requirement and flow for manual IP entry.
   Effort: small

5) Location: `docs/PRD_RS1.md:32`
   Issue: "Confidence-weighted presence answer" is not defined in technical specs.
   Why it matters: Core behavior is untestable and subjective.
   Suggested resolution: Define algorithm or acceptance criteria.
   Effort: medium

6) Location: `docs/PRD_RS1.md:232`
   Issue: Consent model for instrumentation is unresolved and not reflected in requirements.
   Why it matters: Privacy/compliance risk and undefined telemetry behavior.
   Suggested resolution: Add explicit consent and data retention requirements.
   Effort: small

---

### REQUIREMENTS_RS1.md

#### Critical
1) Location: `docs/REQUIREMENTS_RS1.md:58`, `docs/REQUIREMENTS_RS1.md:60`, `docs/REQUIREMENTS_RS1.md:107`, `docs/contracts/PROTOCOL_PROVISIONING.md:688`
   Issue: Heap/flash budgets are likely too small for TLS, MQTT, Native API, mDNS, OTA, and provisioning UI without a detailed budget.
   Why it matters: High risk of instability or feature cuts late in M1.
   Suggested resolution: Publish module-level memory/flash budget and revise limits or hardware.
   Effort: large

2) Location: `docs/REQUIREMENTS_RS1.md:84`, `docs/REQUIREMENTS_RS1.md:250`
   Issue: Presence update latency within 1s conflicts with <50ms to HA.
   Why it matters: Conflicting targets block design and verification.
   Suggested resolution: Reconcile latency target with smoothing and publish rate.
   Effort: small

#### High
3) Location: `docs/REQUIREMENTS_RS1.md:31`, `docs/contracts/PROTOCOL_PROVISIONING.md:437`, `docs/contracts/PROTOCOL_MQTT.md:82`
   Issue: Local-first operation conflicts with cloud-dependent provisioning/config update flows.
   Why it matters: Offline setup and edits are not achievable.
   Suggested resolution: Define LAN-only config/provisioning paths or remove local-first claim.
   Effort: large

4) Location: `docs/REQUIREMENTS_RS1.md:75`, `docs/contracts/SCHEMA_ZONE_CONFIG.json:21`
   Issue: "Unlimited" zones conflicts with schema max 16 and performance assumptions.
   Why it matters: Resource planning and UX expectations diverge.
   Suggested resolution: Define an explicit max zones requirement and align schema.
   Effort: small

5) Location: `docs/REQUIREMENTS_RS1.md:96`, `docs/REQUIREMENTS_RS1.md:187`, `docs/contracts/PROTOCOL_MQTT.md:87`
   Issue: HA integration forbids raw coordinates, but zone editor requires live target positions and only cloud target stream is defined.
   Why it matters: Local-first UX and privacy goals are undermined.
   Suggested resolution: Add LAN target stream API or revise requirement.
   Effort: medium

6) Location: `docs/REQUIREMENTS_RS1.md:122`, `docs/REQUIREMENTS_RS1.md:105`, `docs/REQUIREMENTS_RS1.md:269`
   Issue: "Flash encryption optional" conflicts with "credentials encrypted at rest".
   Why it matters: Security requirements cannot be met without a clear encryption plan.
   Suggested resolution: Mandate NVS or flash encryption for production.
   Effort: medium

#### Medium
7) Location: `docs/REQUIREMENTS_RS1.md:82`
   Issue: Sensitivity mapping to hold time is unspecified.
   Why it matters: Calibration and tests lack pass/fail criteria.
   Suggested resolution: Define mapping curve and defaults.
   Effort: small

8) Location: `docs/REQUIREMENTS_RS1.md:248`
   Issue: Performance targets are not justified or tied to measurement.
   Why it matters: Targets may be unrealistic for ESP32-C3.
   Suggested resolution: Add profiling evidence or adjust targets.
   Effort: small

9) Location: `docs/REQUIREMENTS_RS1.md:258`
   Issue: Uptime requirement is ambiguous given Wi-Fi drops and watchdog resets.
   Why it matters: Unclear verification criteria.
   Suggested resolution: Define acceptable reboot rate and conditions.
   Effort: small

10) Location: `docs/REQUIREMENTS_RS1.md:211`
    Issue: Device identity derivation is not defined beyond "derived from MAC".
    Why it matters: ID inconsistencies propagate across QR/MQTT/cloud.
    Suggested resolution: Specify encoding/length and update all docs.
    Effort: small

---

### PROTOCOL_MQTT.md

#### Critical
1) Location: `docs/contracts/PROTOCOL_MQTT.md:50`, `docs/contracts/PROTOCOL_MQTT.md:334`, `docs/contracts/SCHEMA_TELEMETRY.json:9`, `docs/contracts/PROTOCOL_PROVISIONING.md:94`
   Issue: device_id is 32 hex in MQTT topics/schemas but 12 hex in provisioning/QR.
   Why it matters: Auth, ACLs, and registry mapping will break.
   Suggested resolution: Define a single device_id length/derivation and update all references.
   Effort: medium

#### High
2) Location: `docs/contracts/PROTOCOL_MQTT.md:408`, `docs/contracts/PROTOCOL_MQTT.md:412`
   Issue: ACL table allows devices to publish to all `opticworks/{id}/#` while policy forbids ota/trigger and config/update.
   Why it matters: Broker config would violate security policy.
   Suggested resolution: Tighten ACLs and document explicit denies.
   Effort: small

3) Location: `docs/contracts/PROTOCOL_MQTT.md:377`
   Issue: QoS 1 is described as "exactly once".
   Why it matters: QoS 1 is at-least-once, so OTA/config actions can duplicate.
   Suggested resolution: Require idempotency or use QoS 2 where needed.
   Effort: small

4) Location: `docs/contracts/PROTOCOL_MQTT.md:393`, `docs/contracts/SCHEMA_DEVICE_STATE.json:7`
   Issue: LWT payload lacks firmware_version required by schema.
   Why it matters: Offline state will not validate.
   Suggested resolution: Include firmware_version in LWT or relax schema.
   Effort: small

5) Location: `docs/contracts/PROTOCOL_MQTT.md:87`, `docs/REQUIREMENTS_RS1.md:231`
   Issue: Target stream sends raw positions to cloud despite "no target positions" requirement.
   Why it matters: Privacy promise is violated.
   Suggested resolution: Gate target stream behind explicit consent or keep it local-only.
   Effort: medium

6) Location: `docs/contracts/PROTOCOL_MQTT.md:45`, `docs/contracts/PROTOCOL_MQTT.md:69`, `docs/contracts/PROTOCOL_MQTT.md:95`
   Issue: Topic namespace definition `{category}/{action}` is violated by telemetry/state topics.
   Why it matters: Tooling and ACLs will be inconsistent.
   Suggested resolution: Standardize topics or update namespace definition.
   Effort: small

#### Medium
7) Location: `docs/contracts/PROTOCOL_MQTT.md:134`, `docs/contracts/MOCK_BOUNDARIES.md:122`
   Issue: Missing schemas for OTA status, config status, diagnostics, provisioning, and target stream.
   Why it matters: Contract tests cannot validate most MQTT traffic.
   Suggested resolution: Add schemas or explicitly mark unvalidated payloads.
   Effort: medium

8) Location: `docs/contracts/PROTOCOL_MQTT.md:419`
   Issue: Rate limits omit device state/config status/provisioning and have no retry/backoff guidance.
   Why it matters: Operational behavior is undefined.
   Suggested resolution: Add limits and backoff policy.
   Effort: small

9) Location: `docs/contracts/PROTOCOL_MQTT.md:204`, `docs/REQUIREMENTS_RS1.md:182`
   Issue: Cloud conversion from meters -> mm is stated without defining app units or conversion owner.
   Why it matters: Zone placement can be wrong by 1000x.
   Suggested resolution: Define units and conversion responsibilities at each interface.
   Effort: small

---

### PROTOCOL_PROVISIONING.md

#### Critical
1) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:437`, `docs/contracts/PROTOCOL_PROVISIONING.md:647`
   Issue: Credential storage depends on cloud registration, but fallback says store even if cloud fails.
   Why it matters: Device can get stuck in AP mode or lose credentials.
   Suggested resolution: Store credentials after Wi-Fi connect, retry cloud separately.
   Effort: medium

2) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:270`, `docs/contracts/PROTOCOL_PROVISIONING.md:664`
   Issue: Open AP + HTTP transmits Wi-Fi password and user_token in clear.
   Why it matters: Nearby attackers can steal home Wi-Fi credentials.
   Suggested resolution: Secure AP or encrypt provisioning payloads.
   Effort: large

#### High
3) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:414`, `docs/contracts/PROTOCOL_PROVISIONING.md:357`
   Issue: Provisioning status polling/WebSocket breaks when AP stops for STA connection.
   Why it matters: App cannot show accurate progress or errors.
   Suggested resolution: Keep AP+STA active or provide cloud/LAN status callback.
   Effort: medium

4) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:589`, `docs/contracts/PROTOCOL_MQTT.md:56`
   Issue: Factory reset MQTT command is referenced but not defined in MQTT contract.
   Why it matters: Cloud/app reset path is undefined and insecure.
   Suggested resolution: Add topic/schema/ACLs to MQTT contract.
   Effort: small

5) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:510`
   Issue: Pairing code flow for unclaimed devices is referenced but unspecified.
   Why it matters: Ownership transfer cannot be implemented.
   Suggested resolution: Define pairing code generation, TTL, and claim API.
   Effort: medium

6) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:166`, `docs/REQUIREMENTS_RS1.md:53`
   Issue: AP timeout deep sleep assumes a provisioning button, but hardware spec does not require one.
   Why it matters: Devices can become unreachable.
   Suggested resolution: Require a button or periodic wake windows.
   Effort: medium

#### Medium
7) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:301`, `docs/contracts/PROTOCOL_PROVISIONING.md:429`
   Issue: `timeout_sec=30` conflicts with 5x15s connect attempts.
   Why it matters: UI may time out early.
   Suggested resolution: Align timeout_sec with actual connection budget.
   Effort: small

8) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:252`, `docs/REQUIREMENTS_RS1.md:277`
   Issue: Scan results include 5GHz networks but device is 2.4GHz only.
   Why it matters: Users can choose unsupported networks.
   Suggested resolution: Filter or flag unsupported SSIDs.
   Effort: small

9) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:450`, `docs/REQUIREMENTS_RS1.md:123`
   Issue: No time sync requirement before TLS.
   Why it matters: MQTT/HTTPS can fail with invalid RTC.
   Suggested resolution: Define NTP/RTC behavior.
   Effort: small

10) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:660`
    Issue: "Physical access" is the only protection against rogue provisioning.
    Why it matters: Nearby attackers can hijack AP-mode provisioning.
    Suggested resolution: Require a short pairing window or button press.
    Effort: small

---

### SCHEMA_DEVICE_STATE.json

#### High
1) Location: `docs/contracts/SCHEMA_DEVICE_STATE.json:7`, `docs/contracts/PROTOCOL_MQTT.md:393`
   Issue: Required firmware_version makes LWT offline payload invalid.
   Why it matters: Offline state will not validate or parse.
   Suggested resolution: Include firmware_version in LWT or relax schema.
   Effort: small

2) Location: `docs/contracts/SCHEMA_DEVICE_STATE.json:66`, `docs/contracts/PROTOCOL_MQTT.md:438`
   Issue: `additionalProperties: false` conflicts with versioning rules that allow optional fields.
   Why it matters: Forward compatibility breaks.
   Suggested resolution: Relax schema or version schemas per change policy.
   Effort: small

---

### SCHEMA_OTA_MANIFEST.json

#### Medium
1) Location: `docs/contracts/SCHEMA_OTA_MANIFEST.json:7`, `docs/REQUIREMENTS_RS1.md:113`
   Issue: Signature handling is unspecified despite ECDSA requirement.
   Why it matters: Authenticity verification is ambiguous.
   Suggested resolution: Define signature location or add fields (sig, key_id).
   Effort: medium

2) Location: `docs/contracts/SCHEMA_OTA_MANIFEST.json:48`, `docs/REQUIREMENTS_RS1.md:125`
   Issue: `force` flag can conflict with anti-rollback policy.
   Why it matters: Update logic is unclear for security fixes.
   Suggested resolution: Define allowable use and constraints.
   Effort: small

---

### SCHEMA_ZONE_CONFIG.json

#### High
1) Location: `docs/contracts/SCHEMA_ZONE_CONFIG.json:21`, `docs/REQUIREMENTS_RS1.md:26`
   Issue: Schema caps zones at 16 while requirements/PRD state unlimited.
   Why it matters: Implementation limit is hidden.
   Suggested resolution: Align all docs on max zones.
   Effort: small

2) Location: `docs/contracts/SCHEMA_ZONE_CONFIG.json:35`, `docs/contracts/PROTOCOL_MQTT.md:211`
   Issue: Zone id pattern conflicts with protocol (lowercase underscore vs alphanumeric).
   Why it matters: IDs can be rejected unexpectedly.
   Suggested resolution: Unify allowed character set.
   Effort: small

#### Medium
3) Location: `docs/contracts/SCHEMA_ZONE_CONFIG.json:48`, `docs/REQUIREMENTS_RS1.md:188`
   Issue: Schema cannot enforce non-self-intersecting polygons.
   Why it matters: Validation responsibility is unclear.
   Suggested resolution: Specify validation point and error codes.
   Effort: small

---

### SCHEMA_TELEMETRY.json

#### Critical
1) Location: `docs/contracts/SCHEMA_TELEMETRY.json:22`
   Issue: `oneOf` (number, integer) rejects integers because integer is a subset of number.
   Why it matters: Valid telemetry will fail schema validation.
   Suggested resolution: Replace `oneOf` with `anyOf` or `number`.
   Effort: trivial

#### Medium
2) Location: `docs/contracts/SCHEMA_TELEMETRY.json:29`, `docs/contracts/PROTOCOL_MQTT.md:165`
   Issue: `propertyNames` pattern is restrictive and undocumented.
   Why it matters: Future metrics will be blocked unexpectedly.
   Suggested resolution: Relax pattern or document naming constraints.
   Effort: small

---

### MOCK_BOUNDARIES.md

#### High
1) Location: `docs/contracts/MOCK_BOUNDARIES.md:122`, `docs/contracts/PROTOCOL_MQTT.md:134`
   Issue: Contract tests assume schemas for all MQTT payloads but most are missing.
   Why it matters: Critical flows are unvalidated.
   Suggested resolution: Add schemas for missing payloads.
   Effort: medium

2) Location: `docs/contracts/MOCK_BOUNDARIES.md:235`
   Issue: CI example validates schema as data, not examples.
   Why it matters: False positives in contract validation.
   Suggested resolution: Validate fixtures/examples explicitly.
   Effort: small

#### Medium
3) Location: `docs/contracts/MOCK_BOUNDARIES.md:259`
   Issue: Breaking-change detector does not check enums/constraints despite claiming to.
   Why it matters: Breaking changes can slip through.
   Suggested resolution: Extend script to cover nested properties, enums, and constraints.
   Effort: medium

4) Location: `docs/contracts/MOCK_BOUNDARIES.md:311`, `docs/contracts/SCHEMA_ZONE_CONFIG.json:3`
   Issue: Versioning guidance expects versioned $id but schemas are unversioned.
   Why it matters: Versioning strategy is unclear.
   Suggested resolution: Align $id format or update guidance.
   Effort: small

---

## 3. Cross-Cutting Concerns

1) Location: `docs/REQUIREMENTS_RS1.md:155`, `docs/contracts/PROTOCOL_MQTT.md:50`, `docs/contracts/PROTOCOL_PROVISIONING.md:94`, `docs/contracts/SCHEMA_TELEMETRY.json:9`
   Issue: device_id length/encoding inconsistent across QR, provisioning, MQTT, and schemas.
   Why it matters: Breaks auth, ACLs, and registry mapping.
   Suggested resolution: Define canonical device_id derivation and update all docs/schemas.
   Effort: medium

2) Location: `docs/REQUIREMENTS_RS1.md:31`, `docs/contracts/PROTOCOL_PROVISIONING.md:437`, `docs/contracts/PROTOCOL_MQTT.md:82`, `docs/contracts/PROTOCOL_MQTT.md:87`
   Issue: Local-first promise conflicts with cloud-dependent provisioning/config/target streaming flows.
   Why it matters: Offline setup and edits are impossible.
   Suggested resolution: Define LAN-only workflows or revise local-first requirement.
   Effort: large

3) Location: `docs/REQUIREMENTS_RS1.md:84`, `docs/REQUIREMENTS_RS1.md:250`, `docs/PRD_RS1.md:128`, `docs/REQUIREMENTS_RS1.md:261`
   Issue: Latency/accuracy targets conflict across docs.
   Why it matters: Design and test criteria are unclear.
   Suggested resolution: Reconcile and publish a single set of targets.
   Effort: small

4) Location: `docs/PRD_RS1.md:35`, `docs/REQUIREMENTS_RS1.md:75`, `docs/contracts/SCHEMA_ZONE_CONFIG.json:21`
   Issue: "Unlimited" zones vs schema max 16.
   Why it matters: Resource planning and UX promises conflict.
   Suggested resolution: Set explicit max zones and align all docs.
   Effort: small

5) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:270`, `docs/REQUIREMENTS_RS1.md:122`, `docs/REQUIREMENTS_RS1.md:61`
   Issue: Security posture is inconsistent (open AP with cleartext credentials, flash encryption optional, software-only keys) and threat model is not stated.
   Why it matters: Security guarantees cannot be verified.
   Suggested resolution: Define threat model and align requirements/mitigations.
   Effort: medium

6) Location: `docs/contracts/PROTOCOL_MQTT.md:438`, `docs/contracts/SCHEMA_DEVICE_STATE.json:66`, `docs/contracts/SCHEMA_TELEMETRY.json:80`
   Issue: Protocol versioning allows optional fields but schemas forbid additional properties.
   Why it matters: Forward compatibility breaks.
   Suggested resolution: Adjust schemas or version schemas per change policy.
   Effort: medium

7) Location: `docs/REQUIREMENTS_RS1.md:231`, `docs/contracts/PROTOCOL_MQTT.md:87`, `docs/contracts/SCHEMA_TELEMETRY.json:32`
   Issue: Privacy promise forbids target positions but target stream sends them to cloud and logs may include zone names.
   Why it matters: Privacy and consent compliance risk.
   Suggested resolution: Gate target stream and logs behind explicit consent or keep them local.
   Effort: medium

8) Location: `docs/contracts/PROTOCOL_MQTT.md:204`, `docs/REQUIREMENTS_RS1.md:182`, `docs/contracts/SCHEMA_ZONE_CONFIG.json:5`
   Issue: Coordinate units and conversion ownership are unclear (mm vs m).
   Why it matters: Zone placement errors and inconsistent app/firmware behavior.
   Suggested resolution: Define units and conversion responsibilities at each interface.
   Effort: small

---

## 4. Missing Requirements

1) Location: `docs/REQUIREMENTS_RS1.md:211`
   Issue: Canonical device_id derivation/encoding requirement is missing (length, hashing, formatting).
   Why it matters: Identity mismatches propagate across QR/MQTT/cloud.
   Suggested resolution: Add a single device_id specification and reference it everywhere.
   Effort: small

2) Location: `docs/REQUIREMENTS_RS1.md:123`
   Issue: No time synchronization requirement for TLS/HTTPS validation.
   Why it matters: TLS handshakes can fail on cold boot.
   Suggested resolution: Specify NTP/RTC strategy and boot-time behavior.
   Effort: small

3) Location: `docs/REQUIREMENTS_RS1.md:107`
   Issue: OTA retry/backoff, download timeout, resume behavior, and max image size are unspecified.
   Why it matters: OTA success rate target cannot be met or tested.
   Suggested resolution: Add explicit OTA transport requirements.
   Effort: medium

4) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:119`
   Issue: Manufacturing test and secret injection requirements are not defined beyond QR generation.
   Why it matters: Device identity and security depend on factory process.
   Suggested resolution: Add manufacturing/QA requirements and verification steps.
   Effort: medium

5) Location: `docs/PRD_RS1.md:254`
   Issue: EOL/decommissioning requirements are missing (unclaiming, credential revocation, data wipe).
   Why it matters: Resale and end-of-support security risk.
   Suggested resolution: Define EOL lifecycle and cloud/device behavior.
   Effort: medium

6) Location: `docs/PRD_RS1.md:245`, `docs/contracts/PROTOCOL_MQTT.md:71`
   Issue: Support triage requirements (diagnostic codes, log retention, LED patterns) are missing.
   Why it matters: Field debugging will be slow and inconsistent.
   Suggested resolution: Define diagnostics and support workflow requirements.
   Effort: small

7) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:270`
   Issue: No requirements for hidden SSIDs, WPA2-Enterprise, or captive-portal networks.
   Why it matters: Common real-world Wi-Fi setups will fail unpredictably.
   Suggested resolution: Explicitly support or explicitly exclude with UX messaging.
   Effort: small

8) Location: `docs/contracts/PROTOCOL_MQTT.md:419`, `docs/contracts/PROTOCOL_PROVISIONING.md:402`
   Issue: No global rate limits or timeout budgets for MQTT device state/config status/provisioning endpoints.
   Why it matters: Broker load and device resource use are undefined.
   Suggested resolution: Define per-topic and per-endpoint limits.
   Effort: small

9) Location: `docs/REQUIREMENTS_RS1.md:125`
   Issue: No policy for anti-rollback counter exhaustion (32 updates max).
   Why it matters: Long-term update cadence may brick devices.
   Suggested resolution: Define update strategy and counter usage plan.
   Effort: small

10) Location: `docs/REQUIREMENTS_RS1.md:180`, `docs/contracts/PROTOCOL_MQTT.md:87`
    Issue: No local-only zone editing and target preview API requirement.
    Why it matters: Local-first promise cannot be satisfied.
    Suggested resolution: Define LAN API or HA-based config channel.
    Effort: large

---

## 5. Test Gap Analysis

1) Location: `docs/REQUIREMENTS_RS1.md:246`
   Issue: Performance requirements lack defined measurement or test harness.
   Why it matters: Pass/fail criteria cannot be validated.
   Suggested resolution: Add profiling tests and instrumentation for parse time, zone eval, and CPU.
   Effort: medium

2) Location: `docs/REQUIREMENTS_RS1.md:256`
   Issue: Reliability metrics (uptime, watchdog recovery) have no soak or fault-injection tests.
   Why it matters: Field stability risk.
   Suggested resolution: Add long-run and watchdog trigger tests.
   Effort: medium

3) Location: `docs/REQUIREMENTS_RS1.md:265`
   Issue: Security requirements (secure boot, encryption at rest) have no verification steps.
   Why it matters: Production security could ship unverified.
   Suggested resolution: Add manufacturing checks and CI validation steps.
   Effort: small

4) Location: `docs/PRD_RS1.md:156`
   Issue: Setup time success metric lacks implementation-level instrumentation requirements in technical specs.
   Why it matters: Success criteria cannot be measured.
   Suggested resolution: Add telemetry event requirements and test harness.
   Effort: small

5) Location: `docs/contracts/PROTOCOL_MQTT.md:373`
   Issue: No tests for QoS 1 duplicate delivery handling for OTA/config.
   Why it matters: Duplicate messages can cause repeated updates or config thrash.
   Suggested resolution: Add idempotency tests and duplicate publish simulations.
   Effort: medium

6) Location: `docs/contracts/PROTOCOL_PROVISIONING.md:737`
   Issue: Provisioning tests omit AP+STA transition, 5GHz selection, open-AP eavesdropping, and cloud outage with credential storage.
   Why it matters: Common failures are untested.
   Suggested resolution: Add negative and fault-injection cases.
   Effort: medium

7) Location: `docs/contracts/MOCK_BOUNDARIES.md:351`
   Issue: Integration test scenarios omit provisioning, factory reset, diagnostics, and local-only mode.
   Why it matters: Key flows are unvalidated across system boundaries.
   Suggested resolution: Add contract-driven integration tests for these flows.
   Effort: medium

8) Location: `docs/contracts/SCHEMA_TELEMETRY.json:32`
   Issue: Telemetry opt-in and PII redaction are not tested.
   Why it matters: Privacy commitments may be violated.
   Suggested resolution: Add tests to validate logs/metrics do not include SSIDs or zone names.
   Effort: small

9) Location: `docs/REQUIREMENTS_RS1.md:58`
   Issue: Memory budget is not validated under TLS + MQTT + Native API + OTA concurrency.
   Why it matters: Heap exhaustion could appear in field.
   Suggested resolution: Add stress tests and heap watermark monitoring.
   Effort: medium

---

## 6. Recommended Actions (Prioritized)

1) Unify device_id derivation and length across QR, provisioning, MQTT topics, and schemas; update ACLs and registry accordingly.
2) Resolve local-first behavior by defining LAN-only provisioning/config/target streaming paths and allowing credential storage without cloud; update docs to match.
3) Harden provisioning security (secure AP or encrypted payloads, short pairing windows, button requirement) and align threat model/mitigations.
4) Reconcile latency/accuracy targets and zone limits; set explicit max zones and publish a realistic performance budget backed by profiling.
5) Fix MQTT contract inconsistencies (topic namespace, QoS semantics, ACLs, LWT payload) and add schemas for all MQTT payloads.
6) Correct schema issues (telemetry oneOf bug, id patterns, additionalProperties policy) and align with versioning strategy.
7) Define OTA verification details (signature location, key rotation, retry/backoff, anti-rollback policy) to meet >99% OTA success goal.
8) Expand the test plan to cover provisioning edge cases, MQTT duplicates, OTA power loss, memory stress, and privacy redaction.

