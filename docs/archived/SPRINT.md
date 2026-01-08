# RS-1 Launch Sprint

**Goal**: List RS-1 presence sensor for sale by January 19, 2025

**Start date**: January 6, 2025
**Target launch**: January 19, 2025
**Duration**: 13 days

---

## Current Assets

| Asset | Status |
|-------|--------|
| LD2450 modules | In hand |
| ESP32 boards | In hand |
| Prototype printer (X1C) | Available |
| E-commerce store | 90% ready (needs Medusa enabled) |
| Capital | Available |

---

## Sprint Schedule

### Phase 1: Firmware (Days 1-2)
**Jan 6-7**

- [ ] Wire LD2450 to ESP32 on breadboard
- [ ] Create basic ESPHome config for LD2450
- [ ] Validate target detection in Home Assistant
- [ ] Test multi-target tracking (up to 3)
- [ ] Confirm position data (X/Y) publishing to HA

**Deliverable**: Working firmware on breadboard, entities visible in HA

### Phase 2: Enclosure (Days 2-4)
**Jan 7-9**

- [ ] Measure LD2450 + ESP32 combined dimensions
- [ ] Design enclosure in CAD (Fusion 360 / OnShape)
- [ ] Print prototype on X1C (FDM)
- [ ] Test fit, iterate if needed
- [ ] Finalize design for resin printing

**Deliverable**: Tested enclosure design, ready for JLCPCB order

### Phase 3: Manufacturing Order (Day 3-4)
**Jan 8-9**

- [ ] Export STL/STEP for JLCPCB
- [ ] Order resin prints from JLCPCB (10-20 units)
- [ ] Confirm shipping timeline (target: arrive by Jan 15)

**Deliverable**: JLCPCB order placed

### Phase 4: Store Setup (Days 5-6)
**Jan 10-11**

- [ ] Enable Medusa backend (`NEXT_PUBLIC_MEDUSA_ENABLED=true`)
- [ ] Add Stripe production keys
- [ ] Configure webhook secrets
- [ ] Set warehouse/ship-from address
- [ ] Run product seed script (or manually add RS-1 product)
- [ ] Update product copy:
  - Title: "RS-1 Presence Sensor"
  - Price: $55 (or $49?)
  - Description: Focus on HA integration, multi-target, no cloud
- [ ] Test checkout flow end-to-end

**Deliverable**: Store live with RS-1 product, checkout working

### Phase 5: Assembly (Days 9-10)
**Jan 14-15**

- [ ] Receive enclosures from JLCPCB
- [ ] Flash firmware to 5-10 ESP32 boards
- [ ] Assemble units (ESP32 + LD2450 + enclosure)
- [ ] Basic QA: power on, connects to WiFi, shows in HA

**Deliverable**: 5-10 assembled, tested units

### Phase 6: Product Photos & Listing (Day 11)
**Jan 16**

- [ ] Product photos (white background, phone is fine)
  - Hero shot (front)
  - Scale shot (hand/coin for size)
  - Internals shot (optional)
- [ ] Upload to store
- [ ] Final listing review

**Deliverable**: Product page complete with photos

### Phase 7: Launch (Days 12-13)
**Jan 17-19**

- [ ] Write r/homeassistant post
  - Keep it authentic: "I built this, here's what it does"
  - No overclaiming
  - Link to store
- [ ] Prepare HA Community post (optional)
- [ ] Launch day: Post to Reddit
- [ ] Monitor for questions/feedback

**Deliverable**: Product live, first post published

---

## Contingency: TUI Differentiator

If initial sales are slow or feedback is "why not Sensy?":

1. **Week of Jan 20-27**: Build TUI visualization layer
2. **Same hardware**: No BOM or inventory changes
3. **Position as v1.1 update**: "Now with terminal-based configuration"

TUI scope:
- Python CLI connecting over WiFi/serial
- Real-time ASCII/Unicode visualization of targets
- Zone configuration via terminal
- Runs on any machine with Python

---

## Success Metrics

| Metric | Target |
|--------|--------|
| Product listed | Jan 19 |
| Units available | 5-10 |
| First sale | Within 7 days of launch |
| Customer conversations | 3+ (buyers or inquiries) |

---

## Risk Register

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| JLCPCB delay | Medium | Order by Jan 8, buffer 2 days |
| Enclosure fit issues | Medium | Prototype on X1C first |
| Store checkout broken | Low | Test before launch |
| No sales | Medium | TUI pivot ready, learn from feedback |
| Negative comparison to Sensy | Medium | Don't overclaim, price fairly |

---

## Daily Standup Questions

1. What did I ship yesterday?
2. What's blocking me today?
3. Am I on track for Jan 19?

---

## Reference: Competitor Positioning

**Sensy S1 Pro**: €55, LD2450, zone editor addon, environmental sensors, 300+ sold

**RS-1 v1.0**: $55, LD2450, no zone editor, no environmental sensors, new entrant

**Honest differentiation for v1.0**: Price parity, simpler (no extras), new option in market

**Future differentiation (v1.1+)**: TUI visualization, terminal-based config, hacker-friendly UX
