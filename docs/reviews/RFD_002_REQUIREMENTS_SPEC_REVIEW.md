# RFD-002: Requirements/Spec Consistency Review (RS-1)

**Status:** Draft
**Author:** Systems Engineering Review
**Date:** 2026-01-11
**Reviewers:** Product, Firmware, Cloud

---

## Summary

The latest documentation updates significantly improve structure and clarity, but several cross-document inconsistencies remain across requirements, firmware, cloud, and contract specs. These mismatches introduce ambiguity for implementation and may create privacy/compliance risks if left unresolved.

**Recommendation:** Resolve issues in Sections 2–4 before implementation proceeds, especially telemetry privacy/consent and contract alignment.

---

## 1. Review Scope

Reviewed documents:
- `docs/REQUIREMENTS_RS1.md`
- `docs/PRD_RS1.md`
- `docs/firmware/README.md`
- `docs/firmware/GLOSSARY.md`
- `docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md`
- `docs/firmware/HARDWAREOS_MODULE_ZONE_EDITOR.md`
- `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md`
- `docs/contracts/PROTOCOL_MQTT.md`
- `docs/contracts/SCHEMA_TELEMETRY.json`
- `docs/contracts/SCHEMA_ZONE_CONFIG.json`
- `docs/cloud/README.md`
- `docs/cloud/SERVICE_TELEMETRY.md`

---

## 2. Critical Issues (Must Fix)

### 2.1 Telemetry Privacy/Consent Conflicts with Requirements

**Severity:** Critical  
**Areas:** Requirements, Cloud, Contracts

**Conflict:**
- Requirements explicitly state opt-in telemetry and **no collection of zone names or target positions** (`docs/REQUIREMENTS_RS1.md:192-199`).
- MQTT contract defines a **target stream** to cloud (`docs/contracts/PROTOCOL_MQTT.md:85-89`).
- Cloud telemetry heartbeat includes `presence` and `zone_states` arrays (`docs/cloud/SERVICE_TELEMETRY.md:59-72`).
- Telemetry schema allows arbitrary log messages; example includes `zone_kitchen` (`docs/contracts/SCHEMA_TELEMETRY.json:32-99`).
- Cloud data model stores `config_json` in D1, which will include zone names (`docs/cloud/README.md:167-176`).

**Impact:** The current cloud/contract specs violate the explicit “no target positions / no zone names” requirement and do not define where opt-in is enforced.

**Required Fix:**
1. Decide whether target streams and zone state telemetry are allowed when opt-in is enabled.
2. If disallowed, remove those fields/topics and add schema constraints to prevent PII leakage.
3. If allowed under opt-in, define consent gating and data retention explicitly.

---

### 2.2 Telemetry Contract Mismatch (Topic + Payload)

**Severity:** Critical  
**Areas:** Contracts, Cloud

**Conflict:**
- MQTT contract defines a single telemetry topic and payload schema (`docs/contracts/PROTOCOL_MQTT.md:65-70`, `docs/contracts/SCHEMA_TELEMETRY.json:1-100`).
- Cloud telemetry service uses per-category topics and different payload shapes without `device_id` or `metrics` (`docs/cloud/SERVICE_TELEMETRY.md:59-96`).
- The contract topic namespace expects `{category}/{action}` (`docs/contracts/PROTOCOL_MQTT.md:40-52`), but the catalog only lists `telemetry` without an action (`docs/contracts/PROTOCOL_MQTT.md:65-70`).

**Impact:** Firmware and cloud teams will implement incompatible telemetry pipelines, making validation with shared schemas impossible.

**Required Fix:** Align the MQTT topic catalog and telemetry schemas with the service’s actual topic/payload design (or vice versa).

---

## 3. Major Issues (Should Fix)

### 3.1 “Unlimited Zones” vs Hard Cap of 16

**Severity:** Major  
**Areas:** Requirements, Firmware, Contracts

