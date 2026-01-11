# RFD-003: Senior Engineer Requirements Critique

**Status:** Draft
**Author:** Grumpy Senior Dev Who Has Seen Too Many Projects Fail
**Date:** 2026-01-11
**TL;DR:** This spec is going to ship bugs. Here's why.

---

## Executive Summary

I've reviewed every document in this repo. The structure is pretty, the diagrams are nice, and the ASCII art is adorable. But this thing has **40+ issues** that will bite you in production. Some are "bricked devices in the field" bad. Some are "customer calls at 3am because their lights won't turn off" bad.

**My recommendation:** Do NOT write a single line of firmware until Sections 2-4 are resolved.

---

## 1. Critical Issues (Stop Everything)

### 1.1 You Will Brick Devices During OTA

**Files:** `MEMORY_BUDGET.md:86-103`, `HARDWAREOS_MODULE_OTA.md`

Your memory math doesn't add up:

```
OTA needs:     MQTT TLS (33KB) + HTTPS TLS (33KB) = 66KB
Available:     30KB free heap during OTA
Safety margin: 20KB
Actual buffer: 10KB
```

10KB buffer. That's it. One malloc during OTA and you're dead. One MQTT keepalive with a large payload. One logging call that allocates. Crash. Corrupted firmware. Bricked device.

**What you're missing:**
- Pre-OTA memory audit (force GC, verify heap)
- MQTT disconnect before OTA (free that 33KB)
- Application memory freeze during download
- Rollback validation before rebooting

**Real-world scenario:** User has 14 zones configured (more NVS memory used), connects to HA (Native API buffer allocated), cloud pushes OTA. Boom. Dead device. Support ticket.

---

### 1.2 Coordinate System Will Corrupt Every Zone

**Files:** `COORDINATE_SYSTEM.md:163`, `PROTOCOL_MQTT.md:195`, `HARDWAREOS_MODULE_ZONE_EDITOR.md`

You have TWO places claiming to do unit conversion:

> "M11 is the ONLY module that handles unit conversion" - COORDINATE_SYSTEM.md

> "Cloud services convert from meters before publishing" - PROTOCOL_MQTT.md

So what happens?

1. User creates 2m x 2m zone in app (meters)
2. Cloud converts: 2m → 2000mm, publishes to device
3. M11 receives 2000, thinks "that's meters!", converts: 2000m → 2000000mm
4. Zone is now 2 kilometers wide
5. Everything is always occupied
6. User thinks product is broken

**The fix is trivial:** Pick ONE conversion point. Document it. Test it. But you haven't.

---

### 1.3 Anti-Rollback Will Orphan Devices

**File:** `REQUIREMENTS_RS1.md:269`

> "eFuse anti-rollback: Maximum 32 security-critical updates over device lifetime"

32 updates. That's it. **Forever.** eFuses don't reset.

Let's do math:
- 5-year product lifetime
- Monthly security patches = 60 updates
- You run out of eFuses in **2.6 years**

After that? Device cannot receive security patches. You have a fleet of permanently vulnerable devices in the field. Some in hospitals. Some in corporate offices.

**What you need:**
- Policy: When do you burn an eFuse? (Only for critical CVEs?)
- Telemetry: Track eFuse count per device
- Migration: What do you tell customers at eFuse 30?
- Legal: Does your warranty cover this?

---

### 1.4 TLS Memory for Native API is Fantasy

**File:** `MEMORY_BUDGET.md:92-94`

You allocated 8KB for Noise protocol encryption. Let's see what you actually need:

```
Noise handshake state:  ~2KB
Protobuf encode buffer: ~4KB
Protobuf decode buffer: ~4KB
mDNS responder:         ~2KB
TCP socket buffers:     ~4KB
----------------------------
Actual minimum:         ~16KB
Your budget:            8KB
Deficit:                8KB
```

When Home Assistant connects, you'll OOM. The device will reset. HA will retry. Device resets again. Infinite loop.

**Fix:** Actually measure M05 heap usage during HA connection. I bet you haven't.

---

### 1.5 NVS Will Die and You Won't Know Why

**File:** `MEMORY_BUDGET.md:209-221`

> "Commit to NVS only on actual config changes, never on a timer"
> "< 10/day operations"

Prove it. Where's the code review checklist? Where's the telemetry metric? Where's the static analysis rule?

One developer writes `nvs_commit()` in a loop during debugging. Forgets to remove it. Passes code review because reviewer is tired. Ships. NVS wears out in 3 months.

Customer reports: "Device randomly forgets settings." You have no idea why because you're not tracking NVS write counts.

