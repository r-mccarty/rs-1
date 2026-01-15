# RS-1 Hardware Specification

**Document Number:** HW-RS1-001
**Version:** 1.0
**Date:** 2026-01-14
**Status:** Pre-Production
**Owner:** OpticWorks Hardware Engineering

---

## 1. Document Purpose

This specification defines the hardware requirements, bill of materials, electrical characteristics, and mechanical constraints for the OpticWorks RS-1 presence sensor platform. This document serves as the authoritative reference for:

- PCB design and layout
- Component procurement
- Manufacturing assembly
- Quality assurance testing
- Regulatory compliance

---

## 2. Product Overview

### 2.1 Product Description

The RS-1 is a mmWave radar-based presence detection sensor platform designed for smart home integration. The platform uses a single PCBA design with selective component population to support multiple product variants.

### 2.2 Design Philosophy

| Principle | Implementation |
|-----------|----------------|
| Single PCBA | One PCB design supports all variants via selective population |
| Modular Architecture | Add-on features (PoE, IAQ) via optional components |
| Local-First | Full functionality without cloud dependency |
| ESPHome Native | Direct Home Assistant integration via Native API |

### 2.3 Target Specifications

| Parameter | Specification |
|-----------|---------------|
| Detection Range | Up to 6 meters |
| Field of View | 120° horizontal × 60° vertical (LD2450) |
| Max Tracked Targets | 3 simultaneous (Pro variant) |
| Response Time | < 1 second |
| Operating Temperature | 0°C to 50°C |
| Storage Temperature | -20°C to 70°C |

---

## 3. System Architecture

### 3.1 Block Diagram

```
                                    ┌─────────────────────────────────────────┐
                                    │              RS-1 PCBA                  │
                                    │                                         │
   ┌─────────┐                      │  ┌─────────────────────────────────┐    │
   │ USB-C   │◄────────────────────►│  │       ESP32-WROOM-32E           │    │
   │  5V/    │     USB Data         │  │         + CH340N                │    │
   │  Data   │     (via CH340N)     │  │  ┌─────────┐  ┌─────────────┐  │    │
   └─────────┘                      │  │  │  WiFi   │  │  Bluetooth  │  │    │
                                    │  │  │ 802.11  │  │    4.2      │  │    │
   ┌─────────┐                      │  │  │  b/g/n  │  │             │  │    │
   │  PoE    │◄────────────────────►│  │  └─────────┘  └─────────────┘  │    │
   │ 802.3af │  Ethernet + Power    │  │                                 │    │
   │(Option) │  (RMII PHY + Si3404) │  └────────────┬────────────────────┘    │
   └─────────┘                      │               │                         │
                                    │               │ GPIO / UART / I2C       │
                                    │               ▼                         │
                                    │  ┌───────────────────────────────────┐  │
                                    │  │          Peripherals              │  │
                                    │  │                                   │  │
                                    │  │  ┌─────────┐  ┌─────────┐        │  │
                                    │  │  │ LD2410  │  │ LD2450  │        │  │
                                    │  │  │ Static  │  │Tracking │        │  │
                                    │  │  │ (UART)  │  │ (UART)  │        │  │
                                    │  │  └─────────┘  └─────────┘        │  │
                                    │  │                                   │  │
                                    │  │  ┌─────────┐  ┌─────────┐        │  │
                                    │  │  │  AHT20  │  │ LTR-303 │        │  │
                                    │  │  │Temp/Hum │  │   Lux   │        │  │
                                    │  │  │  (I2C)  │  │  (I2C)  │        │  │
                                    │  │  └─────────┘  └─────────┘        │  │
                                    │  │                                   │  │
                                    │  │  ┌─────────┐                        │  │
                                    │  │  │ WS2812  │                        │  │
                                    │  │  │   LED   │                        │  │
                                    │  │  │ (GPIO)  │                        │  │
                                    │  │  └─────────┘                        │  │
                                    │  │                                   │  │
                                    │  │  ┌─────────┐                     │  │
                                    │  │  │ ENS160  │  (Daughtercard)     │  │
                                    │  │  │  IAQ    │                     │  │
                                    │  │  │  (I2C)  │                     │  │
                                    │  │  └─────────┘                     │  │
                                    │  └───────────────────────────────────┘  │
                                    └─────────────────────────────────────────┘
```

### 3.2 Variant Matrix

