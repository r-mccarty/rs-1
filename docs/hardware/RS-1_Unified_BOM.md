# OpticWorks RS-1 Unified Bill of Materials
**Architecture: Single PCBA, Multi-Variant Population Strategy**  
**Last Updated:** 2025-01-13  
**Status:** Pre-Production / Parts Validation Required

---

## Design Architecture Summary

**Platform:** Single PCBA (MCU under evaluation: ESP32-S3 vs ESP32-WROOM-32E + CH340N)  
**Strategy:** One PCBA with selective population for different SKUs  
**PCB:** 2-Layer (JLCPCB or PCBWAY)  
**Power Options:** USB-C Native or PoE (802.3af)  

---

## ⚠️ OPEN QUESTIONS - REQUIRES RESOLUTION

| Item | Design Doc | Scratchpad | Decision Needed |
|------|-----------|-----------|----------------|
| **MCU family** | ESP32-S3-WROOM-1 | ESP32-WROOM-32E + CH340N | Cost vs native USB, EMAC/RMII |
| **S3 memory** | ESP32-S3-WROOM-1-N4 (4MB/0MB) | ESP32-S3-WROOM-1-N8R2 (8MB/2MB) | If S3 is selected |
| **Static Radar** | LD2410B | LD2410C or LD2412 | Which variant(s)? |
| **Tracking Radar** | LD2450 | - | Confirmed LD2450 for multi-target tracking |
| **PoE power** | Integrated PD module | Discrete PD + flyback (Si3404 or similar) | Cost vs complexity |
| **Magnetics** | Magjack (integrated) | External magnetics + RJ45 | Cost vs layout |

---

## Core Platform BOM (Populated on ALL Variants)

### Power & Connectivity
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| USB-C Connector | SHOU HAN | TYPE-C 16PIN 2MD(073) | USB Data + Power | 1 | TBD |
| LDO Regulator | MICRONE | ME6211C33M5G-N | 3.3V Rail | 1 | TBD |
| Decoupling Cap | CCTC | TCC0603X7R104K500CT | 100nF/0.1µF | Multiple | TBD |
| Bulk Cap | Samsung | CL05A106MQ5NUNC | 10µF | Multiple | TBD |
| USB Resistor | UNI-ROYAL | 0603WAF330JT5E | 33Ω (USB-C) | 2 | TBD |
| USB Resistor | UNI-ROYAL | 0603WAF5101T5E | 5.1kΩ (USB-C CC) | 2 | TBD |
| USB-UART Bridge | WCH | CH340N | USB-UART (required for ESP32-WROOM-32E; DNP if S3) | 1 | $0.3425 |
| Power MOSFET | UMW | AO3401A | Power Gating | 1 | TBD |

### MCU
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| **MCU (S3 option)** | **ESPRESSIF** | **ESP32-S3-WROOM-1-N8R2** | Main Controller (native USB) | 1 | **$3.4981** |
| **MCU (classic option)** | **ESPRESSIF** | **ESP32-WROOM-32E-N8** | Main Controller (EMAC/RMII) | 1 | **$3.0011** |

*Note: Final decision needed on MCU family and flash/PSRAM configuration. If ESP32-WROOM-32E is selected, populate CH340N. If ESP32-S3 is selected, DNP CH340N.*

### Environmental Sensors (All Variants)
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| Temp/Humidity | Aosong | AHT20 | Temperature & Humidity | 1 | TBD |
| Lux Sensor | LITEON | LTR-303ALS-01 | Ambient Light | 1 | TBD |
| Status LED | XINGLIGHT | XL-5050RGBC-2812B-S | WS2812-style RGB | 1 | TBD |

### User Interface
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| Reset Button | XKB Connection | TS-1187A-B-A-B | User Reset | 1 | TBD |

### Debug/Programming
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| UART Header | Generic | SOICbite or equiv | Serial Debug | 1 (DNP) | TBD |
| Generic Header | Ckmtw | B-2100S02P-A110 | Optional Breakout | 1 (DNP) | TBD |

---

## Variant-Specific Components (Selective Population)

### RS-1 Lite: LD2410 Presence Detection
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| **Static Radar** | **HiLink** | **LD2410B/C or LD2412** | Presence Detection | 1 | **~$2.80** |
| mmWave Header | Generic | DNP - Solder Direct | Radar Connection | 1 | N/A |

**Estimated BOM:** ~$7.73
**Target Retail:** $49.00

**Use Case:** Utility rooms - bathrooms, hallways, closets. "I exist" detection.

---

### RS-1 Pro: Dual Radar Fusion
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| Static Radar | HiLink | LD2410B/C or LD2412 | Presence Detection | 1 | ~$2.80 |
| Tracking Radar | HiLink | LD2450 | Multi-Target Tracking | 1 | ~$11.50 |
| mmWave Headers | Generic | DNP - Solder Direct | Radar Connections | 2 | N/A |

**Estimated BOM:** ~$19.23
**Target Retail:** $89.00

**Use Case:** Living spaces - living rooms, kitchens, bedrooms. Zone tracking and motion detection.

**Layout Note:** Place radars at opposite ends of PCB to minimize interference.

---

## Add-On Options (Cross-Variant)

### PoE Power Option (All Variants)

