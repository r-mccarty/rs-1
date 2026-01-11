# OTA Orchestrator Service Specification

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks Cloud
Status: Draft

---

## 1. Purpose

Manage firmware update rollouts for RS-1 devices. The OTA Orchestrator controls staged rollouts, monitors success rates, and can abort updates if failure thresholds are exceeded.

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           OTA Orchestrator                                  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                        Cloudflare Worker                             │   │
│  │                                                                      │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │  /rollouts   │  │  /trigger    │  │  /webhook    │               │   │
│  │  │   (admin)    │  │  (internal)  │  │  (EMQX)      │               │   │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘               │   │
│  │         │                 │                 │                        │   │
│  │         ▼                 ▼                 ▼                        │   │
│  │  ┌───────────────────────────────────────────────────────────────┐  │   │
│  │  │                     Rollout State Machine                     │  │   │
│  │  │  PENDING → STAGED → COMPLETED/ABORTED                         │  │   │
│  │  └───────────────────────────────────────────────────────────────┘  │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐            │
│  │  Cloudflare D1  │  │  Cloudflare R2  │  │      EMQX       │            │
│  │  (rollout state)│  │  (firmware)     │  │  (device comms) │            │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Rollout Lifecycle

### 3.1 State Machine

```
                    create
    ┌─────────────────────────────────────┐
    │                                     │
    ▼                                     │
┌─────────┐     start      ┌─────────┐   │
│ PENDING │───────────────►│ STAGED  │   │
└─────────┘                └────┬────┘   │
                                │        │
                    ┌───────────┼───────────┐
                    │           │           │
                    ▼           ▼           ▼
              ┌──────────┐ ┌─────────┐ ┌─────────┐
              │ COMPLETED│ │ PAUSED  │ │ ABORTED │
              └──────────┘ └─────────┘ └─────────┘
```

### 3.2 States

| State | Description | Transitions |
|-------|-------------|-------------|
| `PENDING` | Rollout created, not started | → STAGED, ABORTED |
| `STAGED` | Rollout active, sending triggers | → COMPLETED, PAUSED, ABORTED |
| `PAUSED` | Temporarily halted | → STAGED, ABORTED |
| `COMPLETED` | All targets updated | Terminal |
| `ABORTED` | Rollout cancelled | Terminal |

---

## 4. Staged Rollout Strategy

### 4.1 Stages

| Stage | Target % | Duration | Auto-Advance |
|-------|----------|----------|--------------|
| 1 | 1% | 1 hour | Yes, if < 1% failures |
| 2 | 10% | 4 hours | Yes, if < 1% failures |
| 3 | 50% | 24 hours | Yes, if < 2% failures |
| 4 | 100% | Until complete | N/A |

### 4.2 Cohort Selection

Devices are assigned to cohorts using consistent hashing:

```typescript
function getDeviceCohort(deviceId: string, totalCohorts: number = 100): number {
  const hash = crypto.createHash('sha256').update(deviceId).digest();
  return hash.readUInt16BE(0) % totalCohorts;
}

function isInRollout(deviceId: string, targetPercent: number): boolean {
  return getDeviceCohort(deviceId) < targetPercent;
}
```

### 4.3 Abort Conditions

| Condition | Action |
|-----------|--------|
| Failure rate > 2% in any stage | Pause, alert |
| Failure rate > 5% | Abort |
| Manual abort request | Abort |
| Rollback detected on > 1% devices | Pause, alert |

---

## 5. API Endpoints

### 5.1 Admin API

#### Create Rollout

```
POST /admin/rollouts

Request:
{
  "firmware_version": "1.2.0",
  "firmware_url": "https://r2.opticworks.io/firmware/rs1/1.2.0.bin",
  "firmware_sha256": "abc123...",
  "min_rssi": -70,
  "schedule_at": "2026-01-20T10:00:00Z"  // optional
}

Response:
{
  "rollout_id": "rollout-2026-01-20-abc",
  "status": "PENDING",
  "target_percent": 0,
  "created_at": "2026-01-20T09:00:00Z"
}
```

#### Start Rollout

```
POST /admin/rollouts/{rollout_id}/start

Response:
{
  "rollout_id": "rollout-2026-01-20-abc",
  "status": "STAGED",
  "target_percent": 1,
  "stage": 1
}
```

#### Get Rollout Status

