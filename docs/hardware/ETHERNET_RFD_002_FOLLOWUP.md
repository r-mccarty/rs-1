# RS-1 Ethernet Options Follow-Up (RFD-002)

**Status:** Draft
**Date:** 2026-01-XX
**Owner:** Hardware + Firmware

---

## 1. Context

RFD-002 and its response identified Ethernet-related issues in the current RS-1 hardware spec:

- **ESP32-S3 has no EMAC/RMII.** The RTL8201F RMII PHY shown in `docs/hardware/HARDWARE_SPEC.md` and `docs/hardware/RS-1_Unified_BOM.md` will not work with ESP32-S3.
- **PoE + USB power muxing is missing.** The Si3404 provides isolation but does not prevent back-powering between USB VBUS and PoE 5V.
- **Doc conflict:** `docs/hardware/hardware-concept-evolution.md` states ESP32-S3 supports RMII PHY, which is superseded by RFD-002.

This document focuses on Ethernet controller options and firmware implications.

---

## 2. Requirements and Constraints

Derived from `docs/hardware/HARDWARE_SPEC.md`, `docs/hardware/hardware-concept-evolution.md`, and firmware specs:

- **Single PCBA strategy** is preferred for manufacturing simplicity.
- **ESP-IDF 5.x** is the firmware base (HardwareOS).
- **Low/medium throughput** use case: MQTT, OTA, ESPHome Native API (TCP).
- **PoE add-on** should coexist safely with USB-C (requires power mux).
- **Firmware assumptions** currently state "Wi-Fi only" (M05) and need review if Ethernet is added.

---

## 3. SPI Ethernet Controller Options

### 3.1 WIZnet W5500 (SPI, hard TCP/IP)

**Summary:** SPI Ethernet controller with integrated TCP/IP stack and MAC/PHY.

**Pros**
- TCP/IP offload reduces MCU CPU and RAM pressure.
- Mature ecosystem; commonly used with ESP32.
- Simple SPI wiring (plus INT, RESET, and CS).

**Cons**
- Higher unit cost than PHY-only parts.
- Still requires RJ45 + magnetics and board area.
- Driver integration must be validated with ESP-IDF 5.x.

**Notes**
- RFD-002 response recommends W5500 as the lowest-risk path for ESP32-S3.
- Verify clocking and magnetics requirements against the datasheet.

### 3.2 WCH CH390H (SPI, MAC+PHY, lwIP)

**Summary:** SPI Ethernet controller that relies on MCU TCP/IP (lwIP).

**Pros**
- Likely lower unit cost than W5500.
- Uses standard lwIP stack already in ESP-IDF.

**Cons**
- No TCP/IP offload; higher MCU CPU/RAM usage.
- Driver maturity and long-term maintenance risk (needs validation).
- More sensitive to lwIP buffer sizing and memory budget.

**Notes**
- Adds firmware complexity but may reduce BOM cost.

---

## 4. Comparison (RS-1 Use Case)

| Criteria | W5500 | CH390H |
| --- | --- | --- |
| Interface | SPI | SPI |
| TCP/IP Stack | On-chip | lwIP on MCU |
| MCU CPU/RAM Impact | Lower | Higher |
| Driver Risk | Lower (mature) | Medium (validate) |
| Firmware Complexity | Lower | Higher |
| Cost | Higher | Lower |
| Fit for MQTT/OTA/HA | Strong | Strong (with tuning) |

**Working assumption:** If we want to minimize firmware risk and keep memory headroom for OTA + TLS, W5500 is safer. If BOM cost is priority and we can invest in driver validation and memory tuning, CH390H is viable.

---

## 5. Firmware Requirements Review (HardwareOS)

**Key affected modules and assumptions:**

- **M05 (Native API):** Assumption A4 says "Wi-Fi only." Must be updated to allow Ethernet as a transport.
- **M07 (OTA), M09 (Logging), M10 (Security):** All depend on network availability; should use a unified network abstraction and be agnostic to Wi-Fi vs Ethernet.
- **M08 (Timebase/Boot):** Boot sequence should initialize Ethernet (if present) and select preferred interface.
- **Memory budget:** lwIP and TLS already consume significant heap. CH390H increases lwIP reliance; W5500 reduces MCU TCP/IP load but still needs socket buffers and driver memory.

**Recommended firmware abstraction:**

```
netif_init()
  -> detect Ethernet present
  -> bring up Ethernet or Wi-Fi
  -> publish active interface to M05/M07/M09/M10
```

This keeps modules transport-agnostic and avoids conditional logic in each module.

---

## 6. MCU Variant Strategy (PoE vs USB-C)

### Option A: Single MCU (ESP32-S3) + SPI Ethernet

**Pros**
- Single PCBA and firmware lineage.
- No USB-UART bridge needed.
- Aligns with existing hardware concept evolution strategy.

**Cons**
- Higher Ethernet BOM vs RMII PHY.

### Option B: Single MCU (ESP32-WROOM-32E) + USB-UART + RMII PHY

**Pros**
- Single PCBA across PoE and USB-C variants.
- RMII PHY (RTL8201F) lowers PoE BOM vs SPI Ethernet.
- Classic ESP32 has EMAC and RMII support.

**Cons**
- Requires USB-UART bridge on all variants.
- Adds BOM cost and board area for the bridge.
- Loses ESP32-S3 native USB and newer features.

**Conclusion:** This can be cheaper than ESP32-S3 + SPI Ethernet if the USB-UART bridge cost is lower than the W5500 delta and if the classic ESP32 meets performance and GPIO needs.

### Option C: Split MCU (ESP32-WROOM-32E for PoE, ESP32-S3 for USB-C)

**Pros**
- RMII PHY (RTL8201F) lowers PoE BOM.
- Classic ESP32 has EMAC and RMII.

**Cons**
- Likely two PCB layouts (different module footprint and routing).
- Requires USB-UART bridge on the ESP32-WROOM-32E SKU.
- Separate firmware build/test matrix (different SoC features and peripherals).
- Increases manufacturing and QA complexity.

**Conclusion:** This split only makes sense if firmware can stay largely common and the added PCB complexity is acceptable. Otherwise, the single-PCBA strategy remains the safer path.

---

## 7. Follow-Up Actions

1. Choose SPI Ethernet controller (W5500 vs CH390H).
2. Update `docs/hardware/HARDWARE_SPEC.md` and `docs/hardware/RS-1_Unified_BOM.md` to remove RMII PHY assumptions if SPI is selected.
3. Update firmware assumptions in `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md` (A4) and add Ethernet transport notes in M07/M09.
4. Validate power mux design for USB + PoE per RFD-002.