| Component | RS-1 Lite | RS-1 Pro |
|-----------|:---------:|:--------:|
| ESP32-WROOM-32E + CH340N | Required | Required |
| USB-C Power/Data | Required | Required |
| AHT20 (Temp/Hum) | Required | Required |
| LTR-303 (Lux) | Required | Required |
| WS2812 Status LED | Required | Required |
| Reset Button | Required | Required |
| LD2410B/C (Static Radar) | **Required** | **Required** |
| LD2450 (Tracking Radar) | - | **Required** |
| PoE Components | Optional | Optional |
| IAQ Daughtercard | Optional | Optional |

---

## 4. Bill of Materials

### 4.1 Core Platform (All Variants)

#### 4.1.1 Microcontroller

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| U1 | Espressif | ESP32-WROOM-32E-N8 | MCU Module, 8MB Flash | 1 | $3.0011 |
| U1-USB | WCH | CH340N | USB-UART Bridge | 1 | $0.3425 |

**Core Cost:** $3.3436 (MCU + USB bridge)

**Specifications:**
- Architecture: Xtensa LX6 dual-core, 240 MHz
- Flash: 8MB (Quad SPI)
- SRAM: 520KB
- GPIO: 34 programmable
- Interfaces: EMAC/RMII, SPI, I2C, I2S, UART, SDIO
- Wireless: WiFi 802.11 b/g/n, Bluetooth 4.2 (Classic + LE)
- Operating Voltage: 3.0V - 3.6V
- Operating Temperature: -40°C to +85°C

**Why ESP32-WROOM-32E:**
- Native EMAC/RMII support enables low-cost Ethernet PHY for PoE variants
- Proven, mature platform with excellent ESP-IDF support
- Lower total BOM cost vs ESP32-S3 + SPI Ethernet
- 8MB flash provides OTA headroom
- Sufficient GPIO for RS-1 Pro (dual radar + Ethernet + sensors)

**Note:** Requires CH340N USB-UART bridge for USB-C data connectivity (no native USB).

#### 4.1.2 Power Supply

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| J1 | SHOU HAN | TYPE-C 16PIN 2MD(073) | USB-C Connector, 16-pin | 1 | $0.15 |
| U2 | MICRONE | ME6211C33M5G-N | LDO Regulator, 3.3V, 500mA, SOT-23-5 | 1 | $0.08 |
| C1-C4 | CCTC | TCC0603X7R104K500CT | MLCC, 100nF, 50V, X7R, 0603 | 4 | $0.01 |
| C5-C6 | Samsung | CL05A106MQ5NUNC | MLCC, 10µF, 6.3V, X5R, 0402 | 2 | $0.02 |
| R1-R2 | UNI-ROYAL | 0603WAF330JT5E | Resistor, 33Ω, 1%, 0603 (USB) | 2 | $0.01 |
| R3-R4 | UNI-ROYAL | 0603WAF5101T5E | Resistor, 5.1kΩ, 1%, 0603 (USB CC) | 2 | $0.01 |
| Q1 | UMW | AO3401A | P-Channel MOSFET, -30V, SOT-23 | 1 | $0.03 |

**Power Specifications:**
| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Input Voltage (USB) | 4.5 | 5.0 | 5.5 | V |
| System Voltage | 3.2 | 3.3 | 3.4 | V |
| Current (Idle) | - | 80 | 120 | mA |
| Current (WiFi TX) | - | 310 | 380 | mA |
| Current (Peak) | - | - | 500 | mA |

#### 4.1.3 Environmental Sensors

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| U3 | Aosong | AHT20 | Temperature & Humidity, I2C, DFN-4 | 1 | $0.65 |
| U4 | LITEON | LTR-303ALS-01 | Ambient Light Sensor, I2C | 1 | $0.45 |

**AHT20 Specifications:**
| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Temperature Range | -40 | - | +85 | °C |
| Temperature Accuracy | - | ±0.3 | - | °C |
| Humidity Range | 0 | - | 100 | %RH |
| Humidity Accuracy | - | ±2 | - | %RH |
| I2C Address | - | 0x38 | - | - |

**LTR-303 Specifications:**
| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Lux Range | 0.01 | - | 65535 | lux |
| I2C Address | - | 0x29 | - | - |

