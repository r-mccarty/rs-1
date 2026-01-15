Here is the comprehensive summary of our design session for the **OpticWorks RS-1 Sensor Platform**. This document traces the evolution of the hardware architecture from initial concept to the finalized production strategy.

---

# **Executive Summary: The Evolution of the RS-1**

We began with a plan to build a low-cost, multi-variant sensor using the **ESP32-C3**. Through technical validation and unit economic modeling, we identified critical flaws in that approach (specifically regarding Ethernet compatibility and pin counts).

We pivoted to the **ESP32-S3** architecture for native USB and GPIO headroom. Later Ethernet findings (RFD-002) and cost review reopened the MCU decision, since ESP32-S3 lacks EMAC/RMII support and needs SPI Ethernet for PoE.

**Prior Verdict (2026-01-14):** The **ESP32-S3-WROOM-1-N4** was selected as the unified platform.
**Status Update (2026-01-XX):** MCU selection is reopened. See the cost and Ethernet addendum below.

---

## **Addendum: Cost and Ethernet Re-evaluation (2026-01-XX)**

**Cost snapshot (LCSC, ~100 qty):**
- **ESP32-WROOM-32E-N8 + CH340N** core: **$3.3436**
- **ESP32-S3-WROOM-1-N8R2** module: **$3.4981** (about **+$0.1545** vs 32E + CH340N)

**Implications:**
1. **RMII data path is cheaper** with classic ESP32 (EMAC + low-cost PHY + magjack).
2. **ESP32-S3 requires SPI Ethernet** for PoE data, which increases BOM cost.
3. **PoE power architecture is still open:** integrated PD module vs discrete PD + flyback (Si3404 or similar).
4. **Single-PCBA strategy remains the goal,** but the core MCU choice is under active re-evaluation.

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

**Conclusion (updated):** The MCU decision is reopened. The 32E option is cost-competitive and keeps RMII Ethernet cheap, while S3 keeps native USB and higher GPIO headroom. Final selection is pending.

1. **Simplicity:** No external USB bridge chips to manage.
2. **Feasibility:** The huge pin count makes routing the Pro board easy (fewer PCB layers).
3. **Future-Proofing:** The S3 allows for re-use of hardware designs in other products potentially.

---

### **4. The Final Architecture: One Board to Rule Them All**

We settled on a **Single PCBA Design** where BOM population determines the product SKU.

#### **The Core Platform (On Every Board)**

* **MCU:** Under evaluation (ESP32-S3-WROOM-1-N4 vs ESP32-WROOM-32E-N8 + CH340N).
* **Power:** USB-C (Native Data + Power).
* **Sensors:** AHT20 (Temp/Hum), LTR-303 (Lux), WS2812 clone (LED).

#### **The "Modality" Upgrades (Population Options)**

* **RS-1 Lite:** Populate **LD2410B** (soldered) - Utility rooms, "I exist" detection.
* **RS-1 Pro:** Populate **Both Radars (LD2410 + LD2450)** - Living spaces, zone tracking.

#### **The "Pro" Upgrades (Add-ons)**

* **PoE:** Populate PD power stage (module or discrete flyback), RMII PHY (classic ESP32) or SPI Ethernet (S3), plus RJ45 with magnetics.
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

1. **Library:** Use `easyeda2kicad` to generate verified footprints for the PoE power stage (module or discrete), RJ45, and PHY.
2. **Schematic:** Route MCU per final selection. RMII pin map applies to classic ESP32 only; S3 requires SPI Ethernet.
3. **Layout:** Place radars at opposite ends of the PCB to minimize interference.
