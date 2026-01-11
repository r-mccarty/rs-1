# Device Registry Service Specification

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks Cloud
Status: Draft

---

## 1. Purpose

Manage device identity, enrollment, ownership, and state for RS-1 devices. The Device Registry is the source of truth for device information and provides APIs for device discovery and management.

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Device Registry                                   │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                        Cloudflare Worker                             │   │
│  │                                                                      │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │  /devices    │  │  /enroll     │  │  /webhook    │               │   │
│  │  │   (API)      │  │  (provision) │  │  (EMQX)      │               │   │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘               │   │
│  │         │                 │                 │                        │   │
│  │         ▼                 ▼                 ▼                        │   │
│  │  ┌───────────────────────────────────────────────────────────────┐  │   │
│  │  │                     Device State Manager                       │  │   │
│  │  │  • Online/offline tracking  • Firmware versions               │  │   │
│  │  │  • Config sync status       • Last seen timestamps            │  │   │
│  │  └───────────────────────────────────────────────────────────────┘  │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  ┌─────────────────┐  ┌─────────────────┐                                  │
│  │  Cloudflare D1  │  │      EMQX       │                                  │
│  │  (device data)  │  │  (LWT, state)   │                                  │
│  └─────────────────┘  └─────────────────┘                                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Device Identity

### 3.1 Device ID Derivation

Device ID is derived from the ESP32 MAC address (eFuse):

```typescript
function deriveDeviceId(macAddress: string): string {
  // MAC: AA:BB:CC:DD:EE:FF
  // Normalize and hash
  const normalized = macAddress.replace(/:/g, '').toLowerCase();
  const hash = crypto.createHash('sha256')
    .update(normalized + 'opticworks-rs1')
    .digest('hex');

  // First 32 chars = device_id
  return hash.substring(0, 32);
}
```

### 3.2 Identity Flow

```
┌─────────────┐                    ┌─────────────┐                    ┌─────────────┐
│   Factory   │                    │   Device    │                    │   Cloud     │
└──────┬──────┘                    └──────┬──────┘                    └──────┬──────┘
       │                                  │                                  │
       │  1. Burn MAC to eFuse            │                                  │
       │─────────────────────────────────►│                                  │
       │                                  │                                  │
       │  2. Generate device_secret       │                                  │
       │─────────────────────────────────►│                                  │
       │                                  │                                  │
       │  3. Register in cloud            │                                  │
       │──────────────────────────────────┼─────────────────────────────────►│
       │                                  │                                  │
       │                                  │  4. First boot: connect MQTT     │
       │                                  │─────────────────────────────────►│
       │                                  │                                  │
       │                                  │  5. Publish device state         │
       │                                  │─────────────────────────────────►│
       │                                  │                                  │
```

---

## 4. Device Enrollment

### 4.1 Manufacturing Enrollment

```typescript
// POST /admin/enroll
async function enrollDevice(request: Request): Promise<Response> {
  const { mac_address, device_secret_hash } = await request.json();

  const deviceId = deriveDeviceId(mac_address);

  // Check if already enrolled
  const existing = await db.execute(
    'SELECT device_id FROM devices WHERE device_id = ?',
    [deviceId]
  );

  if (existing.length > 0) {
    return new Response('Device already enrolled', { status: 409 });
  }

  // Create device record
  await db.execute(`
    INSERT INTO devices (device_id, mac_address, device_secret_hash, created_at)
    VALUES (?, ?, ?, ?)
  `, [deviceId, mac_address, device_secret_hash, new Date()]);

  return Response.json({
    device_id: deviceId,
    enrolled_at: new Date().toISOString()
  });
}
```

### 4.2 Self-Enrollment (Optional)

Devices can self-enroll on first connection:

```typescript
async function handleFirstConnection(deviceId: string, payload: DeviceState): Promise<void> {
  const existing = await db.execute(
    'SELECT device_id FROM devices WHERE device_id = ?',
    [deviceId]
  );

  if (existing.length === 0) {
    // Create minimal record
    await db.execute(`
      INSERT INTO devices (device_id, firmware_version, created_at, first_seen)
      VALUES (?, ?, ?, ?)
    `, [deviceId, payload.firmware_version, new Date(), new Date()]);

    await sendAlert('New device self-enrolled', { deviceId });
  }
}
```

---

## 5. Device State Tracking

### 5.1 State Updates

Devices publish state to `opticworks/{device_id}/state` (retained):