#### 4.1.4 User Interface

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| D1 | XINGLIGHT | XL-5050RGBC-2812B-S | RGB LED, WS2812 compatible, 5050 | 1 | $0.08 |
| SW1 | XKB Connection | TS-1187A-B-A-B | Tactile Switch, Reset | 1 | $0.02 |

#### 4.1.5 Debug/Programming (DNP - Do Not Populate)

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| J2 | Generic | SOICbite footprint | UART Debug Header | 1 | DNP |
| J3 | Ckmtw | B-2100S02P-A110 | Generic Header, 2-pin | 1 | DNP |

---

### 4.2 Radar Modules

#### 4.2.1 Static Presence Radar (RS-1 Lite, RS-1 Pro)

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| MOD1 | Hi-Link | LD2410B | 24GHz mmWave Presence Radar | 1 | $2.80 |

**LD2410B Specifications:**
| Parameter | Value | Unit |
|-----------|-------|------|
| Frequency | 24.00 - 24.25 | GHz |
| Detection Range (Moving) | 0.75 - 6.0 | m |
| Detection Range (Static) | 0.75 - 4.5 | m |
| Field of View (H) | ±60 | ° |
| Field of View (V) | ±60 | ° |
| Interface | UART | - |
| Baud Rate | 256000 | bps |
| Operating Voltage | 5.0 | V |
| Current Consumption | 100 | mA |

**Alternate Parts:**
| Part Number | Description | Notes |
|-------------|-------------|-------|
| LD2410C | Enhanced sensitivity variant | Pin-compatible |
| LD2412 | Extended range variant | Verify footprint |

#### 4.2.2 Multi-Target Tracking Radar (RS-1 Pro)

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| MOD2 | Hi-Link | LD2450 | 24GHz mmWave Multi-Target Radar | 1 | $11.50 |

**LD2450 Specifications:**
| Parameter | Value | Unit |
|-----------|-------|------|
| Frequency | 24.00 - 24.25 | GHz |
| Detection Range | 0 - 6.0 | m |
| Coordinate Range X | ±6000 | mm |
| Coordinate Range Y | 0 - 6000 | mm |
| Max Tracked Targets | 3 | - |
| Field of View (H) | 120 | ° |
| Field of View (V) | 60 | ° |
| Frame Rate | 33 | Hz |
| Frame Size | 40 | bytes |
| Interface | UART | - |
| Baud Rate | 256000 | bps |
| Operating Voltage | 5.0 | V |
| Current Consumption | 150 | mA |

---

### 4.3 PoE Option

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| U6 | CoreChips | SR8201F | Ethernet PHY, 10/100Mbps, RMII | 1 | $0.25 |
| U7 | Silicon Labs | Si3404-A-GMR | PoE PD Controller, Isolated Flyback | 1 | $1.20 |
| J4-ALT | HanRun | HR911105A | RJ45 Connector with Magnetics (magjack) | 1 | $0.95 |
| Y1 | YXC | X322525MOB4SI | Crystal, 25MHz, 20ppm, 3225 | 1 | $0.05 |
| Passives | - | - | Terminations + decoupling | - | $0.10 |

**PHY Alternatives:**
| Part Number | Manufacturer | Price | Notes |
|-------------|--------------|-------|-------|
| SR8201F | CoreChips | ~$0.25 | **Preferred** - lowest cost |
| IP101GRR | IC+ | ~$0.27 | Alternative |
| RTL8201F-VB-CG | Realtek | ~$0.41 | Higher cost |

**Notes:**
- ESP32-WROOM-32E has native EMAC/RMII, enabling low-cost PHY (no SPI Ethernet controller needed)
- SR8201F is the preferred PHY due to lowest cost
- Magjack (integrated magnetics) is baselined; external magnetics + bare RJ45 is an alternative
- Data-only Ethernet BOM: ~$1.35 (PHY + magjack + crystal + passives)
- PoE power architecture: PD module (~$4.22) or discrete Si3404 + flyback (TBD)

**PoE Specifications:**
| Parameter | Value | Unit |
|-----------|-------|------|
| IEEE Standard | 802.3af (Type 1) | - |
| Input Voltage | 37 - 57 | VDC |
| Power Class | 0-3 | - |
| Max Power | 12.95 | W |
| Isolation | 1500 | VAC |

**Safety Note:** Isolation does not prevent USB back-powering. A power mux is required for USB + PoE safety.

---

