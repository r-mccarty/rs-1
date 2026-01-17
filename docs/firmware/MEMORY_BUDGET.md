# HardwareOS Memory Budget Specification

Version: 0.2
Date: 2026-01-15
Owner: OpticWorks Firmware
Status: Draft

---

## 1. Purpose

This document provides a unified memory budget for HardwareOS on the ESP32-WROOM-32E platform. It consolidates resource requirements from all modules and identifies constraints that affect system design decisions.

**Critical:** This document addresses RFD-001 issue C4 (TLS memory not budgeted). TLS connections require significant heap memory that was previously unaccounted for.

**Note:** The memory budget differs between RS-1 Lite and RS-1 Pro variants. Pro requires M02/M03 modules; Lite disables them, reducing memory footprint.

---

## 2. Hardware Constraints

### 2.1 ESP32-WROOM-32E Resources

| Resource | Total | Notes |
|----------|-------|-------|
| **Flash** | 8 MB | Partitioned for app, OTA, NVS, logs |
| **SRAM** | 520 KB | Shared between heap and stack |
| **Heap** | ~250 KB | Available after ESP-IDF and stack |
| **Stack** | 16 KB | FreeRTOS task stacks |

### 2.2 Flash Partition Layout

```
Flash Layout (8MB = 0x800000)
════════════════════════════════════════════════════════════

┌──────────────────────────────┐ 0x000000
│      Bootloader (32KB)       │
├──────────────────────────────┤ 0x008000
│    Partition Table (4KB)     │
├──────────────────────────────┤ 0x009000
│         NVS (16KB)           │ ◀── M06 Config Store
├──────────────────────────────┤ 0x00D000
│       OTA Data (8KB)         │ ◀── M07 OTA state
├──────────────────────────────┤ 0x00F000
│                              │
│       App OTA_0 (3MB)        │ ◀── Active firmware
│                              │
├──────────────────────────────┤ 0x30F000
│                              │
│       App OTA_1 (3MB)        │ ◀── Update partition
│                              │
├──────────────────────────────┤ 0x60F000
│     SPIFFS/Logs (256KB)      │ ◀── M09 persistent logs
├──────────────────────────────┤ 0x64F000
│         Reserved             │
└──────────────────────────────┘ 0x800000

════════════════════════════════════════════════════════════
```

---

## 3. Heap Memory Budget

### 3.1 Module Allocations

| Module | Component | Allocation | Notes |
|--------|-----------|------------|-------|
| **M01** | UART ring buffer | 128 bytes | 2 frames × 40 bytes + overhead |
| **M01** | Detection frame struct | 64 bytes | Current + previous frame |
| **M02** | Track state array | 256 bytes | 3 tracks × ~85 bytes |
| **M02** | Kalman filter state | 384 bytes | 3 tracks × 128 bytes per filter |
| **M03** | Zone config cache | 2,048 bytes | 16 zones × 128 bytes |
| **M03** | Zone state array | 512 bytes | 16 zones × 32 bytes |
| **M04** | Smoothing state | 512 bytes | 16 zones × 32 bytes |
| **M05** | Entity registry | 4,000 bytes | 50 entities × 80 bytes |
| **M05** | Protobuf buffers | 2,048 bytes | TX + RX message buffers |
| **M06** | Config shadow | 4,096 bytes | In-memory config copy |
| **M08** | Timer handles | 256 bytes | ~16 timers × 16 bytes |
| **M09** | Log ring buffer | 8,192 bytes | Configurable |
| **M09** | Metrics storage | 1,024 bytes | ~32 metrics × 32 bytes |
| **M11** | HTTP server buffers | 2,048 bytes | Request/response handling |
| **M11** | WebSocket frame buffer | 512 bytes | Target streaming |

**Subtotal (Application):** ~26 KB

### 3.2 TLS Memory Requirements

**Critical:** TLS connections require substantial heap memory that varies by cipher suite and connection state.

| Connection Type | Memory | When Active |
|-----------------|--------|-------------|
| MQTT TLS (mbedTLS) | 33 KB | Persistent when cloud-connected |
| HTTPS OTA download | 33 KB | During OTA only |
| Native API Noise | 8 KB | When HA connected |

