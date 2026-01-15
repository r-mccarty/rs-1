**OpticWorks RS-1**

Competitive Analysis & Strategic Positioning

*January 2026 | Confidential | v2.0*

# **Executive Summary**

The mmWave presence detection market for smart homes is undergoing rapid evolution. Competitors are converging on sensor fusion strategies (dual radar, PIR combinations) to address detection reliability gaps. However, this hardware arms race exposes a critical weakness: every additional sensor adds complexity that gets passed directly to the end user through more entities, more tuning, and more confusion.

OpticWorks' strategic thesis is that **user experience wins markets, not sensor counts**. While competitors add hardware complexity, OpticWorks can deliver equivalent or superior detection by abstracting fusion logic behind a simple, unified interface. The user should never see 'radar 1 says X, radar 2 says Y' — they should see a single confident answer.

Key strategic insight: *ESPHome's Native API protocol is based on publicly documented Protocol Buffers over TCP. The Python client library (aioesphomeapi) is MIT licensed. This means OpticWorks can implement a compatible API server in custom firmware (HardwareOS), achieving seamless Home Assistant discovery and integration without using ESPHome's GPLv3 C++ runtime or inheriting its limitations.*

# **Market Overview**

The presence detection sensor market serves the Home Assistant ecosystem, which has grown to over 1 million active installations. Users seek reliable occupancy detection for automation (lighting, HVAC, security) that goes beyond simple motion detection. mmWave radar sensors from HiLink have become the dominant sensing technology.

## **Sensor Technology Landscape**

Two distinct mmWave sensor types have emerged, each with different strengths:

| Sensor Type | Strengths | Weaknesses |
| :---- | :---- | :---- |
| LD2410/LD2412 (Static) | Extremely sensitive to micro-movements (breathing). Reliable for completely stationary occupants. | No coordinate tracking. Binary output only. Cannot support zone-based automation. |
| LD2450 (Tracking) | X/Y coordinate tracking for up to 3 targets. Enables zone-based detection and movement vectors. | May drop targets who are completely motionless for extended periods. Less sensitive than static sensors. |

This fundamental tradeoff has driven competitors toward dual-radar fusion products that combine both sensor types. However, fusion introduces new UX challenges that most vendors are failing to address.

## **Market Segments**

* **DIY Enthusiasts:** Price-sensitive, comfortable with YAML configuration, value transparency and hackability. Served by Screek.  
* **Prosumers:** Want polish and reliability without deep technical configuration. Willing to pay premium for better UX. Underserved.  
* **Professional Installers:** Need PoE, security panel integration, fleet management. Targeted by EP Pro.  
* **Mainstream Smart Home:** Expect mobile app setup, zero configuration. Currently served only by Aqara with significant limitations.

# **Competitor Deep Dive**

## **Product Sensor Matrix**

| Vendor | Static Radar | Tracking Radar | Fusion Product | PIR |
| :---- | :---- | :---- | :---- | :---- |
| Screek | ✓ | ✓ | ✗ | ✗ |
| Apollo Automation | ✓ | ✓ | ✓ (R PRO-1) | ✗ |
| Everything Presence | ✓ | ✓ | ✓ (EP Pro) | ✓ |
| Sensy | ✗ | ✓ | ✗ | ✗ |
| Aqara | Proprietary | Proprietary | ✓ | ✗ |

## **Everything Presence Pro (NEW \- December 2025\)**

**Position:** Premium fusion device targeting power users and professional installers. £67 (\~$85-90 USD). Currently backordered.

### **Hardware Specifications**

* **TriSense Architecture:** DFRobot SEN0609 (long-range static, 25m) \+ HiLink LD2450 (tracking, 6m) \+ Panasonic PIR (12m)  
* **Power Options:** PoE (802.3af), USB-C, or 12-24V DC wiring  
* **Security Features:** Physical tamper switch, alarm output headers for panel integration  
* **Additional Sensors:** BH1750 light sensor, optional CO2 module slot, RGBW status LED

### **Strategic Analysis**

**Key insight from creator:** "Most customers didn't understand that EP-1 was for static presence while EP-Lite was for motion tracking." The EP Pro attempts to solve customer confusion with hardware (give them everything) rather than UX (make it simple).

### **Strengths**

* Most comprehensive sensor suite in the market (3 detection technologies)  
* PoE support addresses loudly-requested feature from existing customers  
* Security panel integration opens professional installer channel  
* Aggressive price point (\~$85-90) for feature set compresses competitor margins

### **Weaknesses**

* TriSense creates UX complexity: users must understand which sensor to use for which automation  
* More sensors \= more Home Assistant entities \= more configuration burden  
* Zone Configurator still requires manual setup as HA add-on  
* OTA updates remain manual (download from GitHub, upload via web UI)  
* No mobile app, no cloud services  
* Sensor fusion logic exposed to user rather than abstracted

## **Sensy One (S1 Pro)**

**Position:** UX leader in single-radar segment. LD2450 only. Premium pricing (\~$87 delivered to US).

**Upcoming:** PoE-powered sensor cluster variant expected February 2026, still single LD2450 radar.