### 4.4 IAQ Option (Daughtercard)

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| U8 | ScioSense | ENS160-BGLM | TVOC/eCO2 Sensor, I2C | 1 | $4.60 |
| POGO1-5 | Generic | 0906-2-15-20-75-14-11-0 | Pogo Pins, PWR/GND/I2C | 5 | $0.40 |

**ENS160 Specifications:**
| Parameter | Value | Unit |
|-----------|-------|------|
| TVOC Range | 0 - 65000 | ppb |
| eCO2 Range | 400 - 65000 | ppm |
| I2C Address | 0x52 or 0x53 | - |
| Operating Voltage | 1.71 - 1.98 | V |
| Current (Measurement) | 32 | mA |

**Mechanical:** Daughtercard connects via pogo pins with magnet retention system.

---

## 5. BOM Summary by Variant

**Note:** BOM summaries use ESP32-WROOM-32E + CH340N core ($3.34).

### 5.1 RS-1 Lite

| Category | Components | Est. Cost |
|----------|------------|-----------|
| Core Platform | ESP32-WROOM-32E + CH340N, Power, Sensors, UI | $4.93 |
| LD2410B Radar | Radar module | $2.80 |
| **Total** | | **~$7.73** |

**Target Retail:** $49.00

**Use Case:** Utility rooms - bathrooms, hallways, closets. "I exist" detection.

### 5.2 RS-1 Pro

| Category | Components | Est. Cost |
|----------|------------|-----------|
| Core Platform | ESP32-WROOM-32E + CH340N, Power, Sensors, UI | $4.93 |
| LD2410B Radar | Static presence | $2.80 |
| LD2450 Radar | Multi-target tracking | $11.50 |
| **Total** | | **~$19.23** |

**Target Retail:** $89.00

**Use Case:** Living spaces - living rooms, kitchens, bedrooms. Zone tracking and motion detection.

### 5.3 Add-On Pricing

| Option | BOM Add | Retail Add |
|--------|---------|------------|
| PoE | +$4.15 to +$5.68 | +$30.00 |
| IAQ | +$5.00 | +$35.00 |

### 5.4 Configuration Examples

| Configuration | BOM Est. | Retail |
|---------------|----------|--------|
| RS-1 Lite (USB) | $7.73 | $49.00 |
| RS-1 Pro (USB) | $19.23 | $89.00 |
| RS-1 Lite + PoE | $11.88 to $13.41 | $79.00 |
| RS-1 Pro + PoE | $23.38 to $24.91 | $119.00 |
| RS-1 Pro + PoE + IAQ | $28.38 to $29.91 | $154.00 |

*Note: BOM costs based on 100-unit pricing; costs decrease with volume.*

---

## 6. Electrical Specifications

### 6.1 Absolute Maximum Ratings

| Parameter | Min | Max | Unit |
|-----------|-----|-----|------|
| USB Input Voltage | -0.3 | 6.0 | V |
| PoE Input Voltage | - | 60 | VDC |
| GPIO Voltage | -0.3 | 3.6 | V |
| Operating Temperature | -20 | 70 | °C |
| Storage Temperature | -40 | 85 | °C |
| ESD (HBM) | - | 2000 | V |

### 6.2 Operating Conditions

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Supply Voltage (USB) | 4.5 | 5.0 | 5.5 | V |
| Supply Voltage (PoE) | 37 | 48 | 57 | VDC |
| Operating Temperature | 0 | 25 | 50 | °C |
| Relative Humidity | 10 | - | 90 | %RH |

### 6.3 Power Consumption

| Mode | Typical | Max | Unit |
|------|---------|-----|------|
| Idle (WiFi Connected) | 80 | 120 | mA |
| Active (WiFi TX) | 310 | 380 | mA |
| Peak (WiFi TX + Radar) | 450 | 550 | mA |
| Deep Sleep | - | 10 | µA |

---

## 7. Pin Assignments

### 7.1 ESP32-WROOM-32E GPIO Allocation

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 0 | Boot Select | Input | Active Low, strapping pin |
| 1 | UART0 TX | Output | USB-UART (CH340N) |
| 3 | UART0 RX | Input | USB-UART (CH340N) |
| 4 | LD2410 UART TX | Output | Static radar (UART1) |
| 5 | LD2410 UART RX | Input | Static radar (UART1) |
| 16 | LD2450 UART TX | Output | Tracking radar (UART2, Pro) |
| 17 | LD2450 UART RX | Input | Tracking radar (UART2, Pro) |
| 21 | I2C SDA | Bidirectional | Sensors |
| 22 | I2C SCL | Output | Sensors |
| 25 | WS2812 Data | Output | Status LED |
| 34 | Reset Button | Input | Input-only, Pull-up external |

