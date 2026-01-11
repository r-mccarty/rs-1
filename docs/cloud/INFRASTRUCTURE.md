# Infrastructure Specification

Version: 0.1
Date: 2026-01-11
Owner: OpticWorks Cloud
Status: Draft

---

## 1. Overview

This document describes the infrastructure configuration for RS-1 cloud services. The platform uses Cloudflare for edge compute and storage, with EMQX for MQTT messaging.

---

## 2. Architecture Overview

```
+-----------------------------------------------------------------------------------+
|                              Internet                                              |
+-----------------------------------------------------------------------------------+
           |                           |                           |
           v                           v                           v
+-------------------+      +-------------------+      +-------------------+
|  Cloudflare CDN   |      |  Cloudflare CDN   |      |   EMQX Cloud      |
|  (api.optic.works)|      | (ota.optic.works) |      | (mqtt.optic.works)|
+-------------------+      +-------------------+      +-------------------+
           |                           |                           |
           v                           v                           |
+-------------------+      +-------------------+                    |
|    Workers        |      |    Workers        |                    |
|  - Device API     |      |  - OTA Manager    |                    |
|  - Zone Editor    |      |  - Firmware DL    |                    |
|  - Telemetry      |      +--------+----------+                    |
+--------+----------+               |                               |
         |                          |                               |
         v                          v                               v
+-----------------------------------------------------------------------------------+
|                         Cloudflare Edge Services                                   |
|                                                                                    |
|  +------------------+  +------------------+  +------------------+                 |
|  |    D1 Database   |  |    R2 Storage    |  |      KV         |                 |
|  |   - Devices      |  |   - Firmware     |  |   - Sessions    |                 |
|  |   - Zones        |  |   - Telemetry    |  |   - Rate limits |                 |
|  |   - Rollouts     |  |   - Backups      |  |   - Cache       |                 |
|  +------------------+  +------------------+  +------------------+                 |
|                                                                                    |
+-----------------------------------------------------------------------------------+
```

---

## 3. Cloudflare Configuration

### 3.1 Workers

#### Device API Worker

```toml
# wrangler.toml - device-api

name = "rs1-device-api"
main = "src/index.ts"
compatibility_date = "2025-12-01"

[env.production]
routes = [
  { pattern = "api.optic.works/api/*", zone_name = "optic.works" }
]

[[d1_databases]]
binding = "DB"
database_name = "rs1-production"
database_id = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"

[[kv_namespaces]]
binding = "SESSIONS"
id = "xxxxxxxx"

[[r2_buckets]]
binding = "R2"
bucket_name = "rs1-storage"

[vars]
ENVIRONMENT = "production"
EMQX_HOST = "mqtt.optic.works"
```

#### OTA Worker

```toml
# wrangler.toml - ota

name = "rs1-ota"
main = "src/index.ts"
compatibility_date = "2025-12-01"

[env.production]
routes = [
  { pattern = "ota.optic.works/*", zone_name = "optic.works" }
]

[[d1_databases]]
binding = "DB"
database_name = "rs1-production"
database_id = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"

[[r2_buckets]]
binding = "FIRMWARE"
bucket_name = "rs1-firmware"

[vars]
ENVIRONMENT = "production"
SIGNED_URL_EXPIRY = "900"
```

#### Telemetry Worker

```toml
# wrangler.toml - telemetry

name = "rs1-telemetry"
main = "src/index.ts"
compatibility_date = "2025-12-01"

[env.production]
routes = [
  { pattern = "api.optic.works/webhook/*", zone_name = "optic.works" }
]

[[d1_databases]]
binding = "DB"
database_name = "rs1-production"
database_id = "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"

[[r2_buckets]]
binding = "ARCHIVES"
bucket_name = "rs1-telemetry-archives"

# Scheduled aggregation
[triggers]
crons = ["0 * * * *"]  # Hourly
```

### 3.2 D1 Database

#### Database Configuration

```sql
-- Database: rs1-production
-- Location: Automatic (edge)

-- Enable WAL mode for better concurrency
PRAGMA journal_mode = WAL;

-- Enable foreign keys
PRAGMA foreign_keys = ON;
```

#### Schema Migrations