### **Strategic Analysis**

Sensy's success with LD2450-only validates that single-radar products remain viable. Their expansion strategy focuses on form factors (PoE, sensor clusters) rather than sensor fusion, suggesting they believe UX and environmental sensing are more valuable differentiators than detection technology.

### **Strengths**

* Best-in-class zone editor with 3D visualization, human avatars, and wall representation  
* Custom firmware with proprietary "holding engine" that reduces false target drops  
* Professional website and documentation  
* Clear product focus: zone tracking specialist

### **Weaknesses**

* Zone editor runs as Home Assistant add-on in Docker container  
* Limited to 3 zones due to using native LD2450 zone implementation  
* OTA updates require manual download and upload  
* No mobile app, no cloud services

## **Apollo Automation (R PRO-1)**

**Position:** Broad product line, community-focused. R PRO-1 is their fusion offering with LD2450 \+ optional LD2412 add-on.

### **Strengths**

* PoE and USB-C power options  
* Modular approach: LD2412 static radar is optional add-on  
* Active community engagement, Open Home advocacy

### **Weaknesses**

* Pushes users to HiLink's RadarToolApp for setup (poor UX)  
* Weak firmware with minimal value-add over stock ESPHome  
* Dual-radar configuration exposes complexity to user  
* Documentation lacks polish

## **Aqara FP2**

**Position:** Only mainstream consumer option. Mobile app onboarding. Closed ecosystem.

### **Strengths**

* Only presence sensor with true mobile app setup experience  
* Professional industrial design and retail packaging  
* 30 zone support  
* Multi-platform support (HomeKit, Alexa, Google, Matter)

### **Weaknesses**

* Deliberately limits MQTT access to force users through cloud  
* Data privacy concerns with mandatory cloud dependency  
* Limited Home Assistant integration compared to ESPHome alternatives  
* No local-only operation option

# **Sensor Fusion: Strategic Analysis**

The market is converging on dual-radar fusion as the solution to the static-vs-tracking detection tradeoff. However, the **implementation approach** matters more than the hardware configuration.

## **Two Approaches to Fusion**

| Aspect | Additive Fusion (Competitors) | Unified Fusion (OpticWorks) |
| :---- | :---- | :---- |
| Philosophy | Expose all sensors to user. Let them decide which to use. | Abstract fusion logic. Present single unified result. |
| User Mental Model | "I have 3 sensors. Which do I trust for this automation?" | "I have presence detection. It works." |
| HA Entities | Multiplied by sensor count. PIR motion, mmWave static, mmWave tracking, etc. | Curated, zone-based. One occupancy entity per zone. |
| Configuration | Per-sensor tuning. Users must understand sensor characteristics. | Simple sliders: "Sensitivity: Low/Med/High" |
| Sensor Disagreement | Exposed to user. "Radar says occupied, PIR says empty." | Resolved in firmware. User sees single confident answer. |

## **Strategic Implications**

**Competitors' fusion weakens their UX position:** Every additional sensor EP Pro and Apollo add creates more complexity that gets passed to the end user. More entities in Home Assistant, more documentation to read, more tuning to perform, more edge cases to debug.

**OpticWorks can let competitors weaken themselves:** By shipping a simple, well-tuned single-radar product first, OpticWorks establishes the UX high ground. When OpticWorks later ships fusion (RS-1 Pro or RS-Vision with camera), the fusion logic will be hidden behind the same simple interface — improving detection without degrading experience.

**The marketing narrative writes itself:** "Other sensors bolt on more hardware and pass the complexity to you. OpticWorks fuses multiple detection methods behind the scenes so you get one reliable answer: someone's there, or they're not."

# **Technical Architecture Comparison**

| Capability | EP Pro | Sensy One | Aqara FP2 | OpticWorks RS-1 |
| :---- | :---- | :---- | :---- | :---- |
| Firmware Base | ESPHome | ESPHome \+ Custom | Proprietary | HardwareOS |
| HA Integration | ESPHome Native API | ESPHome Native API | Matter | ESPHome Native API |
| Sensors | 3 (TriSense) | 1 (LD2450) | Proprietary | 1 (LD2450)\* |
| Zone Limit | 3 native | 3 native | 30 | Unlimited |
| OTA Updates | Manual upload | Manual upload | Cloud push | Cloud push |
| Setup Method | Zone Configurator | HA Add-on | Mobile app | Mobile app \+ AR |
| Local Fallback | Full | Full | Limited | Full |
| Price | \~$85-90 | \~$87 | \~$80 | \~$70-80 |

*\*Future RS-1 Pro and RS-Vision products will add sensors while maintaining unified UX.*

## **ESPHome Licensing Analysis**

ESPHome uses a split licensing model that enables OpticWorks' HardwareOS strategy:

* **GPLv3:** C++/runtime codebase. Any modifications require source disclosure.  
* **MIT:** Python tooling and aioesphomeapi client library.  
* **Protocol Specification:** The Native API uses Protocol Buffers over TCP (port 6053). Publicly documented.