```typescript
// EMQX webhook handler
async function handleDeviceState(message: MQTTMessage): Promise<void> {
  const deviceId = extractDeviceId(message.topic);
  const state: DeviceState = JSON.parse(message.payload);

  await db.execute(`
    UPDATE devices
    SET online = ?,
        firmware_version = ?,
        last_seen = ?,
        wifi_rssi = ?,
        config_version = ?,
        radar_connected = ?
    WHERE device_id = ?
  `, [
    state.online,
    state.firmware_version,
    new Date(),
    state.wifi_rssi,
    state.config_version,
    state.radar_connected,
    deviceId
  ]);
}
```

### 5.2 Last Will and Testament (LWT)

EMQX notifies on device disconnect:

```typescript
async function handleLWT(deviceId: string): Promise<void> {
  await db.execute(`
    UPDATE devices
    SET online = 0,
        last_seen = ?
    WHERE device_id = ?
  `, [new Date(), deviceId]);
}
```

---

## 6. Device Ownership

### 6.1 Claiming a Device

Users claim devices by entering a pairing code:

```typescript
// POST /api/devices/claim
async function claimDevice(request: Request, userId: string): Promise<Response> {
  const { pairing_code, device_id } = await request.json();

  // Verify pairing code with device
  // (Device generates code, user enters in app, app sends to cloud)
  const valid = await verifyPairingCode(device_id, pairing_code);

  if (!valid) {
    return new Response('Invalid pairing code', { status: 403 });
  }

  // Check if already claimed
  const existing = await db.execute(
    'SELECT user_id FROM device_ownership WHERE device_id = ?',
    [device_id]
  );

  if (existing.length > 0) {
    return new Response('Device already claimed', { status: 409 });
  }

  // Create ownership
  await db.execute(`
    INSERT INTO device_ownership (device_id, user_id, role, claimed_at)
    VALUES (?, ?, 'owner', ?)
  `, [device_id, userId, new Date()]);

  return Response.json({
    device_id,
    owner: userId,
    claimed_at: new Date().toISOString()
  });
}
```

### 6.2 Sharing a Device

Owners can share access with other users:

```typescript
// POST /api/devices/{id}/share
async function shareDevice(request: Request, deviceId: string, ownerId: string): Promise<Response> {
  // Verify requester is owner
  const ownership = await verifyOwnership(deviceId, ownerId);
  if (!ownership || ownership.role !== 'owner') {
    return new Response('Not authorized', { status: 403 });
  }

  const { user_email, role } = await request.json();

  // Look up user by email
  const targetUser = await getUserByEmail(user_email);
  if (!targetUser) {
    return new Response('User not found', { status: 404 });
  }

  // Grant access
  await db.execute(`
    INSERT INTO device_ownership (device_id, user_id, role, shared_at, shared_by)
    VALUES (?, ?, ?, ?, ?)
    ON CONFLICT (device_id, user_id) DO UPDATE SET role = ?
  `, [deviceId, targetUser.user_id, role, new Date(), ownerId, role]);

  return Response.json({ shared: true });
}
```

---

## 7. API Endpoints

### 7.1 Device API

#### List User's Devices

```
GET /api/devices

Response:
{
  "devices": [
    {
      "device_id": "aabbccdd...",
      "name": "Living Room Sensor",
      "online": true,
      "firmware_version": "1.2.0",
      "last_seen": "2026-01-20T10:00:00Z",
      "role": "owner"
    }
  ]
}
```

#### Get Device Details

```
GET /api/devices/{id}

Response:
{
  "device_id": "aabbccdd...",
  "name": "Living Room Sensor",
  "online": true,
  "firmware_version": "1.2.0",
  "config_version": 3,
  "wifi_rssi": -55,
  "radar_connected": true,
  "last_seen": "2026-01-20T10:00:00Z",
  "uptime_sec": 86400,
  "zone_count": 4
}
```

#### Update Device Name

```
PATCH /api/devices/{id}

Request:
{
  "name": "Kitchen Sensor"
}

Response:
{
  "device_id": "aabbccdd...",
  "name": "Kitchen Sensor",
  "updated_at": "2026-01-20T10:05:00Z"
}
```

#### Remove Device

```
DELETE /api/devices/{id}

Response:
{
  "deleted": true,
  "device_id": "aabbccdd..."
}
```

### 7.2 Admin API

#### List All Devices