**RMII Ethernet (PoE variants):**

| GPIO | RMII Function | Notes |
|------|---------------|-------|
| 18 | MDIO | PHY management data |
| 23 | MDC | PHY management clock |
| 19 | TXD0 | Transmit data 0 |
| 22 | TXD1 | Transmit data 1 |
| 21 | TX_EN | Transmit enable |
| 25 | RXD0 | Receive data 0 |
| 26 | RXD1 | Receive data 1 |
| 27 | CRS_DV | Carrier sense / Data valid |
| 0 | REF_CLK | 50MHz reference clock (input from PHY) |

**Note:** RMII pins overlap with some peripheral pins. PoE variant requires careful GPIO reassignment for I2C and LED.

### 7.2 I2C Bus Devices

| Address | Device | Function |
|---------|--------|----------|
| 0x29 | LTR-303 | Ambient Light |
| 0x38 | AHT20 | Temp/Humidity |
| 0x52 | ENS160 | Air Quality (optional) |

### 7.3 UART Configuration

| Interface | Baud Rate | Data Bits | Parity | Stop Bits |
|-----------|-----------|-----------|--------|-----------|
| UART1 (LD2410) | 256000 | 8 | None | 1 |
| UART2 (LD2450) | 256000 | 8 | None | 1 |

---

## 8. PCB Requirements

### 8.1 General Specifications

| Parameter | Specification |
|-----------|---------------|
| Layers | 2 |
| Thickness | 1.6mm |
| Copper Weight | 1oz (35µm) |
| Surface Finish | HASL or ENIG |
| Solder Mask | Green |
| Silkscreen | White |
| Min Trace Width | 0.15mm (6mil) |
| Min Trace Space | 0.15mm (6mil) |
| Min Via Drill | 0.3mm |

### 8.2 Layout Guidelines

1. **Radar Placement:** Place LD2410 and LD2450 at opposite ends of PCB to minimize interference (Pro variant)
2. **Antenna Keepout:** 15mm clearance around ESP32-WROOM-32E antenna area
3. **Power Planes:** Solid ground plane on Layer 2
4. **USB Routing:** Route USB D+/D- to CH340N (not differential; CH340N is full-speed USB)
5. **RMII Routing:** Matched length for RMII signals (PoE variants)
6. **Isolation:** 3mm minimum creepage for PoE isolated section

### 8.3 Thermal Considerations

| Component | Thermal Pad | Notes |
|-----------|-------------|-------|
| ESP32-WROOM-32E | Yes | Via stitching to ground |
| SY8089 Buck | Thermal relief | Place near input capacitor |
| Si3404 | Required | Exposed pad to ground |

---

## 9. Mechanical Specifications

### 9.1 Enclosure Requirements

| Parameter | Specification |
|-----------|---------------|
| Material | ABS or PC/ABS |
| Color | White (TBD) |
| IP Rating | IP20 (indoor use) |
| Mounting | Wall or ceiling mount |
| Radar Window | RF-transparent material |

### 9.2 Mounting

| Mount Type | Specification |
|------------|---------------|
| Screw Mount | 2× M3 mounting holes |
| Adhesive | 3M VHB compatible surface |
| Bracket | Optional wall bracket (accessory) |

---

## 10. Regulatory Requirements

### 10.1 Certifications Required

| Certification | Region | Status |
|---------------|--------|--------|
| FCC Part 15 | USA | Required |
| IC | Canada | Required |
| CE (RED) | EU | Required |
| RoHS | Global | Required |
| WEEE | EU | Required |

### 10.2 Compliance Notes

1. **FCC/IC:** ESP32-WROOM-32E module pre-certified (modular approval)
2. **24GHz Radar:** Subject to FCC Part 15.255 and ETSI EN 302 288
3. **RoHS:** All components must be RoHS compliant
4. **Labeling:** FCC ID, IC ID to appear on device label

---

## 11. Quality Requirements

### 11.1 Incoming Inspection

