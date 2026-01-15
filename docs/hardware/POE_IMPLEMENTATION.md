# RS-1 PoE Implementation Specification

**Document Number:** HW-RS1-POE-001
**Version:** 1.0
**Date:** 2026-01-15
**Status:** Design Phase
**Owner:** OpticWorks Hardware Engineering

---

## 1. Overview

### 1.1 Purpose

This document specifies the Power over Ethernet (PoE) implementation for the RS-1 platform, enabling wired power and data connectivity as an add-on option to the base USB-C powered variants.

### 1.2 Scope

- IEEE 802.3af (Type 1) PoE Powered Device (PD) implementation
- USB-C and PoE coexistence design (power mux)
- Component selection and tradeoff analysis
- PCB layout considerations
- Complete BOM for both module and discrete approaches

### 1.3 References

| Document | Description |
|----------|-------------|
| HARDWARE_SPEC.md | Main hardware specification |
| RS-1_Unified_BOM.md | Complete bill of materials |
| RFD_002_RESPONSE.md | Power architecture solutions |
| ETHERNET_RFD_002_FOLLOWUP.md | Ethernet architecture decisions |

---

## 2. System Requirements

### 2.1 PoE Standards Compliance

| Parameter | Requirement | Notes |
|-----------|-------------|-------|
| IEEE Standard | 802.3af (Type 1) | Standard PoE |
| Input Voltage | 37 - 57 VDC | Per 802.3af spec |
| Power Class | 0-3 | Class 2 typical (7W) |
| Max Input Power | 12.95W | Per 802.3af |
| Output Voltage | 5.0V ±5% | To power mux |
| Output Current | 1.5A minimum | For RS-1 Pro peak |
| Isolation | 1500VAC minimum | Safety requirement |

### 2.2 System Current Requirements

| Mode | Current (mA) | Notes |
|------|--------------|-------|
| RS-1 Lite Peak | 550 | WiFi TX + LD2410 |
| RS-1 Pro Peak (TDM) | 600 | WiFi TX + one radar active |
| RS-1 Pro Peak (Both) | 700 | WiFi TX + both radars active |
| **Design Target** | **1000** | 30% margin for reliability |

### 2.3 Coexistence Requirements

| Requirement | Implementation |
|-------------|----------------|
| USB + PoE simultaneous | Schottky diode OR-ing (SS34) |
| Priority | None (highest voltage wins, ~equal) |
| Back-power prevention | Diode isolation |
| Hot-plug support | Required |
| Switchover glitch | <10ms, handled by bulk capacitors |

---

## 3. Power Architecture

### 3.1 System Block Diagram

```
    Ethernet Cable (Cat5e/6)
           │
    ┌──────┴──────┐
    │   RJ45      │
    │  + Magnetics│  HR911105A Magjack
    │  (HanRun)   │  1500VAC Isolation
    └──────┬──────┘
           │
           ├───────────────────────────┐
           │                           │
    ┌──────┴──────┐             ┌──────┴──────┐
    │ Data Path   │             │ Power Path  │
    │  (RMII)     │             │  (PoE PD)   │
    └──────┬──────┘             └──────┬──────┘
           │                           │
    ┌──────┴──────┐             ┌──────┴──────┐
    │  SR8201F    │             │   PD + DC   │
    │  Ethernet   │             │   Converter │
    │    PHY      │             │             │
    └──────┬──────┘             │ Option A:   │
           │                    │ DP1435-5V   │
           │                    │    OR       │
           │                    │ Option B:   │
           │                    │ Si3404 +    │
           │                    │ Flyback     │
    ┌──────┴──────┐             └──────┬──────┘
    │ ESP32       │                    │ 5.0V
    │ EMAC/RMII   │                    │
    └─────────────┘                    │
                                       │
    ┌──────────────────────────────────┴────────────────────────┐
    │                      Power Mux                             │
    │                                                            │
    │    PoE 5.0V ────────►|────────┬────────► System 4.7V      │
    │                  (D1 SS34)    │                            │
    │                               │                            │
    │    USB VBUS ────────►|────────┘                            │
    │                  (D2 SS34)                                 │
    │                                                            │
    └───────────────────────────────┬────────────────────────────┘
                                    │
    ┌───────────────────────────────┴────────────────────────────┐
    │                    SY8089AAAC Buck                          │
    │                                                             │
    │    4.7V ──┬──[CIN 10µF]──┬── GND                           │
    │           │              │                                  │
    │           └──[SY8089]────┼── SW ──[L1 2.2µH]── 3.3V        │
    │               │          │              │                   │
    │               FB         │        [COUT 22µF×2]             │
    │               │          │              │                   │
    │           R1(180kΩ)      └──────────────┴── GND             │
    │               │                                             │
    │           R2(39kΩ)                                          │
    │               │                                             │
    │              GND                                            │
    │                                                             │
    │    Output: 3.3V @ up to 2A                                  │
    └─────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                            3.3V System Rail
                    (ESP32, Sensors, Radars via 5V boost)
```

