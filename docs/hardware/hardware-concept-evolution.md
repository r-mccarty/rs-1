Here is the comprehensive summary of our design session for the **OpticWorks RS-1 Sensor Platform**. This document traces the evolution of the hardware architecture from initial concept to the finalized production strategy.

---

# **Executive Summary: The Evolution of the RS-1**

We began with a plan to build a low-cost, multi-variant sensor using the **ESP32-C3**. Through technical validation and unit economic modeling, we identified critical flaws in that approach (specifically regarding Ethernet compatibility and pin counts).

We pivoted to the **ESP32-S3** architecture. While the chip cost is marginally higher, it is the only solution that supports a **Single-PCBA strategy** capable of handling "Fusion" (Dual Radar) and PoE without expensive external controllers.

**Final Verdict:** The **ESP32-S3-WROOM-1-N4** is the unified platform for all OpticWorks sensors.

---

### **1. The Initial Assumptions (The "C3 Plan")**

* **The Concept:** A single PCB supporting three product tiers: Static (LD2410), Dynamic (LD2540), and Fusion (Both).
* **The Hardware Choice:** ESP32-C3-WROOM-02 (chosen for low cost and native USB).
* **The Ethernet Plan:** Realtek RTL8201F PHY (chosen for low cost, ~$0.40).
* **The Assumption:** The C3 could handle the fusion sensor load and drive the Ethernet PHY cheaply.

### **2. The Discovery Phase (Hard Limits Found)**

As we validated the BOM, we hit two "Red Flags" that forced a redesign:

**A. The Ethernet Incompatibility**

* **Discovery:** The ESP32-C3 **does not have an internal Ethernet MAC**. It cannot talk to the cheap Realtek PHY (RTL8201F) via RMII.
* **Impact:** We would be forced to use an SPI Ethernet Controller (W5500), which costs ~$2.10 (vs $0.40 for a PHY). This would add ~$1.70 of pure waste to every PoE unit.

**B. The Pin Count Wall**

* **Discovery:** The "Fusion" variant (2x Radars + Ethernet + I2C Sensors + PIR + LED) requires **14 GPIOs**.
* **Impact:** The ESP32-C3 only has ~12 usable GPIOs. It is physically impossible to route the Fusion board on a C3 without sacrificing features (like the Status LED or PIR).

### **3. The Optimization Pivot (Analyzing Alternatives)**

We evaluated two alternative paths to solve the "MAC & Pin" problem:

**Path A: The "Classic" (ESP32-WROOM-32E)**

* **Pros:** Has native MAC (supports cheap PHY). Cheapest MCU option ($2.70).
* **Cons:** No native USB. Requires an external USB-to-UART bridge (CP2102/CH340), adding cost and complexity ($0.50 + parts). Old technology.

**Path B: The "Modern" (ESP32-S3)**

* **Pros:** Has **Native MAC** (supports cheap PHY) AND **Native USB** (no bridge needed). Massive pin count (45 GPIOs). AI capable.
* **Cons:** Higher MCU cost ($3.42).

**The Economic Showdown (100 Unit Pricing):**

* **Base Model:** The S3 is ~$0.17 more expensive than the 32E combo.
* **PoE Model:** The S3 is ~$0.72 more expensive than the 32E combo.

**Conclusion:** While the 32E is strictly cheaper, the **ESP32-S3** was chosen because:

1. **Simplicity:** No external USB bridge chips to manage.
2. **Feasibility:** The huge pin count makes routing the "Fusion" board easy (fewer PCB layers).
3. **Future-Proofing:** The S3 allows for re-use of hardware designs in other products potentially.

---

### **4. The Final Architecture: One Board to Rule Them All**

We settled on a **Single PCBA Design** where BOM population determines the product SKU.

#### **The Core Platform (On Every Board)**

* **MCU:** **ESP32-S3-WROOM-1-N4** (4MB Flash, No PSRAM).
* **Power:** USB-C (Native Data + Power).
* **Sensors:** AHT20 (Temp/Hum), LTR-303 (Lux), WS2812 clone (LED).

#### **The "Modality" Upgrades (Population Options)**

* **RS-1 Static:** Populate **LD2410B** (soldered)
* **RS-1 Dynamic:** Populate **LD2450** (soldered) + **PIR**.
* **RS-1 Fusion:** Populate **Both Radars** + **PIR**.

#### **The "Pro" Upgrades (Add-ons)**

* **PoE:** Populate **Si3404** (Isolated Flyback Controller), **Transformer**, and **RTL8201F PHY**.
* *Safety Note:* We mandated an **Isolated Flyback** topology to protect users plugging in USB cables while PoE is active.


* **IAQ:** Do not populate components. Solder a **Daughtercard** via Pogo Pins (contains ENS160).

---

### **5. The Product Lineup & Economics**

| SKU | Configuration | BOM Estimate | Retail Price | Gross Margin |
| --- | --- | --- | --- | --- |
| **Static** | USB + LD2410 | ~$6.61 | **$69.00** | **TBD** |
| **Dynamic** | USB + LD2450 | ~$15.31 | **$69.00** | **TBD** |
| **Fusion** | USB + Dual Radar | ~$18.11 | **$99.00** | **TBD** |
| **PoE Option** | Add Ethernet/Power | +$4.32 | **+$30.00** | **TBD** |
| **IAQ Option** | Add Air Quality | ~$4.60 | **+$30.00** | **TBD** |

### **6. Next Actions (The Build)**

1. **Library:** Use `easyeda2kicad` to generate verified footprints for the **Si3404**, **Link-PP Transformer**, and **Kinghelm RJ45**.
2. **Schematic:** Route the ESP32-S3 using the finalized pin map (RMII on GPIO 1-15, USB on 19/20).
3. **Layout:** Place radars at opposite ends of the PCB to minimize interference.