# RS-1 Wiring Guide

Connecting the LD2450 mmWave radar to an ESP32-WROOM-32 (30-pin, Type-C, CP2102).

---

## Components

| Component | Description |
|-----------|-------------|
| ESP32-WROOM-32 | 30-pin dev board with Type-C and CP2102 USB-UART |
| LD2450 | Hi-Link 24GHz mmWave radar module |
| Jumper wires | 4x female-to-female (or as needed for your setup) |

---

## Pinout

### LD2450 Module

```
┌─────────────────────┐
│      LD2450         │
│                     │
│  VCC  GND  TX  RX   │
│   │    │   │   │    │
└───┼────┼───┼───┼────┘
    │    │   │   │
    5V  GND  →   ←
            to   to
           ESP  ESP
           RX   TX
```

### ESP32-WROOM-32 (30-pin)

```
        ┌───────────────────┐
        │      USB-C        │
        │      CP2102       │
        └───────────────────┘
               │   │
    ┌──────────┴───┴──────────┐
    │  EN               D23   │
    │  VP               D22   │
    │  VN               TX0   │
    │  D34              RX0   │
    │  D35              D21   │
    │  D32              D19   │
    │  D33              D18   │
    │  D25              D5    │
    │  D26              D17 ◄─┼── TX (to LD2450 RX)
    │  D27              D16 ◄─┼── RX (from LD2450 TX)
    │  D14              D4    │
    │  D12              D2    │
    │  D13              D15   │
    │  GND ◄────────────GND   │
    │  VIN              3V3   │
    │  5V ◄─────────────5V    │
    └─────────────────────────┘
```

---

## Wiring Table

| LD2450 Pin | ESP32 Pin | Wire Color (suggested) |
|------------|-----------|------------------------|
| VCC | 5V | Red |
| GND | GND | Black |
| TX | GPIO16 (RX2) | Yellow |
| RX | GPIO17 (TX2) | Green |

---

## Wiring Diagram

```
    LD2450                          ESP32-WROOM-32
    ┌─────┐                         ┌─────────────┐
    │ VCC ├────── Red ──────────────┤ 5V          │
    │ GND ├────── Black ────────────┤ GND         │
    │ TX  ├────── Yellow ───────────┤ GPIO16 (RX) │
    │ RX  ├────── Green ────────────┤ GPIO17 (TX) │
    └─────┘                         └─────────────┘
```

---

## Notes

1. **Power**: The LD2450 requires 5V. Use the 5V pin on the ESP32, not 3.3V.

2. **UART Levels**: The LD2450 TX/RX pins are 3.3V compatible, so they connect directly to ESP32 GPIO without level shifting.

3. **UART2**: We use GPIO16/17 (UART2) to keep UART0 (TX0/RX0) free for USB serial debugging.

4. **Baud Rate**: The LD2450 communicates at 256000 baud (configured in ESPHome yaml).

5. **Orientation**: Mount the LD2450 with the component side facing the detection area. The radar has a 120° horizontal and 60° vertical field of view.

---

## Verification

After wiring, flash the firmware and check the ESP32 serial output:

```bash
cd ~/workspace/rs-1/firmware/esphome
esphome logs rs1-presence.yaml
```

You should see target detection data when something moves in front of the sensor.

---

## Troubleshooting

| Issue | Cause | Fix |
|-------|-------|-----|
| No data from sensor | TX/RX swapped | Swap GPIO16 and GPIO17 connections |
| Sensor not powering on | Insufficient power | Ensure 5V connection, try different USB cable/port |
| Garbage serial data | Wrong baud rate | Verify 256000 baud in ESPHome config |
| No targets detected | Sensor orientation | Ensure component side faces detection area |
