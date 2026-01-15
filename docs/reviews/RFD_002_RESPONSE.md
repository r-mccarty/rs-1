# RFD-002 Response: Hardware Specification Solutions

**Status:** Proposed Solutions
**Author:** Hardware Engineering
**Date:** 2026-01-14
**In Response To:** RFD-002 Hardware Spec Review

---

## Executive Summary

This document provides cost-effective solutions to the issues identified in RFD-002, using components available through LCSC and compatible with JLCPCB assembly. All proposed solutions prioritize:

1. **Cost efficiency** - Chinese clone components where viable
2. **LCSC availability** - All parts in LCSC catalog with good stock
3. **JLCPCB assembly** - Compatible with JLCPCB SMT service

**Critical Discovery:** During research, we identified that **ESP32-S3 does not have internal EMAC/RMII support**. The PoE design with RTL8201F PHY requires significant revision. See Section 3.

---

## 1. Power Regulator Solution (Issue 2.1)

### Problem Recap
The ME6211 LDO (500mA) cannot supply peak load (~650mA) and is thermally impossible (1.1W in 0.4W package).

### Proposed Solution: SY8089AAAC Synchronous Buck Converter

| Parameter | Specification |
|-----------|---------------|
| Part Number | SY8089AAAC |
| Manufacturer | Silergy Corp |
| LCSC Part # | [C78988](https://www.lcsc.com/product-detail/C78988.html) |
| Package | SOT-23-5 |
| Input Voltage | 2.7V - 5.5V |
| Output Current | **2A continuous** |
| Switching Freq | 1.5MHz |
| Quiescent Current | 55µA |
| Efficiency | >90% typical |

**Pricing (LCSC):**
| Quantity | Unit Price |
|----------|------------|
| 5+ | $0.1416 |
| 6,000+ | $0.0705 |

**Why SY8089AAAC:**
- 2A rating provides 3x margin over peak 650mA
- Same SOT-23-5 footprint as original LDO (minimal layout change)
- 1.5MHz switching allows smaller inductor
- Excellent efficiency at ESP32 load currents
- Massive stock at LCSC (68,000+ units)

**Alternative (Even Cheaper):** MT3410LB

| Parameter | Specification |
|-----------|---------------|
| Part Number | MT3410LB |
| Manufacturer | XI'AN Aerosemi Tech |
| LCSC Part # | [C883494](https://lcsc.com/product-detail/DC-DC-Converters_XI-AN-Aerosemi-Tech-MT3410LB_C883494.html) |
| Output Current | 1.3A |
| Price | $0.0217 @ 21k qty |

The MT3410LB is half the price but only 1.3A - tight margin for Pro variant. **Recommend SY8089AAAC** for reliability.

### Required Additional Components

| Component | Part | LCSC # | Qty | Unit Price |
|-----------|------|--------|-----|------------|
| Inductor | 2.2µH, 3A, 0805 | C408412 | 1 | ~$0.02 |
| Input Cap | 10µF 10V X5R 0603 | C19702 | 1 | ~$0.01 |
| Output Cap | 22µF 10V X5R 0805 | C45783 | 2 | ~$0.02 |
| Feedback R1 | 200kΩ 1% 0603 | C25811 | 1 | ~$0.01 |
| Feedback R2 | 68kΩ 1% 0603 | C23182 | 1 | ~$0.01 |

**Output Voltage Calculation:**
```
VOUT = 0.6V × (1 + R1/R2)
     = 0.6V × (1 + 200k/68k)
     = 0.6V × 3.94
     = 2.36V...

Corrected values for 3.3V:
R1 = 180kΩ, R2 = 40kΩ (use 39kΩ standard value)
VOUT = 0.6V × (1 + 180k/39k) = 0.6V × 5.62 = 3.37V ≈ 3.3V
```

### Total Power Solution BOM Add

| Item | Cost |
|------|------|
| SY8089AAAC | $0.07 |
| Inductor | $0.02 |
| Capacitors (3) | $0.05 |
| Resistors (2) | $0.02 |
| **Total Add** | **~$0.16** |

Compared to ME6211 @ $0.08, net increase is **~$0.08** for a working design.

### PCB Layout Guidelines

