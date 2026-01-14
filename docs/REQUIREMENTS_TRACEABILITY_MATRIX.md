# RS-1 Requirements Traceability Matrix (RTM)

**Date:** 2026-01-14
**Status:** Updated with S3 Architecture
**Reference Documents:**
- [PRD_RS1.md](PRD_RS1.md)
- [REQUIREMENTS_RS1.md](REQUIREMENTS_RS1.md)
- [hardware/HARDWARE_SPEC.md](hardware/HARDWARE_SPEC.md)
- [hardware/hardware-concept-evolution.md](hardware/hardware-concept-evolution.md)

---

## 1. Product & Vision Requirements

| ID | Requirement | Source | Priority | Evaluation |
|----|-------------|--------|----------|------------|
| REQ-VIS-001 | Setup journey must be completed in ≤60 seconds for 80% of users. | PRD 4.1, 5.0 | **Critical** | **High Risk.** Requires seamless mobile app <-> device handshake. Captive portal fallback is essential. |
| REQ-VIS-002 | Daily operation must require zero user interaction ("invisibility"). | PRD 4.2 | High | **Applicable.** Implies high reliability and "set and forget" design. |
| REQ-VIS-003 | False occupancy rate < 5%, False vacancy rate < 1%. | PRD 4.2, 5.0 | High | **Hard to Verify.** "Normal activity" needs definition in test plan. Single radar (LD2450) might struggle with <1% false vacancy for stationary targets without fusion. |
| REQ-VIS-004 | Zone adjustment must take <30 seconds with immediate effect. | PRD 4.3 | Medium | **Applicable.** Clear UI/UX requirement. |
| REQ-VIS-005 | Support "Unlimited" software zones (practical limit ~16). | PRD 2, REQ 4.1 | High | **Differentiation.** Major competitive advantage over Aqara/LD2450 native limits. Feasible on ESP32-S3 with dual-core processing. |
| REQ-VIS-006 | Cloud-optional OTA (local fallback available). | PRD 2, REQ 4.5 | High | **Applicable.** Aligns with "Local Control" value proposition. |

## 2. Hardware Requirements

| ID | Requirement | Source | Priority | Evaluation |
|----|-------------|--------|----------|------------|
| REQ-HW-001 | MCU: ESP32-S3-WROOM-1-N4 (Xtensa LX7 Dual-Core, 4MB Flash). | HW-RS1-001 | **MVP** | **Confirmed.** Replaces C3. Chosen for native USB + native Ethernet MAC + high pin count. 512KB SRAM. |
| REQ-HW-002 | Radar: Hi-Link LD2450 (24GHz, 3 targets, 6m range). | HW-RS1-001 | **MVP** | **Applicable.** Standard choice for tracking. |
| REQ-HW-003 | Power: USB-C (Native Data + Power). | HW-RS1-001 | **MVP** | **Applicable.** S3 has native USB, eliminating external bridge chip. |
| REQ-HW-004 | Interface: UART at 256000 baud. | HW-RS1-001 | **MVP** | **Applicable.** |
| REQ-HW-005 | Status LED: WS2812 RGB LED. | HW-RS1-001 | Medium | **Defined.** Hardware spec confirms WS2812 (GPIO 38). Behavior (color/pattern) still needs firmware definition. |
| REQ-HW-006 | Target Price: $70-80 USD (Base Model). | PRD 1, Comp Analysis | High | **Business Constraint.** S3 adds cost (~$0.17-$0.72) but enables single-SKU strategy which saves manufacturing complexity. |
| REQ-HW-007 | "Single PCBA" Strategy. | HW Evolution | **Strategic** | **New.** One board supports Static (LD2410), Dynamic (LD2450), and Fusion (Both) via selective population. |

## 3. Firmware Requirements (HardwareOS)

