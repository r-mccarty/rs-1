# RFD-002: Hardware Specification Review (HW-RS1-001)

**Status:** Resolved
**Author:** Hardware Engineering Review
**Date:** 2026-01-14
**Updated:** 2026-01-15
**Reviewers:** OpticWorks Hardware, Firmware, Product
**Document Under Review:** `docs/hardware/HARDWARE_SPEC.md` (HW-RS1-001 v1.0)

---

## Resolution Status (2026-01-15)

**All critical issues have been addressed:**
- MCU changed from ESP32-S3 to **ESP32-WROOM-32E + CH340N**
- Power regulator changed from LDO to **SY8089AAAC buck converter**
- Power mux added (**dual SS34 Schottky diodes**)
- USB ESD protection added (**USBLC6-2SC6**)
- Ethernet architecture resolved: **RMII PHY (SR8201F)** enabled by ESP32-WROOM-32E's native EMAC

See `RFD_002_RESPONSE.md` for detailed solutions to each issue.

---

## Summary (Original Review)

This RFD documents a critical hardware engineering review of the RS-1 Hardware Specification (HW-RS1-001). The review identifies **2 showstopper issues** that will cause hardware failures in production, plus **4 major risks** requiring design changes before PCB layout.

**Original Recommendation:** Do not proceed with PCB layout until Sections 2.1 and 2.2 are resolved.

**Status:** Issues resolved. PCB layout may proceed.

---

## Table of Contents