**Conflict:**
- Requirements claim unlimited zones (practically ~16) (`docs/REQUIREMENTS_RS1.md:75`).
- Contract schema hard-caps at 16 zones (`docs/contracts/SCHEMA_ZONE_CONFIG.json:19-23`).
- Firmware data structures allocate fixed arrays of 16 zones (`docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md:61-85`).

**Impact:** User expectations (“unlimited”) conflict with firmware and contract realities. This will cause UX and marketing confusion.

**Required Fix:** Define and document a hard zone limit (or explicitly support dynamic allocation and update schemas accordingly).

---

### 3.2 Latency Requirements Contradict Each Other

**Severity:** Major  
**Areas:** Requirements, Firmware

**Conflict:**
- Presence output latency allows up to 1 second (`docs/REQUIREMENTS_RS1.md:84`).
- Non-functional requirement demands <50ms detection-to-HA (`docs/REQUIREMENTS_RS1.md:218`).
- Firmware pipeline describes HA output throttled to ~10 Hz (~100ms) (`docs/firmware/README.md:64`).
- Native API module requires <50ms state update latency (`docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md:242-246`).

**Impact:** Implementation targets are inconsistent and may be impossible to satisfy simultaneously.

**Required Fix:** Define a single latency budget and ensure firmware modules and requirements align.

---

### 3.3 Mobile App Requirement vs Firmware Non-Goal

**Severity:** Major  
**Areas:** Requirements, Firmware

**Conflict:**
- Requirements define iOS/Android app onboarding (`docs/REQUIREMENTS_RS1.md:129-156`).
- Firmware spec lists “Full mobile app (zone editor is web-based)” as a non-goal for MVP (`docs/firmware/README.md:51-55`).

**Impact:** Product scope is unclear (mobile vs web) and can cause scheduling or architecture misalignment.

**Required Fix:** Decide MVP platform and reconcile requirement language with firmware scope.

---

### 3.4 Sensitivity Model Inconsistent Across Docs

**Severity:** Major  
**Areas:** Requirements, Firmware, Contracts

**Conflict:**
- M11 zone schema uses `sensitivity: { "z_score": 2.5 }` (`docs/firmware/HARDWAREOS_MODULE_ZONE_EDITOR.md:40-53`).
- Canonical sensitivity is defined as integer 0–100 in the glossary (`docs/firmware/GLOSSARY.md:103-119`) and in the zone config schema (`docs/contracts/SCHEMA_ZONE_CONFIG.json:73-78`).
- Glossary’s hold-time default conflicts with its own formula (50 → 2500ms, not 1500ms) (`docs/firmware/GLOSSARY.md:64-72`).

**Impact:** UI, storage, and smoothing behavior may diverge depending on which spec is followed.

**Required Fix:** Pick one sensitivity model and normalize all docs and schemas to match; correct the hold-time default/formula mismatch.

---

## 4. Minor Issues (Nice to Fix)

### 4.1 Target Stream Rate Inconsistency

**Severity:** Minor  
**Areas:** Requirements, Firmware, Contracts

**Conflict:**
- Requirements specify live preview at ~10 Hz (`docs/REQUIREMENTS_RS1.md:155`).
- Local API spec streams targets at frame rate (~33 Hz) (`docs/firmware/HARDWAREOS_MODULE_ZONE_EDITOR.md:73-76`).
- MQTT target stream is specified at ~10 Hz (`docs/contracts/PROTOCOL_MQTT.md:85-89`).

**Impact:** Performance and bandwidth expectations are unclear.

**Required Fix:** Decide on a canonical target stream rate (or define separate local vs cloud rates explicitly).

---

## 5. Open Questions

1. What is the explicit consent model for telemetry, logs, and target streaming (opt-in gating location)?
2. Is “unlimited zones” a marketing statement or a true product requirement?
3. Is the MVP zone editor mobile-first, web-first, or both?

