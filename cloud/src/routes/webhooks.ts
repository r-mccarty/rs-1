/**
 * MQTT Webhook Routes
 *
 * Handles webhook callbacks from EMQX for MQTT events.
 * EMQX is configured to POST to these endpoints on specific events.
 *
 * Reference: docs/contracts/PROTOCOL_MQTT.md
 */

import { Hono } from 'hono';
import type { Env } from '../types/env';
import { OtaStatusUpdateSchema, type OtaStatusUpdate } from '../types/ota';

const app = new Hono<{ Bindings: Env }>();

// EMQX webhook event types
interface EmqxWebhookEvent {
  event: string;
  clientid: string;
  username: string;
  topic?: string;
  payload?: string;
  timestamp: number;
}

/**
 * POST /webhooks/mqtt/connect
 * Handle device connection events
 */
app.post('/mqtt/connect', async (c) => {
  const event = await c.req.json<EmqxWebhookEvent>();

  if (event.event !== 'client.connected') {
    return c.json({ status: 'ignored' });
  }

  const deviceId = event.username; // Device ID is used as MQTT username

  try {
    await c.env.DB.prepare(
      'UPDATE devices SET online = 1, last_seen = CURRENT_TIMESTAMP WHERE device_id = ?'
    )
      .bind(deviceId)
      .run();

    // Log connection event
    await c.env.DB.prepare(
      `INSERT INTO device_events (device_id, event_type, event_data)
       VALUES (?, 'connected', ?)`
    )
      .bind(deviceId, JSON.stringify({ timestamp: event.timestamp }))
      .run();

    console.log(`Device connected: ${deviceId}`);
    return c.json({ status: 'ok' });
  } catch (error) {
    console.error('Failed to process connect event:', error);
    return c.json({ error: 'Processing error' }, 500);
  }
});

/**
 * POST /webhooks/mqtt/disconnect
 * Handle device disconnection events (including LWT)
 */
app.post('/mqtt/disconnect', async (c) => {
  const event = await c.req.json<EmqxWebhookEvent>();

  if (event.event !== 'client.disconnected') {
    return c.json({ status: 'ignored' });
  }

  const deviceId = event.username;

  try {
    await c.env.DB.prepare(
      'UPDATE devices SET online = 0, last_seen = CURRENT_TIMESTAMP WHERE device_id = ?'
    )
      .bind(deviceId)
      .run();

    // Log disconnection event
    await c.env.DB.prepare(
      `INSERT INTO device_events (device_id, event_type, event_data)
       VALUES (?, 'disconnected', ?)`
    )
      .bind(deviceId, JSON.stringify({ timestamp: event.timestamp }))
      .run();

    console.log(`Device disconnected: ${deviceId}`);
    return c.json({ status: 'ok' });
  } catch (error) {
    console.error('Failed to process disconnect event:', error);
    return c.json({ error: 'Processing error' }, 500);
  }
});

/**
 * POST /webhooks/mqtt/ota-status
 * Handle OTA status updates from devices
 *
 * Topic: opticworks/{device_id}/ota/status
 */