1. [Review Scope](#1-review-scope)
2. [Critical Failures (Showstoppers)](#2-critical-failures-showstoppers)
3. [Major Risks](#3-major-risks)
4. [Minor Issues](#4-minor-issues)
5. [Open Questions Requiring Decisions](#5-open-questions-requiring-decisions)
6. [Recommended Design Changes](#6-recommended-design-changes)
7. [Additional Engineering Insights](#7-additional-engineering-insights)
8. [Action Items](#8-action-items)

---

## 1. Review Scope

**Reviewed Documents:**
- `docs/hardware/HARDWARE_SPEC.md` (HW-RS1-001 v1.0)
- `docs/hardware/RS-1_Unified_BOM.md`
- `docs/hardware/hardware-concept-evolution.md`

**Review Criteria:**
- Power budget and thermal analysis
- RF coexistence and EMC
- Component selection adequacy
- Regulatory compliance feasibility
- Supply chain risk
- Manufacturing readiness

---

## 2. Critical Failures (Showstoppers)

### 2.1 Power Regulator Cannot Supply Load (Thermal Impossibility)

**Severity:** Fatal
**Component:** U2 - ME6211C33M5G-N (LDO Regulator)

#### The Problem

The specified LDO is rated for **500mA maximum**, but the Fusion variant peak load exceeds this:

| Component | Current Draw |
|-----------|--------------|
| ESP32-S3 (WiFi TX @ 20dBm) | 340-380 mA |
| LD2450 (Active) | 150 mA |
| LD2410 (Active) | 100 mA |
| Peripherals (LED, sensors) | 20 mA |
| **Total Peak** | **~650 mA** |

#### The Failure Mode

1. WiFi transmits while radars are active
2. Current demand exceeds LDO capacity
3. Output voltage sags below ESP32-S3 brownout threshold (3.0V)
4. ESP32 resets
5. Repeat (continuous reset loop)

#### Thermal Analysis

Even if a higher-rated LDO were specified, the thermal dissipation is impossible:

```
Power Dissipation = (Vin - Vout) × Iload
                  = (5.0V - 3.3V) × 0.65A
                  = 1.7V × 0.65A
                  = 1.105 Watts
```

A SOT-23-5 package can dissipate approximately **0.3-0.4W** maximum before thermal shutdown. This design will overheat within seconds of sustained WiFi activity.

#### Required Fix

Replace LDO with a **synchronous buck DC-DC converter**:

| Parameter | LDO (Current) | Buck (Required) |
|-----------|---------------|-----------------|
| Topology | Linear | Switching |
| Efficiency @ 650mA | ~66% | ~90%+ |
| Heat Dissipation | ~1.1W | ~0.25W |
| Package | SOT-23-5 | QFN or similar |

**Recommended Parts:**

| Part Number | Manufacturer | Output | Notes |
|-------------|--------------|--------|-------|
| TPS62840 | Texas Instruments | 750mA | Ultra-low quiescent |
| AP62300 | Diodes Inc | 3A | Cheap, easy layout |
| MP2359 | Monolithic Power | 1.2A | Common on ESP32 dev boards |
| SY8088 | Silergy | 1A | LCSC available, very cheap |

**Additional Components Required:**
- Inductor (2.2µH - 4.7µH, depending on part)
- Input/output capacitors (likely 10µF ceramic each)
- Feedback resistors (if adjustable output)

**BOM Impact:** +$0.30-0.50 for buck converter, +$0.10 for inductor, +$0.05 for additional passives.

---

### 2.2 Power Muxing Absent (Magic Smoke Scenario)

**Severity:** Fatal
**Affected:** USB-C input + PoE input coexistence

#### The Problem

The specification claims:
> "The Si3404 provides isolated flyback topology. This protects against simultaneous USB and PoE connection scenarios."

**This is incorrect.** The Si3404 isolation protects the Ethernet infrastructure from ground loops and provides galvanic isolation per IEEE 802.3af requirements. It does **not** prevent two 5V power sources from fighting each other.

#### The Failure Scenario

1. Device operating on PoE (generating internal 5V rail, perhaps 5.1V)
2. User connects USB cable to laptop for debugging
3. Laptop USB provides 5V (perhaps 4.9V)
4. Current flows from device's PoE-derived 5V **into** the laptop's USB port
5. Potential damage to laptop USB controller or device regulator
6. At minimum: undefined behavior, unexpected resets

```
     PoE Input                      USB Input
         │                              │
    ┌────┴────┐                    ┌────┴────┐
    │ Si3404  │                    │  USB-C  │
    │ Flyback │                    │  VBUS   │
    └────┬────┘                    └────┬────┘
         │                              │
         │ 5.1V                    4.9V │
         │                              │
         └──────────┬───────────────────┘
                    │ ← Current flows backwards!
                    │
              ┌─────┴─────┐
              │   WRONG   │
              │  No Mux!  │
              └───────────┘
```

#### Required Fix

Implement power path management. Options:

**Option A: Schottky Diode OR-ing (Simple, Lossy)**

```
     PoE 5V ──►|──┬──► System 5V (~4.7V)
                  │
     USB 5V ──►|──┘
```

- **Pros:** Simple, cheap ($0.05)
- **Cons:** ~0.3V forward drop reduces LDO/Buck headroom
- **Parts:** SS34 or similar Schottky diodes

**Option B: Ideal Diode Controller (Better)**

```
     PoE 5V ──┬──[Q1]──┬──► System 5V (~4.95V)
              │        │
     USB 5V ──┴──[Q2]──┘
              │
         ┌────┴────┐
         │ LTC4412 │ (or similar)
         └─────────┘
```

- **Pros:** Minimal voltage drop (~20mV), automatic switchover
- **Cons:** Higher cost ($0.50-1.00), more components
- **Parts:** LTC4412, TPS2113A, or discrete MOSFET solution

**Option C: USB Priority with PoE Disconnect (Safest for Debug)**

When USB VBUS is detected, disable PoE output via GPIO or load switch. This ensures deterministic behavior during development.

**Recommended:** Option B (Ideal Diode) for production quality, or Option C during prototyping.

---

## 3. Major Risks

### 3.1 RF Coexistence: Dual 24GHz Radar Interference

**Severity:** High
**Affected:** RS-1 Pro variant (LD2410 + LD2450)

#### The Problem

Both radar modules operate in the **24.00 - 24.25 GHz ISM band** using FMCW (Frequency Modulated Continuous Wave). Neither module has synchronization capability.

| Module | Chirp Rate | Issue |
|--------|------------|-------|
| LD2410 | Unsynchronized | Chirp interference |
| LD2450 | Unsynchronized | Ghost targets |

When both radars transmit simultaneously:
- Chirp from Module A appears as interference to Module B
- Module B may report ghost targets
- Detection reliability degrades unpredictably

#### Analysis

"Place at opposite ends of PCB" provides spatial separation of perhaps 60-80mm. At 24GHz (λ ≈ 12.5mm), this is only **5-6 wavelengths**. Direct coupling and near-field effects will still cause interference.

#### Potential Mitigations

1. **Time-Division Multiplexing:** Alternate power to each radar
   - Reduces effective frame rate by 50%
   - May violate "Response Time < 1s" spec
   - Requires firmware coordination

2. **Shielding:** RF absorber or metallic partition between radars
   - Adds mechanical complexity and cost
   - May not be sufficient at close range

3. **Frequency Offset:** Some radar modules allow minor frequency adjustment
   - LD2410/LD2450 do not support this (fixed frequency)

4. **Accept Degradation:** Document that Fusion mode has reduced accuracy
   - Honest but undesirable for "premium" SKU

#### Required Action

**Before finalizing Fusion variant:**
1. Build prototype with both radars active
2. Characterize ghost target rate and detection accuracy
3. Test time-multiplexing feasibility
4. Make go/no-go decision on Fusion SKU

**Alternative Strategy:** Position Fusion as "advanced" with explicit tradeoff documentation, or pivot to sequential activation (one radar at a time).

---

### 3.2 Memory Constraints: No PSRAM on N4 Variant

**Severity:** High
**Component:** ESP32-S3-WROOM-1-N4 (4MB Flash, 0 PSRAM)

#### The Problem

The base MCU selection has **no PSRAM**. The ESP32-S3 internal SRAM is 512KB, but only ~320KB is available for application heap after RTOS, WiFi stack, and TLS buffers.

**Memory Consumers in RS-1:**

| Component | Estimated Usage |
|-----------|-----------------|
| WiFi stack + buffers | ~60KB |
| TLS (mbedTLS) | ~33KB per connection |
| ESPHome Native API | ~20KB |
| Dual UART buffers (256kbaud) | ~8KB |
| Zone configuration | ~2KB |
| Tracking state | ~4KB |
| HTTP server (OTA) | ~16KB |
| **Minimum Required** | **~143KB** |

This leaves ~177KB for heap allocations, string operations, JSON parsing, and fragmentation overhead.

#### The Risk

- **Heap fragmentation** over time causes allocation failures
- **OTA updates** require additional temporary buffers
- **Zone editor** streaming requires JSON parsing buffers
- Long-term stability is questionable

#### Recommendation

**Use ESP32-S3-WROOM-1-N8R2** (8MB Flash, 2MB PSRAM):
- Cost difference: ~$0.50-0.70
- PSRAM provides ~2MB additional heap
- Flash headroom for future features
- Eliminates memory-related field failures

For a $69-99 retail product, the $0.50 BOM increase is negligible compared to the support cost of memory-related bugs.

---

### 3.3 GPIO/RMII Pin Mapping Conflicts

**Severity:** High
**Affected:** PoE variant with Ethernet

#### The Problem

Section 7.1 states:
> "GPIO 1-15: RMII Ethernet"

This is insufficiently specific and contains a critical conflict:

**GPIO 0 Conflict:**
- RMII requires **REF_CLK** input (50MHz from PHY or external crystal)
- GPIO 0 is commonly used for REF_CLK on ESP32-S3
- GPIO 0 is also the **boot strapping pin** (must be HIGH during boot)
- If PHY drives GPIO 0 LOW during ESP32 boot, device won't start

#### Required RMII Pin Mapping (ESP32-S3)

| Signal | Recommended GPIO | Notes |
|--------|------------------|-------|
| TXD0 | GPIO 40 | |
| TXD1 | GPIO 41 | |
| TX_EN | GPIO 42 | |
| RXD0 | GPIO 39 | |
| RXD1 | GPIO 38 | |
| CRS_DV | GPIO 37 | |
| REF_CLK | GPIO 0 **or** use PHY internal clock | See conflict below |
| MDC | GPIO 31 | MDIO management |
| MDIO | GPIO 32 | MDIO management |

**REF_CLK Solutions:**
1. Use RTL8201F internal 50MHz oscillator (requires specific strapping)
2. Use external 50MHz crystal on PHY
3. Use GPIO other than 0 (check ESP32-S3 EMAC peripheral constraints)

#### Required Action

- Specify exact pin mapping in hardware spec
- Verify RTL8201F can source REF_CLK internally
- Add boot strap resistor network to ensure GPIO 0 is HIGH during boot
- Document MDIO configuration sequence

---

### 3.4 FCC Certification: Module Approval Invalidation

**Severity:** High
**Affected:** All variants

#### The Problem

Section 10.2 states:
> "FCC/IC: ESP32-S3-WROOM-1 module pre-certified (modular approval)"

**This is incomplete.** The ESP32-S3 modular approval (FCC ID: 2AC7Z-ESPS3WROOM1) is granted for the module **in isolation**. RS-1 creates a **co-located transmitter** configuration:

| Transmitter | Frequency | Separation |
|-------------|-----------|------------|
| ESP32-S3 WiFi | 2.4 GHz | 0mm (same board) |
| ESP32-S3 BLE | 2.4 GHz | 0mm (same board) |
| LD2410/LD2450 | 24 GHz | <100mm |

#### Regulatory Impact

When intentional radiators are co-located (<20cm separation):
1. Original module certification conditions may be voided
2. RF exposure (SAR) calculations must be redone for combined fields
3. Intermodulation products must be measured
4. May require **Class II Permissive Change** or **full Part 15 certification**

#### Cost Impact

| Certification Path | Estimated Cost | Timeline |
|-------------------|----------------|----------|
| Ride module cert (current assumption) | $0 | N/A |
| Class II Permissive Change | $5,000 - $10,000 | 4-6 weeks |
| Full Part 15 certification | $15,000 - $25,000 | 8-12 weeks |
| CE (RED) + FCC combined | $20,000 - $35,000 | 10-14 weeks |

#### Required Action

1. Engage test lab for pre-compliance assessment
2. Budget for certification costs
3. Determine if 24GHz radar can use existing module certs (Hi-Link LD2450 should have its own cert)
4. Plan combined RF exposure testing

---

## 4. Minor Issues

### 4.1 USB ESD Protection Missing

**Component:** USB-C connector (J1)

No ESD protection specified for USB data lines. Consumer devices require TVS diode arrays on user-accessible ports.

**Required:** Add TVS diode array (e.g., SRV05-4, USBLC6-2SC6, or TPD2E001)

**Cost:** ~$0.10

---

### 4.2 Thermal Management Unaddressed

**Issue:** High-power components in sealed enclosure (IP20)

Heat sources:
- ESP32-S3: ~1W during WiFi TX
- LD2450: ~0.75W
- LD2410: ~0.5W
- Power regulator: 0.2-1.0W (depending on topology)

**Total:** 2.5-3.5W in a small ABS enclosure

**Risk:** Internal ambient exceeds 50°C operating limit

**Required:**
- Thermal simulation before enclosure finalization
- Consider vented enclosure design
- Add copper pours for heat spreading
- Possibly add thermal pad to enclosure

---

### 4.3 Reset Button Debounce

**Component:** SW1 reset button

No hardware debounce capacitor specified. Software debounce is acceptable but hardware (100nF across switch) is more robust.

**Cost:** ~$0.01

---

### 4.4 WS2812 Power Budget

**Component:** D1 status LED

WS2812-style LEDs draw:
- Idle (dark): ~1mA (logic level)
- Full white: ~60mA

This adds to power budget and should be included in calculations.

---

### 4.5 Supply Chain Risk: LD2450 Single Source

**Component:** LD2450 radar module

The LD2450 is:
- Relatively new product
- Single source (Hi-Link only)
- No pin-compatible alternates

**Risk:** If Hi-Link discontinues or has supply issues, Dynamic and Fusion SKUs cannot be built.

**Mitigation:**
- Establish direct relationship with Hi-Link
- Stock buffer inventory
- Monitor for alternate suppliers entering market
- Consider designing footprint-compatible alternate radar option

---

## 5. Open Questions Requiring Decisions

These require product/engineering decisions before proceeding:

### 5.1 Power Architecture

| Question | Options | Tradeoffs |
|----------|---------|-----------|
| Buck converter selection | TPS62840, AP62300, SY8088, etc. | Cost vs availability vs layout complexity |
| Power mux topology | Schottky OR, Ideal diode, USB priority | Cost vs voltage drop vs complexity |

### 5.2 Memory Configuration

| Question | Options | Tradeoffs |
|----------|---------|-----------|
| MCU variant | N4 (no PSRAM) vs N8R2 (2MB PSRAM) | $0.50 cost vs long-term stability |
| If N4: heap budget | Strict limits | Feature limitations, OTA complexity |

### 5.3 Fusion Variant Viability

| Question | Options | Tradeoffs |
|----------|---------|-----------|
| Dual radar coexistence | Time-mux vs shielding vs accept degradation | Response time vs accuracy vs cost |
| Proceed with Fusion? | Yes / No / Defer to prototype testing | Revenue vs engineering risk |

### 5.4 Certification Strategy

| Question | Options | Tradeoffs |
|----------|---------|-----------|
| FCC approach | Rely on module certs vs pre-compliance test vs full cert | $0 vs $10k vs $25k |
| Geographic scope | US only vs US+EU vs global | Certification cost vs market size |

### 5.5 Thermal Strategy

| Question | Options | Tradeoffs |
|----------|---------|-----------|
| Enclosure design | Sealed (IP20) vs vented vs metal | Aesthetics vs thermal vs cost |
| Derate specs? | Reduce max ambient from 50°C to 40°C | Simpler thermal vs reduced operating range |

---

## 6. Recommended Design Changes

### 6.1 Immediate (Before Schematic)

| Priority | Change | Impact |
|----------|--------|--------|
| P0 | Replace LDO with buck converter | Prevents brownouts, thermal failure |
| P0 | Add power mux circuit | Prevents damage from USB+PoE |
| P1 | Specify exact RMII pin mapping | Prevents boot failures |
| P1 | Add USB ESD protection | Required for consumer product |
| P1 | Select N8R2 MCU variant | Prevents memory issues |

### 6.2 Before Layout

| Priority | Change | Impact |
|----------|--------|--------|
| P1 | Complete thermal analysis | Informs enclosure design |
| P1 | Prototype Fusion dual-radar | Validates viability |
| P2 | Add debug test points | Aids manufacturing, support |
| P2 | Plan certification testing | Budget and timeline |

### 6.3 Before Production

| Priority | Change | Impact |
|----------|--------|--------|
| P1 | Complete FCC/CE certification | Legal requirement |
| P2 | Establish Hi-Link supply agreement | Reduces supply risk |
| P2 | Validate thermal in enclosure | Prevents field failures |

---

## 7. Additional Engineering Insights

### 7.1 Buck Converter Layout Considerations

Switching regulators require careful PCB layout:

```
┌─────────────────────────────────────────┐
│           Buck Converter Layout         │
├─────────────────────────────────────────┤
│                                         │
│  VIN ───┬───[CIN]───┬───GND            │
│         │           │                   │
│         └──[BUCK]───┼───VOUT            │
│             │       │                   │
│            [L]      │                   │
│             │       │                   │
│         ────┴───[COUT]───GND            │
│                                         │
│  Key rules:                             │
│  • CIN close to BUCK VIN/GND pins       │
│  • Minimize SW node area (EMI)          │
│  • COUT close to BUCK VOUT/GND          │
│  • Solid ground plane under inductor    │
│  • Keep analog signals away from SW     │
└─────────────────────────────────────────┘
```

### 7.2 RTL8201F Configuration for Internal Clock

The RTL8201F can source its own 50MHz REF_CLK, avoiding the GPIO 0 conflict:

| Strap Pin | Setting | Function |
|-----------|---------|----------|
| RXDV/MODE | Pull-down | RMII mode |
| RXD1/PHYAD0 | Pull-up/down | PHY address bit 0 |
| RXD0/PHYAD1 | Pull-up/down | PHY address bit 1 |
| CRS_DV/REFCLKO | Pull-up | Enable REF_CLK output |

With internal clock enabled, the PHY outputs 50MHz on a dedicated pin that connects to ESP32-S3 EMAC_CLK_IN.

### 7.3 Thermal Estimation

Rough thermal model for sealed enclosure:

```
Thermal Resistance (enclosure to ambient): ~15-25 °C/W (ABS, no vents)
Power dissipation: ~3W
Temperature rise: 3W × 20°C/W = 60°C rise

If ambient = 25°C:
  Internal temp = 25 + 60 = 85°C  ← Exceeds ESP32 limit (85°C junction)

If ambient = 40°C:
  Internal temp = 40 + 60 = 100°C ← Certain thermal shutdown
```

**Vented enclosure** reduces thermal resistance to ~8-12°C/W:
- Temperature rise: 3W × 10°C/W = 30°C
- Internal temp at 40°C ambient: 70°C ← Marginal but survivable

### 7.4 Power Budget Worksheet (Corrected)

| State | ESP32-S3 | LD2450 | LD2410 | Peripherals | Total | Duration |
|-------|----------|--------|--------|-------------|-------|----------|
| Idle | 80mA | 150mA | 100mA | 15mA | 345mA | 90% |
| WiFi RX | 100mA | 150mA | 100mA | 15mA | 365mA | 8% |
| WiFi TX | 380mA | 150mA | 100mA | 15mA | 645mA | 2% |

**Average current:** ~360mA
**Peak current:** ~650mA
**Required regulator:** 800mA+ with transient capability

---

## 8. Action Items

### Immediate (Block PCB Layout)

- [ ] **Select buck converter** - Owner: Hardware
- [ ] **Design power mux circuit** - Owner: Hardware
- [ ] **Finalize RMII pin mapping** - Owner: Hardware
- [ ] **Decide MCU variant (N4 vs N8R2)** - Owner: Product + Firmware

### Short Term (Before Layout)

- [ ] **Prototype Fusion dual-radar** - Owner: Hardware
- [ ] **USB ESD protection selection** - Owner: Hardware
- [ ] **Thermal analysis/simulation** - Owner: Hardware
- [ ] **Pre-compliance RF testing quote** - Owner: Hardware

### Medium Term (Before Production)

- [ ] **FCC/CE certification engagement** - Owner: Operations
- [ ] **Hi-Link supply agreement** - Owner: Operations
- [ ] **Enclosure thermal validation** - Owner: Hardware

---

## Issue Summary Table

| ID | Severity | Category | Summary | Status |
|----|----------|----------|---------|--------|
| 2.1 | Fatal | Power | LDO cannot supply load, thermal impossible | Open |
| 2.2 | Fatal | Power | No power mux, USB+PoE causes damage | Open |
| 3.1 | High | RF | Dual 24GHz radar interference | Open |
| 3.2 | High | Memory | No PSRAM risks heap fragmentation | Open |
| 3.3 | High | GPIO | RMII pin mapping incomplete, GPIO 0 conflict | Open |
| 3.4 | High | Regulatory | FCC module cert may be invalid for co-location | Open |
| 4.1 | Medium | ESD | USB ESD protection missing | Open |
| 4.2 | Medium | Thermal | No thermal analysis for sealed enclosure | Open |
| 4.3 | Low | Debounce | Reset button hardware debounce | Open |
| 4.4 | Low | Power | WS2812 power not in budget | Open |
| 4.5 | Medium | Supply | LD2450 single source risk | Open |

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-14 | Hardware Review | Initial review based on HW-RS1-001 v1.0 |

---

## References

- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [RTL8201F Datasheet](https://www.realtek.com/en/component/zoo/category/rtl8201f)
- [ME6211 Datasheet](https://datasheet.lcsc.com/lcsc/1811081822_MICRONE-Nanjing-Micro-One-Elec-ME6211C33M5G-N_C82942.pdf)
- [Si3404 Datasheet](https://www.silabs.com/documents/public/data-sheets/Si3404-A.pdf)
- [FCC Part 15 Co-location Requirements](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15)
