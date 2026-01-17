# Device Provisioning Protocol

Version: 0.2
Date: 2026-01-13
Owner: OpticWorks Firmware + Mobile + Cloud
Status: Draft

---

## 1. Purpose

This document defines the complete provisioning flow for RS-1 devices, from factory state to fully operational. It covers:

- QR code format and deep linking
- Device AP mode and captive portal
- Local provisioning API
- Cloud registration via MQTT
- Automatic ownership via purchase records

**This is the single source of truth for provisioning.** Related specs (M10 Security, Device Registry) should reference this document.

---

## 2. Provisioning Overview

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Mobile    │     │   RS-1      │     │   EMQX      │     │  OpticWorks │
│   Phone     │     │   Device    │     │   Broker    │     │  Cloud API  │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │                   │
       │  1. Scan QR code  │                   │                   │
       │  (deep link with device_id + AP name)                     │
       │                   │                   │                   │
       │  2. Connect to device AP              │                   │
       │──────────────────>│                   │                   │
       │                   │                   │                   │
       │  3. Open provisioning UI              │                   │
       │  (app or captive portal at 192.168.4.1)                   │
       │──────────────────>│                   │                   │
       │                   │                   │                   │
       │  4. GET /api/device-info              │                   │
       │<──────────────────│                   │                   │
       │                   │                   │                   │
       │  5. POST /api/provision               │                   │
       │  {ssid, password}                     │                   │
       │──────────────────>│                   │                   │
       │                   │                   │                   │
       │  6. 202 Accepted  │                   │                   │
       │<──────────────────│                   │                   │
       │                   │                   │                   │
       │                   │  7. Connect to home WiFi              │
       │                   │─ ─ ─ ─ ─ ─ ─ ─ ─ >│                   │
       │                   │                   │                   │
       │                   │  8. MQTT CONNECT  │                   │
       │                   │──────────────────>│                   │
       │                   │                   │                   │
       │                   │  9. PUBLISH       │                   │
       │                   │  opticworks/{device_id}/provision     │
       │                   │──────────────────>│──────────────────>│
       │                   │                   │                   │
       │                   │                   │  10. Lookup MAC   │
       │                   │                   │  in purchase DB   │
       │                   │                   │  → auto-link owner│
       │                   │                   │<──────────────────│
       │                   │                   │                   │
       │  11. Push notification (if owner found)                   │
       │  "RS-1 connected!"                    │                   │
       │<──────────────────────────────────────────────────────────│
       │                   │                   │                   │
       │  12. Open Zone Editor                 │                   │
       │──────────────────>│                   │                   │
       │                   │                   │                   │
```

**Key simplification:** Device ownership is determined by purchase records, not by tokens passed during provisioning. The cloud looks up the device MAC in the order database to find the purchaser.

---

## 3. QR Code Specification

### 3.1 Format

QR codes use a custom URI scheme with web fallback:

```
opticworks://setup?d={mac_suffix}&ap={ap_ssid}
```

| Parameter | Format | Example | Description |
|-----------|--------|---------|-------------|
| `d` | 12-char hex | `a1b2c3d4e5f6` | MAC suffix (last 6 bytes of MAC, hex-encoded) - used for AP lookup |
| `ap` | String | `OpticWorks-E5F6` | AP SSID (always `OpticWorks-` + last 4 of MAC suffix) |

**Note:** The `d` parameter is the MAC suffix, NOT the device_id. The actual `device_id` used in MQTT and cloud is a 32-char SHA-256 derived hash (see Section 7.1).

**Example QR payload:**
```
opticworks://setup?d=a1b2c3d4e5f6&ap=OpticWorks-E5F6
```

### 3.2 Deep Link Behavior

| Scenario | Behavior |
|----------|----------|
| OpticWorks app installed | App opens directly to setup flow |
| App not installed (iOS) | Opens App Store via Universal Links |
| App not installed (Android) | Opens Play Store via App Links |
| Web fallback | Redirects to `https://setup.opticworks.io/?d={d}&ap={ap}` |

### 3.3 Web Fallback

If the app is not installed and the user dismisses the store prompt, the web fallback provides:

1. Instructions to connect to the device AP manually
2. Link to open `http://192.168.4.1` after connecting
3. Option to download the app

### 3.4 QR Code Generation (Manufacturing)

QR codes are printed on device labels during manufacturing:

```typescript
function generateProvisioningQR(macAddress: string): string {
  // Extract last 6 bytes of MAC for QR code (human-readable suffix)
  const macSuffix = macAddress.replace(/:/g, '').toLowerCase().slice(-12);

  // Generate AP SSID (last 4 chars of MAC suffix)
  const apSuffix = macSuffix.slice(-4).toUpperCase();
  const apSsid = `OpticWorks-${apSuffix}`;

  return `opticworks://setup?d=${macSuffix}&ap=${apSsid}`;
}

// Device ID derivation (used in MQTT topics and cloud)
function deriveDeviceId(macAddress: string): string {
  const normalized = macAddress.replace(/:/g, '').toLowerCase();
  const hash = crypto.createHash('sha256')
    .update(normalized + 'opticworks-rs1')
    .digest('hex');
  return hash.substring(0, 32); // 32-char device_id
}
```

---

## 4. Device AP Mode

### 4.1 AP Configuration

When unconfigured (no valid WiFi credentials), the device starts in AP mode:

| Parameter | Value |
|-----------|-------|
| SSID | `OpticWorks-{XXXX}` (last 4 of device_id, uppercase) |
| Password | None (open network) |
| Channel | 1 (fixed for reliability) |
| IP Address | `192.168.4.1` |
| DHCP Range | `192.168.4.100` - `192.168.4.200` |
| DNS | Captive portal redirect to `192.168.4.1` |

### 4.2 AP Mode Entry Conditions

Device enters AP mode when:

1. **First boot**: No WiFi credentials stored in NVS
2. **WiFi failure**: Cannot connect to stored network after 5 attempts with backoff
3. **Factory reset**: User holds reset button for 10 seconds
4. **Provisioning button**: User presses provisioning button (if equipped)

### 4.3 AP Mode Exit Conditions

Device exits AP mode when:

1. **Provisioning complete**: Valid credentials received and WiFi connected
2. **Timeout**: 10 minutes with no client activity (enters deep sleep, wakes on button)

### 4.4 Captive Portal

The device runs a captive portal that intercepts DNS and HTTP requests:

```c
// Captive portal DNS: all queries resolve to 192.168.4.1
// Captive portal HTTP: redirect all requests to provisioning UI

typedef struct {
    httpd_handle_t server;
    dns_server_handle_t dns;
    bool captive_active;
} provisioning_ap_t;
```

**Captive portal detection endpoints:**

| Platform | Probe URL | Expected Response |
|----------|-----------|-------------------|
| iOS | `http://captive.apple.com/hotspot-detect.html` | Redirect to `/` |
| Android | `http://connectivitycheck.gstatic.com/generate_204` | Redirect to `/` |
| Windows | `http://www.msftconnecttest.com/connecttest.txt` | Redirect to `/` |

---

## 5. Provisioning HTTP API

The device exposes a local HTTP API on `192.168.4.1:80` during AP mode.

### 5.1 GET /api/device-info

Returns device information for the provisioning UI.

**Request:**
```http
GET /api/device-info HTTP/1.1
Host: 192.168.4.1
```