### 3.2 Power Budget Analysis

| Stage | Input | Output | Efficiency | Power Loss |
|-------|-------|--------|------------|------------|
| PoE PD (48V→5V) | 48V @ 180mA | 5V @ 1.5A | ~85% | 1.3W |
| Diode OR | 5.0V | 4.7V | N/A | 0.3V × 0.7A = 0.21W |
| Buck (4.7V→3.3V) | 4.7V @ 780mA | 3.3V @ 1A | ~92% | 0.3W |
| **Total System** | **8.6W max in** | **3.3W max out** | **~38%** | **1.8W** |

**Thermal Considerations:**
- Total system dissipation: ~1.8W in normal operation
- PD module handles ~1.3W internally (designed for this)
- Buck converter handles ~0.3W (within SOT-23-5 limits with proper layout)
- Diode OR handles ~0.2W (within SMA package limits)

---

## 4. Implementation Options

### 4.1 Option A: Integrated PD Module (RECOMMENDED)

#### 4.1.1 Component Selection

| Parameter | Specification |
|-----------|---------------|
| Part Number | SDAPO DP1435-5V |
| Type | Integrated 802.3af PD Module |
| Input Voltage | 37-57 VDC |
| Output Voltage | 5.0V ±5% |
| Output Current | 2A max |
| Isolation | 1500VAC |
| Package | SIP-6 |
| Dimensions | ~25mm × 15mm × 12mm |
| Price (10+ qty) | ~$4.22 |

#### 4.1.2 Module Features

- Integrated PD controller + isolated flyback converter
- Built-in signature resistor (25kΩ) for 802.3af detection
- Classification to Class 2 (7W) or Class 3 (15.4W)
- Short-circuit and over-current protection
- Pre-certified for 802.3af compliance
- Thermal shutdown protection
- Soft-start circuitry included

#### 4.1.3 Module BOM

| Item | Part Number | Description | Qty | Cost |
|------|-------------|-------------|-----|------|
| MOD1 | DP1435-5V | Integrated PD Module | 1 | $4.22 |
| C1 | 100µF 63V Electrolytic | Input capacitor | 1 | $0.10 |
| C2 | 100µF 10V Ceramic/Electrolytic | Output capacitor | 1 | $0.05 |
| **Total** | | | | **$4.37** |

#### 4.1.4 Module Schematic

```
    From Magjack CT
    (48V PoE)
         │
    ┌────┴────┐
    │  C1     │ 100µF 63V
    │  Input  │ (Electrolytic)
    └────┬────┘
         │
    ┌────┴────────────────────┐
    │        DP1435-5V        │
    │                         │
    │  1-VIN+     6-NC        │
    │  2-VIN-     5-GND       │
    │  3-NC       4-VOUT+     │
    │                         │
    └────┬────────────┬───────┘
         │            │
    ┌────┴────┐  ┌────┴────┐
    │  GND    │  │  C2     │ 100µF 10V
    │         │  │  Output │
    └─────────┘  └────┬────┘
                      │
                 5.0V Output
                 (To Power Mux)
```

