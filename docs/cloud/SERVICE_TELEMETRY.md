# Telemetry Service Specification

Version: 0.1
Date: 2026-01-11
Owner: OpticWorks Cloud
Status: Draft

---

## 1. Purpose

Collect, process, and store device telemetry from RS-1 devices. The Telemetry Service ingests metrics via MQTT, aggregates data for analysis, and provides APIs for querying device health and performance.

---

## 2. Architecture

```
+-----------------------------------------------------------------------------+
|                           Telemetry Service                                  |
|                                                                             |
|  +---------------------------------------------------------------------+   |
|  |                        Cloudflare Worker                             |   |
|  |                                                                      |   |
|  |  +--------------+  +--------------+  +--------------+               |   |
|  |  |  /webhook    |  |  /api/       |  |  /admin/     |               |   |
|  |  |  (EMQX)      |  |  telemetry   |  |  telemetry   |               |   |
|  |  +------+-------+  +------+-------+  +------+-------+               |   |
|  |         |                 |                 |                        |   |
|  |         v                 v                 v                        |   |
|  |  +-----------------------------------------------------------+      |   |
|  |  |                  Telemetry Processor                      |      |   |
|  |  |  * Validate schema  * Aggregate metrics  * Detect anomalies|      |   |
|  |  +-----------------------------------------------------------+      |   |
|  |                                                                      |   |
|  +---------------------------------------------------------------------+   |
|                                                                             |
|  +-----------------+  +-----------------+  +-----------------+            |
|  |  Cloudflare D1  |  |  Cloudflare R2  |  |      EMQX       |            |
|  |  (aggregates)   |  |  (raw archives) |  |  (ingestion)    |            |
|  +-----------------+  +-----------------+  +-----------------+            |
|                                                                             |
+-----------------------------------------------------------------------------+
```

---

## 3. Telemetry Types

### 3.1 Metric Categories

| Category | Frequency | Retention | Purpose |
|----------|-----------|-----------|---------|
| Heartbeat | 1/min | 7 days | Online status, basic health |
| Performance | 1/5min | 30 days | CPU, memory, radar stats |
| Events | On occurrence | 90 days | OTA, errors, state changes |
| Diagnostics | On request | 7 days | Deep debugging info |

### 3.2 Telemetry Payload

**All telemetry is published to a single topic per PROTOCOL_MQTT.md:**

`opticworks/{device_id}/telemetry` (QoS 0, No retain)

**Schema:** See `../contracts/SCHEMA_TELEMETRY.json`

```json
{
  "device_id": "aabbccddeeff00112233445566778899",
  "timestamp": "2026-01-20T10:00:00Z",
  "firmware_version": "1.2.0",
  "uptime_sec": 86400,
  "metrics": {
    "system.uptime_sec": 86400,
    "system.free_heap": 45000,
    "system.wifi_rssi": -55,
    "radar.frames_received": 9900,
    "radar.frames_invalid": 50,
    "tracking.active_tracks": 2,
    "zone.occupancy_changes": 45
  },
  "logs": [
    {"ts": 86399000, "lvl": "W", "tag": "ZONE", "msg": "Flicker detected"}
  ]
}
```

**Metric Categories (via metric name prefix):**

| Prefix | Purpose | Example Metrics |
|--------|---------|-----------------|
| `system.` | System health | `uptime_sec`, `free_heap`, `wifi_rssi`, `cpu_usage` |
| `radar.` | Radar performance | `frames_received`, `frames_invalid`, `frame_rate_hz` |
| `tracking.` | Tracking statistics | `active_tracks`, `id_switches` |
| `zone.` | Zone activity | `occupancy_changes`, `false_vacant_events` |
| `api.` | API statistics | `connections`, `messages_sent` |

**Note:** Heartbeat, performance, and event data are combined into a single telemetry batch format rather than using separate topics, per the contract.

### 3.3 Event Logging

Events are included in the telemetry batch as log entries:

```json
{
  "device_id": "aabbccddeeff00112233445566778899",
  "timestamp": "2026-01-20T10:00:00Z",
  "metrics": { ... },
  "logs": [
    {"ts": 86395000, "lvl": "I", "tag": "OTA", "msg": "ota_complete:1.1.0->1.2.0"},
    {"ts": 86390000, "lvl": "I", "tag": "BOOT", "msg": "boot:power_on"}
  ]
}
```