**TLS Scenarios:**

| Scenario | TLS Memory | Duration |
|----------|------------|----------|
| Normal operation (MQTT + Native API) | 41 KB | Continuous |
| OTA download (MQTT + HTTPS) | 66 KB | OTA duration (~2 min) |
| Worst case (MQTT + HTTPS + Native API) | 74 KB | Should not occur |

### 3.3 ESP-IDF System Overhead

| Component | Allocation | Notes |
|-----------|------------|-------|
| Wi-Fi driver | 40 KB | Includes buffers |
| lwIP stack | 16 KB | TCP/IP buffers |
| FreeRTOS | 8 KB | Kernel structures |
| NVS cache | 4 KB | Flash cache |
| mDNS | 2 KB | Service records |

**Subtotal (System):** ~70 KB

### 3.4 Total Heap Budget

```
Heap Budget Summary (RS-1 Pro)
════════════════════════════════════════════════════════════

Total Available:                         250 KB
─────────────────────────────────────────────────
System overhead (ESP-IDF):               - 70 KB
Application modules:                     - 26 KB
TLS (normal operation):                  - 41 KB
─────────────────────────────────────────────────
Available (normal):                       113 KB  ◀── COMFORTABLE

During OTA (additional HTTPS TLS):       - 33 KB
─────────────────────────────────────────────────
Available (during OTA):                    80 KB  ◀── SAFE

Safety margin target:                      20 KB
═════════════════════════════════════════════════

STATUS: HEALTHY - 50KB+ headroom in all scenarios
```

### 3.5 Variant Comparison

| Scenario | Pro | Lite | Notes |
|----------|-----|------|-------|
| Normal operation | 113 KB free | ~120 KB free | Lite saves ~7 KB (no M02/M03) |
| During OTA | 80 KB free | ~87 KB free | Both variants safe |
| Worst case | 47 KB free | ~54 KB free | MQTT + HTTPS + Native API |

---

## 4. Memory Management Strategies

### 4.1 OTA Memory Conservation

To ensure OTA succeeds without heap exhaustion:

1. **Disconnect Native API during OTA**
   - Release 8 KB Noise encryption state
   - HA will reconnect after reboot

2. **Reduce log buffer during OTA**
   - Temporarily reduce from 8 KB to 2 KB
   - Restore after OTA completes

3. **Abort OTA on low heap**
   - Monitor `esp_get_free_heap_size()`
   - Abort if free heap < 20 KB
   - Publish failure status, retry later

```c
// OTA heap check
#define OTA_MIN_FREE_HEAP_KB 20

bool ota_heap_check(void) {
    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < OTA_MIN_FREE_HEAP_KB * 1024) {
        ESP_LOGW(TAG, "OTA aborted: free heap %d KB < %d KB",
                 free_heap / 1024, OTA_MIN_FREE_HEAP_KB);
        return false;
    }
    return true;
}
```

### 4.2 Connection Multiplexing

Consider using single TLS connection for multiple MQTT topics:
- OTA triggers and telemetry share one connection
- Reduces TLS overhead from 66 KB to 33 KB during OTA

### 4.3 Static Allocation Policy

Prefer static allocation over dynamic where possible:

| Prefer Static | Prefer Dynamic |
|---------------|----------------|
| Zone config (fixed max) | Log buffer (configurable) |
| Track state (fixed 3 targets) | HTTP response (variable size) |
| Entity registry (fixed max) | WebSocket frames (streaming) |

---

## 5. NVS Flash Budget

### 5.1 NVS Partition Usage

| Namespace | Item | Size | Notes |
|-----------|------|------|-------|
| `zones` | Zone configs | 4 KB | 16 zones × 256 bytes |
| `zones_prev` | Rollback copy | 4 KB | Previous valid config |
| `device` | Device settings | 256 bytes | Sensitivity, name |
| `network` | Wi-Fi credentials | 256 bytes | SSID, password |
| `security` | Keys and certs | 512 bytes | Noise PSK, device cert |
| `ota` | OTA state | 64 bytes | Current version, flags |

**Total NVS Usage:** ~9 KB of 16 KB available

### 5.2 NVS Wear Considerations

