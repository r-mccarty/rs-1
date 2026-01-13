# Device Provisioning Protocol

Version: 0.1
Date: 2026-01-13
Owner: OpticWorks Firmware + Mobile + Cloud
Status: Draft

---

## 1. Purpose

This document defines the complete provisioning flow for RS-1 devices, from factory state to fully operational and cloud-registered. It covers:

- QR code format and deep linking
- Device AP mode and captive portal
- Local provisioning API
- Cloud registration via MQTT
- Device claiming and ownership

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
       │  {ssid, password, user_token}         │                   │
       │──────────────────>│                   │                   │
       │                   │                   │                   │
       │  6. 202 Accepted  │                   │                   │
       │<──────────────────│                   │                   │
       │                   │                   │                   │
       │                   │  7. Connect to home WiFi              │
       │                   │─ ─ ─ ─ ─ ─ ─ ─ ─ >│                   │
       │                   │                   │                   │
       │                   │  8. MQTT CONNECT  │                   │
       │                   │  (device_id + provisioning creds)     │
       │                   │──────────────────>│                   │
       │                   │                   │                   │
       │                   │  9. PUBLISH       │                   │
       │                   │  opticworks/{device_id}/provision     │
       │                   │──────────────────>│──────────────────>│
       │                   │                   │                   │
       │                   │                   │  10. Validate &   │
       │                   │                   │      register     │
       │                   │                   │<──────────────────│
       │                   │                   │                   │
       │  11. Push notification / WebSocket    │                   │
       │  "RS-1 connected!"                    │                   │
       │<──────────────────────────────────────────────────────────│
       │                   │                   │                   │
       │                   │  12. SUBSCRIBE    │                   │
       │                   │  opticworks/{device_id}/config/update │
       │                   │<──────────────────│<──────────────────│
       │                   │                   │                   │
       │  13. Open Zone Editor                 │                   │
       │──────────────────>│                   │                   │
       │                   │                   │                   │
```

---

## 3. QR Code Specification

### 3.1 Format

QR codes use a custom URI scheme with web fallback:

```
opticworks://setup?d={device_id}&ap={ap_ssid}
```

| Parameter | Format | Example | Description |
|-----------|--------|---------|-------------|
| `d` | 12-char hex | `a1b2c3d4e5f6` | Device ID (last 6 bytes of MAC, hex-encoded) |
| `ap` | String | `OpticWorks-E5F6` | AP SSID (always `OpticWorks-` + last 4 of device_id) |

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
  // Extract last 6 bytes of MAC
  const deviceId = macAddress.replace(/:/g, '').toLowerCase().slice(-12);

  // Generate AP SSID (last 4 chars of device_id)
  const apSuffix = deviceId.slice(-4).toUpperCase();
  const apSsid = `OpticWorks-${apSuffix}`;

  return `opticworks://setup?d=${deviceId}&ap=${apSsid}`;
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
  "device_id": "a1b2c3d4e5f6",
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
| `device_id` | string | 12-char hex device identifier |
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

Submits WiFi credentials and optional user token for provisioning.