**Response:**
```json
{
  "device_id": "a1b2c3d4e5f600112233445566778899",
  "mac_suffix": "d4e5f6",
  "firmware_version": "1.0.0",
  "hardware_version": "1.0",
  "product": "RS-1",
  "manufacturer": "OpticWorks",
  "mac_address": "AA:BB:CC:D4:E5:F6",
  "provisioning_version": "1.0"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `device_id` | string | 32-char hex device identifier (SHA-256 derived from MAC) |
| `mac_suffix` | string | 6-char hex MAC suffix (last 3 bytes, for display/AP naming) |
| `firmware_version` | string | Semantic version of firmware |
| `hardware_version` | string | Hardware revision |
| `product` | string | Product name (`RS-1`) |
| `manufacturer` | string | Always `OpticWorks` |
| `mac_address` | string | Full MAC address |
| `provisioning_version` | string | Provisioning protocol version |

### 5.2 GET /api/networks

Scans for available WiFi networks.

**Request:**
```http
GET /api/networks HTTP/1.1
Host: 192.168.4.1
```

**Response:**
```json
{
  "networks": [
    {
      "ssid": "HomeNetwork",
      "rssi": -45,
      "security": "wpa2",
      "channel": 6
    },
    {
      "ssid": "Neighbor5G",
      "rssi": -72,
      "security": "wpa3",
      "channel": 36
    }
  ],
  "scan_time_ms": 3200
}
```

| Field | Type | Description |
|-------|------|-------------|
| `networks[].ssid` | string | Network SSID |
| `networks[].rssi` | integer | Signal strength (dBm) |
| `networks[].security` | enum | `open`, `wep`, `wpa`, `wpa2`, `wpa3` |
| `networks[].channel` | integer | WiFi channel |
| `scan_time_ms` | integer | Time spent scanning |

### 5.3 POST /api/provision

Submits WiFi credentials for provisioning.

**Request:**
```http
POST /api/provision HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "ssid": "HomeNetwork",
  "password": "secretpassword"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `ssid` | string | Yes | Target WiFi SSID |
| `password` | string | Yes | WiFi password (can be empty for open networks) |

**Response (success):**
```http
HTTP/1.1 202 Accepted
Content-Type: application/json

{
  "status": "provisioning",
  "message": "Connecting to WiFi...",
  "timeout_sec": 30
}
```

**Response (validation error):**
```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "error": "invalid_ssid",
  "message": "SSID cannot be empty"
}
```

### 5.4 GET /api/provision/status

Polls provisioning status (for web UI without WebSocket).

**Request:**
```http
GET /api/provision/status HTTP/1.1
Host: 192.168.4.1
```

**Response:**
```json
{
  "status": "connecting",
  "step": 2,
  "total_steps": 4,
  "message": "Connecting to WiFi...",
  "error": null
}
```

| Status | Step | Description |
|--------|------|-------------|
| `idle` | 0 | Waiting for credentials |
| `connecting` | 1 | Connecting to WiFi |
| `connected` | 2 | WiFi connected, getting IP |
| `registering` | 3 | Registering with cloud |
| `complete` | 4 | Provisioning successful |
| `failed` | - | Error occurred (see `error` field) |

**Error codes:**

| Error | Description |
|-------|-------------|
| `wifi_auth_failed` | Wrong WiFi password |
| `wifi_not_found` | SSID not found |
| `wifi_timeout` | Connection timeout |
| `cloud_unreachable` | Cannot reach MQTT broker |
| `cloud_timeout` | Cloud registration timeout |

### 5.5 WebSocket /api/provision/ws

Real-time provisioning status for app integration.

**Connection:**
```
ws://192.168.4.1/api/provision/ws
```

**Server messages:**
```json
{"type": "status", "status": "connecting", "step": 1, "message": "Connecting to WiFi..."}
{"type": "status", "status": "connected", "step": 2, "message": "WiFi connected"}
{"type": "status", "status": "registering", "step": 3, "message": "Registering with cloud..."}
{"type": "status", "status": "complete", "step": 4, "message": "Setup complete!"}
{"type": "error", "error": "wifi_auth_failed", "message": "Incorrect WiFi password"}
```

### 5.6 Provisioning UI (Captive Portal)

The device serves a minimal web UI at `http://192.168.4.1/`:

```
┌────────────────────────────────────────┐
│         OpticWorks RS-1 Setup          │
├────────────────────────────────────────┤
│                                        │
│  Select your WiFi network:             │
│  ┌────────────────────────────────┐    │
│  │ HomeNetwork          ▓▓▓▓░ -45│    │
│  │ Neighbor5G           ▓▓░░░ -72│    │
│  │ GuestWiFi            ▓░░░░ -80│    │
│  └────────────────────────────────┘    │
│                                        │
│  Password: [________________]          │
│                                        │
│  [        Connect         ]            │
│                                        │
│  ─────────────────────────────────     │
│  Device: OpticWorks-E5F6               │
│  Firmware: 1.0.0                       │
│                                        │
└────────────────────────────────────────┘
```

