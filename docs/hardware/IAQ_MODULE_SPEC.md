# RS-1 IAQ Module Specification

**Document Number:** HW-RS1-IAQ-001
**Version:** 1.0
**Date:** 2026-01-15
**Status:** Design Phase
**Owner:** OpticWorks Hardware Engineering

---

## 1. Overview

### 1.1 Purpose

This document specifies the Indoor Air Quality (IAQ) module for the RS-1 platform. The IAQ module is a **discrete, field-installable daughterboard** that adds TVOC and eCO2 sensing capability to any RS-1 variant.

### 1.2 Product Concept

The IAQ module is designed as a **snap-on accessory** that customers can purchase and install after buying an RS-1. Key design principles:

| Principle | Implementation |
|-----------|----------------|
| Field Installable | No tools required; magnetic attachment |
| Self-Contained | Own PCBA with sensor and interface |
| Zero Host Modification | RS-1 ships with pre-installed landing pads |
| Software Unlock | Firmware auto-detects and enables IAQ features |

### 1.3 Concept of Operations (CONOPs)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        IAQ Module Installation Flow                      │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  1. PURCHASE                                                             │
│     └─► Customer purchases IAQ module separately (+$35)                  │
│                                                                          │
│  2. UNBOX                                                                │
│     └─► Module ships with magnets pre-installed                          │
│     └─► Pogo pins exposed on mating surface                              │
│                                                                          │
│  3. INSTALL                                                              │
│     └─► Customer snaps module onto RS-1 enclosure                        │
│     └─► Magnets align module to correct position                         │
│     └─► Pogo pins contact ENIG landing pads through enclosure cutouts    │
│                                                                          │
│  4. AUTO-DETECT                                                          │
│     └─► RS-1 firmware polls I2C bus                                      │
│     └─► Detects ENS160 at address 0x52 or 0x53                           │
│     └─► Logs "IAQ module detected"                                       │
│                                                                          │
│  5. FIRMWARE UNLOCK                                                      │
│     └─► Device checks for IAQ entitlement via cloud                      │
│     └─► If authorized: Downloads IAQ firmware update                     │
│     └─► Enables: TVOC, eCO2, AQI entities in Home Assistant              │
│                                                                          │
│  6. OPERATION                                                            │
│     └─► ENS160 readings published every 1 second                         │
│     └─► If module removed: Entities become unavailable                   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.4 References

| Document | Description |
|----------|-------------|
| HARDWARE_SPEC.md | RS-1 host hardware specification |
| RS-1_Unified_BOM.md | RS-1 bill of materials |
| HARDWAREOS_MODULE_IAQ.md | Firmware module M12 specification |
| ENS160 Datasheet | ScioSense sensor datasheet |

---

## 2. System Architecture

### 2.1 Module Block Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         IAQ Module PCBA                                  │
│                         (2-Layer PCB)                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │                        Top Layer                                 │   │
│   │                                                                  │   │
│   │    ┌──────────────────────┐                                     │   │
│   │    │                      │                                     │   │
│   │    │       ENS160         │  TVOC / eCO2 Sensor                 │   │
│   │    │      (DFN-10)        │  I2C Interface                      │   │
│   │    │                      │                                     │   │
│   │    └──────────────────────┘                                     │   │
│   │                                                                  │   │
│   │    ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐                          │   │
│   │    │ P1 │ │ P2 │ │ P3 │ │ P4 │ │ P5 │   Male Pogo Pins         │   │
│   │    │VCC │ │GND │ │SDA │ │SCL │ │INT │   (Spring-loaded)        │   │
│   │    └──┬─┘ └──┬─┘ └──┬─┘ └──┬─┘ └──┬─┘                          │   │
│   │       │      │      │      │      │                             │   │
│   └───────┼──────┼──────┼──────┼──────┼─────────────────────────────┘   │
│           │      │      │      │      │                                  │
│           ▼      ▼      ▼      ▼      ▼                                  │
│       To RS-1 Host (via enclosure cutouts)                               │
│                                                                          │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │                      Bottom Layer                                │   │
│   │                                                                  │   │
│   │    ┌─────┐     ┌─────┐                                          │   │
│   │    │ M1  │     │ M2  │   Retention Magnets                      │   │
│   │    │     │     │     │   (Neodymium, press-fit)                 │   │
│   │    └─────┘     └─────┘                                          │   │
│   │                                                                  │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Host Interface (RS-1 Side)