```
GET /admin/devices?status=online&version=1.2.0

Response:
{
  "devices": [...],
  "total": 1234,
  "page": 1
}
```

#### Get Device Audit Log

```
GET /admin/devices/{id}/audit

Response:
{
  "events": [
    {"event": "enrolled", "at": "2026-01-01T00:00:00Z"},
    {"event": "first_seen", "at": "2026-01-02T10:00:00Z"},
    {"event": "claimed", "by": "user123", "at": "2026-01-02T10:05:00Z"},
    {"event": "ota_success", "version": "1.2.0", "at": "2026-01-15T12:00:00Z"}
  ]
}
```

---

## 8. Data Model

### 8.1 D1 Schema

```sql
CREATE TABLE devices (
    device_id TEXT PRIMARY KEY,
    mac_address TEXT UNIQUE,
    device_secret_hash TEXT,
    name TEXT,
    online BOOLEAN DEFAULT 0,
    firmware_version TEXT,
    config_version INTEGER DEFAULT 0,
    wifi_rssi INTEGER,
    radar_connected BOOLEAN DEFAULT 1,
    last_seen DATETIME,
    first_seen DATETIME,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE device_ownership (
    device_id TEXT NOT NULL,
    user_id TEXT NOT NULL,
    role TEXT DEFAULT 'viewer',
    claimed_at DATETIME,
    shared_at DATETIME,
    shared_by TEXT,
    PRIMARY KEY (device_id, user_id)
);

CREATE TABLE device_audit (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    event_type TEXT NOT NULL,
    event_data TEXT,
    actor TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_devices_online ON devices(online);
CREATE INDEX idx_devices_version ON devices(firmware_version);
CREATE INDEX idx_ownership_user ON device_ownership(user_id);
CREATE INDEX idx_audit_device ON device_audit(device_id, created_at);
```

---

## 9. Authorization

### 9.1 Permission Model

| Role | Can View | Can Edit | Can Share | Can Delete |
|------|----------|----------|-----------|------------|
| `owner` | Yes | Yes | Yes | Yes |
| `admin` | Yes | Yes | No | No |
| `viewer` | Yes | No | No | No |

### 9.2 Access Check

```typescript
async function checkAccess(
  deviceId: string,
  userId: string,
  requiredRole: string
): Promise<boolean> {
  const ownership = await db.execute(`
    SELECT role FROM device_ownership
    WHERE device_id = ? AND user_id = ?
  `, [deviceId, userId]);

  if (ownership.length === 0) return false;

  const roleHierarchy = { owner: 3, admin: 2, viewer: 1 };
  return roleHierarchy[ownership[0].role] >= roleHierarchy[requiredRole];
}
```

---

## 10. Statistics

### 10.1 Fleet Overview

```typescript
// GET /admin/stats
async function getFleetStats(): Promise<FleetStats> {
  const results = await db.batch([
    'SELECT COUNT(*) as total FROM devices',
    'SELECT COUNT(*) as online FROM devices WHERE online = 1',
    'SELECT firmware_version, COUNT(*) as count FROM devices GROUP BY firmware_version',
    'SELECT DATE(last_seen) as date, COUNT(*) as active FROM devices WHERE last_seen > ? GROUP BY date'
  ]);

  return {
    total_devices: results[0][0].total,
    online_devices: results[1][0].online,
    version_distribution: results[2],
    daily_active: results[3]
  };
}
```

---

## 11. Configuration

| Parameter | Default | Description |
|-----------|---------|-------------|
| `PAIRING_CODE_EXPIRY_SEC` | 300 | Pairing code validity |
| `OFFLINE_THRESHOLD_SEC` | 120 | Time before marking offline |
| `AUDIT_RETENTION_DAYS` | 90 | Audit log retention |
| `MAX_DEVICES_PER_USER` | 50 | Device limit per user |

---

## 12. Monitoring

### 12.1 Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `registry.devices.total` | Gauge | Total registered devices |
| `registry.devices.online` | Gauge | Currently online devices |
| `registry.claims.total` | Counter | Device claims |
| `registry.api.requests` | Counter | API requests |

---

## 13. References

| Document | Purpose |
|----------|---------|
| `../contracts/PROTOCOL_MQTT.md` | MQTT protocol |
| `../contracts/SCHEMA_DEVICE_STATE.json` | Device state schema |
| `../firmware/HARDWAREOS_MODULE_SECURITY.md` | Device authentication |
