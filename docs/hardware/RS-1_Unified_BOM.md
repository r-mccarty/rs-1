# OpticWorks RS-1 Unified Bill of Materials
**Architecture: Single PCBA, Multi-Variant Population Strategy**
**Last Updated:** 2026-01-15
**Status:** Pre-Production / Parts Validation Required

---

## Design Architecture Summary

**Platform:** Single PCBA with ESP32-WROOM-32E + CH340N
**Strategy:** One PCBA with selective population for different SKUs
**PCB:** 2-Layer (JLCPCB or PCBWAY)
**Power Options:** USB-C Native or PoE (802.3af via RMII PHY)

---

## ✅ RESOLVED DECISIONS

| Item | Decision | Rationale |
|------|----------|-----------|
| **MCU family** | **ESP32-WROOM-32E + CH340N** | Native EMAC/RMII enables low-cost PHY; lower BOM |
| **MCU flash** | **N8 (8MB)** | OTA headroom |
| **Tracking Radar** | **LD2450** | Multi-target tracking confirmed |
| **Ethernet Architecture** | **RMII PHY (SR8201F)** | Lowest cost; EMAC in MCU |

## ⚠️ PENDING DECISIONS

| Item | Options | Decision Needed |
|------|---------|----------------|
| **Static Radar** | LD2410B / LD2410C / LD2412 | Which variant(s)? |
| **PoE power** | Integrated PD module vs discrete Si3404 + flyback | Cost vs complexity |
| **Magnetics** | Magjack (integrated) vs external magnetics + RJ45 | Cost vs layout |

---

## Core Platform BOM (Populated on ALL Variants)

### Power & Connectivity
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| USB-C Connector | SHOU HAN | TYPE-C 16PIN 2MD(073) | USB Data + Power | 1 | $0.15 |
| **Buck Converter** | **Silergy** | **SY8089AAAC** | **3.3V 2A Rail** | 1 | **$0.07** |
| Buck Inductor | TBD | 2.2µH 3A 0805 | Buck Inductor | 1 | $0.02 |
| Buck Output Cap | Samsung | CL10A226MQ8NRNC | 22µF 6.3V X5R 0805 | 2 | $0.02 |
| Buck FB Resistor | UNI-ROYAL | 0603WAF1803T5E | 180kΩ 1% 0603 | 1 | $0.01 |
| Buck FB Resistor | UNI-ROYAL | 0603WAF3902T5E | 39kΩ 1% 0603 | 1 | $0.01 |
| Decoupling Cap | CCTC | TCC0603X7R104K500CT | 100nF/0.1µF | 4 | $0.01 |
| Bulk Cap | Samsung | CL05A106MQ5NUNC | 10µF | 2 | $0.02 |
| USB Resistor | UNI-ROYAL | 0603WAF330JT5E | 33Ω (USB-C) | 2 | $0.01 |
| USB Resistor | UNI-ROYAL | 0603WAF5101T5E | 5.1kΩ (USB-C CC) | 2 | $0.01 |
| USB-UART Bridge | WCH | CH340N | USB-UART (required) | 1 | $0.3425 |
| **Power Mux Diode** | **MDD** | **SS34** | **Schottky 3A 40V SMA** | 2 | **$0.013** |
| **USB ESD** | **TECH PUBLIC** | **USBLC6-2SC6** | **ESD Protection SOT-23-6** | 1 | **$0.02** |
| Power MOSFET | UMW | AO3401A | Power Gating | 1 | $0.03 |

**Buck Converter Notes:**
- SY8089AAAC replaces ME6211 LDO due to thermal/current limitations (see RFD-002)
- 2A output provides >2.8x margin over 700mA peak load
- LCSC Part #: C78988

**Power Mux Notes:**
- SS34 Schottky diodes isolate USB and PoE rails (see HARDWARE_SPEC.md Section 4.1.2a)
- ~0.3V forward drop at 650mA, system voltage ~4.7V after diode
- LCSC Part #: C8678

### MCU
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| **MCU** | **ESPRESSIF** | **ESP32-WROOM-32E-N8** | Main Controller | 1 | **$3.0011** |

**Core Cost:** $3.3436 (MCU $3.0011 + CH340N $0.3425)

**Why ESP32-WROOM-32E:**
- Native EMAC/RMII enables low-cost Ethernet PHY for PoE variants
- Proven, mature platform with excellent ESP-IDF support
- Lower total BOM cost vs ESP32-S3 + SPI Ethernet
- 8MB flash provides OTA headroom

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

**See:** `docs/hardware/POE_IMPLEMENTATION.md` for complete PoE design specification.