| ID | Requirement | Source | Priority | Evaluation |
|----|-------------|--------|----------|------------|
| REQ-FW-001 | Track up to 3 concurrent targets (X, Y coords). | REQ 4.1 | **MVP** | **Applicable.** Hardware limit of LD2450. |
| REQ-FW-002 | Coordinate system: X ±6000mm, Y 0-6000mm. | REQ 4.1 | **MVP** | **Applicable.** |
| REQ-FW-003 | Zone evaluation: Point-in-polygon per zone per frame. | REQ 4.1 | **MVP** | **Applicable.** S3 dual-core allows dedicated core for radar processing if needed. |
| REQ-FW-004 | Home Assistant Protocol: ESPHome Native API (TCP 6053). | REQ 4.3 | **Critical** | **Strategic Core.** Must mimic ESPHome to HA without using GPL ESPHome runtime. Custom Protobuf implementation required. |
| REQ-FW-005 | Discovery: mDNS `_esphomelib._tcp.local`. | REQ 4.3 | **Critical** | **Applicable.** Essential for "it just works" discovery. |
| REQ-FW-006 | OTA: Cloud trigger via MQTT, download via HTTPS. | REQ 4.5 | **MVP** | **Complex.** Requires robust TLS stack. 4MB flash limit means ~1.5MB app partition limit. |
| REQ-FW-007 | Security: ESP32-S3 Secure Boot V2. | REQ 4.6 | High | **Applicable.** |
| REQ-FW-008 | Latency: Occupancy update < 1 second. | REQ 4.2 | High | **Applicable.** |
| REQ-FW-009 | Configuration persistence via NVS with atomic writes. | REQ 4.4 | High | **Applicable.** |
| REQ-FW-010 | Native USB Support (CDC/JTAG). | HW-RS1-001 | **New** | **Applicable.** Firmware must handle USB CDC for logging/console instead of UART0. |

## 4. Mobile App & Onboarding Requirements

| ID | Requirement | Source | Priority | Evaluation |
|----|-------------|--------|----------|------------|
| REQ-APP-001 | Platforms: iOS 15+ and Android 10+. | REQ 5.1 | **MVP** | **Applicable.** |
| REQ-APP-002 | Setup Flow: QR Scan -> AP Connect -> WiFi Creds -> Done. | REQ 5.2 | **Critical** | **Applicable.** |
| REQ-APP-003 | QR Code Scheme: `opticworks://setup...`. | REQ 5.3 | High | **Applicable.** |
| REQ-APP-004 | Zone Editor: Drag handles, resize, rename, live preview. | REQ 5.6 | **MVP** | **Complex UI.** |
| REQ-APP-005 | AR Scanning (RoomPlan). | REQ 5.8, 2.2 | **Post-Beta** | **Out of Scope** for MVP. |

## 5. Cloud & Backend Requirements

| ID | Requirement | Source | Priority | Evaluation |
|----|-------------|--------|----------|------------|
| REQ-CLD-001 | Device Registry: Identity derived from eFuse MAC. | REQ 6.1 | **MVP** | **Applicable.** |
| REQ-CLD-002 | OTA Orchestrator: Staged rollouts. | REQ 6.2 | High | **Applicable.** |
| REQ-CLD-003 | Telemetry: Opt-in only system metrics. | REQ 6.3 | Medium | **Applicable.** |

## 6. Constraints & Out of Scope

| ID | Requirement | Source | Priority | Evaluation |
|----|-------------|--------|----------|------------|
| CON-001 | No Multi-sensor fusion in *Base* MVP. | REQ 2.2 | N/A | **Clarified.** Base model is LD2450 only. Hardware supports Fusion (LD2410+LD2450+PIR) but software support is Post-MVP or "Pro" tier. |
| CON-002 | Memory Limit: ~200KB heap budget (estimated). | REQ 3.2 | N/A | **Constraint.** S3 has 512KB SRAM, but WiFi/BT stacks consume significant portion. Monitoring required. No PSRAM on N4. |
| CON-003 | Flash Limit: 4MB total. | HW-RS1-001 | N/A | **Constraint.** OTA partitions limited to ~1.5MB. |

## 7. Strategic & Competitive Requirements

| ID | Requirement | Source | Priority | Evaluation |
|----|-------------|--------|----------|------------|
| REQ-STRAT-001| Unified UX: User sees single "Occupancy" state. | Comp Analysis | **Critical** | **Design Philosophy.** Even with "Fusion" hardware capability, the output must remain simple. |
| REQ-STRAT-002| Local-First: Cloud not required for core operation. | Comp Analysis | **Critical** | **Trust Factor.** |
| REQ-STRAT-003| "Single Board" Manufacturing. | HW Evolution | **Critical** | **Operational Efficiency.** Reduces inventory risk by sharing one PCBA across all SKUs. |

---

## Summary of Changes (C3 -> S3)

1.  **MCU Upgrade:** Switched to ESP32-S3-WROOM-1-N4.
    *   *Gain:* Native USB, Native Ethernet MAC (future PoE), 45 GPIOs (Fusion capable), Dual Core.
    *   *Cost:* Slightly higher BOM (~$0.17-$0.72), but offsets external USB bridge cost.
2.  **PoE Strategy:** Now feasible via "PoE Option" population (Si3404 + RTL8201F) on the same board.
3.  **Fusion Ready:** The hardware is now capable of driving LD2410 + LD2450 + PIR simultaneously (Fusion SKU), unlike the C3 which ran out of pins.
4.  **USB:** Firmware must now handle native USB CDC (Console) instead of relying on a hardware USB-to-UART bridge.