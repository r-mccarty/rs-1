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
| Max Tracked Targets | 3 simultaneous (Dynamic/Fusion variants) |
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
   │ USB-C   │◄────────────────────►│  │       ESP32-S3-WROOM-1          │    │
   │  5V/    │     USB Data         │  │                                 │    │
   │  Data   │     + Power          │  │  ┌─────────┐  ┌─────────────┐  │    │
   └─────────┘                      │  │  │  WiFi   │  │  Bluetooth  │  │    │
                                    │  │  │ 802.11  │  │    5.0      │  │    │
   ┌─────────┐                      │  │  │  b/g/n  │  │             │  │    │
   │  PoE    │◄────────────────────►│  │  └─────────┘  └─────────────┘  │    │
   │ 802.3af │  Ethernet + Power    │  │                                 │    │
   │(Option) │  (via Si3404)        │  └────────────┬────────────────────┘    │
   └─────────┘                      │               │                         │
                                    │               │ GPIO / UART / I2C       │
                                    │               ▼                         │
                                    │  ┌───────────────────────────────────┐  │
                                    │  │          Peripherals              │  │
                                    │  │                                   │  │
                                    │  │  ┌─────────┐  ┌─────────┐        │  │
                                    │  │  │ LD2410  │  │ LD2450  │        │  │
                                    │  │  │ Static  │  │ Dynamic │        │  │
                                    │  │  │ (UART)  │  │ (UART)  │        │  │
                                    │  │  └─────────┘  └─────────┘        │  │
                                    │  │                                   │  │
                                    │  │  ┌─────────┐  ┌─────────┐        │  │
                                    │  │  │  AHT20  │  │ LTR-303 │        │  │
                                    │  │  │Temp/Hum │  │   Lux   │        │  │
                                    │  │  │  (I2C)  │  │  (I2C)  │        │  │
                                    │  │  └─────────┘  └─────────┘        │  │
                                    │  │                                   │  │
                                    │  │  ┌─────────┐  ┌─────────┐        │  │
                                    │  │  │  AS312  │  │ WS2812  │        │  │
                                    │  │  │   PIR   │  │   LED   │        │  │
                                    │  │  │ (GPIO)  │  │ (GPIO)  │        │  │
                                    │  │  └─────────┘  └─────────┘        │  │
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

| Component | RS-1 Static | RS-1 Dynamic | RS-1 Fusion |
|-----------|:-----------:|:------------:|:-----------:|
| ESP32-S3-WROOM-1 | Required | Required | Required |
| USB-C Power/Data | Required | Required | Required |
| AHT20 (Temp/Hum) | Required | Required | Required |
| LTR-303 (Lux) | Required | Required | Required |
| WS2812 Status LED | Required | Required | Required |
| Reset Button | Required | Required | Required |
| LD2410B/C (Static Radar) | **Required** | - | **Required** |
| LD2450 (Dynamic Radar) | - | **Required** | **Required** |
| AS312 PIR Sensor | - | **Required** | **Required** |
| PoE Components | Optional | Optional | Optional |
| IAQ Daughtercard | Optional | Optional | Optional |

---

## 4. Bill of Materials

### 4.1 Core Platform (All Variants)

#### 4.1.1 Microcontroller

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| U1 | Espressif | ESP32-S3-WROOM-1-N4 | MCU Module, 4MB Flash, 0 PSRAM | 1 | $3.42 |

**Specifications:**
- Architecture: Xtensa LX7 dual-core, 240 MHz
- Flash: 4MB (Quad SPI)
- SRAM: 512KB
- GPIO: 45 programmable
- Interfaces: USB OTG, SPI, I2C, I2S, UART, SDIO, Ethernet MAC
- Wireless: WiFi 802.11 b/g/n, Bluetooth 5 (LE)
- Operating Voltage: 3.0V - 3.6V
- Operating Temperature: -40°C to +85°C

**Alternate Part (for higher memory requirements):**
| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| U1-ALT | Espressif | ESP32-S3-WROOM-1-N8R2 | MCU Module, 8MB Flash, 2MB PSRAM | 1 | $4.10 |

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

