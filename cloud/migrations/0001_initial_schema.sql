-- RS-1 Cloud Services - Initial Database Schema
-- Reference: docs/cloud/README.md Section 7

-- Device Registry
-- Stores device identity and current state
CREATE TABLE IF NOT EXISTS devices (
    device_id TEXT PRIMARY KEY,
    mac_address TEXT NOT NULL UNIQUE,
    firmware_version TEXT,
    last_seen DATETIME,
    online INTEGER DEFAULT 0,
    config_version INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    variant TEXT DEFAULT 'unknown'  -- 'lite' or 'pro'
);

-- Index for online device queries
CREATE INDEX IF NOT EXISTS idx_devices_online ON devices(online);
CREATE INDEX IF NOT EXISTS idx_devices_firmware ON devices(firmware_version);

-- Device Ownership
-- Links devices to user accounts
CREATE TABLE IF NOT EXISTS device_ownership (
    device_id TEXT NOT NULL,
    user_id TEXT NOT NULL,
    role TEXT DEFAULT 'owner' CHECK (role IN ('owner', 'viewer')),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (device_id, user_id),
    FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_ownership_user ON device_ownership(user_id);

-- Zone Configurations
-- Versioned zone configs for each device
CREATE TABLE IF NOT EXISTS zone_configs (
    device_id TEXT NOT NULL,
    version INTEGER NOT NULL,
    config_json TEXT NOT NULL,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_by TEXT,
    PRIMARY KEY (device_id, version),
    FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE
);

-- Keep only last 10 versions per device (cleanup handled in application)
CREATE INDEX IF NOT EXISTS idx_zone_configs_device ON zone_configs(device_id, version DESC);

-- OTA Rollouts
-- Firmware update campaigns
CREATE TABLE IF NOT EXISTS ota_rollouts (
    rollout_id TEXT PRIMARY KEY,
    firmware_version TEXT NOT NULL,
    firmware_url TEXT NOT NULL,
    firmware_sha256 TEXT NOT NULL,
    firmware_size INTEGER NOT NULL,
    status TEXT DEFAULT 'pending' CHECK (status IN ('pending', 'active', 'paused', 'completed', 'aborted')),
    target_percent INTEGER DEFAULT 0 CHECK (target_percent >= 0 AND target_percent <= 100),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    started_at DATETIME,
    completed_at DATETIME,
    abort_reason TEXT
);

CREATE INDEX IF NOT EXISTS idx_rollouts_status ON ota_rollouts(status);
CREATE INDEX IF NOT EXISTS idx_rollouts_version ON ota_rollouts(firmware_version);

-- OTA Device Status
-- Per-device progress for each rollout
CREATE TABLE IF NOT EXISTS ota_device_status (
    device_id TEXT NOT NULL,
    rollout_id TEXT NOT NULL,
    status TEXT DEFAULT 'pending' CHECK (status IN ('pending', 'downloading', 'verifying', 'installing', 'success', 'failed')),
    progress INTEGER DEFAULT 0 CHECK (progress >= 0 AND progress <= 100),
    error_message TEXT,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (device_id, rollout_id),
    FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE,
    FOREIGN KEY (rollout_id) REFERENCES ota_rollouts(rollout_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_ota_status_rollout ON ota_device_status(rollout_id, status);

-- Telemetry Metadata
-- Summary of telemetry for quick queries (detailed data in R2)
CREATE TABLE IF NOT EXISTS telemetry_summary (
    device_id TEXT NOT NULL,
    date TEXT NOT NULL,  -- YYYY-MM-DD
    message_count INTEGER DEFAULT 0,
    error_count INTEGER DEFAULT 0,
    avg_heap_kb REAL,
    avg_wifi_rssi REAL,
    PRIMARY KEY (device_id, date),
    FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE
);

-- Device Events
-- Audit log of significant device events
CREATE TABLE IF NOT EXISTS device_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    event_type TEXT NOT NULL,
    event_data TEXT,  -- JSON
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(device_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_events_device ON device_events(device_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_events_type ON device_events(event_type, created_at DESC);
