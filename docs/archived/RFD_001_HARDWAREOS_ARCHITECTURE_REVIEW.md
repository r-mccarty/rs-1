# RFD-001: HardwareOS Architecture Review

**Status:** Draft
**Author:** Systems Engineering Review
**Date:** 2026-01-09
**Reviewers:** OpticWorks Firmware, Product

---

## Summary

This RFD documents a critical systems engineering review of the HardwareOS firmware architecture for RS-1. The review identifies **25+ issues** across the 11-module specification, ranging from fatal inconsistencies to missing edge case handling. Several issues will cause runtime failures if implemented as specified.

**Recommendation:** Do not proceed with implementation until the issues in Sections 2-4 are resolved. The current specifications would result in a non-functional or unreliable product.

---

## Table of Contents

1. [Review Scope](#1-review-scope)
2. [Critical Issues (Must Fix)](#2-critical-issues-must-fix)
3. [Major Issues (Should Fix)](#3-major-issues-should-fix)
4. [Minor Issues (Nice to Fix)](#4-minor-issues-nice-to-fix)
5. [Module-by-Module Analysis](#5-module-by-module-analysis)
6. [Cross-Cutting Concerns](#6-cross-cutting-concerns)
7. [Recommendations](#7-recommendations)
8. [Open Questions](#8-open-questions)

---

## 1. Review Scope

Reviewed documents:
- `docs/firmware/README.md` (Architecture Overview)
- `docs/firmware/HARDWAREOS_MODULE_*.md` (11 modules)
- `docs/PRD_RS1.md` (Product Requirements)
- `docs/TECH_REQUIREMENTS_RS1.md` (Technical Requirements)

**Note:** Documentation was restructured after this review. `hardwareos/` is now `firmware/`.

Review criteria:
- Internal consistency across modules
- Completeness of specifications
- Edge case coverage
- Resource constraints (ESP32-C3)
- Security posture
- Testability

---

## 2. Critical Issues (Must Fix)

### 2.1 Frame Rate Contradiction Between M01 and M02

**Severity:** Fatal
**Modules:** M01, M02

M01 (Radar Ingest) correctly states:
> "LD2450 outputs at ~33 Hz (30ms frame interval)"

M02 (Tracking) incorrectly states:
> "Frame rate: assumed 10 Hz (pending confirmation)"

**Impact:** The tracking module is designed for 10 Hz input but will receive 33 Hz. Gate distances, prediction intervals, hold times, and velocity calculations will all be wrong by a factor of 3.3x.

**Required Fix:** M02 must be redesigned for 33 Hz input. All timing parameters (gate distance scaling, dt calculations) must use 30ms intervals.

---

### 2.2 M02 Tracking Module is Dangerously Underspecified

**Severity:** Fatal
**Module:** M02

M02 is the thinnest specification in the entire stack, yet it's arguably the most complex module. Critical gaps:

| Missing Element | Impact |
|-----------------|--------|
| No data structures defined | Implementers will guess |
| No state machine for track lifecycle | Undefined behavior |
| No memory budget | Could exceed ESP32-C3 limits |
| No error handling | Silent failures |
| No defined output format | M03 must guess structure |
| No occlusion prediction algorithm | Core feature unspecified |
| "Kalman or alpha-beta filter" - which one? | Implementation variance |

**Comparison:** M01 has 180 lines of specification. M02 has 99 lines. M02 is 3x more complex than M01.

**Required Fix:** M02 needs complete rewrite with:
- Full state machine diagram
- Memory layout
- Algorithm selection rationale
- Complete track_t structure definition
- Occlusion prediction pseudocode

---

### 2.3 Coordinate Unit Mismatch Across Modules

**Severity:** Fatal
**Modules:** M01, M03, M11

| Module | Unit | Source |
|--------|------|--------|
| M01 Radar Ingest | mm | "X position in mm (-6000 to +6000)" |
| M03 Zone Engine | mm | "Coordinates in mm, matching M01 output" |
| M11 Zone Editor | meters | JSON shows `"vertices": [[0.2, 0.4], [2.0, 0.4]...]` |

**Impact:** Zone configurations from the editor will be off by 1000x. A zone intended to be 2 meters wide will be interpreted as 2 millimeters.

**Required Fix:**
1. Define canonical unit (recommend mm throughout firmware)
2. Add explicit conversion in M11 on config receive
3. Document unit in every API contract

---

### 2.4 Memory Budget Does Not Account for TLS

**Severity:** Critical
**Modules:** M05, M07, M09, M10

ESP32-C3 RAM budget from spec: 200KB heap.

TLS connection memory requirements (mbedTLS on ESP-IDF):
- ~16KB per TLS connection (minimum)
- ~33KB per connection with typical cipher suites

Required simultaneous TLS connections:
| Connection | Module | Memory |
|------------|--------|--------|
| MQTT (OTA) | M07 | 33KB |
| MQTT (Telemetry) | M09 | (shared) |
| HTTPS (OTA download) | M07 | 33KB |
| Native API (Noise) | M05 | 8KB |

**Worst case:** 74KB just for TLS during OTA.

Other memory consumers not fully budgeted:
- M03: "< 2KB" for zones
- M04: "~32 bytes per zone" (512 bytes for 16 zones)
- M05: "< 8KB" (excludes TLS)
- M09: "8KB" log buffer

**Impact:** OTA updates will likely fail due to heap exhaustion. Device may crash during firmware download.

**Required Fix:**
1. Create unified memory budget spreadsheet
2. Measure actual TLS memory usage
3. Consider single TLS connection with multiplexing
4. Add heap monitoring with OTA abort threshold

---

### 2.5 NVS Wear Rate Contradiction

**Severity:** Critical
**Modules:** M06, M08

M06 (Config Store) assumes:
> "Config updates are infrequent (< 10/day)"

M08 (Timebase) schedules:
> "nvs_commit | 60000ms | Commit pending NVS writes"

**Math:** 60-second commits = 1,440 commits/day, not <10.

**Impact:** NVS partition will wear out in:
- ESP32 NVS endurance: ~100,000 erase cycles
- Days to failure: 100,000 / 1,440 = **69 days**

Device will fail within 3 months of deployment.

**Required Fix:**
1. Remove automatic NVS commit from scheduler
2. Commit only on actual config changes
3. Add wear-leveling monitoring to telemetry

---

### 2.6 Presence Smoothing Timing Assumptions Wrong

**Severity:** Critical
**Module:** M04

M04 assumes:
> "Frame rate from M03 is ~10 Hz (after throttling)"

But M03 Zone Engine:
> "For each track frame from M02" - processes at full rate

The architecture shows M03 runs at radar rate (33 Hz), with throttling only at M05 Native API output.

**Impact:**
- Timer granularity calculations are wrong
- Hold times will be 3.3x shorter than intended
- More flicker, not less

**Required Fix:** Clarify where throttling occurs. If M04 runs at 33 Hz, adjust all timer calculations.

---

## 3. Major Issues (Should Fix)

### 3.1 Sensitivity Has Three Different Meanings

**Severity:** High
**Modules:** M03, M04, M11

| Module | Sensitivity Meaning |
|--------|---------------------|
| M03 Zone Engine | `uint8_t sensitivity` field in zone_config_t (undefined purpose) |
| M04 Smoothing | Maps 0-100 to hold_time_ms and enter_delay_ms |
| M11 Zone Editor | `"sensitivity": { "z_score": 2.5 }` (statistical threshold) |

PRD mentions:
> "configurable sensitivity based on z-score analysis"

**Impact:** Three different interpretations of the same concept. Zone editor z_score won't map to M04's hold time formula.

**Required Fix:** Define single sensitivity model, document mapping, ensure all modules agree.

---

### 3.2 Watchdog Will Reset on Radar Disconnect

**Severity:** High
**Module:** M08

M08 watchdog expects feed from source 1 (Radar ingest):
> "| 1 | Radar ingest | Every frame |"

If LD2450 disconnects or fails:
1. No frames arrive
2. Radar ingest stops feeding watchdog
3. System resets
4. Radar still disconnected
5. Reset loop

**Impact:** Radar hardware failure causes infinite reboot loop.

**Required Fix:**
1. Radar module should detect silence and report degraded state
2. Watchdog should accept "no radar" as valid state
3. System should operate in degraded mode without presence detection

---

### 3.3 Native API Reboot Timeout Conflicts with Non-HA Use

**Severity:** High
**Module:** M05

M05 specifies:
> "reboot_timeout_ms | uint32 | 900000 | Reboot if no connection (15 min)"

**Impact:** Device with no Home Assistant configured will reboot every 15 minutes forever.

**Required Fix:**
1. Disable reboot timeout by default
2. Only enable if HA connection has been established previously
3. Add user-configurable option

---

### 3.4 Zone Editor Pairing Token Expires During Use

**Severity:** High
**Modules:** M10, M11

M10 Security:
> "pairing_timeout_sec | uint16 | 300 | Pairing code validity"

M11 Zone Editor:
> "Local API requires device pairing token"

**Impact:** User starts zone editing, takes 6 minutes to configure, POST fails with auth error. Terrible UX.

**Required Fix:**
1. Pairing generates session token with longer validity (1 hour)
2. Or: pairing token is one-time, generates persistent session
3. Document session management

---

### 3.5 Point-in-Polygon Edge Cases Undefined

**Severity:** High
**Module:** M03

M03 mentions in testing:
> "Edge cases: point on vertex, point on edge"

But ray casting algorithm provided does NOT handle these deterministically:
```c
if (((yi > y) != (yj > y)) && ...)
```

**Impact:** Track exactly on zone boundary will flicker between inside/outside depending on floating point rounding.

**Required Fix:**
1. Define boundary behavior (inside or outside?)
2. Add epsilon margin to zone checks
3. Or: shrink zones by 1mm for evaluation

---

### 3.6 Boot Sequence Undefined

**Severity:** High
**All Modules**

No specification defines:
- Module initialization order
- Dependencies between modules at boot
- Behavior if dependency fails to initialize

**Example race conditions:**
- Zone Engine starts before Config Store loads zones
- Native API starts before Wi-Fi connects
- OTA Manager queries version before Security initializes

**Required Fix:** Add `docs/firmware/BOOT_SEQUENCE.md` with:
- Initialization DAG
- Timeout handling per stage
- Fallback behavior

---

### 3.7 Sensor Coordinate System Ambiguous

**Severity:** High
**Module:** M01

M01 states:
> "X-axis: horizontal, positive right"

**But right relative to what?**
- Looking at the sensor face?
- Looking from behind the sensor?
- Standing in the room facing the sensor?

**Impact:** Every zone will be mirrored on 50% of installations.

**Required Fix:** Add diagram showing:
- Physical sensor orientation
- Coordinate axis directions
- Example room layout

---

### 3.8 mDNS Service Type Collision

**Severity:** Medium
**Module:** M05

M05 advertises:
> "Service: _esphomelib._tcp.local"

**Impact:** Home Assistant will discover RS-1 as an ESPHome device. If real ESPHome devices exist on network, discovery may behave unexpectedly. ESPHome version checks may fail.

**Consider:** Using `_opticworks._tcp.local` with `native_api_compat=esphome` attribute.

---

### 3.9 No Wi-Fi Disconnect Behavior Defined

**Severity:** Medium
**All Modules**

What happens when Wi-Fi drops mid-operation?

| Component | Expected Behavior | Specified? |
|-----------|-------------------|------------|
| Presence detection | Continue locally | No |
| Zone editor streaming | Stop gracefully | No |
| OTA download | Abort, retry later | Partial |
| Native API | Reconnect | No |
| Telemetry | Buffer locally | Partial |

**Required Fix:** Add "Degraded Mode" section to each external interface module.

---

### 3.10 Anti-Rollback Limited to 32 Versions

**Severity:** Medium
**Module:** M10

M10 states:
> "ESP32-C3 provides 32 bits for anti-rollback"

**Impact:** After 32 security-relevant updates, device cannot accept new firmware. For 10-year product lifetime with quarterly security updates, this is 40 updates needed.

**Options:**
1. Accept 8-year limit (document clearly)
2. Reserve anti-rollback for critical CVEs only
3. Research eFuse extension mechanisms

---

## 4. Minor Issues (Nice to Fix)

### 4.1 Histogram Bucket Boundaries Undefined

**Module:** M09

Histograms have 8 buckets but no defined boundaries. Telemetry will be meaningless without consistent bucket definitions.

### 4.2 Calibration Has No Calibration Procedure

**Module:** M06

`calibration_t` is defined but no module specifies how to populate it. Dead data structure.

### 4.3 Track Structure Defined in Wrong Module

**Modules:** M02, M03

`track_t` structure shown in M03, but it's M02's output. Should be in M02.

### 4.4 No Connection Limit Enforcement

**Module:** M05

Says single connection is sufficient but doesn't specify rejecting second connection.

### 4.5 MQTT Topic Structure Uncoordinated

**Modules:** M07, M09, M11

Each module defines its own topic pattern. Should have unified topic schema document.

### 4.6 Zone Version Ownership Unclear

**Modules:** M03, M06, M11

Version field exists in multiple places. Who is authoritative?

### 4.7 Default Sensitivity Value Inconsistent

**Modules:** M04, M06

M04 says `default_sensitivity | uint8 | 50`
M06 shows in device_settings_t but doesn't specify default.

### 4.8 CPU Budget Not Tracked

**All Modules**

M01 claims "<2% CPU" but no aggregate budget. No measurement methodology.

---

## 5. Module-by-Module Analysis

### M01: Radar Ingest - Grade: B+

**Strengths:**
- Well-specified frame format
- Clear error handling
- Performance requirements defined

**Weaknesses:**
- Coordinate system ambiguity
- No handling of sensor firmware updates/versions

---

### M02: Tracking - Grade: F

**Strengths:**
- Mentions the right concepts (Kalman, gating, association)

**Weaknesses:**
- Fatally incomplete
- Wrong frame rate assumption
- No data structures
- No state machine
- No memory budget
- No algorithm pseudocode

**Verdict:** Rewrite required before implementation.

---

### M03: Zone Engine - Grade: B

**Strengths:**
- Good point-in-polygon algorithm
- Validation rules defined
- Performance budgeted

**Weaknesses:**
- Edge case handling incomplete
- Depends on unspecified M02 output
- Sensitivity field unused

---

### M04: Presence Smoothing - Grade: B-

**Strengths:**
- Clear state machine
- Sensitivity mapping defined
- Confidence weighting is clever

**Weaknesses:**
- Wrong frame rate assumption (10 Hz vs 33 Hz)
- Timer calculations will be wrong
- No handling of zone config changes mid-state

---

### M05: Native API - Grade: B

**Strengths:**
- Protocol well understood
- Good entity design
- Privacy-conscious (excludes raw data)

**Weaknesses:**
- Memory budget excludes TLS
- Reboot timeout will cause problems
- Multi-connection undefined

---

### M06: Config Store - Grade: A-

**Strengths:**
- Atomic writes well designed
- Rollback mechanism
- Encryption at rest

**Weaknesses:**
- NVS wear assumption contradicted by M08
- Schema migration undefined

---

### M07: OTA - Grade: B-

**Strengths:**
- Rollback strategy clear
- Security requirements present

**Weaknesses:**
- Too brief compared to complexity
- Memory during download not budgeted
- Retry backoff may conflict with MQTT keepalive

---

### M08: Timebase - Grade: B

**Strengths:**
- Comprehensive scheduling
- Watchdog well thought out
- NTP handling reasonable

**Weaknesses:**
- NVS commit interval will kill flash
- Watchdog doesn't handle sensor failure gracefully

---

### M09: Logging - Grade: A-

**Strengths:**
- Privacy considered
- Multi-target output
- Crash logging

**Weaknesses:**
- Histogram buckets undefined
- Memory during cloud upload not budgeted

---

### M10: Security - Grade: B+

**Strengths:**
- Comprehensive coverage
- Proper use of eFuses
- Noise protocol for API

**Weaknesses:**
- No key revocation mechanism
- Anti-rollback has 32-version limit
- Pairing token expiration too short

---

### M11: Zone Editor - Grade: C+

**Strengths:**
- Local-first design
- Streaming architecture

**Weaknesses:**
- Coordinate units don't match firmware
- Authentication/session management unclear
- Very brief for the complexity involved

---

## 6. Cross-Cutting Concerns

### 6.1 No Integration Test Scenarios

Each module defines unit tests but there's no specification for integration testing:
- Full data path from radar to HA
- OTA while presence detection active
- Zone update while tracking active
- Multiple failure scenarios

### 6.2 No Performance Profiling Plan

Modules estimate CPU/memory but there's no plan to validate these estimates or detect regressions.

### 6.3 No Graceful Degradation Model

What features work when:
- Radar disconnected?
- Wi-Fi down?
- NVS corrupted?
- OTA failed mid-update?

### 6.4 Implicit Coupling Not Documented

Example: M05 reboot timeout depends on Wi-Fi being configured. M07 OTA depends on M10 Security being initialized. These dependencies aren't captured.

---

## 7. Recommendations

### Immediate (Before Implementation)

1. **Rewrite M02 Tracking specification** - Current spec is not implementable
2. **Create unified memory budget** - Actual measurements required
3. **Fix frame rate assumptions** - 33 Hz throughout
4. **Standardize coordinate units** - mm everywhere, convert at edge
5. **Document boot sequence** - Initialization order and dependencies

### Short-term (During Implementation)

6. **Add integration test scenarios** - Full path tests
7. **Create system timing diagram** - Show all rates/throttles
8. **Fix NVS commit strategy** - Only on actual changes
9. **Define degraded mode behaviors** - What works when X fails
10. **Add coordinate system diagram** - Remove ambiguity

### Medium-term (Before Production)

11. **Memory profiling under load** - Validate budgets
12. **Flash wear testing** - Validate NVS endurance
13. **Security penetration testing** - Key management, auth
14. **Long-term stability testing** - 30+ day continuous operation

---

## 8. Open Questions

These require product/architecture decisions:

1. Should device work without any HA connection ever configured?
2. What's the maximum supported zone count? (Memory budget dependent)
3. Is z-score sensitivity model actually desired vs simple hold times?
4. Should we support ESPHome companion devices on same mDNS namespace?
5. What's the device lifetime in years? (Affects anti-rollback budget)
6. Should OTA be deferrable by user? What's the timeout?
7. Is flash encryption required for production? (Performance impact)

---

## Appendix A: Issue Summary Table

| ID | Severity | Module(s) | Summary |
|----|----------|-----------|---------|
| 2.1 | Fatal | M01, M02 | Frame rate 33 Hz vs 10 Hz |
| 2.2 | Fatal | M02 | Tracking module underspecified |
| 2.3 | Fatal | M01, M03, M11 | Coordinate units mm vs meters |
| 2.4 | Critical | M05, M07, M10 | TLS memory not budgeted |
| 2.5 | Critical | M06, M08 | NVS wear rate 1440/day |
| 2.6 | Critical | M04 | Wrong timing assumptions |
| 3.1 | High | M03, M04, M11 | Sensitivity has 3 meanings |
| 3.2 | High | M08 | Watchdog reset loop on radar fail |
| 3.3 | High | M05 | Reboot timeout without HA |
| 3.4 | High | M10, M11 | Pairing expires during use |
| 3.5 | High | M03 | Point-on-edge undefined |
| 3.6 | High | All | Boot sequence undefined |
| 3.7 | High | M01 | Coordinate system ambiguous |
| 3.8 | Medium | M05 | mDNS collision with ESPHome |
| 3.9 | Medium | All | Wi-Fi disconnect behavior undefined |
| 3.10 | Medium | M10 | Anti-rollback 32 version limit |

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-09 | Systems Review | Initial review |