**Required:**
- `ESP_LOGI("nvs", "commit count: %d", count)` on every boot
- Telemetry: `nvs_commits_lifetime` metric
- Alert: > 100 commits/day = bug

---

## 2. Major Issues (Will Cause Support Tickets)

### 2.1 Kalman Filter Will Diverge and You Won't Recover

**File:** `HARDWAREOS_MODULE_TRACKING.md`

Your tracking module has zero error handling for:

- Singular covariance matrix (P becomes non-invertible)
- NaN/Inf propagation (one bad radar reading → filter corrupted forever)
- Numerical drift after 30 days uptime

When the Kalman filter diverges, tracks freeze. Zones report "vacant" permanently. User's lights never turn on. They return the product.

**What every Kalman implementation needs:**
```c
if (isnan(state.x) || isinf(state.x)) {
    ESP_LOGW("track", "Filter diverged, resetting track %d", id);
    reset_track(id);
}
if (covariance_determinant(P) < EPSILON) {
    ESP_LOGW("track", "Covariance singular, reinitializing");
    reinit_covariance(P);
}
```

You have none of this.

---

### 2.2 Point-in-Polygon Has Edge Case Bugs

**File:** `HARDWAREOS_MODULE_ZONE_ENGINE.md:126-150`

Your ray-casting algorithm:

```c
if ((yi > y) != (yj > y)) {
    // point may be inside
}
```

This is the standard algorithm. It has a known bug: **points exactly on edges give undefined results.**

User creates two adjacent zones: Kitchen and Dining. They share an edge. Target walks along the edge. Your algorithm:

- Kitchen: "Target inside!" (ray crosses odd edges)
- Dining: "Target outside!" (ray crosses even edges)

Zone occupancy flickers. Automations fire randomly. User posts 1-star review.

**The fix:** You mention "shrink zone by 1mm for evaluation" but WHERE? I don't see it implemented. It's a comment, not code.

---

### 2.3 Sensitivity Formula is Wrong

**Files:** `GLOSSARY.md:113-125`, `HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md:130-142`

Your docs say:

| Sensitivity | Hold Time |
|-------------|-----------|
| 50 (default)| 1500ms    |
| 75          | 500ms     |

Your formula says:
```
hold_time_ms = (100 - sensitivity) * 50
```

Let's check:
- sensitivity=50: (100-50)*50 = **2500ms** (not 1500ms)
- sensitivity=75: (100-75)*50 = **1250ms** (not 500ms)

Your table is wrong. Or your formula is wrong. Either way, customer sets sensitivity to 50, expects 1.5s hold time, gets 2.5s. Lights stay on too long. They complain.

---

### 2.4 MQTT Rate Limits Are Unenforceable

**File:** `PROTOCOL_MQTT.md:361-366`

> "Target stream: 15 msg/sec, drop oldest"

Questions:
- WHO enforces this? Device? Broker? Cloud worker?
- WHAT does "drop oldest" mean? Oldest in a queue? Which queue?
- WHERE is the queue? Device memory? EMQX buffer?

If nobody enforces it, device publishes 33 msg/sec (one per frame). Cloud can't keep up. EMQX buffers grow. Eventually OOM or connection timeout. Telemetry lost.

---

### 2.5 Wi-Fi Reconnection Will Frustrate Users

**File:** `DEGRADED_MODES.md:70-85`

> `delay_ms = min(1000 * (1 << attempt), 60000)`

What happens when Wi-Fi is flaky?

1. Connect (attempt=0)
2. Connected for 2 seconds
3. Disconnect (attempt=1, wait 2s)
4. Connect
5. Connected for 2 seconds
6. Disconnect (attempt=2, wait 4s)
...
10. Disconnect (attempt=10, wait 60s)
11. User unplugs router to reboot
12. Router back in 30 seconds
13. Device waits 60 seconds anyway
14. User thinks device is broken

**What you need:** Reset attempt counter after stable connection (e.g., connected > 5 minutes).

---

### 2.6 Mobile App vs Web App: Pick One

**Files:** `REQUIREMENTS_RS1.md:129-156`, `firmware/README.md:51-55`

Requirements say:
> "iOS/Android app onboarding"

Firmware spec says:
> "Full mobile app is a non-goal for MVP (zone editor is web-based)"

So which is it? If product is designing a mobile app and firmware team is building web-only, you'll discover this mismatch when it's too late.

---

## 3. Significant Issues (Technical Debt)

### 3.1 No Factory Reset Procedure

Where in your docs does it say how a user factory resets the device? Button hold? Serial command? App action?

