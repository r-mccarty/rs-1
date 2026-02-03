/**
 * Zone Configuration API Routes
 *
 * Handles zone configuration CRUD operations.
 *
 * Reference: docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md
 * Reference: docs/contracts/schemas/zone-config.schema.json
 */

import { Hono } from 'hono';
import type { Env } from '../types/env';
import { ZoneConfigSchema, type ZoneConfig, type ZoneConfigRecord } from '../types/zone';

const app = new Hono<{ Bindings: Env }>();

/**
 * GET /api/zones/:deviceId
 * Get current zone configuration for a device
 */
app.get('/:deviceId', async (c) => {
  const deviceId = c.req.param('deviceId');

  try {
    // Get device to verify it exists
    const device = await c.env.DB.prepare(
      'SELECT device_id, config_version FROM devices WHERE device_id = ?'
    )
      .bind(deviceId)
      .first<{ device_id: string; config_version: number }>();

    if (!device) {
      return c.json({ error: 'Device not found' }, 404);
    }

    // Get latest zone config
    const config = await c.env.DB.prepare(
      `SELECT config_json, version, updated_at, updated_by
       FROM zone_configs
       WHERE device_id = ? AND version = ?`
    )
      .bind(deviceId, device.config_version)
      .first<ZoneConfigRecord>();

    if (!config) {
      // Return empty config if none exists
      return c.json({
        device_id: deviceId,
        version: 0,
        config: { version: 0, zones: [] },
      });
    }

    return c.json({
      device_id: deviceId,
      version: config.version,
      config: JSON.parse(config.config_json),
      updated_at: config.updated_at,
      updated_by: config.updated_by,
    });
  } catch (error) {
    console.error('Failed to get zone config:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * PUT /api/zones/:deviceId
 * Update zone configuration for a device
 */
app.put('/:deviceId', async (c) => {
  const deviceId = c.req.param('deviceId');
  const userId = c.req.header('X-User-Id') || 'anonymous';

  let config: ZoneConfig;

  try {
    config = ZoneConfigSchema.parse(await c.req.json());
  } catch (error) {
    return c.json({ error: 'Invalid zone configuration', details: error }, 400);
  }

  try {
    // Get current device version
    const device = await c.env.DB.prepare(
      'SELECT device_id, config_version FROM devices WHERE device_id = ?'
    )
      .bind(deviceId)
      .first<{ device_id: string; config_version: number }>();

    if (!device) {
      return c.json({ error: 'Device not found' }, 404);
    }

    const newVersion = device.config_version + 1;

    // Validate version matches if provided
    if (config.version && config.version !== newVersion) {
      return c.json(
        {
          error: 'Version mismatch',
          expected: newVersion,
          received: config.version,
        },
        409
      );
    }

    // Update config version
    config.version = newVersion;
    config.updated_at = new Date().toISOString();

    // Insert new zone config
    await c.env.DB.prepare(
      `INSERT INTO zone_configs (device_id, version, config_json, updated_by)
       VALUES (?, ?, ?, ?)`
    )
      .bind(deviceId, newVersion, JSON.stringify(config), userId)
      .run();

    // Update device config_version
    await c.env.DB.prepare(
      'UPDATE devices SET config_version = ? WHERE device_id = ?'
    )
      .bind(newVersion, deviceId)
      .run();

    // Clean up old versions (keep last 10)
    await c.env.DB.prepare(
      `DELETE FROM zone_configs
       WHERE device_id = ? AND version < ?`
    )
      .bind(deviceId, newVersion - 10)
      .run();

    return c.json({
      device_id: deviceId,
      version: newVersion,
      status: 'updated',
    });
  } catch (error) {
    console.error('Failed to update zone config:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * GET /api/zones/:deviceId/history
 * Get zone configuration version history
 */
app.get('/:deviceId/history', async (c) => {
  const deviceId = c.req.param('deviceId');
  const limit = parseInt(c.req.query('limit') || '10', 10);

  try {
    const history = await c.env.DB.prepare(
      `SELECT version, updated_at, updated_by
       FROM zone_configs
       WHERE device_id = ?
       ORDER BY version DESC
       LIMIT ?`
    )
      .bind(deviceId, limit)
      .all<{ version: number; updated_at: string; updated_by: string }>();

    return c.json({
      device_id: deviceId,
      versions: history.results,
    });
  } catch (error) {
    console.error('Failed to get zone history:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * GET /api/zones/:deviceId/version/:version
 * Get specific zone configuration version
 */
app.get('/:deviceId/version/:version', async (c) => {
  const deviceId = c.req.param('deviceId');
  const version = parseInt(c.req.param('version'), 10);

  if (isNaN(version)) {
    return c.json({ error: 'Invalid version' }, 400);
  }

  try {
    const config = await c.env.DB.prepare(
      `SELECT config_json, version, updated_at, updated_by
       FROM zone_configs
       WHERE device_id = ? AND version = ?`
    )
      .bind(deviceId, version)
      .first<ZoneConfigRecord>();

    if (!config) {
      return c.json({ error: 'Version not found' }, 404);
    }

    return c.json({
      device_id: deviceId,
      version: config.version,
      config: JSON.parse(config.config_json),
      updated_at: config.updated_at,
      updated_by: config.updated_by,
    });
  } catch (error) {
    console.error('Failed to get zone version:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

export { app as zoneRoutes };