#### 4.1.5 Module Advantages

1. **Simplicity** - Drop-in solution, minimal design effort
2. **Certification** - Pre-certified reduces FCC/CE compliance risk
3. **Time-to-market** - No high-voltage design validation needed
4. **Reliability** - Proven thermal and electrical design
5. **Layout** - No isolation boundary design required
6. **Support** - Vendor provides reference designs

#### 4.1.6 Module Disadvantages

1. **Cost** - ~$1.67 more than discrete approach per unit
2. **Flexibility** - Fixed output voltage (5V only)
3. **Footprint** - Larger than optimized discrete solution
4. **Availability** - Single source (SDAPO)

---

### 4.2 Option B: Discrete Si3404 + Flyback

#### 4.2.1 Si3404 PD Controller

| Parameter | Specification |
|-----------|---------------|
| Part Number | Si3404-A-GMR |
| Manufacturer | Silicon Labs |
| Type | PoE PD Controller with Integrated FET |
| Input Voltage | 37-57 VDC |
| Integrated FET | Yes (reduces BOM) |
| Package | QFN-20 (4mm × 4mm) |
| LCSC Part # | TBD |
| Price | ~$1.20 |

**Si3404 Features:**
- Integrated high-voltage MOSFET switch
- Programmable current limit
- Under-voltage lockout (UVLO)
- Over-temperature protection
- Classification support (Class 0-3)
- Soft-start control

#### 4.2.2 Flyback Transformer

| Parameter | Specification |
|-----------|---------------|
| Part Number | TBD (e.g., Wurth 750315371 or similar) |
| Type | Flyback transformer |
| Turns Ratio | 8:1 (primary:secondary) |
| Primary Inductance | 100µH minimum |
| Isolation | 1500VAC (reinforced) |
| Primary Current Rating | 500mA |
| Secondary Current Rating | 4A |
| Package | SMD or Through-hole |
| Price | ~$0.80-1.20 |

**Transformer Selection Criteria:**
- Must meet 1500VAC isolation for safety
- Primary inductance sets discontinuous/continuous boundary
- Turns ratio determines duty cycle at max load
- Leakage inductance affects snubber design

#### 4.2.3 Feedback Optocoupler

| Parameter | Specification |
|-----------|---------------|
| Part Number | PC817 or equivalent |
| Type | Optocoupler |
| CTR | 80-160% |
| Isolation | 5000VAC |
| Package | DIP-4 or SMD (SOP-4) |
| Price | ~$0.05 |

#### 4.2.4 Complete Discrete BOM

| Item | Part Number | Description | Qty | Cost |
|------|-------------|-------------|-----|------|
| U1 | Si3404-A-GMR | PD Controller | 1 | $1.20 |
| T1 | TBD | Flyback Transformer | 1 | $0.80 |
| U2 | PC817 | Optocoupler | 1 | $0.05 |
| U3 | TL431 | Shunt Reference | 1 | $0.02 |
| D1 | SS54 | Output Rectifier Schottky 5A 40V | 1 | $0.03 |
| D2 | SMBJ58A | TVS Diode 58V Input Protection | 1 | $0.05 |
| D3 | 1N4148 | Snubber Diode | 1 | $0.01 |
| C1 | 10µF 100V | Input Capacitor | 1 | $0.05 |
| C2 | 100µF 63V | Bulk Input | 1 | $0.10 |
| C3 | 100nF 100V | Si3404 Bypass | 1 | $0.01 |
| C4 | 220µF 10V | Output Capacitor | 2 | $0.06 |
| C5 | 1nF 100V | Snubber Capacitor | 1 | $0.01 |
| R1 | 25kΩ 1% | Detection Resistor | 1 | $0.01 |
| R2 | 10Ω | Snubber Resistor | 1 | $0.01 |
| R3-R5 | Various | Feedback Network | 3 | $0.03 |
| L1 | 10µH | Output Filter (optional) | 1 | $0.05 |
| **Total** | | | | **~$2.55** |

