/**
 * OTA (Over-the-Air Update) API Routes
 *
 * Handles firmware rollout management and device update tracking.
 *
 * Reference: docs/cloud/SERVICE_OTA_ORCHESTRATOR.md
 */

import { Hono } from 'hono';
import type { Env } from '../types/env';
import {
  CreateRolloutSchema,
  type OtaRollout,
  type OtaDeviceStatus,
} from '../types/ota';

const app = new Hono<{ Bindings: Env }>();

/**
 * GET /api/ota/rollouts
 * List all rollouts
 */
app.get('/rollouts', async (c) => {
  const status = c.req.query('status');
  const limit = parseInt(c.req.query('limit') || '20', 10);

  try {
    let query = 'SELECT * FROM ota_rollouts';
    const params: unknown[] = [];

    if (status) {
      query += ' WHERE status = ?';
      params.push(status);
    }

    query += ' ORDER BY created_at DESC LIMIT ?';
    params.push(limit);

    const result = await c.env.DB.prepare(query).bind(...params).all<OtaRollout>();

    return c.json({ rollouts: result.results });
  } catch (error) {
    console.error('Failed to list rollouts:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * POST /api/ota/rollouts
 * Create a new rollout
 */
app.post('/rollouts', async (c) => {
  let body;

  try {
    body = CreateRolloutSchema.parse(await c.req.json());
  } catch (error) {
    return c.json({ error: 'Invalid request body', details: error }, 400);
  }

  const rolloutId = crypto.randomUUID();

  try {
    // Get firmware metadata from R2
    const firmwareKey = `firmware/${body.firmware_version}/${body.firmware_key}`;
    const firmware = await c.env.FIRMWARE_BUCKET.head(firmwareKey);

    if (!firmware) {
      return c.json({ error: 'Firmware file not found in R2' }, 404);
    }

    // Generate signed URL (valid for 24 hours)
    // Note: This is a placeholder - actual implementation would use signed URLs
    const firmwareUrl = `https://firmware.opticworks.io/${firmwareKey}`;
    const sha256 = firmware.checksums?.sha256 || 'unknown';

    await c.env.DB.prepare(
      `INSERT INTO ota_rollouts
       (rollout_id, firmware_version, firmware_url, firmware_sha256, firmware_size, status, target_percent)
       VALUES (?, ?, ?, ?, ?, 'pending', ?)`
    )
      .bind(
        rolloutId,
        body.firmware_version,
        firmwareUrl,
        sha256,
        firmware.size,
        body.target_percent
      )
      .run();

    return c.json(
      {
        rollout_id: rolloutId,
        firmware_version: body.firmware_version,
        status: 'pending',
        target_percent: body.target_percent,
      },
      201
    );
  } catch (error) {
    console.error('Failed to create rollout:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * GET /api/ota/rollouts/:rolloutId
 * Get rollout details with device status
 */
app.get('/rollouts/:rolloutId', async (c) => {
  const rolloutId = c.req.param('rolloutId');

  try {
    const rollout = await c.env.DB.prepare(
      'SELECT * FROM ota_rollouts WHERE rollout_id = ?'
    )
      .bind(rolloutId)
      .first<OtaRollout>();

    if (!rollout) {
      return c.json({ error: 'Rollout not found' }, 404);
    }

    // Get device status summary
    const statusSummary = await c.env.DB.prepare(
      `SELECT status, COUNT(*) as count
       FROM ota_device_status
       WHERE rollout_id = ?
       GROUP BY status`
    )
      .bind(rolloutId)
      .all<{ status: string; count: number }>();

    const summary = statusSummary.results.reduce(
      (acc, row) => {
        acc[row.status] = row.count;
        return acc;
      },
      {} as Record<string, number>
    );

    return c.json({
      ...rollout,
      device_summary: summary,
    });
  } catch (error) {
    console.error('Failed to get rollout:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * POST /api/ota/rollouts/:rolloutId/start
 * Start a pending rollout
 */
app.post('/rollouts/:rolloutId/start', async (c) => {
  const rolloutId = c.req.param('rolloutId');

  try {
    const rollout = await c.env.DB.prepare(
      'SELECT * FROM ota_rollouts WHERE rollout_id = ?'
    )
      .bind(rolloutId)
      .first<OtaRollout>();

    if (!rollout) {
      return c.json({ error: 'Rollout not found' }, 404);
    }

    if (rollout.status !== 'pending') {
      return c.json(
        { error: 'Rollout is not in pending state', current_status: rollout.status },
        400
      );
    }

    await c.env.DB.prepare(
      'UPDATE ota_rollouts SET status = ?, started_at = CURRENT_TIMESTAMP WHERE rollout_id = ?'
    )
      .bind('active', rolloutId)
      .run();

    // TODO: Trigger MQTT messages to devices
    // This would involve calling EMQX API to publish to selected devices

    return c.json({ status: 'active', rollout_id: rolloutId });
  } catch (error) {
    console.error('Failed to start rollout:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * POST /api/ota/rollouts/:rolloutId/abort
 * Abort an active rollout
 */
app.post('/rollouts/:rolloutId/abort', async (c) => {
  const rolloutId = c.req.param('rolloutId');
  const body = await c.req.json<{ reason?: string }>();

  try {
    const result = await c.env.DB.prepare(
      `UPDATE ota_rollouts
       SET status = 'aborted', abort_reason = ?, completed_at = CURRENT_TIMESTAMP
       WHERE rollout_id = ? AND status = 'active'`
    )
      .bind(body.reason || 'Manual abort', rolloutId)
      .run();

    if (result.meta.changes === 0) {
      return c.json({ error: 'Rollout not found or not active' }, 404);
    }

    return c.json({ status: 'aborted', rollout_id: rolloutId });
  } catch (error) {
    console.error('Failed to abort rollout:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * GET /api/ota/rollouts/:rolloutId/devices
 * Get device status for a rollout
 */
app.get('/rollouts/:rolloutId/devices', async (c) => {
  const rolloutId = c.req.param('rolloutId');
  const status = c.req.query('status');
  const limit = parseInt(c.req.query('limit') || '50', 10);

  try {
    let query = 'SELECT * FROM ota_device_status WHERE rollout_id = ?';
    const params: unknown[] = [rolloutId];

    if (status) {
      query += ' AND status = ?';
      params.push(status);
    }

    query += ' ORDER BY updated_at DESC LIMIT ?';
    params.push(limit);

    const result = await c.env.DB.prepare(query).bind(...params).all<OtaDeviceStatus>();

    return c.json({ devices: result.results });
  } catch (error) {
    console.error('Failed to get rollout devices:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * POST /api/ota/firmware
 * Upload firmware to R2
 */
app.post('/firmware', async (c) => {
  const version = c.req.query('version');
  const filename = c.req.query('filename') || 'firmware.bin';

  if (!version || !/^\d+\.\d+\.\d+$/.test(version)) {
    return c.json({ error: 'Invalid version format (expected semver)' }, 400);
  }

  try {
    const body = await c.req.arrayBuffer();

    if (body.byteLength === 0) {
      return c.json({ error: 'Empty file' }, 400);
    }

    const key = `firmware/${version}/${filename}`;

    await c.env.FIRMWARE_BUCKET.put(key, body, {
      httpMetadata: {
        contentType: 'application/octet-stream',
      },
      customMetadata: {
        version,
        uploadedAt: new Date().toISOString(),
      },
    });

    // Get the uploaded object to retrieve checksum
    const uploaded = await c.env.FIRMWARE_BUCKET.head(key);

    return c.json({
      key,
      version,
      size: body.byteLength,
      sha256: uploaded?.checksums?.sha256 || 'pending',
    });
  } catch (error) {
    console.error('Failed to upload firmware:', error);
    return c.json({ error: 'Upload failed' }, 500);
  }
});

export { app as otaRoutes };