```bash
# Create migration
wrangler d1 migrations create rs1-production add-telemetry-tables

# Apply migrations
wrangler d1 migrations apply rs1-production --remote
```

#### Backup Strategy

```typescript
// Scheduled backup (daily)
export default {
  async scheduled(event: ScheduledEvent, env: Env) {
    const backup = await env.DB.dump();
    const date = new Date().toISOString().split('T')[0];
    await env.R2.put(`backups/d1/${date}.sqlite`, backup);
  }
};
```

### 3.3 R2 Storage

#### Buckets

| Bucket | Purpose | Lifecycle |
|--------|---------|-----------|
| `rs1-firmware` | Firmware binaries | Retain forever |
| `rs1-storage` | Zone configs, user uploads | Retain forever |
| `rs1-telemetry-archives` | Raw telemetry archives | Delete after 365 days |
| `rs1-backups` | D1 backups | Delete after 30 days |

#### Bucket Configuration

```typescript
// r2 lifecycle rules (via API)
const lifecycleRules = [
  {
    id: 'telemetry-expiration',
    status: 'Enabled',
    filter: { prefix: 'telemetry/' },
    expiration: { days: 365 }
  },
  {
    id: 'backup-expiration',
    status: 'Enabled',
    filter: { prefix: 'backups/' },
    expiration: { days: 30 }
  }
];
```

#### CORS Configuration

```json
{
  "AllowedOrigins": ["https://app.optic.works"],
  "AllowedMethods": ["GET", "PUT"],
  "AllowedHeaders": ["*"],
  "MaxAgeSeconds": 3600
}
```

### 3.4 KV Namespaces

| Namespace | Purpose | TTL |
|-----------|---------|-----|
| `RS1_SESSIONS` | User session tokens | 24 hours |
| `RS1_RATE_LIMITS` | API rate limiting | 1 minute |
| `RS1_CACHE` | API response cache | 5 minutes |

### 3.5 Secrets

```bash
# Set production secrets
wrangler secret put EMQX_API_KEY --env production
wrangler secret put SIGNING_KEY --env production
wrangler secret put WEBHOOK_SECRET --env production
```

| Secret | Purpose |
|--------|---------|
| `EMQX_API_KEY` | EMQX HTTP API authentication |
| `SIGNING_KEY` | Firmware URL signing |
| `WEBHOOK_SECRET` | EMQX webhook validation |
| `JWT_SECRET` | User authentication |

---

## 4. EMQX Configuration

### 4.1 Deployment Options

| Option | Pros | Cons | Recommended |
|--------|------|------|-------------|
| EMQX Cloud | Fully managed, global | Higher cost | Yes (10K+ devices) |
| Self-hosted (Fly.io) | Lower cost | Ops overhead | Yes (< 10K devices) |
| Self-hosted (Cloudflare) | Integrated | Limited features | No |

### 4.2 EMQX Cloud Setup

```yaml
# emqx-cluster.yaml
cluster:
  name: rs1-production
  plan: standard
  region: us-east-1
  replicas: 3

listeners:
  - name: tcp-tls
    port: 8883
    protocol: mqtt
    tls:
      enabled: true
      verify_peer: false

authentication:
  - type: password_based
    mechanism: password_based:built_in_database
    enable: true

authorization:
  - type: built_in_database
    enable: true
```

### 4.3 Authentication

#### Device Authentication

Devices authenticate using device_id and HMAC-derived password:

```typescript
// Device-side (firmware)
function generateMQTTPassword(deviceId: string, secret: string): string {
  const timestamp = Math.floor(Date.now() / 3600000); // Hour-based
  const message = `${deviceId}:${timestamp}`;
  return hmacSha256(secret, message);
}

// Cloud-side validation
async function validateDevice(username: string, password: string): Promise<boolean> {
  const device = await db.execute(
    'SELECT device_secret_hash FROM devices WHERE device_id = ?',
    [username]
  );

  if (device.length === 0) return false;

  // Check current and previous hour (clock skew tolerance)
  const now = Math.floor(Date.now() / 3600000);
  for (const ts of [now, now - 1]) {
    const expected = hmacSha256(device[0].device_secret_hash, `${username}:${ts}`);
    if (password === expected) return true;
  }

  return false;
}
```

#### EMQX Auth Plugin Configuration