#### 4.2.5 Discrete Schematic (Simplified)

```
    From Magjack CT
    (48V PoE)
         │
    ┌────┴────┐
    │ D2 TVS  │ SMBJ58A
    │ 58V     │
    └────┬────┘
         │
    ┌────┴────┐
    │ C1,C2   │ Input Caps
    │ Bulk    │
    └────┬────┘
         │
         ├──────────────────────────────────────┐
         │                                      │
    ┌────┴────┐                                 │
    │         │ Signature                       │
    │   R1    │ 25kΩ                           │
    │ Detect  │                                │
    └────┬────┘                                │
         │                           ┌─────────┴─────────┐
    ┌────┴────────────────┐          │   Transformer     │
    │      Si3404         │          │       T1          │
    │                     │          │                   │
    │  VIN   ────────────►├──────────┤►  Primary         │
    │  GATE  ────────────►│          │                   │
    │  SOURCE ───────────►│          │   Secondary  ────►├──┬──► 5V Out
    │  FB    ◄────────────│          │                   │  │
    │  VOUT  ◄────────────│          │◄────────────────┐ │  │
    │                     │          └─────────────────┼─┘  │
    └─────────────────────┘                            │    │
                                                       │    │
    ┌──────────────────────────────────────────────────┘    │
    │                                                       │
    │  ┌─────────┐   ┌─────────┐                           │
    │  │ PC817   │   │ TL431   │                           │
    │  │Optocplr │───│ Ref     │◄──────────────────────────┘
    │  └────┬────┘   └─────────┘      (Feedback from output)
    │       │
    └───────┴──► To Si3404 FB pin
```

#### 4.2.6 Discrete Advantages

1. **Cost** - ~$1.67 less than module per unit
2. **Flexibility** - Can tune output voltage and current
3. **Footprint** - Can be optimized for space
4. **Multiple sources** - Si3404 widely available

#### 4.2.7 Discrete Disadvantages

1. **Complexity** - Requires flyback design expertise
2. **Layout** - Isolation boundary requires careful design
3. **Certification** - Must validate 802.3af compliance
4. **Thermal** - Must design thermal management
5. **Development time** - Longer validation cycle
6. **EMI** - Flyback switching requires careful filtering

---

## 5. Power Mux Design

### 5.1 Schottky Diode OR-ing

The RS-1 uses passive diode OR-ing to allow simultaneous USB and PoE connection without damage.

#### 5.1.1 Circuit

```
                        SS34
     PoE 5.0V ──────────►|────────┬──────────► System 4.7V
                    (D1)          │                 │
                                  │                 ▼
     USB VBUS ──────────►|────────┘           SY8089AAAC
                    (D2)                           │
                        SS34                       ▼
                                              3.3V Rail
```

#### 5.1.2 Component Selection

| Parameter | SS34 Specification |
|-----------|-------------------|
| Part Number | SS34 |
| Manufacturer | MDD (Microdiode Electronics) |
| Type | Schottky Barrier Diode |
| Vrrm | 40V |
| If (avg) | 3A |
| Ifsm | 80A |
| Vf @ 1A | 0.45V |
| Vf @ 0.5A | ~0.35V |
| Vf @ 0.1A | ~0.25V |
| Package | SMA (DO-214AC) |
| LCSC Part # | C8678 |
| Price | $0.013 |

#### 5.1.3 Voltage Drop Analysis

| Load Current | Forward Voltage | System Voltage |
|--------------|-----------------|----------------|
| 100mA (Idle) | ~0.25V | 4.75V |
| 300mA (Active) | ~0.30V | 4.70V |
| 500mA (WiFi TX) | ~0.35V | 4.65V |
| 700mA (Peak) | ~0.40V | 4.60V |