#### Ethernet (data-only) BOM
| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| **Ethernet PHY** | **CoreChips** | **SR8201F** | 10/100 PHY (RMII) | 1 | **~$0.25** |
| Ethernet PHY (alt) | IC+ | IP101GRR | 10/100 PHY (RMII) | 1 | ~$0.27 |
| Ethernet PHY (alt) | REALTEK | RTL8201F-VB-CG | 10/100 PHY (RMII) | 1 | ~$0.41 |
| RJ45 + Magnetics | HanRun | HR911105A | Integrated magjack | 1 | ~$0.95 |
| Crystal | YXC | X322525MOB4SI | 25MHz Oscillator | 1 | ~$0.05 |
| Passives | - | - | Terminations + decoupling | - | ~$0.10 |

**Estimated Ethernet add-on (data only):** ~$1.35 (SR8201F + magjack + crystal + passives)

**Note:** ESP32-WROOM-32E has native EMAC/RMII, enabling low-cost PHY (no SPI Ethernet needed).

#### PoE Power Architecture (802.3af PD)

| Approach | Description | BOM Add | Status |
|----------|-------------|---------|--------|
| **Option A: PD Module** | SDAPO DP1435-5V integrated | **~$4.22** | **Recommended** |
| Option B: Discrete | Si3404 + flyback + optocoupler | ~$2.55 | Future optimization |

##### Option A: Integrated PD Module (RECOMMENDED)

| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| **PD Module** | **SDAPO** | **DP1435-5V** | 802.3af PD + Isolated DC-DC | 1 | **~$4.22** |
| Input Cap | TBD | 100µF 63V | PD Input Filter | 1 | ~$0.10 |
| Output Cap | TBD | 100µF 10V | PD Output Filter | 1 | ~$0.05 |

**Why Module Approach:**
- Pre-certified IEEE 802.3af compliance
- Integrated isolation transformer and flyback
- No high-voltage layout complexity
- Proven thermal and protection circuitry
- Faster time-to-market

##### Option B: Discrete Si3404 + Flyback (Future Optimization)

| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| PD Controller | Silicon Labs | Si3404-A-GMR | PoE PD Controller | 1 | ~$1.20 |
| Flyback Transformer | TBD | TBD | Isolated DC-DC | 1 | ~$0.80 |
| Optocoupler | Sharp | PC817 | Isolated Feedback | 1 | ~$0.05 |
| Output Rectifier | MDD | SS54 | Schottky 5A 40V | 1 | ~$0.03 |
| TVS Diode | Littelfuse | SMBJ58A | Input Protection | 1 | ~$0.05 |
| Passives | - | - | Caps, resistors | - | ~$0.40 |
| **Discrete Total** | | | | | **~$2.55** |

**Note:** Discrete approach requires isolation boundary layout expertise and thermal validation. Recommended for high-volume cost optimization after initial production.

#### Complete PoE Add-On Summary

| Configuration | Components | Est. Cost |
|---------------|------------|-----------|
| Ethernet Data | SR8201F + HR911105A + Crystal + Passives | ~$1.35 |
| PoE Power (Module) | DP1435-5V + Caps | ~$4.37 |
| PoE Power (Discrete) | Si3404 + Flyback + Passives | ~$2.55 |
| **Total (Module)** | | **~$5.72** |
| **Total (Discrete)** | | **~$3.90** |

**Target Retail Add:** +$30.00

**Safety Note:** Power mux (SS34 diodes in core platform) isolates USB and PoE 5V rails to prevent back-powering. See HARDWARE_SPEC.md Section 4.1.2a.

---

### IAQ Air Quality Option (Separate Module)

**See:** `docs/hardware/IAQ_MODULE_SPEC.md` for complete IAQ module specification.

The IAQ module is a **separate, discrete product** with its own 2-layer PCBA. Pogo pins are on the IAQ module; RS-1 only requires passive landing pads.

#### RS-1 Host Interface (On RS-1 PCBA)

| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| ENIG Landing Pads | - | - | 5-pin pogo contact pads (2.54mm pitch) | 1 set | ~$0.02 |
| Steel Targets | Generic | N35 3mm×2mm disc | Magnetic retention targets | 2 | ~$0.05 |

**Note:** Pogo pins are NOT on RS-1 BOM—RS-1 only needs passive ENIG pads and steel/nickel targets for magnet attraction.

#### IAQ Module BOM (Separate SKU)

| Part | Manufacturer | Part Number | Function | Qty | Est. Cost |
|------|-------------|-------------|----------|-----|-----------|
| TVOC/CO2 Sensor | ScioSense | ENS160-BGLM | Air Quality (I2C) | 1 | ~$4.60 |
| Pogo Pins | Mill-Max | 0906-2-15-20-75-14-11-0 | Spring-Loaded Interface Pins | 5 | ~$0.40 |
| Neodymium Magnets | Generic | N35 3mm×2mm disc | Magnetic retention | 2 | ~$0.04 |
| LDO (1.8V) | Microne | ME6211A18M3G | ENS160 VDD (1.8V core) | 1 | ~$0.02 |
| Passives | Samsung | CL05A series | Input/output/decoupling caps (3×) | 3 | ~$0.04 |