| Component | Test | Acceptance Criteria |
|-----------|------|---------------------|
| ESP32-WROOM-32E | Visual + Programming | Boot successful |
| Radar Modules | Visual + Functional | Data frames received |
| PCB | Visual + Electrical | No shorts, correct impedance |

### 11.2 End-of-Line Testing

| Test | Method | Pass Criteria |
|------|--------|---------------|
| Power | Measure current at 5V | 50-150mA idle |
| WiFi | Scan networks | ≥3 networks detected |
| Radar | Target detection | Valid frames at 1m |
| Sensors | I2C read | Valid temp/humidity/lux |
| LED | Visual | RGB cycling |
| Button | Press test | Reset triggered |

### 11.3 Burn-In

| Parameter | Specification |
|-----------|---------------|
| Duration | 24 hours minimum |
| Temperature | 40°C ambient |
| Power | Continuous operation |
| WiFi | Connected, transmitting |

---

## 12. Open Items and Decisions

### 12.1 Resolved Decisions

| Item | Decision | Rationale |
|------|----------|-----------|
| **MCU Family** | **ESP32-WROOM-32E + CH340N** | Native EMAC/RMII enables low-cost Ethernet PHY; lower total BOM |
| **MCU Flash** | **N8 (8MB)** | OTA headroom; $3.0011 at 100 qty |
| **Ethernet Architecture** | **RMII PHY (SR8201F)** | Lowest cost PHY; EMAC support in MCU |

### 12.2 Pending Decisions

| Item | Options | Impact | Decision By |
|------|---------|--------|-------------|
| Static Radar | LD2410B vs LD2410C vs LD2412 | Sensitivity, cost | Testing |
| Magnetics | Magjack vs external magnetics + RJ45 | Cost, layout complexity | Hardware |
| PoE Power Architecture | PD module vs discrete PD + flyback | Cost, layout complexity | Hardware |
| Power Mux | Diode OR vs ideal diode vs USB-priority | Safety, cost | Hardware |
| LED Behavior | Status only vs Activity indicator | UX, power | Product review |
| Form Factor | Wall vs Ceiling vs Universal | Enclosure design | Market research |

### 12.3 Action Items

1. [x] ~~Resolve MCU family~~ → ESP32-WROOM-32E + CH340N
2. [x] ~~Select Ethernet architecture~~ → RMII PHY (SR8201F preferred)
3. [ ] Validate radar module selection via prototype testing
4. [ ] Select PoE power architecture (module vs discrete) and update schematic
5. [ ] Generate component libraries for PCB design (PoE power stage, RJ45, PHY)
6. [ ] Create LCSC parts database with volume pricing
7. [ ] Verify JLCPCB assembly capabilities for all parts
8. [ ] Finalize enclosure mechanical design
9. [ ] Initiate pre-compliance RF testing

---

## 13. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-14 | OpticWorks | Initial release |

---

## 14. References

| Document | Description |
|----------|-------------|
| RS-1_Unified_BOM.md | Detailed BOM with part numbers |
| hardware-concept-evolution.md | Architecture decision rationale |
| PRD_RS1.md | Product requirements |
| REQUIREMENTS_RS1.md | Functional requirements |
| WIRING.md | Development wiring guide |

---

## Appendix A: Part Number Cross-Reference

### A.1 Critical Components

| Function | Primary PN | Alternate PN | Notes |
|----------|------------|--------------|-------|
| MCU | ESP32-WROOM-32E-N8 | - | Baselined |
| USB-UART Bridge | CH340N | - | Required for USB-C data |
| Buck Converter | SY8089AAAC | MT3410LB | SY8089 preferred |
| Static Radar | LD2410B | LD2410C, LD2412 | Test performance |
| Tracking Radar | LD2450 | - | No alternate |
| Ethernet PHY | SR8201F | IP101GRR, RTL8201F | SR8201F preferred |
| PoE Controller | Si3404-A-GMR | TPS2376 | Different topology |

### A.2 LCSC Part Numbers

| Component | LCSC PN |
|-----------|---------|
| ESP32-WROOM-32E-N8 | C701342 |
| CH340N | C2977777 |
| SY8089AAAC | C78988 |
| AHT20 | C2846063 |
| LTR-303ALS-01 | C2846066 |
| SR8201F | TBD |
| RTL8201F-VB-CG | C47055 |

*Note: Verify LCSC part numbers before ordering; availability changes.*