**Buck Converter Headroom:**
- Minimum system voltage: 4.6V (at 700mA peak)
- SY8089AAAC minimum input: 2.7V
- Available headroom: 4.6V - 3.3V = 1.3V (adequate)

#### 5.1.4 Power Dissipation

| Load Current | Power per Diode | Package Rating |
|--------------|-----------------|----------------|
| 100mA | 25mW | OK |
| 300mA | 90mW | OK |
| 500mA | 175mW | OK |
| 700mA | 280mW | OK (SMA rated ~1W) |

#### 5.1.5 Alternative: Ideal Diode Controller

For higher efficiency (if needed in future):

| Parameter | LTC4412 |
|-----------|---------|
| Voltage Drop | ~20mV |
| Quiescent Current | 14µA |
| Cost | ~$0.80-1.50 |

**Not recommended** for RS-1 due to cost vs minimal benefit. The ~0.3V drop is acceptable with adequate buck headroom.

---

## 6. Ethernet Data Path

### 6.1 RMII PHY Selection

| Parameter | SR8201F |
|-----------|---------|
| Manufacturer | CoreChips |
| Type | 10/100 Ethernet PHY |
| Interface | RMII |
| Package | LQFP-48 |
| Operating Voltage | 3.3V |
| LCSC Part # | TBD |
| Price | ~$0.25 |

**Why SR8201F:**
- Lowest cost RMII PHY available
- Compatible with ESP32 EMAC
- Internal reference clock option reduces BOM

### 6.2 Magnetics Selection

| Parameter | HR911105A |
|-----------|-----------|
| Manufacturer | HanRun |
| Type | RJ45 with integrated magnetics (magjack) |
| Speed | 10/100 Mbps |
| Isolation | 1500VAC |
| PoE Support | Yes (center-tap access) |
| Package | Through-hole |
| Price | ~$0.95 |

### 6.3 Crystal

| Parameter | X322525MOB4SI |
|-----------|---------------|
| Manufacturer | YXC |
| Frequency | 25MHz |
| Tolerance | ±20ppm |
| Load Capacitance | 20pF |
| Package | 3225 |
| Price | ~$0.05 |

### 6.4 RMII Pin Mapping (ESP32-WROOM-32E)

| GPIO | RMII Function | Direction |
|------|---------------|-----------|
| 18 | MDIO | Bidirectional |
| 23 | MDC | Output |
| 19 | TXD0 | Output |
| 22 | TXD1 | Output |
| 21 | TX_EN | Output |
| 25 | RXD0 | Input |
| 26 | RXD1 | Input |
| 27 | CRS_DV | Input |
| 0 | REF_CLK | Input (50MHz from PHY) |

**Note:** GPIO 0 conflict with boot strapping requires careful PHY configuration. SR8201F can output 50MHz REF_CLK with proper strapping.

---

## 7. PCB Layout Considerations

### 7.1 Isolation Boundary (Discrete Option Only)

If using discrete Si3404 design, a proper isolation boundary is critical:

```
┌───────────────────────────────────────────────────────────────────┐
│                           PCB Layout                              │
│                                                                   │
│   ┌─────────────────┐        3mm MIN        ┌─────────────────┐  │
│   │                 │       ◄────────►      │                 │  │
│   │    PRIMARY      │      ISOLATION        │   SECONDARY     │  │
│   │    (48V)        │      BOUNDARY         │    (5V)         │  │
│   │                 │                       │                 │  │
│   │  • Si3404       │         [T1]          │  • ESP32        │  │
│   │  • Input Caps   │     Transformer       │  • Buck Conv    │  │
│   │  • TVS Diode    │     (crosses gap)     │  • Radars       │  │
│   │  • Detect Res   │                       │                 │  │
│   │                 │       [PC817]         │                 │  │
│   │                 │     Optocoupler       │                 │  │
│   │                 │     (crosses gap)     │                 │  │
│   └─────────────────┘                       └─────────────────┘  │
│                                                                   │
│   Design Rules:                                                   │
│   • Creepage: 3mm minimum                                         │
│   • Clearance: 3mm minimum                                        │
│   • Slot recommended under transformer                            │
│   • No traces crossing boundary except through transformer        │
│   • Optocoupler straddles boundary                               │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### 7.2 Module Layout (Option A - Recommended)

For DP1435-5V module, layout is simpler:

```
┌───────────────────────────────────────────────────────────────────┐
│                           PCB Layout                              │
│                                                                   │
│   ┌─────────────┐    ┌─────────────────────────────────────────┐ │
│   │   Magjack   │    │                                         │ │
│   │  HR911105A  │    │              DP1435-5V                  │ │
│   │             │    │                Module                    │ │
│   │  ┌───────┐  │    │                                         │ │
│   │  │       │  │    │   (Isolation handled internally)        │ │
│   │  │  CT   │──┼────┤                                         │ │
│   │  │       │  │    │                                         │ │
│   │  └───────┘  │    │                                         │ │
│   │             │    │    [C_in]          [C_out]    5V Out    │ │
│   └─────────────┘    └────────────────────────────────┬────────┘ │
│                                                        │          │
│   Notes:                                               │          │
│   • Module handles isolation internally                │          │
│   • Place input cap close to module input pins        │          │
│   • Place output cap close to module output pins       ▼          │
│   • No isolation slot required                    To Power Mux   │
│   • Thermal pad (if present) to copper pour                      │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### 7.3 Power Mux Layout

```
                           Keep Short and Wide
                                  │
    PoE 5V ──────────────────┐    │
                             │    │
                          ┌──┴──┐ │
                          │ D1  │ │  SS34 (SMA)
                          └──┬──┘ │
                             │    │
    USB 5V ──────────────────┼────┴───────────► To Buck Input
                             │                        │
                          ┌──┴──┐                     │
                          │ D2  │  SS34 (SMA)    ┌────┴────┐
                          └──┬──┘                │ CIN 10µF│
                             │                   └─────────┘
                            GND

    Layout Guidelines:
    • Keep diodes close together
    • Short, wide traces to buck input
    • Input capacitor at buck, not at diodes
    • Ground plane continuous under diodes
```

### 7.4 RMII Signal Routing

| Signal Group | Length Matching | Impedance |
|--------------|-----------------|-----------|
| TXD0, TXD1, TX_EN | ±2mm within group | 50Ω ±10% |
| RXD0, RXD1, CRS_DV | ±2mm within group | 50Ω ±10% |
| MDIO, MDC | No matching required | 50Ω ±10% |
| REF_CLK | Short as possible | 50Ω ±10% |

**General Guidelines:**
- Keep RMII traces short (<50mm)
- Avoid vias in high-speed signals
- Ground plane under all RMII traces
- 10Ω series termination on TX signals (optional)

---

## 8. Complete PoE BOM Summary

### 8.1 Option A: Module Approach (RECOMMENDED)

| Category | Components | Cost |
|----------|------------|------|
| **Ethernet Data** | | |
| - SR8201F Ethernet PHY | LQFP-48 | ~$0.25 |
| - HR911105A Magjack | Through-hole | ~$0.95 |
| - 25MHz Crystal | 3225 | ~$0.05 |
| - Passives | Terminations + decoupling | ~$0.10 |
| **Ethernet Subtotal** | | **~$1.35** |
| | | |
| **PoE Power** | | |
| - DP1435-5V Module | SIP-6 | ~$4.22 |
| - Input Cap 100µF 63V | Electrolytic | ~$0.10 |
| - Output Cap 100µF 10V | Ceramic/Electrolytic | ~$0.05 |
| **PoE Power Subtotal** | | **~$4.37** |
| | | |
| **Power Mux** | | |
| - SS34 × 2 | SMA | ~$0.03 |
| (Shared with core platform - may already be populated) | | |
| | | |
| **Total PoE Add-On (Module)** | | **~$5.72** |

