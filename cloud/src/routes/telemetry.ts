/**
 * Telemetry API Routes
 *
 * Handles telemetry ingestion and queries.
 *
 * Reference: docs/cloud/SERVICE_TELEMETRY.md
 */

import { Hono } from 'hono';
import type { Env } from '../types/env';
import { TelemetryPayloadSchema, type TelemetryPayload } from '../types/telemetry';

const app = new Hono<{ Bindings: Env }>();

/**
 * POST /api/telemetry
 * Ingest telemetry data from device
 */
app.post('/', async (c) => {
  let payload: TelemetryPayload;

  try {
    payload = TelemetryPayloadSchema.parse(await c.req.json());
  } catch (error) {
    return c.json({ error: 'Invalid telemetry payload', details: error }, 400);
  }

  const date = payload.timestamp.split('T')[0]; // YYYY-MM-DD

  try {
    // Update device last_seen
    await c.env.DB.prepare(
      'UPDATE devices SET last_seen = CURRENT_TIMESTAMP, online = 1 WHERE device_id = ?'
    )
      .bind(payload.device_id)
      .run();

    // Update or insert telemetry summary
    const heapKb = payload.metrics['system.free_heap_kb'] as number | undefined;
    const rssi = payload.metrics['system.wifi_rssi'] as number | undefined;
    const errorCount = payload.logs?.filter((l) => l.level === 'E').length || 0;

    await c.env.DB.prepare(
      `INSERT INTO telemetry_summary (device_id, date, message_count, error_count, avg_heap_kb, avg_wifi_rssi)
       VALUES (?, ?, 1, ?, ?, ?)
       ON CONFLICT(device_id, date) DO UPDATE SET
         message_count = message_count + 1,
         error_count = error_count + excluded.error_count,
         avg_heap_kb = (avg_heap_kb * message_count + excluded.avg_heap_kb) / (message_count + 1),
         avg_wifi_rssi = (avg_wifi_rssi * message_count + excluded.avg_wifi_rssi) / (message_count + 1)`
    )
      .bind(payload.device_id, date, errorCount, heapKb || null, rssi || null)
      .run();

    // Store full telemetry in R2 for archival
    const archiveKey = `telemetry/${payload.device_id}/${date}/${payload.timestamp}.json`;
    await c.env.TELEMETRY_BUCKET.put(archiveKey, JSON.stringify(payload), {
      httpMetadata: { contentType: 'application/json' },
    });

    return c.json({ status: 'accepted' }, 202);
  } catch (error) {
    console.error('Failed to process telemetry:', error);
    return c.json({ error: 'Processing error' }, 500);
  }
});

/**
 * GET /api/telemetry/:deviceId
 * Get telemetry summary for a device
 */
app.get('/:deviceId', async (c) => {
  const deviceId = c.req.param('deviceId');
  const days = parseInt(c.req.query('days') || '7', 10);

  try {
    const result = await c.env.DB.prepare(
      `SELECT date, message_count, error_count, avg_heap_kb, avg_wifi_rssi
       FROM telemetry_summary
       WHERE device_id = ?
       ORDER BY date DESC
       LIMIT ?`
    )
      .bind(deviceId, days)
      .all();

    return c.json({
      device_id: deviceId,
      summaries: result.results,
    });
  } catch (error) {
    console.error('Failed to get telemetry:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

/**
 * GET /api/telemetry/:deviceId/raw
 * Get raw telemetry from R2 archive
 */
app.get('/:deviceId/raw', async (c) => {
  const deviceId = c.req.param('deviceId');
  const date = c.req.query('date') || new Date().toISOString().split('T')[0];
  const limit = parseInt(c.req.query('limit') || '100', 10);

  try {
    const prefix = `telemetry/${deviceId}/${date}/`;
    const listed = await c.env.TELEMETRY_BUCKET.list({ prefix, limit });

    const payloads: TelemetryPayload[] = [];

    for (const obj of listed.objects) {
      const data = await c.env.TELEMETRY_BUCKET.get(obj.key);
      if (data) {
        const payload = await data.json<TelemetryPayload>();
        payloads.push(payload);
      }
    }

    return c.json({
      device_id: deviceId,
      date,
      count: payloads.length,
      telemetry: payloads,
    });
  } catch (error) {
    console.error('Failed to get raw telemetry:', error);
    return c.json({ error: 'Storage error' }, 500);
  }
});

/**
 * GET /api/telemetry/aggregate
 * Get aggregated telemetry across all devices
 */
app.get('/aggregate', async (c) => {
  const date = c.req.query('date') || new Date().toISOString().split('T')[0];

  try {
    const result = await c.env.DB.prepare(
      `SELECT
         COUNT(DISTINCT device_id) as device_count,
         SUM(message_count) as total_messages,
         SUM(error_count) as total_errors,
         AVG(avg_heap_kb) as avg_heap_kb,
         AVG(avg_wifi_rssi) as avg_wifi_rssi
       FROM telemetry_summary
       WHERE date = ?`
    )
      .bind(date)
      .first();

    return c.json({
      date,
      aggregate: result,
    });
  } catch (error) {
    console.error('Failed to get aggregate telemetry:', error);
    return c.json({ error: 'Database error' }, 500);
  }
});

export { app as telemetryRoutes };