### 3.4 Event Types

Events are encoded in log entries with tag indicating the event type:

| Tag | Log Level | Message Format | Description |
|-----|-----------|----------------|-------------|
| `BOOT` | I | `boot:{reason}` | Device started |
| `OTA` | I | `ota_start:{version}` | OTA download began |
| `OTA` | I | `ota_complete:{from}->{to}` | OTA succeeded |
| `OTA` | E | `ota_failed:{version}:{error}` | OTA failed |
| `RADAR` | W | `radar_disconnect:{age_ms}` | Radar stopped responding |
| `RADAR` | I | `radar_reconnect:{downtime_ms}` | Radar recovered |
| `WIFI` | W | `wifi_disconnect:{rssi}` | Wi-Fi lost |
| `WIFI` | I | `wifi_reconnect:{downtime_ms}` | Wi-Fi restored |
| `CONFIG` | I | `config_change:{version}` | Configuration updated |
| `MAIN` | E | `watchdog_reset:{task}` | Watchdog triggered |
| `*` | E | `error:{code}:{message}` | Application error |

---

## 4. Ingestion Pipeline

### 4.1 EMQX Webhook

EMQX forwards telemetry messages to the Worker. All telemetry arrives on a single topic per device.

```typescript
// POST /webhook/telemetry
// Topic: opticworks/{device_id}/telemetry
async function handleTelemetry(request: Request): Promise<Response> {
  const message = await request.json();

  const deviceId = extractDeviceId(message.topic);
  const payload: TelemetryPayload = JSON.parse(message.payload);

  // Validate against SCHEMA_TELEMETRY.json
  const valid = await validateSchema('telemetry', payload);
  if (!valid) {
    console.error(`Invalid telemetry from ${deviceId}:`, payload);
    return new Response('Invalid payload', { status: 400 });
  }

  // Process metrics
  await processMetrics(deviceId, payload.metrics, payload.timestamp);

  // Process log entries (events)
  if (payload.logs && payload.logs.length > 0) {
    await processLogs(deviceId, payload.logs, payload.timestamp);
  }

  // Update device state
  await updateDeviceState(deviceId, payload);

  return new Response('OK');
}
```

### 4.2 Metrics Processing