### 8.2 Option B: Discrete Approach

| Category | Components | Cost |
|----------|------------|------|
| **Ethernet Data** | | |
| - SR8201F + Magjack + Crystal + Passives | | ~$1.35 |
| | | |
| **PoE Power (Discrete)** | | |
| - Si3404-A-GMR PD Controller | | ~$1.20 |
| - Flyback Transformer | | ~$0.80 |
| - PC817 Optocoupler | | ~$0.05 |
| - TL431 Reference | | ~$0.02 |
| - SS54 Output Rectifier | | ~$0.03 |
| - SMBJ58A TVS | | ~$0.05 |
| - Passives (caps, resistors) | | ~$0.40 |
| **PoE Power Subtotal** | | **~$2.55** |
| | | |
| **Power Mux** | | |
| - SS34 × 2 | | ~$0.03 |
| | | |
| **Total PoE Add-On (Discrete)** | | **~$3.93** |

### 8.3 Cost Comparison

| Approach | BOM Add | Retail Add | Margin | Development Effort |
|----------|---------|------------|--------|-------------------|
| Module (Option A) | ~$5.72 | +$30.00 | 81% | Low |
| Discrete (Option B) | ~$3.93 | +$30.00 | 87% | High |
| **Savings (Discrete)** | **$1.79/unit** | - | +6% | - |

**Break-even Analysis:**
- Engineering cost for discrete design: ~$5,000-10,000 (layout, validation, certification)
- Break-even volume: 2,800-5,600 units
- **Recommendation:** Use module for initial production; evaluate discrete for Rev 2 at >1000 units/month

---

## 9. Recommendations

### 9.1 Initial Production (Rev 1)

**Use Option A: DP1435-5V Integrated PD Module**

**Rationale:**
1. **Time-to-market** - No high-voltage design required
2. **Certification** - Pre-certified module reduces compliance risk
3. **Reliability** - Proven thermal and protection design
4. **Layout** - No isolation boundary expertise needed
5. **Low volume** - Cost optimization not justified yet

### 9.2 Future Optimization (Rev 2+)

**Evaluate Option B: Discrete Si3404 + Flyback**

**When to consider:**
- PoE variant reaches >1000 units/month
- Total production >5000 units expected
- Engineering resources available for validation
- Certification budget available (~$5,000-10,000)

### 9.3 Power Mux

**SS34 Schottky diode OR-ing is baselined for all variants.**

- Included in core platform BOM (populated on all units)
- Simple, reliable, cost-effective
- Adequate headroom for buck converter

---

## 10. Open Items

### 10.1 Resolved

- [x] MCU selection: ESP32-WROOM-32E (native EMAC/RMII)
- [x] Ethernet PHY: SR8201F (RMII, lowest cost)
- [x] Power mux: SS34 Schottky diode OR-ing
- [x] Buck converter: SY8089AAAC (2A, replaces LDO)
- [x] PoE approach: Module recommended for Rev 1

### 10.2 Pending

- [ ] Source DP1435-5V module - verify LCSC/AliExpress availability
- [ ] Verify SR8201F LCSC part number and stock
- [ ] If discrete: Select flyback transformer (Wurth 750315371 or equivalent)
- [ ] Validate thermal performance of module in RS-1 enclosure
- [ ] Pre-compliance testing with PoE injector
- [ ] GPIO 0 boot strap validation with SR8201F REF_CLK

---

## 11. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-15 | Hardware Engineering | Initial release |

---

## 12. References

| Document | Description |
|----------|-------------|
| IEEE 802.3af-2003 | PoE Standard (Type 1) |
| Si3404-A Datasheet | Silicon Labs PD Controller |
| DP1435-5V Datasheet | SDAPO Integrated PD Module |
| SY8089AAAC Datasheet | Silergy Buck Converter |
| SS34 Datasheet | MDD Schottky Diode |
| SR8201F Datasheet | CoreChips Ethernet PHY |
| HR911105A Datasheet | HanRun Magjack |