```
        ┌─────────────────────────────────────────┐
        │         SY8089 Layout (Top View)        │
        ├─────────────────────────────────────────┤
        │                                         │
        │    VIN ──┬──[CIN]──┬── GND             │
        │          │         │                    │
        │          └──[U1]───┼── SW ──[L1]── VOUT│
        │              │     │          │        │
        │              FB    │     [COUT1][COUT2]│
        │              │     │          │        │
        │          ┌───┴──┐  │          │        │
        │          R1     │  └──────────┴── GND  │
        │          │      │                      │
        │          ├──────┴── GND                │
        │          R2                            │
        │          │                             │
        │          └── GND                       │
        │                                         │
        │  Critical: Keep SW node area small!    │
        │  Use solid ground plane under L1       │
        └─────────────────────────────────────────┘
```

---

## 2. Power Mux Solution (Issue 2.2)

### Problem Recap
USB VBUS and PoE 5V outputs can fight each other when both connected, potentially damaging the laptop or device.

### Proposed Solution: Dual Schottky Diode OR-ing

For cost-sensitive design, simple Schottky diode OR-ing is the most economical solution.

| Component | Part | LCSC # | Qty | Price |
|-----------|------|--------|-----|-------|
| Schottky Diode | SS34 (3A, 40V) | [C8678](https://lcsc.com/product-detail/Schottky-Barrier-Diodes-SBD_MDD-Microdiode-Electronics-SS34_C8678.html) | 2 | $0.013 ea |

**Total Cost:** ~$0.03

### Circuit Implementation

```
     PoE 5.0V ──────►|────┬──────► System 4.7V
                (D1 SS34) │
                          │
     USB VBUS ─────►|─────┘
                (D2 SS34)
```

### Tradeoff Analysis

| Parameter | Schottky OR | Ideal Diode |
|-----------|-------------|-------------|
| Cost | $0.03 | $0.80+ |
| Voltage Drop | ~0.3V @ 500mA | ~0.02V |
| System Voltage | ~4.7V | ~4.95V |
| Buck Headroom | 4.7V - 3.3V = 1.4V ✓ | 4.95V - 3.3V = 1.65V |
| Complexity | Simple | Moderate |

**Analysis:** The SY8089AAAC operates down to 2.7V input, so 4.7V (after Schottky drop) provides **1.4V headroom** - more than adequate. The ~0.3V drop at full load results in ~200mW additional loss (0.3V × 0.65A), which is acceptable for the cost savings.

**Recommendation:** Use SS34 Schottky OR-ing. The $0.77+ savings vs ideal diode controller is worthwhile given adequate headroom.

### Alternative: USB Priority (Debug-Friendly)

For development units, consider USB-priority design where PoE is disabled when USB connected:

```
     USB VBUS ────┬────► System 5V
                  │
              [Q1 PMOS]──► /POE_EN (active low)
                  │
     PoE 5V ──────┴────► (blocked when USB present)
```

This uses the existing AO3401A PMOS for PoE enable control.

---

## 3. ESP32-S3 Ethernet Architecture (CRITICAL FINDING)

### Problem Discovery

**The ESP32-S3 does NOT have an internal EMAC or RMII interface.** This was not identified in the original hardware spec.

| ESP32 Variant | Internal EMAC | RMII Support |
|---------------|---------------|--------------|
| ESP32 (Classic) | Yes | Yes |
| ESP32-S2 | No | No |
| **ESP32-S3** | **No** | **No** |
| ESP32-C3 | No | No |
| ESP32-P4 | Yes (future) | Yes |

**Source:** [ESP-IDF Ethernet Documentation](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32s3/api-reference/network/esp_eth.html)

### Impact on Current Design

The specified PoE design with RTL8201F PHY ($0.40) **will not work** with ESP32-S3:
- RTL8201F is an RMII PHY - requires EMAC on MCU side
- ESP32-S3 has no EMAC hardware
- All GPIO pin mapping for RMII (GPIO 1-15) is irrelevant

### Revised PoE Options

#### Option A: W5500 SPI Ethernet Controller (Recommended)

| Parameter | Specification |
|-----------|---------------|
| Part Number | W5500 |
| Manufacturer | WIZnet |
| LCSC Part # | [C32843](https://lcsc.com/product-detail/Ethernet-ICs_WIZNET-W5500_C32843.html) |
| Interface | SPI (up to 80MHz) |
| Package | LQFP-48 (7x7) |
| Price | **$1.77** |
| Stock | 40,000+ units |

**W5500 Advantages:**
- Hardwired TCP/IP stack (offloads MCU)
- Integrated MAC + PHY in one chip
- Only needs 4 SPI pins + CS + INT + RST
- Well-supported in ESP-IDF and ESPHome
- 8 simultaneous sockets

**W5500 Disadvantages:**
- Higher cost than RTL8201F ($1.77 vs $0.40)
- Requires external Ethernet magnetics + RJ45

#### Option B: Revert to ESP32 Classic for PoE Variant

Keep ESP32-S3 for USB-only variants, use ESP32-WROOM-32E for PoE variant:

| Aspect | ESP32-S3 | ESP32-WROOM-32E |
|--------|----------|-----------------|
| RMII Support | No | Yes |
| Native USB | Yes | No (needs CH340/CP2102) |
| Cost | $3.42 | $2.70 |
| PoE PHY | W5500 ($1.77) | RTL8201F ($0.40) |
| **PoE Total** | $5.19 | $3.10 + $0.50 (USB bridge) = $3.60 |

**Issue:** Two different MCUs complicates firmware and manufacturing.

#### Option C: Remove PoE Variant from MVP

Focus on USB-powered variants only for initial launch. Add PoE as future SKU once product is validated.

### Recommended Approach: Option A (W5500)

Despite higher cost, W5500 provides:
- Single MCU platform (ESP32-S3 for all variants)
- Proven ESP-IDF support
- Hardware TCP/IP offload reduces CPU load
- Simplified firmware development

### Revised PoE BOM

| Component | Original | Revised | Delta |
|-----------|----------|---------|-------|
| Ethernet Controller | RTL8201F $0.40 | W5500 $1.77 | +$1.37 |
| Si3404 PoE Controller | $1.20 | $1.20 | - |
| Magnetics/RJ45 | $1.30 | $1.30 | - |
| 25MHz Crystal | $0.12 | Removed (W5500 internal) | -$0.12 |
| **Total PoE Add** | $3.02 | **$4.27** | **+$1.25** |

New PoE retail add should be **+$35** (was +$30) to maintain margin.

---

## 4. RF Coexistence Solution (Issue 3.1)

### Problem Recap
Dual 24GHz radars (LD2410 + LD2450) will interfere, causing ghost targets.

### Confirmed Solution: Time-Division Multiplexing via Power Gating

The hardware design already includes a power gating MOSFET (AO3401A). Firmware will implement time-division multiplexing:

```
┌─────────────────────────────────────────────────────────────┐
│              Time-Division Radar Operation                  │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Phase 1 (50ms): LD2410 Active, LD2450 Power-Gated         │
│  ┌────────────────┐                                         │
│  │ LD2410: ON     │ → Presence detection (stationary)      │
│  │ LD2450: OFF    │                                         │
│  └────────────────┘                                         │
│                                                             │
│  Phase 2 (50ms): LD2450 Active, LD2410 Power-Gated         │
│  ┌────────────────┐                                         │
│  │ LD2410: OFF    │                                         │
│  │ LD2450: ON     │ → Multi-target tracking (motion)       │
│  └────────────────┘                                         │
│                                                             │
│  Effective Frame Rate: ~10 Hz each (vs 33 Hz standalone)   │
│  Total Cycle: 100ms = 10 Hz combined update rate           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Firmware Implementation Requirements

| Requirement | Specification |
|-------------|---------------|
| GPIO for LD2410 Power | GPIO TBD (via AO3401A) |
| GPIO for LD2450 Power | GPIO TBD (via AO3401A) |
| Switching Interval | 50-100ms configurable |
| Radar Startup Time | ~20ms (account for in timing) |
| Effective Update Rate | 5-10 Hz per radar |

### Hardware Support Needed

Add second power gating MOSFET for independent radar control:

| Component | Part | LCSC # | Qty | Price |
|-----------|------|--------|-----|-------|
| PMOS Gate | AO3401A | C15127 | 1 | $0.02 |
| Gate Resistor | 10kΩ 0603 | C25804 | 1 | $0.01 |

**Total Add:** ~$0.03

### Impact on Specifications

| Spec | Original | With TDM |
|------|----------|----------|
| Response Time | <1 second | <1 second ✓ |
| LD2450 Frame Rate | 33 Hz | ~10 Hz |
| LD2410 Update Rate | Continuous | ~10 Hz |
| Power (Pro) | ~600mA | ~450mA (alternating) |

**Note:** TDM actually reduces peak power since only one radar is active at a time.

---

## 5. USB ESD Protection Solution (Issue 4.1)

### Proposed Solution: USBLC6-2SC6

| Parameter | Specification |
|-----------|---------------|
| Part Number | USBLC6-2SC6 |
| Manufacturer | TECH PUBLIC (clone) |
| LCSC Part # | [C2827654](https://lcsc.com/product-detail/ESD-and-Surge-Protection-TVS-ESD_TECH-PUBLIC-USBLC6-2SC6_C2827654.html) |
| Package | SOT-23-6 |
| ESD Rating | ±15kV (HBM) |
| Price | **$0.0194** |
| Stock | 152,000+ units |

### Circuit Placement

```
     USB Connector              ESP32-S3
     ┌─────────┐               ┌─────────┐
     │    D+   ├───┬───────────┤ GPIO20  │
     │         │   │           │         │
     │    D-   ├───┼───┬───────┤ GPIO19  │
     │         │   │   │       │         │
     │   GND   ├───┼───┼───────┤ GND     │
     └─────────┘   │   │       └─────────┘
                   │   │
              ┌────┴───┴────┐
              │  USBLC6-2SC6 │
              │              │
              │  1-IO1  6-IO2│
              │  2-GND  5-GND│
              │  3-IO1  4-IO2│
              └──────────────┘
```

### Alternative (Cheaper)

| Part | Manufacturer | LCSC # | Price |
|------|--------------|--------|-------|
| USBLC6-2SC6 | KUU | C47147619 | $0.0151 |

---

## 6. MCU Memory Configuration (Issue 3.2)

### Recommendation: ESP32-S3-WROOM-1-N8R2

| Parameter | N4 (Current) | N8R2 (Recommended) |
|-----------|--------------|---------------------|
| Flash | 4MB | 8MB |
| PSRAM | 0 | 2MB |
| LCSC Part # | C2913202 | C2913206 |
| Price | $3.42 | ~$3.90 |
| **Delta** | - | **+$0.48** |

### Justification

The $0.48 increase provides:
- 2MB PSRAM eliminates heap fragmentation concerns
- 8MB flash provides OTA headroom
- Future-proofs for feature additions
- Reduces firmware debugging effort
- Prevents field failures from memory issues

For a $69-99 retail product, this is negligible insurance.

---

## 7. FCC Certification Plan (Issue 3.4)

### Confirmed Approach: Nemko USA (Carlsbad, CA)

| Service | Estimated Cost | Timeline |
|---------|----------------|----------|
| Pre-compliance testing | $2,000 - $3,000 | 1-2 weeks |
| Full FCC Part 15 certification | $8,000 - $12,000 | 4-6 weeks |
| CE RED (if EU market) | $5,000 - $8,000 | 4-6 weeks |
| Combined FCC + CE | $12,000 - $18,000 | 6-8 weeks |

### Testing Scope

| Test | Requirement |
|------|-------------|
| Conducted emissions | FCC Part 15 Subpart B |
| Radiated emissions | FCC Part 15 Subpart B |
| 24GHz radar | FCC Part 15.255 |
| WiFi/BLE (ESP32-S3) | Covered by modular approval |
| RF exposure | Required for co-located transmitters |

### Pre-Submission Checklist

- [ ] Finalize PCB layout
- [ ] Build 5 production-representative samples
- [ ] Document all intentional radiators
- [ ] Prepare block diagram for FCC filing
- [ ] Obtain Hi-Link LD2450/LD2410 FCC grants (reference)
- [ ] Schedule pre-compliance slot with Nemko

---

## 8. Revised BOM Summary

### Core Platform (All Variants)

| Item | Original | Revised | Delta |
|------|----------|---------|-------|
| ESP32-S3-WROOM-1 | N4 $3.42 | N8R2 $3.90 | +$0.48 |
| Power Regulator | ME6211 $0.08 | SY8089 + passives $0.16 | +$0.08 |
| USB ESD | None | USBLC6-2SC6 $0.02 | +$0.02 |
| Power Mux | None | 2× SS34 $0.03 | +$0.03 |
| **Total Core Delta** | | | **+$0.61** |

### Variant-Specific Updates

| Variant | Original BOM | Revised BOM | Delta |
|---------|--------------|-------------|-------|
| RS-1 Lite | $7.73 | $8.34 | +$0.61 |
| RS-1 Pro | $19.23 | $19.90 | +$0.67 |

### PoE Add-On (Revised)

| Item | Original | Revised | Delta |
|------|----------|---------|-------|
| Ethernet IC | RTL8201F $0.40 | W5500 $1.77 | +$1.37 |
| Si3404 + Magnetics | $2.50 | $2.50 | - |
| 25MHz Crystal | $0.12 | Removed | -$0.12 |
| **PoE Add Total** | $3.02 | $4.15 | **+$1.13** |

**Revised PoE Retail Add:** +$30

### Pro Variant (Additional)

| Item | Change | Delta |
|------|--------|-------|
| Second power gate MOSFET | AO3401A + resistor | +$0.03 |

---

## 9. Updated Product Lineup

| SKU | Revised BOM | Retail | Gross Margin |
|-----|-------------|--------|--------------|
| RS-1 Lite | $7.73 | $49 | 84% |
| RS-1 Pro | $19.23 | $89 | 78% |
| PoE Add-On | +$4.15 | +$30 | 86% |
| IAQ Add-On | +$5.00 | +$35 | 86% |

### Example Configurations

| Configuration | BOM | Retail | Margin |
|---------------|-----|--------|--------|
| Lite + PoE | $11.88 | $79 | 85% |
| Pro + PoE | $23.38 | $119 | 80% |
| Pro + PoE + IAQ | $28.38 | $154 | 82% |

---

## 10. Action Items

### Immediate (Update Hardware Spec)

- [x] Replace ME6211 LDO with SY8089AAAC buck converter
- [x] Add SS34 Schottky diode power OR-ing circuit
- [x] Add USBLC6-2SC6 USB ESD protection
- [x] Update MCU to ESP32-S3-WROOM-1-N8R2
- [ ] **Revise PoE design: Replace RTL8201F with W5500**
- [ ] Add second power gating MOSFET for Pro TDM

### Schematic Updates Required

- [ ] Buck converter schematic (SY8089AAAC + passives)
- [ ] Power mux schematic (dual SS34)
- [ ] USB ESD schematic (USBLC6-2SC6)
- [ ] W5500 SPI Ethernet schematic (replaces RMII)
- [ ] Dual radar power gating schematic

### Firmware Coordination Required

- [ ] Radar TDM implementation spec
- [ ] W5500 Ethernet driver integration
- [ ] Power management for TDM operation

### Certification

- [ ] Contact Nemko Carlsbad for quote
- [ ] Schedule pre-compliance testing
- [ ] Budget certification costs ($15-20k)

---

## 11. Parts Quick Reference

### New/Changed Components

| Function | Part Number | LCSC # | Package | Price |
|----------|-------------|--------|---------|-------|
| Buck Converter | SY8089AAAC | C78988 | SOT-23-5 | $0.07 |
| Buck Inductor | 2.2µH 3A | C408412 | 0805 | $0.02 |
| Power Diode | SS34 | C8678 | SMA | $0.013 |
| USB ESD | USBLC6-2SC6 | C2827654 | SOT-23-6 | $0.02 |
| MCU | ESP32-S3-WROOM-1-N8R2 | C2913206 | Module | $3.90 |
| Ethernet | W5500 | C32843 | LQFP-48 | $1.77 |
| PMOS Gate | AO3401A | C15127 | SOT-23 | $0.02 |

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-14 | Hardware Engineering | Initial response to RFD-002 |

---

## References

- [SY8089AAAC Datasheet (LCSC)](https://datasheet.lcsc.com/lcsc/1810121532_Silergy-Corp-SY8088AAC_C79313.pdf)
- [W5500 LCSC Listing](https://lcsc.com/product-detail/Ethernet-ICs_WIZNET-W5500_C32843.html)
- [ESP-IDF Ethernet Documentation](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32s3/api-reference/network/esp_eth.html)
- [SS34 Schottky Diode LCSC](https://lcsc.com/product-detail/Schottky-Barrier-Diodes-SBD_MDD-Microdiode-Electronics-SS34_C8678.html)
- [USBLC6-2SC6 LCSC](https://lcsc.com/product-detail/ESD-and-Surge-Protection-TVS-ESD_TECH-PUBLIC-USBLC6-2SC6_C2827654.html)