When NVS corrupts (and it will), your only documented recovery is "serial OTA option" with no protocol specified. User has bricked device, no instructions, calls support.

---

### 3.2 Track Retirement is Undefined

**File:** `HARDWAREOS_MODULE_TRACKING.md`

How many consecutive misses before a track is retired? 5? 10? 100?

If too few: tracks disappear when person briefly occluded
If too many: zombie tracks consume memory, reduce available slots

I don't see a number anywhere.

---

### 3.3 mDNS Will Collide

**File:** `HARDWAREOS_MODULE_NATIVE_API.md`

Two RS-1 devices on same network. Both advertise as `RS-1._esphomelib._tcp.local`. Home Assistant sees two devices with same name. Can't distinguish.

Should be: `rs1-AABBCCDD._esphomelib._tcp.local` (MAC suffix)

---

### 3.4 Config Downgrades Will Break

**File:** `HARDWAREOS_MODULE_CONFIG_STORE.md:43-49`

Firmware v1.1 adds new zone property. User upgrades. Zone config migrates to v1.1 format.

OTA fails. Device rolls back to v1.0. v1.0 can't parse v1.1 config format.

Device boots with no zones. All automations broken.

---

### 3.5 Zone Editor Has No Auth

**File:** `HARDWAREOS_MODULE_ZONE_EDITOR.md`

> "Accept zone updates via local REST API or cloud MQTT"

What authentication? Can anyone on local network POST to the device and reconfigure zones? Can anyone with the MQTT topic publish zone configs?

---

### 3.6 PSK Provisioning is Magic

**File:** `HARDWAREOS_MODULE_SECURITY.md`

You use "Noise Protocol PSK" for API encryption. How does the PSK get:
- Into the device? (Manufacturing? First boot?)
- Shared with Home Assistant? (QR code? Manual entry?)
- Rotated if compromised?

If PSK is hardcoded, your "encryption" is theater.

---

## 4. Missing Specifications

| What's Missing | Why It Matters |
|----------------|----------------|
| Device naming collision resolution | Multiple devices = broken HA |
| Zone migration across firmware versions | Upgrades lose config |
| Factory reset UX | Users can't recover |
| Per-zone sensitivity persistence | NVS corrupt = tuning lost |
| Radar frame jitter tolerance | Kalman timing assumptions break |
| Ground truth sensor model | Can't validate accuracy |
| Test coverage targets | Don't know when testing is "done" |
| Safe mode recovery steps | Support nightmare |

---

## 5. The Contradictions

| Document A | Document B | The Lie |
|------------|------------|---------|
| REQUIREMENTS: "unlimited zones" | SCHEMA: maxItems=16 | It's 16. Say 16. |
| REQUIREMENTS: <50ms detection latency | FIRMWARE: 100ms HA throttle | Can't be both |
| COORDINATE_SYSTEM: M11 converts | PROTOCOL_MQTT: Cloud converts | Who actually converts? |
| GLOSSARY: sensitivity 50 → 2500ms | SMOOTHING: sensitivity 50 → 1500ms | Do the math |
| REQUIREMENTS: No target positions in telemetry | PROTOCOL_MQTT: target stream topic | Privacy violation |

---

## 6. My Verdict

This is a **spec-only repo** and it already has 40+ issues. That's actually good news - you found them before writing code.