#### 4.2.1 Static Presence Radar (RS-1 Static, RS-1 Fusion)

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

#### 4.2.2 Multi-Target Tracking Radar (RS-1 Dynamic, RS-1 Fusion)

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

#### 4.2.3 PIR Sensor (RS-1 Dynamic, RS-1 Fusion)

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| U5 | Senba Sensing | AS312 | PIR Motion Sensor, Digital Output | 1 | $0.35 |
| LENS1 | Senba Sensing | SB-F-02 | Fresnel Lens for AS312 | 1 | $0.20 |

**AS312 Specifications:**
| Parameter | Value | Unit |
|-----------|-------|------|
| Detection Range | 3 - 5 | m |
| Field of View | 100 | ° |
| Output | Digital (Active High) | - |
| Operating Voltage | 2.7 - 3.3 | V |
| Current Consumption | 15 | µA |

---

### 4.3 PoE Option

| Item | Manufacturer | Part Number | Description | Qty | Unit Cost |
|------|--------------|-------------|-------------|-----|-----------|
| U6 | Realtek | RTL8201F-VB-CG | Ethernet PHY, 10/100Mbps, RMII | 1 | $0.40 |
| U7 | Silicon Labs | Si3404-A-GMR | PoE PD Controller, Isolated Flyback | 1 | $1.20 |
| J4 | Kinghelm | KH-RJ45-58-8P8C | RJ45 Connector with Magnetics | 1 | $0.85 |
| T1 | Mentech | H1601CG | Ethernet Transformer, 10/100Base-T | 1 | $0.45 |
| BR1 | Shikues | MB10S | Bridge Rectifier, 1A, 1000V, SOP-4 | 1 | $0.05 |
| Y1 | YXC | X322525MOB4SI | Crystal, 25MHz, 20ppm, 3225 | 1 | $0.12 |

**PoE Specifications:**
| Parameter | Value | Unit |
|-----------|-------|------|
| IEEE Standard | 802.3af (Type 1) | - |
| Input Voltage | 37 - 57 | VDC |
| Power Class | 0-3 | - |
| Max Power | 12.95 | W |
| Isolation | 1500 | VAC |

**Safety Note:** The Si3404 provides isolated flyback topology. This protects against simultaneous USB and PoE connection scenarios.

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

### 5.1 RS-1 Static

| Category | Components | Est. Cost |
|----------|------------|-----------|
| Core Platform | ESP32-S3, Power, Sensors, UI | $4.93 |
| LD2410B Radar | Radar module | $2.80 |
| **Total** | | **~$7.73** |

**Target Retail:** $69.00

### 5.2 RS-1 Dynamic

| Category | Components | Est. Cost |
|----------|------------|-----------|
| Core Platform | ESP32-S3, Power, Sensors, UI | $4.93 |
| LD2450 Radar | Radar module | $11.50 |
| PIR System | AS312 + Lens | $0.55 |
| **Total** | | **~$16.98** |

**Target Retail:** $69.00

### 5.3 RS-1 Fusion

| Category | Components | Est. Cost |
|----------|------------|-----------|
| Core Platform | ESP32-S3, Power, Sensors, UI | $4.93 |
| LD2410B Radar | Static presence | $2.80 |
| LD2450 Radar | Multi-target tracking | $11.50 |
| PIR System | AS312 + Lens | $0.55 |
| **Total** | | **~$19.78** |

**Target Retail:** $99.00

### 5.4 Add-On Pricing

| Option | BOM Add | Retail Add |
|--------|---------|------------|
| PoE | +$3.07 | +$30.00 |
| IAQ | +$5.00 | +$30.00 |

### 5.5 Configuration Examples

| Configuration | BOM Est. | Retail |
|---------------|----------|--------|
| RS-1 Static (USB) | $7.73 | $69.00 |
| RS-1 Dynamic (USB) | $16.98 | $69.00 |
| RS-1 Fusion (USB) | $19.78 | $99.00 |
| RS-1 Dynamic + PoE | $20.05 | $99.00 |
| RS-1 Fusion + PoE | $22.85 | $129.00 |
| RS-1 Fusion + PoE + IAQ | $27.85 | $159.00 |

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