```
GET /admin/rollouts/{rollout_id}

Response:
{
  "rollout_id": "rollout-2026-01-20-abc",
  "firmware_version": "1.2.0",
  "status": "STAGED",
  "stage": 2,
  "target_percent": 10,
  "stats": {
    "targeted": 1000,
    "triggered": 985,
    "success": 950,
    "failed": 12,
    "pending": 23
  },
  "failure_rate": 0.012
}
```

#### Abort Rollout

```
POST /admin/rollouts/{rollout_id}/abort

Request:
{
  "reason": "Too many failures reported"
}

Response:
{
  "rollout_id": "rollout-2026-01-20-abc",
  "status": "ABORTED",
  "aborted_at": "2026-01-20T14:30:00Z"
}
```

---

## 6. Device Triggering

### 6.1 Trigger Flow

```typescript
async function triggerDevice(deviceId: string, rollout: Rollout): Promise<void> {
  // Generate signed URL (expires in 15 min)
  const signedUrl = await generateSignedUrl(rollout.firmware_url, 15 * 60);

  const trigger: OTATrigger = {
    version: rollout.firmware_version,
    url: signedUrl,
    sha256: rollout.firmware_sha256,
    min_rssi: rollout.min_rssi || -70,
    rollout_id: rollout.rollout_id,
    issued_at: new Date().toISOString()
  };

  // Publish to MQTT
  await mqttPublish(`opticworks/${deviceId}/ota/trigger`, trigger);

  // Record trigger sent
  await db.execute(
    'INSERT INTO ota_device_status (device_id, rollout_id, status, updated_at) VALUES (?, ?, ?, ?)',
    [deviceId, rollout.rollout_id, 'triggered', new Date()]
  );
}
```

### 6.2 Trigger Scheduling

Triggers are sent in batches to avoid overwhelming EMQX:

```typescript
const BATCH_SIZE = 100;
const BATCH_DELAY_MS = 1000;

async function triggerStage(rollout: Rollout): Promise<void> {
  const eligibleDevices = await getEligibleDevices(rollout);

  for (let i = 0; i < eligibleDevices.length; i += BATCH_SIZE) {
    const batch = eligibleDevices.slice(i, i + BATCH_SIZE);

    await Promise.all(batch.map(device => triggerDevice(device.device_id, rollout)));

    await sleep(BATCH_DELAY_MS);
  }
}
```

---

## 7. Status Collection

### 7.1 EMQX Webhook

EMQX forwards device status messages to the orchestrator:

```typescript
// POST /webhook/ota-status
async function handleOTAStatus(request: Request): Promise<Response> {
  const message = await request.json();

  const { deviceId, status, version, progress, error, rollout_id } = message.payload;

  // Update device status
  await db.execute(`
    UPDATE ota_device_status
    SET status = ?, progress = ?, error_message = ?, updated_at = ?
    WHERE device_id = ? AND rollout_id = ?
  `, [status, progress, error, new Date(), deviceId, rollout_id]);

  // Check for abort conditions
  if (status === 'failed') {
    await checkAbortConditions(rollout_id);
  }

  return new Response('OK');
}
```

### 7.2 Failure Analysis

```typescript
async function checkAbortConditions(rolloutId: string): Promise<void> {
  const stats = await getRolloutStats(rolloutId);

  if (stats.triggered === 0) return;

  const failureRate = stats.failed / stats.triggered;

  if (failureRate > 0.05) {
    await abortRollout(rolloutId, 'Failure rate exceeded 5%');
  } else if (failureRate > 0.02) {
    await pauseRollout(rolloutId, 'Failure rate exceeded 2%');
    await sendAlert('OTA rollout paused', { rolloutId, failureRate });
  }
}
```

---

## 8. Cooldown Management

### 8.1 Per-Device Cooldown

Devices that recently updated are excluded from new rollouts:

```typescript
const COOLDOWN_HOURS = 24;

async function getEligibleDevices(rollout: Rollout): Promise<Device[]> {
  const cutoff = new Date(Date.now() - COOLDOWN_HOURS * 60 * 60 * 1000);

  return await db.execute(`
    SELECT d.*
    FROM devices d
    LEFT JOIN ota_device_status s ON d.device_id = s.device_id
    WHERE (s.status IS NULL OR s.status = 'failed' OR s.updated_at < ?)
      AND d.firmware_version < ?
      AND d.online = 1
      AND getDeviceCohort(d.device_id) < ?
  `, [cutoff, rollout.firmware_version, rollout.target_percent]);
}
```