#### Ethernet (data-only) BOM
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| Ethernet PHY (preferred) | CoreChips | SR8201F | 10/100 PHY (RMII) | 1 | ~$0.2481 |
| Ethernet PHY (alt) | IP101 | IP101GRR | 10/100 PHY (RMII) | 1 | ~$0.2684 |
| Ethernet PHY (alt) | REALTEK | RTL8201F-VB-CG | 10/100 PHY (RMII) | 1 | ~$0.4072 |
| RJ45 (no magnetics) | TBD | TBD | RJ45 connector | 1 | TBD |
| Ethernet Magnetics | TBD | TBD | Transformer/magnetics | 1 | TBD |
| RJ45 + Magnetics (alt) | HanRun | HR911105A | Integrated magjack | 1 | ~$0.9474 |
| Crystal | YXC | X322525MOB4SI | 25MHz Oscillator | 1 | ~$0.0546 |
| Passives | - | - | Terminations + decoupling | - | ~$0.10 |

**Estimated Ethernet add-on (data only):** +$1.30 to +$1.46
**Note:** Estimate assumes magjack pricing; update if external magnetics are selected.

#### PoE power (802.3af PD) options
- **Option A (module):** SDAPO DP1435-5V integrated PD module, ~$4.22
- **Option B (discrete):** Si3404 + flyback transformer + passives, cost TBD

**Estimated PoE add-on (module path):** +$5.52 to +$5.68 (data + PD module)
**Target Retail Add:** TBD

**Safety Note:** PoE isolation does not prevent USB back-powering. Power mux is required (see RFD-002).

---

### IAQ Air Quality Option (All Variants)
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| TVOC/CO2 Sensor | ScioSense | ENS160-BGLM | Air Quality | 1 | ~$4.60 |
| Pogo Pins | Generic | 0906-2-15-20-75-14-11-0 | Daughtercard Interface | 5 | TBD |

**Implementation:** Sensor mounted on separate daughtercard, connects via pogo pins (PWR, GND, I2C/SPI)  
**Mechanical:** Magnet retention system  

**Estimated BOM Add:** ~$4.60  
**Target Retail Add:** +$30.00

---

## Product Lineup Summary

| SKU | Configuration | Estimated BOM | Retail Price | Margin |
|-----|--------------|---------------|--------------|--------|
| RS-1 Lite | USB + LD2410 | ~$7.73 | $49.00 | TBD |
| RS-1 Pro | USB + Dual Radar | ~$19.23 | $89.00 | TBD |
| PoE Add-On | + Ethernet/PoE | +$4.15 to +$5.68 | +$30.00 | TBD |
| IAQ Add-On | + Air Quality | +$5.00 | +$35.00 | TBD |

**Example Configurations:**
- RS-1 Lite + PoE: $79.00 (~$11.88 to ~$13.41 BOM)
- RS-1 Pro + PoE: $119.00 (~$23.38 to ~$24.91 BOM)
- RS-1 Pro + PoE + IAQ: $154.00 (~$28.38 to ~$29.91 BOM)

---

## Pin Mapping (MCU dependent)

### RMII Ethernet (PoE Variant)
- Classic ESP32 only (ESP32-WROOM-32E has EMAC/RMII).
- RMII pin map depends on PHY choice and clocking scheme.

### USB Interface
- ESP32-S3: Native USB D+/D- on GPIO 19/20.
- ESP32-WROOM-32E: USB-UART bridge (CH340N) required.

### Radar Interfaces
- Static (LD2410): UART
- Tracking (LD2450): UART (separate pins, Pro only)

### I2C Bus (Sensors)
- SDA/SCL: AHT20, LTR-303, ENS160 (if populated)

### Other Peripherals
- WS2812 LED: GPIO (Single-wire data)
- Reset Button: GPIO (Input w/ pullup)

**Total GPIO Required (Pro+PoE):** ~13 pins  
**Note:** Re-validate GPIO budget if ESP32-WROOM-32E is selected.

---

## Next Actions

### Immediate (Design Phase)
1. **Resolve open questions** (MCU config, radar part numbers)
2. **Component library generation** using `easyeda2kicad` for:
   - Si3404 (PoE Controller)
   - Link-PP Transformer (if different from H1601CG)
   - Kinghelm RJ45
3. **Create LCSC parts database** with live pricing
4. **Verify all part availability** and lead times

### Schematic Phase
1. Route MCU per selection (ESP32-S3 vs ESP32-WROOM-32E)
2. Select PoE power architecture (module vs discrete) and design power stage
3. Design I2C sensor bus (AHT20, LTR-303, ENS160)
4. Design dual UART interfaces for radars

### Layout Phase
1. 2-layer PCB optimization
2. Radar placement (opposite ends for Pro)
3. PoE isolation boundary design
4. EMI/RF considerations for mmWave sensors

---

## Notes & Assumptions

1. **Single PCBA Strategy:** One PCB design handles all variants through selective component population
2. **Cost Estimates:** Based on 100-unit pricing; will decrease with volume
3. **PoE Safety:** Isolation does not prevent USB back-powering; power mux required
4. **IAQ Modularity:** Pogo pin + magnet approach allows field upgrades
5. **MCU Rationale:** ESP32-S3 offers native USB and high GPIO; ESP32-WROOM-32E offers EMAC/RMII and lower Ethernet BOM
6. **Legacy Options Removed:** ESP32-C3 removed due to MAC/pin limitations; ESP32-32E re-evaluated as cost option

---

## Pricing Database TODO
- [ ] Extract all parts into LCSC-compatible format
- [ ] Build automated pricing sheets with volume breaks
- [ ] Calculate margin models for each configuration
- [ ] Identify second sources for critical components
- [ ] Validate JLCPCB assembly capabilities for all parts
