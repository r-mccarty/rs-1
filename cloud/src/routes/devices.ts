/**
 * Device API Routes
 *
 * Handles device registration, status queries, and management.
 *
 * Reference: docs/cloud/SERVICE_DEVICE_REGISTRY.md
 */

import { Hono } from 'hono';
import type { Env } from '../types/env';
import {
  DeviceRegisterSchema,
  type Device,
  type DeviceRegisterRequest,
} from '../types/device';
import { generateDeviceId } from '../utils/crypto';

const app = new Hono<{ Bindings: Env }>();

/**
 * GET /api/devices
 * List all devices (with optional filters)
 */
app.get('/', async (c) => {
  const online = c.req.query('online');
  const limit = parseInt(c.req.query('limit') || '50', 10);
  const offset = parseInt(c.req.query('offset') || '0', 10);

  let query = 'SELECT * FROM devices';
  const params: unknown[] = [];

  if (online !== undefined) {
    query += ' WHERE online = ?';
    params.push(online === 'true' ? 1 : 0);
  }

  query += ' ORDER BY last_seen DESC LIMIT ? OFFSET ?';
  params.push(limit, offset);

  try {
    const result = await c.env.DB.prepare(query).bind(...params).all<Device>();

    return c.json({
      devices: result.results,
      meta: {
        limit,
        offset,
        total: result.results.length, // Note: D1 doesn't return total count easily
      },
    });
  } catch (error) {
    console.error('Failed to list devices:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * GET /api/devices/:deviceId
 * Get single device details
 */
app.get('/:deviceId', async (c) => {
  const deviceId = c.req.param('deviceId');

  try {
    const device = await c.env.DB.prepare(
      'SELECT * FROM devices WHERE device_id = ?'
    )
      .bind(deviceId)
      .first<Device>();

    if (!device) {
      return c.json({ error: 'Device not found' }, 404);
    }

    return c.json({ device });
  } catch (error) {
    console.error('Failed to get device:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * POST /api/devices
 * Register a new device
 */
app.post('/', async (c) => {
  let body: DeviceRegisterRequest;

  try {
    body = DeviceRegisterSchema.parse(await c.req.json());
  } catch (error) {
    return c.json({ error: 'Invalid request body', details: error }, 400);
  }

  const deviceId = generateDeviceId(body.mac_address);

  try {
    // Check if device already exists
    const existing = await c.env.DB.prepare(
      'SELECT device_id FROM devices WHERE device_id = ?'
    )
      .bind(deviceId)
      .first();

    if (existing) {
      // Update last_seen for existing device
      await c.env.DB.prepare(
        'UPDATE devices SET last_seen = CURRENT_TIMESTAMP, firmware_version = COALESCE(?, firmware_version) WHERE device_id = ?'
      )
        .bind(body.firmware_version || null, deviceId)
        .run();

      return c.json({ device_id: deviceId, status: 'updated' });
    }

    // Insert new device
    await c.env.DB.prepare(
      `INSERT INTO devices (device_id, mac_address, firmware_version, last_seen, online)
       VALUES (?, ?, ?, CURRENT_TIMESTAMP, 1)`
    )
      .bind(deviceId, body.mac_address, body.firmware_version || null)
      .run();

    return c.json({ device_id: deviceId, status: 'created' }, 201);
  } catch (error) {
    console.error('Failed to register device:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * DELETE /api/devices/:deviceId
 * Remove a device
 */
app.delete('/:deviceId', async (c) => {
  const deviceId = c.req.param('deviceId');

  try {
    const result = await c.env.DB.prepare(
      'DELETE FROM devices WHERE device_id = ?'
    )
      .bind(deviceId)
      .run();

    if (result.meta.changes === 0) {
      return c.json({ error: 'Device not found' }, 404);
    }

    return c.json({ status: 'deleted' });
  } catch (error) {
    console.error('Failed to delete device:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * GET /api/devices/:deviceId/status
 * Get device status summary
 */
app.get('/:deviceId/status', async (c) => {
  const deviceId = c.req.param('deviceId');

  try {
    const device = await c.env.DB.prepare(
      'SELECT device_id, firmware_version, last_seen, online, config_version FROM devices WHERE device_id = ?'
    )
      .bind(deviceId)
      .first<Device>();

    if (!device) {
      return c.json({ error: 'Device not found' }, 404);
    }

    // Get latest telemetry summary
    const telemetry = await c.env.DB.prepare(
      `SELECT date, message_count, error_count, avg_heap_kb, avg_wifi_rssi
       FROM telemetry_summary
       WHERE device_id = ?
       ORDER BY date DESC
       LIMIT 1`
    )
      .bind(deviceId)
      .first();

    return c.json({
      device_id: device.device_id,
      firmware_version: device.firmware_version,
      online: Boolean(device.online),
      last_seen: device.last_seen,
      config_version: device.config_version,
      telemetry: telemetry || null,
    });
  } catch (error) {
    console.error('Failed to get device status:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

export { app as deviceRoutes };