app.post('/mqtt/ota-status', async (c) => {
  const event = await c.req.json<EmqxWebhookEvent>();

  if (!event.payload) {
    return c.json({ error: 'Missing payload' }, 400);
  }

  let status: OtaStatusUpdate;

  try {
    status = OtaStatusUpdateSchema.parse(JSON.parse(event.payload));
  } catch (error) {
    return c.json({ error: 'Invalid OTA status payload', details: error }, 400);
  }

  try {
    // Update device OTA status
    await c.env.DB.prepare(
      `INSERT INTO ota_device_status (device_id, rollout_id, status, progress, error_message, updated_at)
       VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
       ON CONFLICT(device_id, rollout_id) DO UPDATE SET
         status = excluded.status,
         progress = excluded.progress,
         error_message = excluded.error_message,
         updated_at = CURRENT_TIMESTAMP`
    )
      .bind(
        status.device_id,
        status.rollout_id,
        status.status,
        status.progress,
        status.error_message || null
      )
      .run();

    // Check if we need to auto-abort rollout (>2% failure rate)
    if (status.status === 'failed') {
      const rolloutStats = await c.env.DB.prepare(
        `SELECT
           COUNT(*) as total,
           SUM(CASE WHEN status = 'failed' THEN 1 ELSE 0 END) as failed
         FROM ota_device_status
         WHERE rollout_id = ?`
      )
        .bind(status.rollout_id)
        .first<{ total: number; failed: number }>();

      if (rolloutStats && rolloutStats.total >= 10) {
        const failureRate = rolloutStats.failed / rolloutStats.total;
        if (failureRate > 0.02) {
          // Auto-abort rollout
          await c.env.DB.prepare(
            `UPDATE ota_rollouts
             SET status = 'aborted', abort_reason = 'Auto-aborted: failure rate exceeded 2%'
             WHERE rollout_id = ? AND status = 'active'`
          )
            .bind(status.rollout_id)
            .run();

          console.log(`Auto-aborted rollout ${status.rollout_id}: failure rate ${(failureRate * 100).toFixed(1)}%`);
        }
      }
    }

    // Update device firmware version on success
    if (status.status === 'success') {
      const rollout = await c.env.DB.prepare(
        'SELECT firmware_version FROM ota_rollouts WHERE rollout_id = ?'
      )
        .bind(status.rollout_id)
        .first<{ firmware_version: string }>();

      if (rollout) {
        await c.env.DB.prepare(
          'UPDATE devices SET firmware_version = ? WHERE device_id = ?'
        )
          .bind(rollout.firmware_version, status.device_id)
          .run();
      }
    }

    return c.json({ status: 'ok' });
  } catch (error) {
    console.error('Failed to process OTA status:', error);
    return c.json({ error: 'Processing error' }, 500);
  }
});

/**
 * POST /webhooks/mqtt/telemetry
 * Handle telemetry messages from devices
 *
 * Topic: opticworks/{device_id}/telemetry
 */
app.post('/mqtt/telemetry', async (c) => {
  const event = await c.req.json<EmqxWebhookEvent>();

  if (!event.payload) {
    return c.json({ error: 'Missing payload' }, 400);
  }

  // Forward to telemetry API endpoint for processing
  // This allows reuse of the telemetry processing logic
  const telemetryUrl = new URL('/api/telemetry', c.req.url);

  try {
    const response = await fetch(telemetryUrl.toString(), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: event.payload,
    });

    return c.json({ status: response.ok ? 'ok' : 'error' }, response.status);
  } catch (error) {
    console.error('Failed to forward telemetry:', error);
    return c.json({ error: 'Processing error' }, 500);
  }
});

/**
 * POST /webhooks/mqtt/config-status
 * Handle config status updates from devices
 *
 * Topic: opticworks/{device_id}/config/status
 */
app.post('/mqtt/config-status', async (c) => {
  const event = await c.req.json<EmqxWebhookEvent>();

  if (!event.payload) {
    return c.json({ error: 'Missing payload' }, 400);
  }

  try {
    const payload = JSON.parse(event.payload) as {
      device_id: string;
      config_version: number;
      status: 'applied' | 'rejected';
      error?: string;
    };

    // Log config application event
    await c.env.DB.prepare(
      `INSERT INTO device_events (device_id, event_type, event_data)
       VALUES (?, 'config_status', ?)`
    )
      .bind(payload.device_id, event.payload)
      .run();

    // Update device config version if applied
    if (payload.status === 'applied') {
      await c.env.DB.prepare(
        'UPDATE devices SET config_version = ? WHERE device_id = ? AND config_version < ?'
      )
        .bind(payload.config_version, payload.device_id, payload.config_version)
        .run();
    }

    return c.json({ status: 'ok' });
  } catch (error) {
    console.error('Failed to process config status:', error);
    return c.json({ error: 'Processing error' }, 500);
  }
});

export { app as webhookRoutes };