**Strategic implication:** OpticWorks can implement a compatible Native API server in proprietary firmware without GPLv3 obligations. Home Assistant discovers and integrates with HardwareOS devices identically to ESPHome devices, but OpticWorks retains full control over firmware capabilities.

# **OpticWorks Product Strategy**

## **Product Roadmap**

| Product | Sensors | Target Segment | Price Point |
| :---- | :---- | :---- | :---- |
| RS-1 Lite | LD2410 (presence) | Utility rooms | \~$49 |
| RS-1 Pro | LD2410 \+ LD2450 (dual radar) | Living spaces, zone tracking | \~$89 |
| RS-1 Lite + PoE | LD2410 \+ PoE | Utility + installers | \~$79 |
| RS-1 Pro + PoE | LD2410 \+ LD2450 \+ PoE | Premium, whole-home | \~$119 |
| RS-Vision | LD2450 \+ Camera \+ ML | Premium, semantic detection | \~$120-150 |

## **MVP Strategy: RS-1 Lite/Pro**

**Product lineup:** RS-1 Lite (LD2410 only, $49) for utility rooms. RS-1 Pro (LD2410 + LD2450 dual radar, $89) for living spaces. Single PCBA with selective population.

**Strategic rationale:** 

1. **Fastest path to market.** Beta launch January 2026\. Adding dual-radar fusion delays timeline.  
2. **Validates what matters.** The beta proves HardwareOS, cloud OTA, mobile onboarding, and zone UX. These work identically whether single or dual radar.  
3. **Preserves the leapfrog.** EP Pro and Apollo are fighting over radar+radar+PIR. RS-Vision with camera fusion jumps past all of them. Don't get distracted winning a race you're about to make irrelevant.  
4. **Competitors' fusion helps OpticWorks.** Every fusion product competitors ship adds UX complexity and generates user complaints that inform OpticWorks' approach.  
5. **Clear upgrade path.** RS-1 users who need static detection can upgrade to RS-1 Pro or RS-Vision when available.

## **Competitive Advantages**

6. **Aqara-level onboarding \+ Full HA compatibility:** No other product offers both. Mobile app setup with native ESPHome API integration.  
7. **Unlimited software-defined zones:** Every ESPHome competitor is limited to 3 zones. HardwareOS processes raw coordinates in software.  
8. **Cloud-push OTA updates:** Automatic firmware updates. No manual download/upload workflow.  
9. **AR-based room scanning:** Novel setup experience generating zones from actual room geometry.  
10. **Unified fusion (future):** When OpticWorks ships fusion products, complexity is abstracted rather than exposed.  
11. **Price advantage:** Single-radar at $70-80 undercuts EP Pro ($85-90) while delivering superior UX.

## **Positioning Statement**

*"The presence sensor that doesn't require a PhD to set up. One device, one app, 60 seconds. Other sensors bolt on more hardware and pass the complexity to you. OpticWorks just works."*

## **Target Customer Segments**

* **Primary:** Prosumers who want Home Assistant reliability without ESPHome complexity. Willing to pay $70-80 for "it just works" experience.  
* **Secondary:** Technical users frustrated with competitor limitations (3 zones, manual updates, poor setup UX).  
* **Future:** Mainstream smart home users as mobile app and cloud features mature.

## **Business Model**

* **Hardware:** One-time purchase at $70-80, undercutting EP Pro  
* **Free tier:** LAN operation, Home Assistant integration, basic zones, local web UI  
* **Cloud subscription (optional):** Remote access, cloud visualization, AR room scanning, advanced analytics, multi-site management ($3-5/month)

# **Risks and Mitigations**

| Risk | Mitigation |
| :---- | :---- |
| Single-radar can't detect completely stationary occupants | Position RS-1 for active living spaces. Clear messaging about use cases. RS-1 Pro addresses gap for users who need it. |
| EP Pro price ($85-90) compresses margins | Undercut at $70-80. Win on UX, not feature parity. Cloud subscription provides recurring revenue. |
| ESPHome community perception of "closed" firmware | Emphasize local-first operation, open Native API protocol, transparency about data practices. |
| Native API protocol changes break compatibility | Monitor ESPHome releases, maintain compatibility test suite, version negotiation in protocol. |
| PoE demand (validated by EP Pro feedback) | RS-1 PoE variant planned for Q2-Q3 2026 after core product validation. |
| Competitors copy unified fusion approach | First-mover advantage on UX. ESPHome architecture makes it harder for competitors to abstract complexity. |

# **Conclusion**

The presence detection market is splitting into two directions: competitors adding hardware complexity (sensor fusion) and OpticWorks adding UX simplicity (unified abstraction). This divergence creates opportunity.

EP Pro and Apollo's fusion products will generate user feedback about configuration complexity, entity proliferation, and sensor disagreement edge cases. This feedback will inform OpticWorks' eventual fusion implementation while validating the UX-first thesis.

The RS-1 MVP should ship with single radar (LD2450), proving the HardwareOS platform, cloud infrastructure, and mobile onboarding experience. Fusion comes later — and when it does, it will demonstrate that more sensors doesn't have to mean more complexity.

**Strategic north star: The user should never see 'radar 1 says X, radar 2 says Y.' They should see a single confident answer.**