```typescript
async function processMetrics(
  deviceId: string,
  metrics: Record<string, number>,
  timestamp: string
): Promise<void> {
  // Store performance metrics in D1 for aggregation
  await db.execute(`
    INSERT INTO telemetry_performance (
      device_id, timestamp,
      cpu_usage_percent, heap_min_free_kb,
      radar_frames_received, radar_frames_errors,
      tracks_created, zone_triggers
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
  `, [
    deviceId,
    timestamp,
    metrics['system.cpu_usage'] ?? null,
    Math.floor((metrics['system.free_heap'] ?? 0) / 1024),
    metrics['radar.frames_received'] ?? 0,
    metrics['radar.frames_invalid'] ?? 0,
    metrics['tracking.active_tracks'] ?? 0,
    metrics['zone.occupancy_changes'] ?? 0
  ]);

  // Detect anomalies from metrics
  const freeHeap = metrics['system.free_heap'] ?? 100000;
  if (freeHeap < 50000) {
    await createAlert(deviceId, 'low_memory', { free_heap: freeHeap });
  }

  const radarFrames = metrics['radar.frames_received'] ?? 1;
  const radarErrors = metrics['radar.frames_invalid'] ?? 0;
  const errorRate = radarErrors / radarFrames;
  if (errorRate > 0.01) {
    await createAlert(deviceId, 'high_radar_errors', { errorRate });
  }
}
```

### 4.3 Log/Event Processing

```typescript
async function processLogs(
  deviceId: string,
  logs: Array<{ts: number, lvl: string, tag: string, msg: string}>,
  batchTimestamp: string
): Promise<void> {
  for (const log of logs) {
    // Store in D1
    await db.execute(`
      INSERT INTO telemetry_events (
        device_id, timestamp, event_type, event_data
      ) VALUES (?, ?, ?, ?)
    `, [
      deviceId,
      batchTimestamp,
      `${log.tag}:${log.lvl}`,
      JSON.stringify({ ts: log.ts, msg: log.msg })
    ]);

    // Handle specific events based on tag and level
    if (log.tag === 'MAIN' && log.msg.startsWith('watchdog_reset')) {
      await createAlert(deviceId, 'watchdog_reset', { message: log.msg });
    }
    if (log.tag === 'OTA' && log.msg.startsWith('ota_failed')) {
      await notifyOTAOrchestrator(deviceId, 'failed', { message: log.msg });
    }
    if (log.lvl === 'E') {
      await createAlert(deviceId, 'error', { tag: log.tag, message: log.msg });
    }
  }
}
```

### 4.4 Device State Update

```typescript
async function updateDeviceState(
  deviceId: string,
  payload: TelemetryPayload
): Promise<void> {
  await db.execute(`
    UPDATE devices
    SET online = 1,
        last_seen = ?,
        wifi_rssi = ?,
        firmware_version = ?
    WHERE device_id = ?
  `, [
    payload.timestamp,
    payload.metrics['system.wifi_rssi'] ?? null,
    payload.firmware_version ?? null,
    deviceId
  ]);
}
```

---

## 5. Aggregation

### 5.1 Hourly Aggregation

Scheduled Worker (every hour):

```typescript
async function aggregateHourly(): Promise<void> {
  const cutoff = new Date(Date.now() - 60 * 60 * 1000);

  // Aggregate performance metrics
  await db.execute(`
    INSERT INTO telemetry_hourly (
      device_id, hour,
      avg_cpu_percent, min_heap_kb,
      total_radar_frames, total_radar_errors,
      total_tracks, total_zone_triggers
    )
    SELECT
      device_id,
      strftime('%Y-%m-%dT%H:00:00Z', timestamp) as hour,
      AVG(cpu_usage_percent),
      MIN(heap_min_free_kb),
      SUM(radar_frames_received),
      SUM(radar_frames_errors),
      SUM(tracks_created),
      SUM(zone_triggers)
    FROM telemetry_performance
    WHERE timestamp >= ?
    GROUP BY device_id, hour
    ON CONFLICT (device_id, hour) DO UPDATE SET
      avg_cpu_percent = excluded.avg_cpu_percent,
      min_heap_kb = excluded.min_heap_kb,
      total_radar_frames = excluded.total_radar_frames,
      total_radar_errors = excluded.total_radar_errors,
      total_tracks = excluded.total_tracks,
      total_zone_triggers = excluded.total_zone_triggers
  `, [cutoff]);
}
```

### 5.2 Daily Archival

Scheduled Worker (daily):

```typescript
async function archiveDaily(): Promise<void> {
  const archiveDate = new Date(Date.now() - 24 * 60 * 60 * 1000);
  const dateStr = archiveDate.toISOString().split('T')[0];

  // Export raw telemetry to R2
  const rawData = await db.execute(`
    SELECT * FROM telemetry_performance
    WHERE DATE(timestamp) = ?
  `, [dateStr]);

  // Write to R2 as NDJSON
  const ndjson = rawData.map(row => JSON.stringify(row)).join('\n');
  await env.R2.put(`telemetry/${dateStr}/performance.ndjson`, ndjson);

  // Delete archived data from D1
  await db.execute(`
    DELETE FROM telemetry_performance
    WHERE DATE(timestamp) = ?
  `, [dateStr]);
}
```

---

## 6. API Endpoints

### 6.1 Device Telemetry API

#### Get Recent Telemetry

```
GET /api/devices/{id}/telemetry?type=performance&hours=24

Response:
{
  "device_id": "aabbccdd...",
  "type": "performance",
  "from": "2026-01-19T10:00:00Z",
  "to": "2026-01-20T10:00:00Z",
  "data": [
    {
      "timestamp": "2026-01-20T09:55:00Z",
      "cpu_usage_percent": 15,
      "heap_min_free_kb": 95,
      "radar_frames_received": 9900,
      "radar_frames_errors": 50
    }
  ]
}
```

#### Get Device Events

```
GET /api/devices/{id}/events?from=2026-01-19&to=2026-01-20

Response:
{
  "device_id": "aabbccdd...",
  "events": [
    {
      "timestamp": "2026-01-19T14:30:00Z",
      "event_type": "ota_complete",
      "event_data": {
        "from_version": "1.1.0",
        "to_version": "1.2.0"
      }
    }
  ]
}
```

### 6.2 Admin Telemetry API

#### Fleet Health Overview

```
GET /admin/telemetry/health

Response:
{
  "timestamp": "2026-01-20T10:00:00Z",
  "fleet": {
    "total_devices": 10000,
    "online_devices": 9500,
    "radar_errors_24h": 1234,
    "ota_failures_24h": 5,
    "watchdog_resets_24h": 12
  },
  "alerts": {
    "low_memory": 3,
    "radar_disconnected": 7,
    "high_error_rate": 2
  }
}
```

#### Aggregated Metrics

```
GET /admin/telemetry/metrics?period=7d

Response:
{
  "period": "7d",
  "metrics": {
    "avg_uptime_hours": 168,
    "avg_cpu_percent": 12,
    "avg_heap_free_kb": 110,
    "total_tracks": 1500000,
    "total_zone_triggers": 450000
  },
  "version_distribution": {
    "1.2.0": 8500,
    "1.1.0": 1500
  }
}
```

#### Error Analysis

```
GET /admin/telemetry/errors?period=24h

Response:
{
  "period": "24h",
  "error_counts": {
    "radar_frame_parse": 1234,
    "wifi_disconnect": 89,
    "heap_allocation": 12,
    "watchdog": 5
  },
  "top_affected_devices": [
    {"device_id": "aabb...", "error_count": 45}
  ]
}
```

---

## 7. Alerting

### 7.1 Alert Types

| Alert | Condition | Severity | Action |
|-------|-----------|----------|--------|
| `low_memory` | heap_free_kb < 50 | Warning | Log, notify |
| `critical_memory` | heap_free_kb < 20 | Critical | Log, notify, page |
| `radar_disconnected` | radar_connected = false | Warning | Log |
| `radar_prolonged` | Radar down > 1 hour | Critical | Log, notify |
| `high_error_rate` | Radar errors > 1% | Warning | Log, notify |
| `watchdog_reset` | Any occurrence | Critical | Log, notify |
| `ota_failure_spike` | > 5 failures in 1 hour | Critical | Log, notify, pause rollout |

### 7.2 Alert Storage

```typescript
async function createAlert(
  deviceId: string,
  alertType: string,
  data: object
): Promise<void> {
  await db.execute(`
    INSERT INTO alerts (device_id, alert_type, data, created_at, status)
    VALUES (?, ?, ?, ?, 'open')
  `, [deviceId, alertType, JSON.stringify(data), new Date()]);

  // Send notification for critical alerts
  const critical = ['critical_memory', 'watchdog_reset', 'ota_failure_spike'];
  if (critical.includes(alertType)) {
    await sendNotification(alertType, deviceId, data);
  }
}
```

---

## 8. Diagnostics on Demand

### 8.1 Request Diagnostics

Admins can request deep diagnostics from a device:

```typescript
// POST /admin/devices/{id}/diagnostics
async function requestDiagnostics(deviceId: string): Promise<Response> {
  // Publish request to device
  await mqttPublish(`opticworks/${deviceId}/diag/request`, {
    request_id: generateId(),
    requested_at: new Date().toISOString(),
    include: ['memory_map', 'task_stats', 'wifi_scan', 'radar_raw']
  });

  return Response.json({
    status: 'requested',
    message: 'Diagnostics will be available shortly'
  });
}
```

### 8.2 Diagnostics Response

Device publishes to `opticworks/{device_id}/telemetry/diagnostics`:

```json
{
  "request_id": "diag-123",
  "timestamp": "2026-01-20T10:05:00Z",
  "memory_map": {
    "dram_used": 180000,
    "dram_free": 220000,
    "iram_used": 45000,
    "iram_free": 19000,
    "largest_free_block": 81920
  },
  "task_stats": [
    {"name": "radar_task", "stack_hwm": 1024, "cpu_percent": 8},
    {"name": "tracking_task", "stack_hwm": 512, "cpu_percent": 5}
  ],
  "wifi_scan": [
    {"ssid": "Home", "rssi": -55, "channel": 6}
  ],
  "radar_raw": {
    "last_10_frames": "base64_encoded..."
  }
}
```

---

## 9. Data Model

### 9.1 D1 Schema

```sql
CREATE TABLE telemetry_performance (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    timestamp DATETIME NOT NULL,
    period_sec INTEGER,
    cpu_usage_percent INTEGER,
    heap_min_free_kb INTEGER,
    radar_frames_received INTEGER,
    radar_frames_errors INTEGER,
    tracks_created INTEGER,
    zone_triggers INTEGER
);

CREATE TABLE telemetry_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    timestamp DATETIME NOT NULL,
    event_type TEXT NOT NULL,
    event_data TEXT
);

CREATE TABLE telemetry_hourly (
    device_id TEXT NOT NULL,
    hour DATETIME NOT NULL,
    avg_cpu_percent REAL,
    min_heap_kb INTEGER,
    total_radar_frames INTEGER,
    total_radar_errors INTEGER,
    total_tracks INTEGER,
    total_zone_triggers INTEGER,
    PRIMARY KEY (device_id, hour)
);

CREATE TABLE alerts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    alert_type TEXT NOT NULL,
    data TEXT,
    status TEXT DEFAULT 'open',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    resolved_at DATETIME
);

CREATE INDEX idx_perf_device_time ON telemetry_performance(device_id, timestamp);
CREATE INDEX idx_events_device_time ON telemetry_events(device_id, timestamp);
CREATE INDEX idx_events_type ON telemetry_events(event_type, timestamp);
CREATE INDEX idx_alerts_status ON alerts(status, created_at);
```

### 9.2 R2 Archive Structure

```
r2://opticworks-telemetry/
  telemetry/
    2026-01-19/
      performance.ndjson
      events.ndjson
    2026-01-20/
      ...
```

---

## 10. Configuration

| Parameter | Default | Description |
|-----------|---------|-------------|
| `HEARTBEAT_INTERVAL_SEC` | 60 | Expected heartbeat frequency |
| `PERFORMANCE_INTERVAL_SEC` | 300 | Expected performance report frequency |
| `OFFLINE_THRESHOLD_SEC` | 180 | Time before marking device offline |
| `LOW_MEMORY_THRESHOLD_KB` | 50 | Alert threshold for low memory |
| `CRITICAL_MEMORY_THRESHOLD_KB` | 20 | Critical alert threshold |
| `RADAR_ERROR_RATE_THRESHOLD` | 0.01 | Alert if error rate exceeds 1% |
| `RAW_RETENTION_DAYS` | 7 | Days to keep raw telemetry in D1 |
| `HOURLY_RETENTION_DAYS` | 30 | Days to keep hourly aggregates |
| `ARCHIVE_RETENTION_DAYS` | 365 | Days to keep R2 archives |

---

## 11. Monitoring

### 11.1 Service Metrics

| Metric | Type | Description |
|--------|------|-------------|
| `telemetry.messages.received` | Counter | Messages ingested |
| `telemetry.messages.invalid` | Counter | Schema validation failures |
| `telemetry.processing.latency_ms` | Histogram | Processing time |
| `telemetry.alerts.created` | Counter | New alerts |
| `telemetry.archive.size_bytes` | Gauge | R2 archive size |

### 11.2 Health Checks

```typescript
// GET /health
async function healthCheck(): Promise<Response> {
  const checks = {
    d1: await checkD1(),
    r2: await checkR2(),
    emqx: await checkEMQX()
  };

  const healthy = Object.values(checks).every(v => v);

  return Response.json(
    { status: healthy ? 'healthy' : 'degraded', checks },
    { status: healthy ? 200 : 503 }
  );
}
```

---

## 12. Privacy Considerations

### 12.1 Data Minimization

- No personally identifiable information (PII) in telemetry
- Device IDs are derived hashes, not MAC addresses
- Zone names and locations not included in telemetry

### 12.2 Data Retention

| Data Type | Retention | Deletion Method |
|-----------|-----------|-----------------|
| Raw telemetry | 7 days | Automatic D1 cleanup |
| Hourly aggregates | 30 days | Automatic D1 cleanup |
| R2 archives | 1 year | Lifecycle policy |
| Alerts | 90 days | Automatic cleanup |

### 12.3 Access Control

- Telemetry API requires device ownership
- Admin API requires admin role
- Audit log for all telemetry access

---

## 13. References

| Document | Purpose |
|----------|---------|
| `../contracts/PROTOCOL_MQTT.md` | MQTT topics |
| `../contracts/SCHEMA_TELEMETRY.json` | Telemetry schema |
| `../firmware/HARDWAREOS_MODULE_LOGGING.md` | Device logging |