The RS-1 host provides the mating interface:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         RS-1 Host Interface                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   RS-1 PCB (Main Board)                                                  │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │                                                                  │   │
│   │    ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐                          │   │
│   │    │ L1 │ │ L2 │ │ L3 │ │ L4 │ │ L5 │   ENIG Landing Pads      │   │
│   │    │VCC │ │GND │ │SDA │ │SCL │ │INT │   (Gold plated, 1.5mm Ø) │   │
│   │    └────┘ └────┘ └────┘ └────┘ └────┘                          │   │
│   │                                                                  │   │
│   │    ┌─────┐     ┌─────┐                                          │   │
│   │    │ SM1 │     │ SM2 │   Steel Target Pads                      │   │
│   │    │     │     │     │   (For magnet attraction)                │   │
│   │    └─────┘     └─────┘                                          │   │
│   │                                                                  │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                          │
│   RS-1 Enclosure                                                         │
│   ┌─────────────────────────────────────────────────────────────────┐   │
│   │                                                                  │   │
│   │    ╔════╗ ╔════╗ ╔════╗ ╔════╗ ╔════╗   Pin Cutouts             │   │
│   │    ║    ║ ║    ║ ║    ║ ║    ║ ║    ║   (Through enclosure)     │   │
│   │    ╚════╝ ╚════╝ ╚════╝ ╚════╝ ╚════╝   2.0mm Ø holes           │   │
│   │                                                                  │   │
│   │    ┌─────┐     ┌─────┐                                          │   │
│   │    │     │     │     │   Magnet Pockets                         │   │
│   │    │     │     │     │   (Recessed for flush mount)             │   │
│   │    └─────┘     └─────┘                                          │   │
│   │                                                                  │   │
│   └─────────────────────────────────────────────────────────────────┘   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.3 Mated Assembly

```
                    IAQ Module
                        │
                        ▼
    ┌───────────────────────────────────────┐
    │           IAQ Module PCBA             │
    │  ┌─────────────────────────────────┐  │
    │  │          ENS160                 │  │
    │  └─────────────────────────────────┘  │
    │       P1   P2   P3   P4   P5          │  ◄── Pogo pins extend
    │       ║    ║    ║    ║    ║           │      through enclosure
    │    [M1]                    [M2]       │  ◄── Magnets on bottom
    └───────╫────╫────╫────╫────╫───────────┘
            ║    ║    ║    ║    ║
    ════════╬════╬════╬════╬════╬═══════════════  RS-1 Enclosure Surface
            ║    ║    ║    ║    ║               (Pin cutouts aligned)
    ┌───────╫────╫────╫────╫────╫───────────┐
    │       L1   L2   L3   L4   L5          │  ◄── ENIG landing pads
    │    [SM1]                  [SM2]       │  ◄── Steel targets
    │  ┌─────────────────────────────────┐  │
    │  │          RS-1 PCB               │  │
    │  │    (ESP32, Radars, Sensors)     │  │
    │  └─────────────────────────────────┘  │
    └───────────────────────────────────────┘
                        │
                        ▼
                    RS-1 Host
```

---

## 3. Electrical Specifications

### 3.1 Power Requirements

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Supply Voltage (VCC) | 1.71 | 1.8 | 1.98 | V |
| Supply Current (Measurement) | - | 32 | - | mA |
| Supply Current (Idle) | - | 10 | - | µA |

**Note:** ENS160 operates at 1.8V. RS-1 provides 3.3V; a voltage regulator or level shifter may be required on the IAQ module.

### 3.2 I2C Interface

| Parameter | Value | Notes |
|-----------|-------|-------|
| I2C Address | 0x52 or 0x53 | Selectable via ADDR pin |
| I2C Speed | Standard (100kHz) or Fast (400kHz) | |
| Pull-ups | External required | On RS-1 host (shared bus) |
| SDA/SCL Voltage | 1.8V or 3.3V | Level shifter if needed |

