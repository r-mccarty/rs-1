# RS-1 Product Requirements Document

**Version:** 0.2
**Date:** 2026-01-09
**Owner:** OpticWorks Product
**Status:** Draft

> This document defines the product vision, target users, and success criteria. For detailed requirements, see [REQUIREMENTS_RS1.md](REQUIREMENTS_RS1.md).

---

## 1. Problem Statement

Current mmWave presence sensors force users to choose between reliability and usability:

| Product Type | Reliability | Usability | Limitations |
|--------------|-------------|-----------|-------------|
| **Raw ESPHome** | High (if tuned) | Low | Requires YAML expertise, manual zone math, no OTA |
| **Aqara FP2** | Medium | High | 30-zone limit, cloud-dependent features, closed ecosystem |
| **Everything Presence** | Medium | Medium | Complex multi-sensor setup, DIY assembly |

**The gap:** No product delivers reliable zone-based presence with consumer-grade setup AND full local control.

RS-1 fills this gap: professional reliability, consumer UX, prosumer control.

---

## 2. Vision

**One-liner:** The presence sensor that doesn't require a PhD to set up.

**Product thesis:** Users want presence detection to "just work" like a light switch. They don't want to understand radar physics, tune sensitivity thresholds, or debug false triggers. RS-1 abstracts the complexity into a single confidence-weighted presence answer per zone, configured through an intuitive mobile app.