---

## 9. Firmware Storage

### 9.1 Upload Flow

```typescript
// POST /admin/firmware
async function uploadFirmware(request: Request): Promise<Response> {
  const formData = await request.formData();
  const file = formData.get('firmware') as File;
  const version = formData.get('version') as string;

  // Validate signature block
  const buffer = await file.arrayBuffer();
  if (!validateSignatureBlock(buffer)) {
    return new Response('Invalid firmware signature', { status: 400 });
  }

  // Compute SHA256
  const sha256 = await crypto.subtle.digest('SHA-256', buffer);
  const sha256Hex = arrayBufferToHex(sha256);

  // Upload to R2
  const key = `firmware/rs1/${version}.bin`;
  await env.R2.put(key, buffer);

  return Response.json({
    version,
    url: `https://r2.opticworks.io/${key}`,
    sha256: sha256Hex,
    size: buffer.byteLength
  });
}
```

### 9.2 Signed URL Generation

```typescript
async function generateSignedUrl(baseUrl: string, expiresIn: number): Promise<string> {
  const expires = Math.floor(Date.now() / 1000) + expiresIn;
  const signature = await sign(`${baseUrl}:${expires}`, env.SIGNING_KEY);

  return `${baseUrl}?expires=${expires}&sig=${signature}`;
}
```

---

## 10. Data Model

### 10.1 D1 Schema

```sql
CREATE TABLE ota_rollouts (
    rollout_id TEXT PRIMARY KEY,
    firmware_version TEXT NOT NULL,
    firmware_url TEXT NOT NULL,
    firmware_sha256 TEXT NOT NULL,
    min_rssi INTEGER DEFAULT -70,
    status TEXT DEFAULT 'PENDING',
    stage INTEGER DEFAULT 0,
    target_percent INTEGER DEFAULT 0,
    abort_reason TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    started_at DATETIME,
    completed_at DATETIME
);

CREATE TABLE ota_device_status (
    device_id TEXT NOT NULL,
    rollout_id TEXT NOT NULL,
    status TEXT,
    progress INTEGER DEFAULT 0,
    error_message TEXT,
    triggered_at DATETIME,
    updated_at DATETIME,
    PRIMARY KEY (device_id, rollout_id)
);

CREATE INDEX idx_ota_status_rollout ON ota_device_status(rollout_id, status);
```

---

## 11. Configuration

| Parameter | Default | Description |
|-----------|---------|-------------|
| `STAGE_1_PERCENT` | 1 | First stage rollout percent |
| `STAGE_2_PERCENT` | 10 | Second stage rollout percent |
| `STAGE_3_PERCENT` | 50 | Third stage rollout percent |
| `STAGE_DURATION_HOURS` | [1, 4, 24] | Duration per stage |
| `FAILURE_PAUSE_THRESHOLD` | 0.02 | Pause if failure rate exceeds |
| `FAILURE_ABORT_THRESHOLD` | 0.05 | Abort if failure rate exceeds |
| `COOLDOWN_HOURS` | 24 | Per-device update cooldown |
| `SIGNED_URL_EXPIRY_SEC` | 900 | Firmware URL validity |

---

## 12. Monitoring

### 12.1 Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `ota.rollouts.active` | Gauge | Active rollouts count |
| `ota.triggers.sent` | Counter | Total triggers sent |
| `ota.updates.success` | Counter | Successful updates |
| `ota.updates.failed` | Counter | Failed updates |
| `ota.rollouts.aborted` | Counter | Aborted rollouts |

### 12.2 Alerts

| Alert | Condition | Severity |
|-------|-----------|----------|
| Rollout paused | Failure rate > 2% | Warning |
| Rollout aborted | Failure rate > 5% | Critical |
| No progress | 0 updates in 1 hour | Warning |

---

## 13. References

| Document | Purpose |
|----------|---------|
| `../contracts/PROTOCOL_MQTT.md` | MQTT protocol |
| `../contracts/SCHEMA_OTA_MANIFEST.json` | Trigger schema |
| `../firmware/HARDWAREOS_MODULE_OTA.md` | Device OTA handling |