**UI requirements:**
- Single HTML file with embedded CSS/JS (< 32KB)
- Works offline (no external resources)
- Responsive for mobile browsers
- Polls `/api/provision/status` every 1 second during provisioning

---

## 6. WiFi Connection Flow

### 6.1 Connection Sequence

```c
esp_err_t provisioning_connect_wifi(const char *ssid, const char *password) {
    // 1. Store credentials temporarily (not in NVS yet)
    // 2. Stop AP mode
    // 3. Configure STA mode
    // 4. Connect with timeout (15 seconds)
    // 5. On success: store to NVS, proceed to cloud registration
    // 6. On failure: restart AP mode, report error
}
```

### 6.2 Connection Timeout

| Attempt | Timeout | Backoff |
|---------|---------|---------|
| 1 | 15s | - |
| 2 | 15s | 2s |
| 3 | 15s | 5s |
| 4 | 15s | 10s |
| 5 | 15s | Fail, restart AP |

### 6.3 Credential Storage

WiFi credentials are stored in NVS after successful WiFi connection:

```c
// Commit after WiFi connection success
if (wifi_connected) {
    config_store_set_network(ssid, password);
    config_store_set_provisioned(true);
    config_store_commit();
}

// Cloud registration happens after, but WiFi creds are already saved
// This ensures device works locally even if cloud is unreachable
```

---

## 7. Cloud Registration (MQTT)

### 7.1 Provisioning Topic

After WiFi connection, device publishes to registration topic:

**Topic:** `opticworks/{device_id}/provision`
**QoS:** 1
**Retain:** No

**Note:** `{device_id}` is the 32-char SHA-256 derived identifier (see Section 3.4).

**Payload:**
```json
{
  "device_id": "a1b2c3d4e5f600112233445566778899",
  "mac_address": "AA:BB:CC:D4:E5:F6",
  "firmware_version": "1.0.0",
  "timestamp": "2026-01-13T10:00:00Z"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `device_id` | string | Yes | 32-char hex device identifier (SHA-256 derived) |
| `mac_address` | string | Yes | Full MAC address |
| `firmware_version` | string | Yes | Current firmware version |
| `timestamp` | string | Yes | ISO 8601 timestamp |

### 7.2 Ownership Lookup

Cloud determines ownership by looking up the MAC address in the purchase database:

```typescript
async function lookupOwner(macAddress: string): Promise<string | null> {
  // Orders table has: { order_id, user_email, device_mac, shipped_at }
  const order = await db.execute(
    'SELECT user_id FROM orders WHERE device_mac = ? AND shipped_at IS NOT NULL',
    [macAddress]
  );

  return order.length > 0 ? order[0].user_id : null;
}
```

**Ownership scenarios:**

| Scenario | Result |
|----------|--------|
| MAC found in orders DB | Device auto-linked to purchaser |
| MAC not found | Device registered but unowned |
| Device resold/gifted | Original owner can transfer via support |

### 7.3 Provisioning Response

Cloud responds on:

**Topic:** `opticworks/{device_id}/provision/response`

**Success (owner found):**
```json
{
  "status": "registered",
  "device_id": "a1b2c3d4e5f600112233445566778899",
  "owner": "user_abc123",
  "timestamp": "2026-01-13T10:00:01Z"
}
```

**Success (no owner):**
```json
{
  "status": "registered",
  "device_id": "a1b2c3d4e5f600112233445566778899",
  "owner": null,
  "timestamp": "2026-01-13T10:00:01Z"
}
```

### 7.4 Unowned Devices

Devices without a purchase record still function fully:

1. Local Home Assistant integration works
2. Local Zone Editor works (see Section 8)
3. Cloud features (remote access, OTA) require ownership
4. User can claim via OpticWorks account + proof of purchase (support flow)

---

## 8. Device Authentication

### 8.1 Local Access (Zone Editor on LAN)

Devices use standard HTTP authentication for local access:

| Parameter | Value |
|-----------|-------|
| Username | `admin` |
| Default Password | Unique per device, printed on label |
| Auth Method | HTTP Basic Auth or session cookie |

**Default password format:** 8-character alphanumeric, printed on device label next to QR code.

Example label:
```
┌─────────────────────────────────┐
│  OpticWorks RS-1                │
│  [QR CODE]                      │
│                                 │
│  WiFi: OpticWorks-E5F6          │
│  Password: Xk7mP2nQ             │
│  MAC: AA:BB:CC:D4:E5:F6         │
└─────────────────────────────────┘
```

**Password can be changed** via local web UI at `http://{device_ip}/settings`.