```hocon
# emqx.conf
authentication = [
  {
    backend = "http"
    mechanism = "password_based"
    method = "post"
    url = "https://api.optic.works/webhook/mqtt-auth"
    headers {
      "Content-Type" = "application/json"
      "X-Webhook-Secret" = "${WEBHOOK_SECRET}"
    }
    body {
      username = "${username}"
      password = "${password}"
      clientid = "${clientid}"
    }
    ssl {
      enable = true
      verify = verify_peer
    }
  }
]
```

### 4.4 Authorization (ACLs)

```hocon
# Device ACL rules
authorization = {
  sources = [
    {
      type = "http"
      url = "https://api.optic.works/webhook/mqtt-acl"
      method = "post"
      headers {
        "Content-Type" = "application/json"
      }
      body {
        username = "${username}"
        topic = "${topic}"
        action = "${action}"
      }
    }
  ]
}
```

#### ACL Logic

```typescript
// POST /webhook/mqtt-acl
async function handleACL(request: Request): Promise<Response> {
  const { username, topic, action } = await request.json();

  // Parse topic: opticworks/{device_id}/{path}
  const parts = topic.split('/');
  if (parts[0] !== 'opticworks') {
    return Response.json({ result: 'deny' });
  }

  const topicDeviceId = parts[1];

  // Devices can only access their own topics
  if (username !== topicDeviceId) {
    return Response.json({ result: 'deny' });
  }

  // Define allowed patterns
  const allowedPatterns = {
    publish: [
      'opticworks/{device_id}/state',
      'opticworks/{device_id}/telemetry/#',
      'opticworks/{device_id}/ota/status',
      'opticworks/{device_id}/config/status'
    ],
    subscribe: [
      'opticworks/{device_id}/ota/trigger',
      'opticworks/{device_id}/config/update',
      'opticworks/{device_id}/diag/request'
    ]
  };

  const patterns = allowedPatterns[action] || [];
  const allowed = patterns.some(p =>
    matchTopic(topic, p.replace('{device_id}', username))
  );

  return Response.json({ result: allowed ? 'allow' : 'deny' });
}
```

### 4.5 Webhooks (Rules Engine)

EMQX forwards messages to Cloudflare Workers:

```hocon
# EMQX Rule Engine configuration
rule_engine {
  rules = [
    {
      id = "telemetry-forward"
      sql = "SELECT * FROM 'opticworks/+/telemetry/#'"
      actions = [
        {
          function = "webhook"
          args = {
            url = "https://api.optic.works/webhook/telemetry"
            method = "POST"
            headers = {
              "Content-Type" = "application/json"
              "X-Webhook-Secret" = "${WEBHOOK_SECRET}"
            }
          }
        }
      ]
    },
    {
      id = "state-forward"
      sql = "SELECT * FROM 'opticworks/+/state'"
      actions = [
        {
          function = "webhook"
          args = {
            url = "https://api.optic.works/webhook/device-state"
            method = "POST"
          }
        }
      ]
    },
    {
      id = "ota-status-forward"
      sql = "SELECT * FROM 'opticworks/+/ota/status'"
      actions = [
        {
          function = "webhook"
          args = {
            url = "https://api.optic.works/webhook/ota-status"
            method = "POST"
          }
        }
      ]
    },
    {
      id = "lwt-handler"
      sql = "SELECT * FROM '$SYS/brokers/+/clients/+/disconnected'"
      actions = [
        {
          function = "webhook"
          args = {
            url = "https://api.optic.works/webhook/device-offline"
            method = "POST"
          }
        }
      ]
    }
  ]
}
```

### 4.6 TLS Configuration

```hocon
# EMQX TLS settings
listeners.ssl.default {
  bind = "0.0.0.0:8883"
  ssl_options {
    cacertfile = "/etc/emqx/certs/ca.pem"
    certfile = "/etc/emqx/certs/server.pem"
    keyfile = "/etc/emqx/certs/server.key"
    verify = verify_none
    versions = ["tlsv1.3", "tlsv1.2"]
    ciphers = [
      "TLS_AES_256_GCM_SHA384",
      "TLS_CHACHA20_POLY1305_SHA256",
      "TLS_AES_128_GCM_SHA256"
    ]
  }
}
```

---

## 5. DNS Configuration

### 5.1 Records