### 7.1 ESP32-S3 GPIO Allocation

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 0 | Boot Select | Input | Active Low |
| 1-15 | RMII Ethernet | Bidirectional | PoE variant only |
| 16 | LD2410 UART TX | Output | Static radar |
| 17 | LD2410 UART RX | Input | Static radar |
| 18 | LD2450 UART TX | Output | Dynamic radar |
| 19 | USB D- | Bidirectional | Native USB |
| 20 | USB D+ | Bidirectional | Native USB |
| 21 | LD2450 UART RX | Input | Dynamic radar |
| 22 | I2C SDA | Bidirectional | Sensors |
| 23 | I2C SCL | Output | Sensors |
| 38 | WS2812 Data | Output | Status LED |
| 39 | PIR Input | Input | Motion detect |
| 40 | Reset Button | Input | Active Low, Pull-up |

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

1. **Radar Placement:** Place LD2410 and LD2450 at opposite ends of PCB to minimize interference (Fusion variant)
2. **Antenna Keepout:** 15mm clearance around ESP32-S3 antenna area
3. **Power Planes:** Solid ground plane on Layer 2
4. **USB Routing:** Differential pair routing for USB D+/D-
5. **RMII Routing:** Matched length for RMII signals (PoE variant)
6. **Isolation:** 3mm minimum creepage for PoE isolated section

### 8.3 Thermal Considerations

| Component | Thermal Pad | Notes |
|-----------|-------------|-------|
| ESP32-S3-WROOM | Yes | Via stitching to ground |
| ME6211 LDO | Thermal relief | Maximum 1W dissipation |
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

1. **FCC/IC:** ESP32-S3-WROOM-1 module pre-certified (modular approval)
2. **24GHz Radar:** Subject to FCC Part 15.255 and ETSI EN 302 288
3. **RoHS:** All components must be RoHS compliant
4. **Labeling:** FCC ID, IC ID to appear on device label

---

## 11. Quality Requirements

### 11.1 Incoming Inspection

| Component | Test | Acceptance Criteria |
|-----------|------|---------------------|
| ESP32-S3 | Visual + Programming | Boot successful |
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

### 12.1 Pending Decisions

| Item | Options | Impact | Decision By |
|------|---------|--------|-------------|
| MCU Flash/PSRAM | N4 (4MB/0) vs N8R2 (8MB/2MB) | Cost, capability | Firmware review |
| Static Radar | LD2410B vs LD2410C vs LD2412 | Sensitivity, cost | Testing |
| LED Behavior | Status only vs Activity indicator | UX, power | Product review |
| Form Factor | Wall vs Ceiling vs Universal | Enclosure design | Market research |

### 12.2 Action Items

1. [ ] Resolve MCU configuration (N4 vs N8R2)
2. [ ] Validate radar module selection via prototype testing
3. [ ] Generate component libraries for PCB design (Si3404, RJ45, transformer)
4. [ ] Create LCSC parts database with volume pricing
5. [ ] Verify JLCPCB assembly capabilities for all parts
6. [ ] Finalize enclosure mechanical design
7. [ ] Initiate pre-compliance RF testing

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
| MCU | ESP32-S3-WROOM-1-N4 | ESP32-S3-WROOM-1-N8R2 | Verify memory needs |
| LDO | ME6211C33M5G-N | AP2112K-3.3 | Pin compatible |
| Static Radar | LD2410B | LD2410C, LD2412 | Test performance |
| Dynamic Radar | LD2450 | - | No alternate |
| PoE Controller | Si3404-A-GMR | TPS2376 | Different topology |

### A.2 LCSC Part Numbers

| Component | LCSC PN |
|-----------|---------|
| ESP32-S3-WROOM-1-N4 | C2913202 |
| ME6211C33M5G-N | C82942 |
| AHT20 | C2846063 |
| LTR-303ALS-01 | C2846066 |
| RTL8201F-VB-CG | C47055 |

*Note: Verify LCSC part numbers before ordering; availability changes.*