### 8.2 Cloud Access (Zone Editor via OpticWorks)

Cloud access uses standard OAuth:

```
┌─────────────────────────────────────────────────────┐
│  https://app.opticworks.io/devices/a1b2c3d4e5f6    │
│                                                     │
│  ┌───────────────────────────────────────────────┐ │
│  │                                               │ │
│  │     Sign in to manage your RS-1              │ │
│  │                                               │ │
│  │     [  Continue with Google  ]               │ │
│  │     [  Continue with Apple   ]               │ │
│  │     [  Sign in with Email    ]               │ │
│  │                                               │ │
│  └───────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
```

- User authenticates with OpticWorks account
- Cloud verifies user owns the device (purchase record lookup)
- Cloud proxies commands to device via MQTT

### 8.3 Authentication Summary

| Access Method | Auth Mechanism | When to Use |
|---------------|----------------|-------------|
| Local (LAN) | HTTP Basic Auth with device password | Home network, no internet needed |
| Cloud (WAN) | OAuth with OpticWorks account | Remote access, multiple devices |

---

## 9. Factory Reset

### 9.1 Trigger Methods

| Method | Action |
|--------|--------|
| Hardware button | Hold reset button for 10 seconds |
| Local web UI | Settings → Factory Reset (requires device password) |
| Cloud command | MQTT `opticworks/{device_id}/command/factory-reset` |

### 9.2 Reset Behavior

```c
void provisioning_factory_reset(void) {
    // 1. Erase WiFi credentials from NVS
    config_store_erase_network();

    // 2. Erase zone configuration
    config_store_erase_zones();

    // 3. Reset device password to default (from flash)
    config_store_reset_password();

    // 4. Clear provisioned flag
    config_store_set_provisioned(false);

    // 5. Keep device identity (device_id, device_secret)
    // 6. Keep firmware (no rollback)

    // 7. Reboot into AP mode
    esp_restart();
}
```

**Preserved after reset:**
- Device ID and secret
- Firmware version
- eFuse-burned data (secure boot keys, anti-rollback counter)
- Default device password (burned in flash at manufacturing)

**Erased on reset:**
- WiFi credentials
- Zone configuration
- Custom device password
- Telemetry opt-in preference

---

## 10. Error Handling

### 10.1 WiFi Connection Errors

| Error | User Message | Recovery |
|-------|--------------|----------|
| `WIFI_REASON_AUTH_FAIL` | "Incorrect password" | Show password field again |
| `WIFI_REASON_NO_AP_FOUND` | "Network not found" | Rescan and show network list |
| `WIFI_REASON_ASSOC_FAIL` | "Could not connect" | Retry with backoff |
| Timeout | "Connection timed out" | Retry or show network list |

### 10.2 Cloud Registration Errors

| Error | User Message | Recovery |
|-------|--------------|----------|
| MQTT connect fail | "Cannot reach OpticWorks" | Retry, check internet |
| Timeout | "Cloud registration timed out" | Device works locally, retries on next boot |

### 10.3 Graceful Degradation

If cloud registration fails but WiFi is connected:

1. WiFi credentials already saved (see 6.3)
2. Device operates in local-only mode
3. Retry cloud registration on next boot
4. User can still use with Home Assistant
5. Local Zone Editor works with device password

---

## 11. Security Considerations

### 11.1 Open AP Network

The provisioning AP is intentionally open (no password) for ease of setup. Mitigations:

| Risk | Mitigation |
|------|------------|
| Eavesdropping | No sensitive data transmitted over AP |
| Rogue provisioning | Device accepts provisioning only in AP mode; user must physically access device |
| AP spoofing | QR code contains expected AP name; app warns if mismatch |