**Critical:** NVS has limited write endurance (~100,000 erase cycles).

| Operation | Frequency | Cycles/Day |
|-----------|-----------|------------|
| Zone config update | User-initiated | < 10 |
| OTA state update | Per OTA | < 1 |
| Device settings | User-initiated | < 5 |

**Policy:** Commit to NVS only on actual config changes, never on a timer.

See M06 and M08 specifications for NVS commit policy details.

---

## 6. Stack Memory Budget

### 6.1 Task Stack Allocations

| Task | Stack Size | Priority | Notes |
|------|------------|----------|-------|
| Main/App | 4 KB | Normal | Application logic |
| Wi-Fi | 3.5 KB | High | ESP-IDF managed |
| TCP/IP | 2 KB | High | lwIP callbacks |
| Radar RX | 2 KB | High | UART ISR processing |
| Native API | 2 KB | Normal | Protobuf handling |
| OTA | 4 KB | Low | Download and flash |

**Total Stack:** ~18 KB (may exceed 16 KB budget)

### 6.2 Stack Optimization

- Share OTA task stack with other low-priority tasks
- Use stack high-water-mark monitoring in debug builds
- Avoid deep recursion in zone evaluation

---

## 7. Memory Profiling

### 7.1 Telemetry Metrics

| Metric | Description | Warning Threshold |
|--------|-------------|-------------------|
| `system.free_heap` | Current free heap | < 30 KB |
| `system.min_free_heap` | Lowest free heap since boot | < 20 KB |
| `system.heap_fragmentation` | Fragmentation percentage | > 30% |
| `nvs.used_bytes` | NVS usage | > 14 KB |

### 7.2 Debug Commands

```bash
# Via M09 logging console
> heap          # Show current heap stats
> heap_trace    # Start heap tracing (debug builds)
> nvs_stats     # Show NVS usage
> stack_hwm     # Show task stack high water marks
```

---

## 8. Capacity Limits

Based on the memory budget, the following limits apply:

| Resource | Limit | Constraint |
|----------|-------|------------|
| Maximum zones | 24 | ~3 KB zone config allocation |
| Maximum vertices per zone | 12 | 192 bytes per zone |
| Maximum entities | 75 | 6 KB entity registry |
| Maximum log buffer | 16 KB | Heap availability |
| Simultaneous TLS connections | 3 | Heap during OTA |

**Note:** ESP32-WROOM-32E provides ~250 KB heap, more than sufficient for dual-radar fusion.

---

## 9. Failure Modes

### 9.1 Heap Exhaustion

| Trigger | Symptom | Recovery |
|---------|---------|----------|
| OTA + Native API + MQTT | malloc returns NULL | OTA abort, retry |
| Memory leak | Gradual free heap decline | Watchdog reset |
| Fragmentation | malloc fails despite free space | Reboot |

### 9.2 Monitoring and Prevention

```c
// Periodic heap check (M08 scheduler)
void heap_monitor(void) {
    size_t free = esp_get_free_heap_size();
    size_t min_free = esp_get_minimum_free_heap_size();

    log_metric("system.free_heap", free);
    log_metric("system.min_free_heap", min_free);

    if (free < 15 * 1024) {
        ESP_LOGW(TAG, "Low heap warning: %d KB free", free / 1024);
        // Consider releasing optional resources
    }

    if (free < 10 * 1024) {
        ESP_LOGE(TAG, "Critical heap: %d KB free, rebooting", free / 1024);
        esp_restart();
    }
}
```

---

## 10. Validation Checklist

Before implementation, verify:

- [ ] All module allocations fit within 26 KB application budget
- [ ] OTA can complete with 30 KB free heap
- [ ] NVS usage stays under 14 KB (leaving 2 KB margin)
- [ ] Stack sizes measured with high-water-mark testing
- [ ] No timer-based NVS commits (only on change)
- [ ] Heap monitoring enabled in production builds

---

## 11. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-09 | OpticWorks | Initial specification, addresses RFD-001 issue C4 |
| 0.2 | 2026-01-15 | OpticWorks | Updated for ESP32-WROOM-32E (520KB SRAM, 8MB flash), added variant comparison |
