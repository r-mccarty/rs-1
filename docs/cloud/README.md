# RS-1 Cloud Services Architecture

Version: 0.1
Date: 2026-01-09
Owner: OpticWorks Cloud
Status: Draft

---

## 1. Overview

This document describes the cloud services architecture for RS-1 devices. The cloud provides OTA updates, device management, telemetry collection, and zone configuration sync.

---

## 2. Technology Stack

| Component | Technology | Rationale |
|-----------|------------|-----------|
| Compute | Cloudflare Workers | Edge-first, low latency globally |
| Database | Cloudflare D1 | SQLite at edge, simple relational model |
| Object Storage | Cloudflare R2 | Firmware binaries, cost-effective |
| MQTT Broker | EMQX | Scalable, cloud-native MQTT 5.0 |
| Language | TypeScript (Workers), Go (optional services) | Type safety, familiar to team |

---

## 3. Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              RS-1 Cloud Platform                            │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                        Cloudflare Edge                               │   │
│  │                                                                      │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │  Zone Editor │  │  OTA Manager │  │  Device API  │               │   │
│  │  │   Worker     │  │    Worker    │  │    Worker    │               │   │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘               │   │
│  │         │                 │                 │                        │   │
│  │         ▼                 ▼                 ▼                        │   │
│  │  ┌───────────────────────────────────────────────────────────────┐  │   │
│  │  │                     Cloudflare D1 (SQLite)                    │  │   │
│  │  │  • Device registry   • Zone configs   • OTA rollouts         │  │   │
│  │  └───────────────────────────────────────────────────────────────┘  │   │
│  │                                                                      │   │
│  │  ┌───────────────────────────────────────────────────────────────┐  │   │
│  │  │                     Cloudflare R2 (Storage)                   │  │   │
│  │  │  • Firmware binaries   • Telemetry archives                   │  │   │
│  │  └───────────────────────────────────────────────────────────────┘  │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                           EMQX Cluster                               │   │
│  │                                                                      │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │   MQTT OTA   │  │ MQTT Telemetry│  │  MQTT Config │               │   │
│  │  │   Topics     │  │    Topics     │  │    Topics    │               │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │   │
│  │                                                                      │   │
│  │  Device Auth: Username/Password (HMAC-based)                        │   │
│  │  TLS: Required (port 8883)                                          │   │
│  │                                                                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ MQTT (TLS)
                                    ▼
                    ┌───────────────────────────────┐
                    │         RS-1 Devices          │
                    │  ESP32-WROOM-32E + Radar      │
                    └───────────────────────────────┘
```

---

## 4. Services

| Service | Document | Purpose |
|---------|----------|---------|
| OTA Orchestrator | `SERVICE_OTA_ORCHESTRATOR.md` | Firmware rollout management |
| Device Registry | `SERVICE_DEVICE_REGISTRY.md` | Device identity and enrollment |
| Telemetry | `SERVICE_TELEMETRY.md` | Metrics ingestion and storage |
| Infrastructure | `INFRASTRUCTURE.md` | Platform configuration |

---

## 5. Data Flow

### 5.1 Device → Cloud

```
Device                          EMQX                      Workers
  │                              │                           │
  │──── telemetry ──────────────►│────── webhook ───────────►│
  │                              │                           │
  │──── ota/status ─────────────►│────── webhook ───────────►│
  │                              │                           │
  │──── config/status ──────────►│────── webhook ───────────►│
  │                              │                           │
```

### 5.2 Cloud → Device

```
Workers                         EMQX                      Device
  │                              │                           │
  │──── ota/trigger ────────────►│────────────────────────►│
  │                              │                           │
  │──── config/update ──────────►│────────────────────────►│
  │                              │                           │
  │──── diag/request ───────────►│────────────────────────►│
  │                              │                           │