### 11.2 WiFi Password Handling

- Password received via HTTP (within AP network, not internet)
- Password stored encrypted in NVS (see M06 Config Store)
- Password never transmitted to cloud
- Password cleared on factory reset

### 11.3 Device Password Security

- Default password is unique per device (generated at manufacturing)
- Password printed on physical label (requires physical access)
- User can change password after setup
- Password stored hashed in NVS

### 11.4 Cloud Authentication

- OAuth 2.0 with industry-standard providers
- Device ownership verified via purchase records
- No sensitive tokens stored on device

---

## 12. Implementation Notes

### 12.1 Memory Budget (ESP32-WROOM-32E)

| Component | RAM Usage |
|-----------|-----------|
| HTTP server | ~8KB |
| DNS server | ~2KB |
| WiFi AP + STA | ~40KB |
| Provisioning UI (flash) | 32KB |
| **Total provisioning overhead** | ~50KB |

### 12.2 Firmware Module Integration

| Module | Provisioning Responsibility |
|--------|---------------------------|
| M06 Config Store | Store/retrieve WiFi credentials, device password, provisioned flag |
| M08 Timebase | AP mode timeout, connection timeouts |
| M09 Logging | Provisioning events for diagnostics |
| M10 Security | Password hashing, credential encryption |

### 12.3 State Machine

```
┌──────────────────────────────────────────────────────────────────┐
│                     Provisioning State Machine                    │
│                                                                  │
│  ┌─────────┐    ┌──────────┐    ┌───────────┐    ┌───────────┐  │
│  │ FACTORY │───>│ AP_MODE  │───>│CONNECTING │───>│REGISTERING│  │
│  └─────────┘    └──────────┘    └───────────┘    └───────────┘  │
│       │              │               │                 │         │
│       │              │               │                 ▼         │
│       │              │               │          ┌───────────┐    │
│       │              │               │          │PROVISIONED│    │
│       │              │               │          └───────────┘    │
│       │              │               │                 │         │
│       │              ▼               ▼                 │         │
│       │         ┌─────────┐    ┌─────────┐            │         │
│       │         │ TIMEOUT │    │  FAILED │<───────────┘         │
│       │         └────┬────┘    └────┬────┘  (cloud fail)        │
│       │              │              │                            │
│       │              ▼              ▼                            │
│       │         Deep Sleep     Back to AP                        │
│       │                                                          │
│       └──────────────────────────────────────────────────────────│
│                        Factory Reset                              │
└──────────────────────────────────────────────────────────────────┘
```

---

## 13. Testing

### 13.1 Test Scenarios

| Scenario | Steps | Expected |
|----------|-------|----------|
| Happy path (app) | Scan QR → app opens → enter WiFi → success | Device provisioned, auto-linked to purchaser |
| Happy path (web) | Scan QR → captive portal → enter WiFi → success | Device provisioned, auto-linked if purchased |
| Wrong password | Enter wrong password | Error shown, can retry |
| Network not found | Enter non-existent SSID | Error shown, rescan |
| Cloud unreachable | Block MQTT port | WiFi saved, local-only mode |
| No purchase record | Device MAC not in orders DB | Device works, unowned |
| AP timeout | Don't interact for 10 min | Device sleeps |
| Factory reset | Hold button 10s | Returns to AP mode, password reset |

### 13.2 Mock Server

For development, a mock provisioning server:

```bash
# Start mock server
cd tools/mock-provisioning
python3 mock_server.py --port 80
```

---

## 14. References

| Document | Purpose |
|----------|---------|
| `PROTOCOL_MQTT.md` | MQTT topic structure |
| `../firmware/HARDWAREOS_MODULE_SECURITY.md` | Security implementation |
| `../firmware/HARDWAREOS_MODULE_CONFIG_STORE.md` | Credential storage |
| `../cloud/SERVICE_DEVICE_REGISTRY.md` | Cloud registration |

---

## 15. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 0.1 | 2026-01-13 | OpticWorks | Initial draft |
| 0.2 | 2026-01-13 | OpticWorks | Simplified: removed user_token, added purchase record lookup, standard device password auth |
