Here is the comprehensive summary of our design session for the **OpticWorks RS-1 Sensor Platform**. This document traces the evolution of the hardware architecture from initial concept to the finalized production strategy.

---

# **Executive Summary: The Evolution of the RS-1**

We began with a plan to build a low-cost, multi-variant sensor using the **ESP32-C3**. Through technical validation and unit economic modeling, we identified critical flaws in that approach (specifically regarding Ethernet compatibility and pin counts).

We briefly considered the **ESP32-S3** architecture for native USB and GPIO headroom, but Ethernet findings (RFD-002) revealed ESP32-S3 lacks EMAC/RMII support and requires expensive SPI Ethernet for PoE.

**Final Decision (2026-01-15):** The **ESP32-WROOM-32E-N8 + CH340N** is selected as the unified platform.

---

## **Final Decision Rationale (2026-01-15)**

**Cost snapshot (LCSC, ~100 qty):**
- **ESP32-WROOM-32E-N8 + CH340N** core: **$3.3436**
- **ESP32-S3-WROOM-1-N8R2** module: **$3.4981** (about **+$0.1545** vs 32E + CH340N)

**Why ESP32-WROOM-32E:**
1. **RMII data path is cheaper** - Native EMAC enables low-cost PHY (~$0.25) instead of SPI Ethernet (~$1.77).
2. **Lower PoE BOM cost** - RMII PHY + magjack is ~$1.35 vs W5500 + magjack at ~$2.70.
3. **Mature platform** - Proven ESP-IDF support, widely available.
4. **8MB flash** - OTA headroom for dual-partition updates.
5. **34 GPIO** - Sufficient for RS-1 Pro (dual radar + RMII Ethernet + sensors).

**Trade-off accepted:**
- Requires CH340N USB-UART bridge (no native USB like ESP32-S3).
- Bluetooth 4.2 instead of Bluetooth 5 (not critical for this product).

---

### **1. The Initial Assumptions (The "C3 Plan")**

* **The Concept:** A single PCB supporting multiple product tiers via selective population.
* **The Hardware Choice:** ESP32-C3-WROOM-02 (chosen for low cost and native USB).
* **The Ethernet Plan:** Realtek RTL8201F PHY (chosen for low cost, ~$0.40).
* **The Assumption:** The C3 could handle the fusion sensor load and drive the Ethernet PHY cheaply.

### **2. The Discovery Phase (Hard Limits Found)**

As we validated the BOM, we hit two "Red Flags" that forced a redesign:

**A. The Ethernet Incompatibility**

* **Discovery:** The ESP32-C3 **does not have an internal Ethernet MAC**. It cannot talk to the cheap Realtek PHY (RTL8201F) via RMII.
* **Impact:** We would be forced to use an SPI Ethernet Controller (W5500), which costs ~$2.10 (vs $0.40 for a PHY). This would add ~$1.70 of pure waste to every PoE unit.

**B. The Pin Count Wall**

* **Discovery:** The Pro variant (2x Radars + Ethernet + I2C Sensors + LED) requires **13+ GPIOs**.
* **Impact:** The ESP32-C3 only has ~12 usable GPIOs. It is physically impossible to route the Pro board on a C3 without sacrificing features.

### **3. The Optimization Pivot (Analyzing Alternatives)**

We evaluated two alternative paths to solve the "MAC & Pin" problem:

**Path A: The "Classic" (ESP32-WROOM-32E)**

* **Pros:** Has native EMAC/RMII (supports low-cost PHY). Lower Ethernet BOM. Core pricing at ~100: ESP32-WROOM-32E-N8 ($3.0011) + CH340N ($0.3425) = $3.3436.
* **Cons:** No native USB. Requires an external USB-to-UART bridge on USB-C variants. Older core vs S3.

**Path B: The "Modern" (ESP32-S3)**

* **Pros:** Native USB (no bridge needed). High GPIO count (45). Strong DSP/AI capability.
* **Cons:** No EMAC/RMII. PoE data requires SPI Ethernet. Module pricing at ~100: $3.4981.

**The Economic Showdown (100 Unit Pricing):**

* **Base Model:** The S3 module is about $0.1545 more expensive than 32E + CH340N.
* **PoE Model:** RMII Ethernet add-on is about $1.30 to $1.46 (PHY + magjack + passives). SPI Ethernet adds more BOM cost on S3. See `docs/hardware/ETHERNET_RFD_002_FOLLOWUP.md`.

**Final Decision:** ESP32-WROOM-32E-N8 + CH340N selected for lower PoE BOM cost via native EMAC/RMII.

**Key advantages:**
1. **Cost:** Lower PoE variant BOM (~$1.35 for RMII Ethernet vs ~$2.70 for SPI Ethernet).
2. **Simplicity:** RMII PHY is simpler than SPI Ethernet controller.
3. **Proven:** Mature platform with excellent ESP-IDF support.

**Trade-off:** Requires CH340N USB-UART bridge on all variants.

---

### **4. The Final Architecture: One Board to Rule Them All**

We settled on a **Single PCBA Design** where BOM population determines the product SKU.

#### **The Core Platform (On Every Board)**

* **MCU:** ESP32-WROOM-32E-N8 + CH340N USB-UART bridge.
* **Power:** USB-C (Data via CH340N + 5V Power).
* **Sensors:** AHT20 (Temp/Hum), LTR-303 (Lux), WS2812 clone (LED).

#### **The "Modality" Upgrades (Population Options)**

* **RS-1 Lite:** Populate **LD2410B** (soldered) - Utility rooms, "I exist" detection.
* **RS-1 Pro:** Populate **Both Radars (LD2410 + LD2450)** - Living spaces, zone tracking.

#### **The "Pro" Upgrades (Add-ons)**

* **PoE:** Populate SR8201F RMII PHY, PD power stage (module or discrete flyback), plus RJ45 with magnetics (magjack).
* *Safety Note:* PoE isolation does not prevent USB back-powering. Power mux is required (see RFD-002).

* **IAQ:** Do not populate components. Solder a **Daughtercard** via Pogo Pins (contains ENS160).

---

### **5. The Product Lineup & Economics**

| SKU | Configuration | BOM Estimate | Retail Price | Gross Margin |
| --- | --- | --- | --- | --- |
| **RS-1 Lite** | USB + LD2410 | ~$7.73 | **$49.00** | **TBD** |
| **RS-1 Pro** | USB + Dual Radar | ~$19.23 | **$89.00** | **TBD** |
| **PoE Option** | Add Ethernet/Power | +$4.15 | **+$30.00** | **TBD** |
| **IAQ Option** | Add Air Quality | ~$5.00 | **+$35.00** | **TBD** |

### **6. Next Actions (The Build)**

1. **Library:** Use `easyeda2kicad` to generate verified footprints for SR8201F PHY, HR911105A magjack, Si3404 PoE controller.
2. **Schematic:** Route ESP32-WROOM-32E with CH340N USB bridge and RMII Ethernet interface.
3. **Layout:** Place radars at opposite ends of the PCB to minimize interference. Route RMII with matched lengths.