### 3.3 Interrupt Pin (Optional)

| Parameter | Value |
|-----------|-------|
| INT Function | Data ready / threshold alert |
| Logic Level | Active low, open-drain |
| Required | No (polling supported) |

### 3.4 Pin Assignment

| Pin # | Name | Direction | Description |
|-------|------|-----------|-------------|
| P1 | VCC | Power In | 3.3V supply from host |
| P2 | GND | Power | Ground reference |
| P3 | SDA | Bidirectional | I2C data |
| P4 | SCL | Input | I2C clock |
| P5 | INT | Output (optional) | Interrupt / not connected |

---

## 4. Bill of Materials

### 4.1 IAQ Module BOM

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| U1 | ScioSense | ENS160-BGLM | TVOC/eCO2 Sensor, DFN-10 | 1 | $4.60 |
| U2 | TBD | TBD | LDO 3.3V→1.8V, SOT-23-5 (if needed) | 1 | $0.03 |
| P1-P5 | Mill-Max | 0906-2-15-20-75-14-11-0 | Pogo Pin, Spring-Loaded | 5 | $0.08 ea |
| M1-M2 | Generic | N35 3mm×2mm | Neodymium Magnet, Disc | 2 | $0.02 ea |
| C1 | Samsung | CL05A104KA5NNNC | MLCC, 100nF, 10V, 0402 | 1 | $0.01 |
| C2 | Samsung | CL05A106MQ5NUNC | MLCC, 10µF, 6.3V, 0402 | 1 | $0.02 |
| PCB | JLCPCB | - | 2-Layer, 15×20mm, 0.8mm | 1 | $0.10 |

### 4.2 BOM Cost Summary

| Category | Cost |
|----------|------|
| ENS160 Sensor | $4.60 |
| Pogo Pins (5×) | $0.40 |
| Magnets (2×) | $0.04 |
| LDO + Passives | $0.06 |
| PCB | $0.10 |
| **Total Module BOM** | **~$5.20** |

### 4.3 Retail Pricing

| Parameter | Value |
|-----------|-------|
| Module BOM | ~$5.20 |
| Packaging + Labor | ~$1.00 |
| **Total COGS** | **~$6.20** |
| **Retail Price** | **$35.00** |
| **Gross Margin** | **82%** |

---

## 5. Mechanical Specifications

### 5.1 Module Dimensions

| Parameter | Value | Tolerance |
|-----------|-------|-----------|
| Length | 20.0 mm | ±0.2 mm |
| Width | 15.0 mm | ±0.2 mm |
| Height (without pins) | 3.0 mm | ±0.2 mm |
| Height (with pins extended) | 6.0 mm | ±0.3 mm |
| PCB Thickness | 0.8 mm | ±0.1 mm |

### 5.2 Pogo Pin Specifications