**Differentiation:**
- Unlimited software zones (vs. Aqara's 30-zone hardware limit)
- ESPHome-native (vs. proprietary protocols)
- Cloud-optional OTA (vs. manual firmware updates)
- Mobile-first setup (vs. YAML configuration)

---

## 3. Target Users

### 3.1 Primary: Home Assistant Prosumers

**Profile:**
- Runs Home Assistant (likely on dedicated hardware)
- Has 5-20 smart devices already integrated
- Comfortable with technology but doesn't want to spend weekends debugging
- Values local control and privacy

**Pain points:**
- Tried ESPHome presence sensors, spent hours tuning
- Zone configuration requires coordinate math
- Firmware updates are manual hassle
- Raw sensor data creates entity sprawl in HA

**What they want:**
- "It just works" reliability
- Per-room/zone occupancy for automations
- Native HA integration (no bridges)
- Occasional updates that don't require attention

### 3.2 Secondary: Technical Users with Competitor Frustrations

**Profile:**
- May have Aqara FP2 or similar
- Hit limitations (zone count, cloud dependency, ecosystem lock-in)
- Willing to pay more for better solution

**Pain points:**
- 30-zone limit forces compromises
- Cloud outage breaks presence detection
- Can't customize or extend functionality

### 3.3 Future: Mainstream Smart Home Users

**Profile:**
- Has Alexa/Google Home ecosystem
- Wants "smart home magic" without technical knowledge
- Dependent on mobile app maturity and retail availability

**Not MVP target** - requires significant app polish and support infrastructure.

---

## 4. User Journeys

### 4.1 New Device Setup

```
┌─────────────────────────────────────────────────────────────────┐
│  SETUP JOURNEY (Target: 60 seconds)                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. UNBOX          2. SCAN QR         3. ENTER WI-FI            │
│  ┌─────────┐       ┌─────────┐        ┌─────────┐               │
│  │   📦    │  ──▶  │  📱 📷  │  ──▶   │ SSID:   │               │
│  │  RS-1   │       │  [QR]   │        │ Pass:   │               │
│  └─────────┘       └─────────┘        └─────────┘               │
│     5 sec            10 sec             15 sec                  │
│                                                                 │
│  4. DEVICE CONNECTS    5. CREATE ZONES    6. DONE               │
│  ┌─────────┐           ┌─────────┐        ┌─────────┐           │
│  │ ····    │   ──▶     │ [Zone]  │  ──▶   │   ✓     │           │
│  │ Connected│          │ Kitchen │        │ Ready!  │           │
│  └─────────┘           └─────────┘        └─────────┘           │
│     10 sec               20 sec             -                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Success criteria:** 80% of users complete steps 1-6 in ≤60 seconds.

**Failure modes to handle:**
- Wi-Fi password wrong → clear error, retry
- Device not found → troubleshooting tips
- HA not discovered → manual IP entry option

### 4.2 Daily Operation

User should NOT interact with RS-1 daily. Success = invisibility.

- Automations trigger based on zone occupancy
- No false triggers requiring manual override
- No "presence sensor offline" notifications

**Success criteria:** <5% false occupancy rate, <1% false vacancy rate during normal activity.

### 4.3 Zone Tuning

When user DOES need to adjust:

1. Open app → device → zone editor
2. See live target positions overlaid on zones
3. Drag zone boundary or adjust sensitivity slider
4. Save → immediate effect

**Success criteria:** Zone adjustment takes <30 seconds.

### 4.4 Firmware Update

1. App shows "Update available" badge
2. User taps update → confirms
3. Progress bar → complete
4. Device back online automatically

**Success criteria:** >99% OTA success rate, <2 minute total time, no user SSH/serial required.

---

## 5. Success Metrics

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Setup completion time | ≤60 sec for 80% of users | App instrumentation (time from QR scan to "Done") |
| Setup success rate | >95% complete without support | App instrumentation (funnel completion) |
| False occupancy rate | <5% | Lab testing with defined scenarios |
| False vacancy rate | <1% | Lab testing with defined scenarios |
| OTA success rate | >99% | Cloud telemetry |
| Manual update rate | <1% of users | Support ticket tracking |
| NPS | >50 | Post-setup survey |
| Return rate | <3% | Sales/returns tracking |

---

## 6. Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Single radar misses stationary occupants | High | Medium | Clear positioning guidance; RS-1 Pro upgrade path (dual radar fusion) |
| ESPHome protocol changes break compatibility | Medium | High | Protocol version negotiation; compatibility test suite |
| Competitors copy UX approach | Medium | Medium | First-mover advantage; continuous improvement |
| Cloud outage during setup | Low | High | Local-only setup mode; cloud features additive |
| OTA bricks devices | Low | Critical | Staged rollout; auto-rollback; serial recovery |
| App store rejection | Low | High | Follow platform guidelines; no private APIs |

---

## 7. Open Questions and Unknowns

> **This section identifies gaps that must be resolved to strengthen the PRD.**

### 7.1 User Research Gaps

| Question | Impact | How to Resolve |
|----------|--------|----------------|
| What is actual setup time for real users? | Success metric validity | Beta user instrumentation |
| How do users mentally model "zones"? | Zone editor UX | User interviews, prototype testing |
| What sensitivity language resonates? | UI copy | A/B testing ("Responsive" vs "Stable" vs slider) |
| Do users understand exclude zones? | Feature complexity | User testing |
| What triggers "I need to adjust zones"? | Long-term UX | Post-deployment interviews |

### 7.2 Technical Unknowns

| Question | Impact | How to Resolve |
|----------|--------|----------------|
| Actual LD2450 frame rate and jitter | Tracking accuracy | Hardware testing |
| ESP32-C3 memory under real load | Stability | Profiling during integration |
| TLS memory during OTA | OTA reliability | Load testing |
| NVS wear rate in practice | Device lifetime | Long-term testing |
| Real-world occlusion patterns | Hold time defaults | Field data collection |

### 7.3 Product Decisions Needed

| Decision | Options | Deadline |
|----------|---------|----------|
| Mobile app technology | Native (Swift/Kotlin) vs Cross-platform (Flutter/RN) | Before M1 |
| Sensitivity model | Z-score vs Simple hold time vs Hybrid | Before M1 |
| LED behavior | None / Status only / Activity indicator | Before hardware finalization |
| Form factor | Wall-mount / Ceiling-mount / Universal | Before hardware finalization |
| Subscription pricing | $3/mo vs $5/mo vs Free tier + premium | Before beta |
| AR scanning scope | iOS only vs iOS+Android vs Post-launch | Before M1 |

### 7.4 Instrumentation Plan Needed

To measure success metrics, the app must track:

| Event | Data | Purpose |
|-------|------|---------|
| `setup_started` | timestamp, app_version | Funnel entry |
| `qr_scanned` | timestamp, device_id | Step timing |
| `wifi_entered` | timestamp, success/failure | Drop-off point |
| `device_connected` | timestamp, connection_time_ms | Performance |
| `zone_created` | timestamp, zone_count | Feature usage |
| `setup_completed` | timestamp, total_duration_sec | Success metric |
| `setup_abandoned` | timestamp, last_step, error_code | Failure analysis |
| `zone_edited` | timestamp, edit_type | Long-term engagement |
| `ota_started` | timestamp, from_version, to_version | OTA tracking |
| `ota_completed` | timestamp, duration_sec, success | OTA success rate |

**Open question:** What's the consent model for instrumentation? Opt-in only? Anonymous by default?

### 7.5 Competitive Response Scenarios

| If Competitor Does... | Our Response |
|-----------------------|--------------|
| Aqara removes 30-zone limit | Emphasize local-first, open ecosystem |
| ESPHome adds mobile setup | Emphasize reliability, single-vendor support |
| New entrant with better UX | Accelerate feature development, price competition |
| Apple/Google enters market | Focus on HA ecosystem, prosumer positioning |

### 7.6 Support and Documentation Gaps

| Gap | Impact | Resolution |
|-----|--------|------------|
| No defined support channels | User frustration | Define before beta |
| No troubleshooting guide | Support load | Create during M1 |
| No positioning guidance | Suboptimal detection | Include in app onboarding |
| No multi-device setup flow | Scaling users | Design before v1.1 |

---

## 8. What This PRD Does NOT Cover

Explicitly out of scope for this document (covered elsewhere):

| Topic | Document |
|-------|----------|
| Detailed functional requirements | [REQUIREMENTS_RS1.md](REQUIREMENTS_RS1.md) |
| Firmware architecture | [docs/firmware/README.md](firmware/README.md) |
| Test plan | [testing/VALIDATION_PLAN_RS1.md](testing/VALIDATION_PLAN_RS1.md) |
| Competitive analysis | [opticworks_rs1_competitive_analysis_v2.docx.md](opticworks_rs1_competitive_analysis_v2.docx.md) |

---

## 9. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-XX | OpticWorks | Initial draft |
| 0.2 | 2026-01-09 | OpticWorks | Refocused on vision/users/metrics; added open questions section; moved requirements to REQUIREMENTS_RS1.md |