| Record | Type | Value | Proxy |
|--------|------|-------|-------|
| `api.optic.works` | CNAME | Workers route | Yes |
| `ota.optic.works` | CNAME | Workers route | Yes |
| `mqtt.optic.works` | CNAME | EMQX Cloud endpoint | No |
| `app.optic.works` | CNAME | Cloudflare Pages | Yes |

### 5.2 SSL Certificates

- API/OTA: Cloudflare Universal SSL (automatic)
- MQTT: EMQX-managed certificate (Let's Encrypt)
- App: Cloudflare Pages SSL (automatic)

---

## 6. Networking

### 6.1 Cloudflare Security

```yaml
# Page Rules
- url: "api.optic.works/*"
  settings:
    ssl: full_strict
    min_tls_version: "1.2"
    security_level: medium
    browser_check: on

# Rate Limiting
- expression: 'http.request.uri.path contains "/api/"'
  action: rate_limit
  characteristics:
    - ip.src
  requests_per_period: 100
  period: 60

# WAF Rules
- expression: 'http.request.uri.path contains "/admin/"'
  action: challenge
```

### 6.2 EMQX Network

```yaml
# EMQX Cloud VPC Peering (if using private endpoints)
vpc:
  cidr: 10.100.0.0/16
  peering:
    - cloudflare_warp  # For Workers → EMQX private access
```

---

## 7. Monitoring

### 7.1 Cloudflare Analytics

```typescript
// Workers Analytics Engine
export interface Env {
  ANALYTICS: AnalyticsEngine;
}

export default {
  async fetch(request: Request, env: Env) {
    const start = Date.now();

    // Handle request
    const response = await handleRequest(request, env);

    // Log analytics
    env.ANALYTICS.writeDataPoint({
      blobs: [request.url, response.status.toString()],
      doubles: [Date.now() - start],
      indexes: [request.cf?.colo || 'unknown']
    });

    return response;
  }
};
```

### 7.2 EMQX Metrics

| Metric | Description | Alert Threshold |
|--------|-------------|-----------------|
| `emqx_connections_count` | Active connections | < 80% of expected |
| `emqx_messages_received` | Messages per second | Deviation > 50% |
| `emqx_messages_dropped` | Dropped messages | > 0.1% |
| `emqx_session_created` | New sessions | Spike detection |
| `emqx_client_auth_failure` | Auth failures | > 10/min |

### 7.3 Alerting

```typescript
// Cloudflare Worker for monitoring
async function checkHealth(): Promise<void> {
  const checks = {
    emqx: await checkEMQX(),
    d1: await checkD1(),
    r2: await checkR2()
  };

  for (const [service, healthy] of Object.entries(checks)) {
    if (!healthy) {
      await sendPagerDutyAlert(`${service} unhealthy`);
    }
  }
}

// Scheduled every 5 minutes
export default {
  async scheduled(event: ScheduledEvent, env: Env) {
    await checkHealth();
  }
};
```

---

## 8. Deployment

### 8.1 CI/CD Pipeline

```yaml
# .github/workflows/deploy.yml
name: Deploy Cloud Services

on:
  push:
    branches: [main]
    paths:
      - 'cloud/**'

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Setup Node
        uses: actions/setup-node@v4
        with:
          node-version: '20'

      - name: Install Wrangler
        run: npm install -g wrangler

      - name: Run Tests
        run: npm test
        working-directory: cloud

      - name: Deploy Device API
        run: wrangler deploy --env production
        working-directory: cloud/device-api
        env:
          CLOUDFLARE_API_TOKEN: ${{ secrets.CF_API_TOKEN }}

      - name: Deploy OTA
        run: wrangler deploy --env production
        working-directory: cloud/ota
        env:
          CLOUDFLARE_API_TOKEN: ${{ secrets.CF_API_TOKEN }}

      - name: Deploy Telemetry
        run: wrangler deploy --env production
        working-directory: cloud/telemetry
        env:
          CLOUDFLARE_API_TOKEN: ${{ secrets.CF_API_TOKEN }}

      - name: Run D1 Migrations
        run: wrangler d1 migrations apply rs1-production --remote
        env:
          CLOUDFLARE_API_TOKEN: ${{ secrets.CF_API_TOKEN }}
```

### 8.2 Blue-Green Deployment

```bash
# Deploy to staging
wrangler deploy --env staging

# Run integration tests against staging
npm run test:integration -- --env staging

# Promote to production
wrangler deploy --env production
```

### 8.3 Rollback

```bash
# List previous deployments
wrangler deployments list

# Rollback to previous version
wrangler rollback
```

---

## 9. Cost Optimization

### 9.1 Workers

| Optimization | Impact |
|--------------|--------|
| Use Durable Objects sparingly | Reduce per-request cost |
| Cache API responses in KV | Reduce D1 queries |
| Batch D1 operations | Reduce row reads |
| Use Workers Analytics Engine | Lower cost than external analytics |

### 9.2 D1

| Optimization | Impact |
|--------------|--------|
| Use appropriate indexes | Reduce scan time |
| Batch writes | Reduce write operations |
| Archive old data to R2 | Keep D1 size small |

### 9.3 EMQX

| Optimization | Impact |
|--------------|--------|
| Right-size cluster | Match to actual connection count |
| Use QoS 0 for telemetry | Reduce message overhead |
| Batch webhooks | Reduce HTTP requests |

---

## 10. Disaster Recovery

### 10.1 Backup Schedule

| Data | Frequency | Retention | Storage |
|------|-----------|-----------|---------|
| D1 database | Daily | 30 days | R2 |
| Zone configs | On change | 10 versions | D1 |
| Firmware binaries | On upload | Forever | R2 |
| EMQX config | Weekly | 4 weeks | Git |

### 10.2 Recovery Procedures

#### D1 Recovery

```bash
# List available backups
wrangler r2 object list rs1-backups --prefix "backups/d1/"

# Download backup
wrangler r2 object get rs1-backups backups/d1/2026-01-20.sqlite

# Restore (requires new database)
wrangler d1 execute rs1-recovery --file=2026-01-20.sqlite
```

#### EMQX Recovery

```bash
# EMQX Cloud: Use dashboard to restore from snapshot
# Self-hosted: Restore from mnesia backup
emqx ctl cluster leave
emqx ctl cluster join emqx@node1
```

### 10.3 RTO/RPO Targets

| Scenario | RTO | RPO |
|----------|-----|-----|
| Worker failure | < 1 min | 0 (automatic failover) |
| D1 corruption | < 1 hour | < 24 hours |
| EMQX cluster failure | < 15 min | 0 (retained messages) |
| Full region outage | < 4 hours | < 1 hour |

---

## 11. Security

### 11.1 Access Control

| Resource | Access |
|----------|--------|
| Cloudflare Dashboard | SSO + 2FA |
| Wrangler CLI | API token (scoped) |
| EMQX Dashboard | SSO + IP allowlist |
| D1 Direct | Not exposed |

### 11.2 Secrets Rotation

| Secret | Rotation | Method |
|--------|----------|--------|
| `WEBHOOK_SECRET` | Quarterly | Wrangler secret |
| `SIGNING_KEY` | Annually | Key rollover |
| Device secrets | Never | Burned in factory |
| API tokens | On compromise | Regenerate |

### 11.3 Security Monitoring

```typescript
// Log suspicious activity
async function logSecurityEvent(event: SecurityEvent): Promise<void> {
  await db.execute(`
    INSERT INTO security_log (event_type, details, ip, timestamp)
    VALUES (?, ?, ?, ?)
  `, [event.type, JSON.stringify(event.details), event.ip, new Date()]);

  if (event.severity === 'high') {
    await sendSecurityAlert(event);
  }
}

// Events to log
const securityEvents = [
  'auth_failure_repeated',  // > 5 failures from same IP
  'acl_violation',          // Attempt to access unauthorized topic
  'rate_limit_exceeded',    // Unusual traffic pattern
  'admin_api_access',       // Any admin API call
];
```

---

## 12. References

| Document | Purpose |
|----------|---------|
| [Cloudflare Workers Docs](https://developers.cloudflare.com/workers/) | Worker development |
| [Cloudflare D1 Docs](https://developers.cloudflare.com/d1/) | Database reference |
| [EMQX Documentation](https://docs.emqx.com/) | MQTT broker |
| `../contracts/PROTOCOL_MQTT.md` | MQTT topic design |