**Power Architecture:** ENS160 has separate VDD (1.8V) and VDDIO (1.71-3.6V) pins. LDO provides 1.8V for core; VDDIO connects directly to 3.3V VCC for I2C compatibility. **No level shifter required.**

**Mechanical:** Magnet snap-on attachment. RS-1 enclosure has cutouts for pogo pin access.

**Estimated IAQ Module BOM:** ~$5.20
**Target IAQ Retail:** $35.00

---

## Product Lineup Summary

| SKU | Configuration | Estimated BOM | Retail Price | Margin |
|-----|--------------|---------------|--------------|--------|
| RS-1 Lite | USB + LD2410 | ~$7.90 | $49.00 | 84% |
| RS-1 Pro | USB + Dual Radar | ~$19.40 | $89.00 | 78% |
| PoE Add-On (Module) | + Ethernet/PoE | +$5.72 | +$30.00 | 81% |
| PoE Add-On (Discrete) | + Ethernet/PoE | +$3.90 | +$30.00 | 87% |
| IAQ Add-On | + Air Quality | +$5.00 | +$35.00 | 86% |

**Note:** BOM estimates include buck converter, power mux, and USB ESD protection per RFD-002 fixes.

**Example Configurations:**
- RS-1 Lite + PoE (Module): $79.00 (~$13.62 BOM, 83% margin)
- RS-1 Pro + PoE (Module): $119.00 (~$25.12 BOM, 79% margin)
- RS-1 Pro + PoE + IAQ: $154.00 (~$30.12 BOM, 80% margin)

---

## Pin Mapping (ESP32-WROOM-32E)

### USB Interface
- USB-UART bridge (CH340N) on UART0 (GPIO 1/3)

### RMII Ethernet (PoE Variant)
- ESP32-WROOM-32E has native EMAC/RMII
- GPIO 0, 18-27 used for RMII signals (see HARDWARE_SPEC.md for full map)

### Radar Interfaces
- Static (LD2410): UART1 (GPIO 4/5)
- Tracking (LD2450): UART2 (GPIO 16/17, Pro only)

### I2C Bus (Sensors)
- SDA/SCL (GPIO 21/22): AHT20, LTR-303, ENS160 (if populated)

### Other Peripherals
- WS2812 LED: GPIO 25 (Single-wire data)
- Reset Button: GPIO 34 (Input-only, external pullup)

**Total GPIO Required (Pro+PoE):** ~18 pins
**Available on ESP32-WROOM-32E:** 34 GPIO (sufficient)

---

## Next Actions

### Immediate (Design Phase)
1. [x] ~~Resolve MCU selection~~ → ESP32-WROOM-32E + CH340N
2. **Component library generation** using `easyeda2kicad` for:
   - Si3404 (PoE Controller)
   - SR8201F (Ethernet PHY)
   - HR911105A (Magjack)
3. **Create LCSC parts database** with live pricing
4. **Verify all part availability** and lead times

### Schematic Phase
1. Route ESP32-WROOM-32E with CH340N USB bridge
2. Route RMII Ethernet interface to SR8201F PHY
3. Select PoE power architecture (module vs discrete) and design power stage
4. Design I2C sensor bus (AHT20, LTR-303, ENS160)
5. Design dual UART interfaces for radars

### Layout Phase
1. 2-layer PCB optimization
2. Radar placement (opposite ends for Pro)
3. PoE isolation boundary design
4. RMII signal routing (matched lengths)
5. EMI/RF considerations for mmWave sensors

---

## Notes & Assumptions

1. **Single PCBA Strategy:** One PCB design handles all variants through selective component population
2. **Cost Estimates:** Based on 100-unit pricing; will decrease with volume
3. **PoE Safety:** Isolation does not prevent USB back-powering; power mux required
4. **IAQ Modularity:** Pogo pin + magnet approach allows field upgrades
5. **MCU Selection:** ESP32-WROOM-32E baselined for native EMAC/RMII and lower PoE BOM
6. **USB Bridge:** CH340N required on all variants (ESP32-WROOM-32E has no native USB)
7. **Legacy Options Removed:** ESP32-C3 removed due to MAC/pin limitations; ESP32-S3 not selected due to higher PoE cost (requires SPI Ethernet)

---

## Pricing Database TODO
- [ ] Extract all parts into LCSC-compatible format
- [ ] Build automated pricing sheets with volume breaks
- [ ] Calculate margin models for each configuration
- [ ] Identify second sources for critical components
- [ ] Validate JLCPCB assembly capabilities for all parts