| Parameter | Value |
|-----------|-------|
| Pin Model | Mill-Max 0906-2-15-20-75-14-11-0 |
| Travel | 1.5 mm |
| Spring Force | 75g @ 1.0mm compression |
| Contact Resistance | <50 mΩ |
| Durability | >100,000 cycles |
| Pin Diameter (barrel) | 1.02 mm (0.040") |
| Pin Diameter (tip) | 0.64 mm (0.025") |

### 5.3 Pin Layout

```
    ┌─────────────────────────────────────┐
    │         IAQ Module (Top View)        │
    │                                      │
    │           ┌──────────┐               │
    │           │  ENS160  │               │
    │           │          │               │
    │           └──────────┘               │
    │                                      │
    │    [M1]                    [M2]      │  Magnets
    │                                      │
    │     P1    P2    P3    P4    P5       │  Pogo Pins
    │      ●     ●     ●     ●     ●       │
    │                                      │
    │    ◄─2.54mm─►                        │  Pin pitch
    │                                      │
    └─────────────────────────────────────┘

    Pin Coordinates (origin = center of P3):

    | Pin | X (mm) | Y (mm) | Signal |
    |-----|--------|--------|--------|
    | P1  | -5.08  |  0.0   | VCC    |
    | P2  | -2.54  |  0.0   | GND    |
    | P3  |  0.0   |  0.0   | SDA    |
    | P4  | +2.54  |  0.0   | SCL    |
    | P5  | +5.08  |  0.0   | INT    |
```

### 5.4 Magnet Specifications

| Parameter | Value |
|-----------|-------|
| Type | Neodymium (NdFeB), Grade N35 |
| Shape | Disc |
| Diameter | 3.0 mm |
| Height | 2.0 mm |
| Pull Force | ~300g each |
| Coating | Nickel (Ni-Cu-Ni) |
| Operating Temp | -40°C to +80°C |

### 5.5 Magnet Placement

```
    ┌─────────────────────────────────────┐
    │         Module Bottom View           │
    │                                      │
    │    ┌───┐                    ┌───┐   │
    │    │M1 │                    │M2 │   │  Magnets (press-fit pockets)
    │    │ ● │                    │ ● │   │
    │    └───┘                    └───┘   │
    │                                      │
    │   Center: 4.0mm from edge           │
    │   Spacing: 12.0mm center-to-center  │
    │                                      │
    └─────────────────────────────────────┘
```

---

## 6. Host Interface Requirements

### 6.1 RS-1 PCB Requirements

The RS-1 host PCB must include:

| Feature | Specification |
|---------|---------------|
| Landing Pads | 5× ENIG pads, 1.5mm diameter |
| Pad Pitch | 2.54mm (0.1") center-to-center |
| Pad Finish | ENIG (Electroless Nickel Immersion Gold) |
| Steel Targets | 2× steel pads for magnet attraction, 4mm diameter |
| I2C Connection | Routed to shared I2C bus (GPIO 21/22) |
| Power Connection | 3.3V rail with 50mA capacity |

### 6.2 RS-1 Enclosure Requirements

The RS-1 enclosure must include:

| Feature | Specification |
|---------|---------------|
| Pin Cutouts | 5× holes, 2.0mm diameter, aligned with landing pads |
| Magnet Pockets | 2× recesses, 3.5mm diameter × 2.5mm deep |
| Alignment Features | Edge lip or registration marks for module positioning |
| Mounting Surface | Flat area ≥25mm × 20mm for module |
| Material | ABS or PC/ABS (non-magnetic) |

### 6.3 Enclosure Cutout Drawing

```
    RS-1 Enclosure (External Surface)
    ┌─────────────────────────────────────────────────────────────┐
    │                                                             │
    │     IAQ Module Mounting Area                                │
    │     ┌───────────────────────────────────┐                   │
    │     │                                   │                   │
    │     │    ○     ○     ○     ○     ○      │  Pin cutouts      │
    │     │   2.0mm diameter                  │  (through holes)  │
    │     │                                   │                   │
    │     │    ◐                    ◐         │  Magnet pockets   │
    │     │   3.5mm diameter                  │  (2.5mm deep)     │
    │     │                                   │                   │
    │     └───────────────────────────────────┘                   │
    │                                                             │
    │     Alignment edge / lip                                    │
    │                                                             │
    └─────────────────────────────────────────────────────────────┘
```

---

## 7. Sensor Specifications

### 7.1 ENS160 Overview

| Parameter | Value |
|-----------|-------|
| Manufacturer | ScioSense |
| Part Number | ENS160-BGLM |
| Technology | Metal Oxide (MOx) gas sensor |
| Package | DFN-10 (3.0 × 3.0 × 0.9 mm) |
| Interface | I2C or SPI (I2C used) |

### 7.2 Measurement Capabilities

| Measurement | Range | Accuracy | Resolution |
|-------------|-------|----------|------------|
| TVOC | 0 - 65,000 ppb | ±15% | 1 ppb |
| eCO2 | 400 - 65,000 ppm | ±15% | 1 ppm |
| AQI | 1 - 5 | - | 1 |
| Ethanol | 0 - 1,000 ppm | - | 1 ppm |

### 7.3 Operating Conditions

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Temperature | -40 | 25 | 85 | °C |
| Humidity | 5 | 50 | 95 | %RH |
| Warm-up Time | - | 3 | - | minutes |
| Conditioning Time | - | 48 | - | hours |

### 7.4 Calibration

The ENS160 includes on-chip auto-calibration:
- Baseline calibration every 24 hours
- Automatic adjustment to clean air baseline
- Requires periodic exposure to clean air for accurate readings

---

## 8. Assembly and Installation

### 8.1 Module Assembly (Manufacturing)

1. **SMT Assembly**
   - Place ENS160, LDO, passives using standard pick-and-place
   - Reflow solder (lead-free profile)

2. **Pogo Pin Installation**
   - Press-fit pogo pins into plated through-holes
   - Verify pin height uniformity (±0.1mm)

3. **Magnet Installation**
   - Press-fit magnets into PCB pockets (adhesive optional)
   - Verify polarity consistency across production

4. **Testing**
   - Power-on test (verify VCC/GND continuity)
   - I2C communication test (read ENS160 ID register)
   - Functional test (TVOC reading in ambient air)

### 8.2 User Installation (Field)

1. **Preparation**
   - Power off RS-1 (optional but recommended)
   - Remove any debris from RS-1 mounting area

2. **Alignment**
   - Position IAQ module over RS-1 mounting area
   - Align module edge with RS-1 enclosure alignment feature

3. **Attachment**
   - Allow magnets to pull module into position
   - Verify module is flush with enclosure surface

4. **Verification**
   - Power on RS-1
   - Check Home Assistant for IAQ entities (after OTA update)

### 8.3 Removal

1. **Slide or lift** module edge to break magnetic seal
2. Do not pull directly upward (pogo pins may catch)
3. Module can be reinstalled on same or different RS-1

---

## 9. Quality Requirements

### 9.1 Incoming Inspection

| Component | Test | Criteria |
|-----------|------|----------|
| ENS160 | Visual + I2C ID read | ID = 0x0160 |
| Pogo Pins | Dimensional | Height ±0.1mm |
| Magnets | Pull force | >250g each |
| PCB | Visual + continuity | No shorts |

### 9.2 End-of-Line Testing

| Test | Method | Pass Criteria |
|------|--------|---------------|
| I2C Communication | Read ID register | 0x0160 returned |
| TVOC Measurement | Ambient air | 0-500 ppb (office) |
| eCO2 Measurement | Ambient air | 400-800 ppm (office) |
| Magnet Attachment | Pull test | >400g total |
| Pin Contact | Continuity | <100 mΩ each |

### 9.3 Reliability Testing

| Test | Specification |
|------|---------------|
| Temperature Cycling | -20°C to +60°C, 100 cycles |
| Humidity Exposure | 85°C / 85% RH, 168 hours |
| Mechanical Shock | 50g, 11ms, 3 axes |
| Insertion Cycles | 1,000 attach/detach cycles |
| Vibration | 10-500 Hz, 1g RMS, 1 hour |

---

## 10. Regulatory Considerations

### 10.1 Certifications

| Certification | Requirement | Notes |
|---------------|-------------|-------|
| RoHS | Required | All components compliant |
| REACH | Required | No SVHC substances |
| CE | TBD | May not require separate cert (accessory) |
| FCC | TBD | No intentional radiator; likely exempt |

### 10.2 Safety

- No hazardous materials
- Magnets pose choking hazard (include warning for children)
- No user-serviceable parts

---

## 11. Open Items

### 11.1 Resolved

- [x] Sensor selection: ENS160-BGLM
- [x] Interface: Pogo pins on module, ENIG pads on host
- [x] Attachment: Magnetic retention

### 11.2 Pending

- [ ] Finalize LDO requirement (1.8V for ENS160 vs 3.3V operation mode)
- [ ] Select specific pogo pin part number (Mill-Max 0906 series baseline)
- [ ] Define exact enclosure cutout positions (pending RS-1 enclosure design)
- [ ] Verify I2C address selection mechanism
- [ ] Create mechanical drawing (DXF/STEP)
- [ ] Prototype and validate magnetic attachment force

---

## 12. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-15 | Hardware Engineering | Initial release |

---

## 13. References

| Document | Description |
|----------|-------------|
| ENS160 Datasheet | ScioSense sensor specification |
| Mill-Max 0906 Series | Pogo pin specifications |
| HARDWARE_SPEC.md | RS-1 host hardware specification |
| HARDWAREOS_MODULE_IAQ.md | Firmware module specification |