```

---

## 6. Authentication

### 6.1 Device Authentication

- **Method**: MQTT username/password
- **Username**: 32-character hex device ID (derived from MAC)
- **Password**: HMAC-SHA256(device_secret, device_id || timestamp)
- **Rotation**: Password regenerated hourly with timestamp

### 6.2 User Authentication

- **Method**: OAuth 2.0 (Cloudflare Access or custom)
- **Scope**: Per-user device ownership
- **Storage**: D1 user_devices table

### 6.3 Service Authentication

- **Worker → EMQX**: Shared secret (Cloudflare secrets)
- **Worker → D1**: Binding (automatic)
- **Worker → R2**: Binding (automatic)

---

## 7. Data Models

### 7.1 Device Registry (D1)

```sql
CREATE TABLE devices (
    device_id TEXT PRIMARY KEY,
    mac_address TEXT NOT NULL,
    firmware_version TEXT,
    last_seen DATETIME,
    online BOOLEAN DEFAULT 0,
    config_version INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE device_ownership (
    device_id TEXT NOT NULL,
    user_id TEXT NOT NULL,
    role TEXT DEFAULT 'owner',
    PRIMARY KEY (device_id, user_id)
);
```

### 7.2 Zone Configurations (D1)

```sql
CREATE TABLE zone_configs (
    device_id TEXT NOT NULL,
    version INTEGER NOT NULL,
    config_json TEXT NOT NULL,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_by TEXT,
    PRIMARY KEY (device_id, version)
);
```

### 7.3 OTA Rollouts (D1)

```sql
CREATE TABLE ota_rollouts (
    rollout_id TEXT PRIMARY KEY,
    firmware_version TEXT NOT NULL,
    firmware_url TEXT NOT NULL,
    firmware_sha256 TEXT NOT NULL,
    status TEXT DEFAULT 'pending',
    target_percent INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE ota_device_status (
    device_id TEXT NOT NULL,
    rollout_id TEXT NOT NULL,
    status TEXT,
    progress INTEGER,
    error_message TEXT,
    updated_at DATETIME,
    PRIMARY KEY (device_id, rollout_id)
);
```

---

## 8. API Endpoints

### 8.1 Device API (for Zone Editor)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/devices` | GET | List user's devices |
| `/api/devices/{id}` | GET | Device details |
| `/api/devices/{id}/zones` | GET/PUT | Zone configuration |
| `/api/devices/{id}/targets` | GET | Live target stream (WebSocket) |

### 8.2 Admin API

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/admin/rollouts` | GET/POST | Manage OTA rollouts |
| `/admin/rollouts/{id}/abort` | POST | Abort rollout |
| `/admin/devices/{id}/diag` | POST | Request diagnostics |

---

## 9. Scalability

### 9.1 Estimated Load (10,000 devices)

| Metric | Value | Calculation |
|--------|-------|-------------|
| MQTT connections | 10,000 | 1 per device |
| Telemetry messages/day | 144,000 | 10,000 × 1/min × 60 × 24 |
| OTA downloads/month | ~10,000 | 1 update/device/month |
| D1 rows (devices) | 10,000 | 1 per device |
| R2 storage | ~15 GB | 10 firmware versions × 1.5 MB |

### 9.2 Cloudflare Limits

| Resource | Limit | Our Usage |
|----------|-------|-----------|
| Workers requests/day | 100M (paid) | ~200K |
| D1 storage | 10 GB | ~100 MB |
| D1 reads/day | 10B | ~1M |
| R2 storage | 10 GB free | ~15 GB |
| R2 operations | 10M free | ~100K |

---

## 10. Security

### 10.1 Data at Rest

- D1: Encrypted by Cloudflare
- R2: Encrypted by Cloudflare
- Firmware binaries: Signed (ECDSA P-256)

### 10.2 Data in Transit

- MQTT: TLS 1.2+ required
- API: HTTPS only
- Firmware downloads: HTTPS with signed URLs

### 10.3 Access Control

- MQTT ACLs per device (see `contracts/PROTOCOL_MQTT.md`)
- API authentication required
- Zone configs scoped to device owner

---

## 11. Monitoring

### 11.1 Metrics

| Metric | Source | Alert Threshold |
|--------|--------|-----------------|
| Device online count | EMQX | < 90% of registered |
| OTA failure rate | D1 | > 2% per rollout |
| Telemetry ingestion rate | Worker | Deviation > 20% |
| API error rate | Worker | > 1% |

### 11.2 Logging

- Cloudflare Workers logs (Logpush to R2)
- EMQX audit logs
- D1 query logs (development only)

---

## 12. Disaster Recovery

### 12.1 Backup Strategy

| Data | Backup Frequency | Retention |
|------|------------------|-----------|
| D1 database | Daily | 30 days |
| R2 firmware | On upload | Forever |
| Zone configs | On change | 10 versions |

### 12.2 Recovery Procedures

| Scenario | Recovery |
|----------|----------|
| D1 corruption | Restore from backup |
| EMQX outage | Devices reconnect automatically |
| Worker failure | Automatic Cloudflare failover |
| R2 unavailable | Devices retry OTA later |

---

## 13. Cost Estimation (10,000 devices)

| Service | Monthly Cost (USD) |
|---------|-------------------|
| Cloudflare Workers | $5-25 |
| Cloudflare D1 | $5-15 |
| Cloudflare R2 | $5-10 |
| EMQX Cloud (10K connections) | $50-200 |
| **Total** | **~$65-250** |

---

## 14. References

| Document | Purpose |
|----------|---------|
| `SERVICE_OTA_ORCHESTRATOR.md` | OTA rollout details |
| `SERVICE_DEVICE_REGISTRY.md` | Device management details |
| `SERVICE_TELEMETRY.md` | Telemetry pipeline details |
| `INFRASTRUCTURE.md` | Platform configuration |
| `../contracts/PROTOCOL_MQTT.md` | MQTT protocol specification |