But if you start implementing now, you will:
1. Brick devices during OTA (Critical #1.1)
2. Corrupt every zone config (Critical #1.2)
3. Ship devices that become unpatchable (Critical #1.3)
4. Crash on HA connection (Critical #1.4)
5. Kill NVS silently (Critical #1.5)

**Minimum viable fixes before implementation:**
1. Resolve coordinate conversion ownership (1 hour meeting, 1 line change)
2. Add OTA memory pre-check (1 day implementation)
3. Document eFuse policy (1 hour discussion)
4. Measure actual M05 memory usage (2 hours profiling)
5. Add NVS commit telemetry (30 min implementation)
6. Add Kalman divergence recovery (2 hours implementation)
7. Fix sensitivity formula OR table (5 minutes)

Total: ~2 days of work to avoid months of production issues.

---

## 7. Action Items

### Must Fix (Blocks Implementation)
- [ ] **OTA Memory:** Add pre-flight heap check, document MQTT disconnect strategy
- [ ] **Coordinates:** Single source of truth for unit conversion (M11 or Cloud, not both)
- [ ] **eFuse Policy:** Document when to burn, add telemetry, define end-of-life
- [ ] **M05 Memory:** Profile actual usage, update budget
- [ ] **Sensitivity:** Fix formula/table mismatch

### Should Fix (Before Beta)
- [ ] **Kalman Recovery:** Add divergence detection and reset
- [ ] **Edge Handling:** Implement 1mm zone shrink, not just document it
- [ ] **Track States:** Document which states M03 receives from M02
- [ ] **MQTT Rate Limits:** Define enforcement point
- [ ] **Wi-Fi Backoff:** Add stable connection reset

### Nice to Have (Before GA)
- [ ] **Factory Reset:** Document user procedure
- [ ] **mDNS Naming:** Add MAC suffix
- [ ] **Config Downgrade:** Add compatibility matrix
- [ ] **Zone Auth:** Define authentication for local API

---

## 8. Appendix: All 40 Issues

<details>
<summary>Click to expand full issue list</summary>

### Critical (5)
1. OTA memory crisis - MEMORY_BUDGET.md:86-103
2. Coordinate conversion ambiguity - COORDINATE_SYSTEM.md:163 vs PROTOCOL_MQTT.md:195
3. TLS memory not budgeted for M05 - MEMORY_BUDGET.md:92-94
4. Anti-rollback eFuse limit (32) - REQUIREMENTS_RS1.md:269
5. NVS wear not enforced - MEMORY_BUDGET.md:209-221

### Major (10)
6. Kalman filter no error recovery - HARDWAREOS_MODULE_TRACKING.md
7. Point-in-polygon edge cases - HARDWAREOS_MODULE_ZONE_ENGINE.md:126-150
8. Confirmed track visibility undefined - HARDWAREOS_MODULE_ZONE_ENGINE.md:23,39
9. Sensitivity formula mismatch - GLOSSARY.md:121-125 vs PRESENCE_SMOOTHING.md
10. MQTT rate limits unenforceable - PROTOCOL_MQTT.md:361-366
11. Wi-Fi backoff unbounded - DEGRADED_MODES.md:70-85
12. Multi-connection API undefined - HARDWAREOS_MODULE_NATIVE_API.md:24-26
13. Safe mode recovery undocumented - BOOT_SEQUENCE.md:280-287
14. NVS corruption loses Wi-Fi - DEGRADED_MODES.md:293-305
15. OTA cooldown storage undefined - HARDWAREOS_MODULE_OTA.md:98

### Significant (10)
16. Telemetry opt-in mechanism missing - REQUIREMENTS_RS1.md:192-199
17. LD2450 frame sync recovery missing - HARDWAREOS_MODULE_RADAR_INGEST.md:113-121
18. Track retirement criteria missing - HARDWAREOS_MODULE_TRACKING.md
19. Zone evaluation load assumed - REQUIREMENTS_RS1.md:74
20. mDNS discovery collision - HARDWAREOS_MODULE_NATIVE_API.md
21. Config downgrade not handled - HARDWAREOS_MODULE_CONFIG_STORE.md:43-49
22. Noise PSK management undefined - HARDWAREOS_MODULE_SECURITY.md
23. Zone editor no auth - HARDWAREOS_MODULE_ZONE_EDITOR.md
24. Kalman tuning parameters undefined - HARDWAREOS_MODULE_TRACKING.md
25. Signature block verification order - HARDWAREOS_MODULE_SECURITY.md:98-109

### Documentation (6)
26. Assumptions staleness not enforced - All module specs
27. RFD-001 not linked - Throughout docs
28. No API stability guarantee - HARDWAREOS_MODULE_NATIVE_API.md:18-27
29. Test plan lacks coverage metrics - VALIDATION_PLAN_RS1.md
30. Integration test framework unspecified - INTEGRATION_TESTS.md
31. Ground truth sensor not named - VALIDATION_PLAN_RS1.md

### Contradictions (5)
32. Unlimited zones vs 16 cap - REQUIREMENTS_RS1.md:75 vs SCHEMA_ZONE_CONFIG.json:19-23
33. <50ms vs 100ms latency - REQUIREMENTS_RS1.md:218 vs firmware/README.md:64
34. Mobile app vs web MVP - REQUIREMENTS_RS1.md:129-156 vs firmware/README.md:51-55
35. Telemetry privacy vs target stream - REQUIREMENTS_RS1.md:192-199 vs PROTOCOL_MQTT.md:85-89
36. Sensitivity table vs formula - Multiple files

### Missing (4)
37. Device naming collision resolution
38. Zone migration strategy
39. Factory reset procedure
40. OTA RSSI re-check during download

</details>

---

*"The best time to find bugs is before you write the code that creates them."*

-- A Senior Dev Who Has Debugged Too Many Production Incidents