**Request:**
```http
POST /api/provision HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "ssid": "HomeNetwork",
  "password": "secretpassword",
  "user_token": "eyJhbGciOiJIUzI1NiIs..."
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `ssid` | string | Yes | Target WiFi SSID |
| `password` | string | Yes | WiFi password (can be empty for open networks) |
| `user_token` | string | No | JWT from OpticWorks app for automatic claiming |

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
| `cloud_auth_failed` | Cloud rejected credentials |
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

WiFi credentials are stored in NVS only after successful connection and cloud registration:

```c
// Only commit after full provisioning success
if (wifi_connected && cloud_registered) {
    config_store_set_network(ssid, password);
    config_store_set_provisioned(true);
    config_store_commit();
}
```

---

## 7. Cloud Registration (MQTT)

### 7.1 Provisioning Topic

After WiFi connection, device publishes to registration topic:

**Topic:** `opticworks/{device_id}/provision`
**QoS:** 1
**Retain:** No

**Payload:**
```json
{
  "device_id": "a1b2c3d4e5f6",
  "mac_address": "AA:BB:CC:D4:E5:F6",
  "firmware_version": "1.0.0",
  "user_token": "eyJhbGciOiJIUzI1NiIs...",
  "timestamp": "2026-01-13T10:00:00Z"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `device_id` | string | Yes | 12-char hex device identifier |
| `mac_address` | string | Yes | Full MAC address |
| `firmware_version` | string | Yes | Current firmware version |
| `user_token` | string | No | JWT from app for automatic claiming |
| `timestamp` | string | Yes | ISO 8601 timestamp |

### 7.2 Provisioning Response

Cloud responds on:

**Topic:** `opticworks/{device_id}/provision/response`

**Success:**
```json
{
  "status": "registered",
  "device_id": "a1b2c3d4e5f6",
  "claimed_by": "user_abc123",
  "timestamp": "2026-01-13T10:00:01Z"
}
```

**Failure:**
```json
{
  "status": "rejected",
  "error": "invalid_token",
  "message": "User token expired or invalid",
  "timestamp": "2026-01-13T10:00:01Z"
}
```

### 7.3 Provisioning Without User Token

If no `user_token` is provided (web-only provisioning):

1. Device is registered but unclaimed
2. User can claim later via pairing code (see M10 Security)
3. Device functions normally for local Home Assistant use

---

## 8. User Token (JWT)

### 8.1 Token Format

The `user_token` is a JWT issued by OpticWorks authentication service:

```
Header:
{
  "alg": "RS256",
  "typ": "JWT",
  "kid": "2026-01"
}

Payload:
{
  "sub": "user_abc123",
  "email": "user@example.com",
  "iat": 1704931200,
  "exp": 1704934800,
  "scope": "device:provision",
  "iss": "https://auth.opticworks.io"
}
```

### 8.2 Token Validation (Cloud)

```typescript
async function validateProvisioningToken(token: string): Promise<TokenPayload | null> {
  try {
    const payload = await jwt.verify(token, publicKey, {
      issuer: 'https://auth.opticworks.io',
      audience: 'opticworks-api',
    });

    if (!payload.scope.includes('device:provision')) {
      return null;
    }

    return payload;
  } catch (e) {
    return null;
  }
}
```

### 8.3 Token Acquisition (App)

The app obtains a provisioning token before starting setup:

```typescript
async function getProvisioningToken(): Promise<string> {
  const response = await fetch('https://api.opticworks.io/auth/provision-token', {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${userAccessToken}`,
    },
  });

  const { provision_token } = await response.json();
  return provision_token; // Valid for 1 hour
}
```

---

## 9. Factory Reset

### 9.1 Trigger Methods

| Method | Action |
|--------|--------|
| Hardware button | Hold reset button for 10 seconds |
| App command | Send factory reset via Native API |
| Cloud command | MQTT `opticworks/{device_id}/command/factory-reset` |

### 9.2 Reset Behavior

```c
void provisioning_factory_reset(void) {
    // 1. Erase WiFi credentials from NVS
    config_store_erase_network();

    // 2. Erase zone configuration
    config_store_erase_zones();

    // 3. Clear provisioned flag
    config_store_set_provisioned(false);

    // 4. Keep device identity (device_id, device_secret)
    // 5. Keep firmware (no rollback)

    // 6. Reboot into AP mode
    esp_restart();
}
```

**Preserved after reset:**
- Device ID and secret
- Firmware version
- eFuse-burned data (secure boot keys, anti-rollback counter)

**Erased on reset:**
- WiFi credentials
- Zone configuration
- User pairing tokens
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
| Token invalid | "Please sign in again" | Re-authenticate in app |
| Token expired | "Session expired" | Refresh token |
| Device blocked | "Device cannot be registered" | Contact support |

### 10.3 Graceful Degradation

If cloud registration fails but WiFi is connected:

1. Store WiFi credentials anyway
2. Device operates in local-only mode
3. Retry cloud registration on next boot
4. User can still use with Home Assistant

---

## 11. Security Considerations

### 11.1 Open AP Network

The provisioning AP is intentionally open (no password) for ease of setup. Mitigations:

| Risk | Mitigation |
|------|------------|
| Eavesdropping | No sensitive data transmitted over AP (password is for target network, not stored until after connection) |
| Rogue provisioning | Device accepts provisioning only in AP mode; user must physically access device |
| AP spoofing | QR code contains expected AP name; app warns if mismatch |

### 11.2 WiFi Password Handling

- Password received via HTTP (within AP network, not internet)
- Password stored encrypted in NVS (see M06 Config Store)
- Password never transmitted to cloud
- Password cleared on factory reset

### 11.3 User Token Security

- Token is short-lived (1 hour)
- Token scope is limited to `device:provision`
- Token transmitted over MQTT (TLS)
- Token validated server-side

---

## 12. Implementation Notes

### 12.1 Memory Budget (ESP32-C3)

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
| M06 Config Store | Store/retrieve WiFi credentials, provisioned flag |
| M08 Timebase | AP mode timeout, connection timeouts |
| M09 Logging | Provisioning events for diagnostics |
| M10 Security | Credential encryption, token handling |

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
| Happy path (app) | Scan QR → app opens → enter WiFi → success | Device provisioned, claimed by user |
| Happy path (web) | Scan QR → open browser → captive portal → enter WiFi → success | Device provisioned, unclaimed |
| Wrong password | Enter wrong password | Error shown, can retry |
| Network not found | Enter non-existent SSID | Error shown, rescan |
| Cloud unreachable | Block MQTT port | WiFi saved, local-only mode |
| Token expired | Wait > 1 hour | "Session expired" error |
| AP timeout | Don't interact for 10 min | Device sleeps |
| Factory reset | Hold button 10s | Returns to AP mode |

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
