/**
 * RS-1 Cloud Services - Cloudflare Worker Entry Point
 *
 * This worker handles:
 * - Device API (registration, status, zones)
 * - OTA orchestration API
 * - Telemetry ingestion
 * - MQTT webhook processing
 *
 * Reference: docs/cloud/README.md
 */

import { Hono } from 'hono';
import { cors } from 'hono/cors';
import { logger } from 'hono/logger';

import type { Env } from './types/env';
import { deviceRoutes } from './routes/devices';
import { zoneRoutes } from './routes/zones';
import { otaRoutes } from './routes/ota';
import { telemetryRoutes } from './routes/telemetry';
import { webhookRoutes } from './routes/webhooks';

// Create Hono app with environment binding
const app = new Hono<{ Bindings: Env }>();

// Global middleware
app.use('*', logger());
app.use(
  '/api/*',
  cors({
    origin: ['http://localhost:3000', 'https://app.opticworks.io'],
    allowMethods: ['GET', 'POST', 'PUT', 'DELETE', 'OPTIONS'],
    allowHeaders: ['Content-Type', 'Authorization'],
    exposeHeaders: ['X-Request-Id'],
    maxAge: 86400,
    credentials: true,
  })
);

// Health check
app.get('/', (c) => {
  return c.json({
    service: 'RS-1 Cloud API',
    version: '0.1.0',
    environment: c.env.ENVIRONMENT,
    status: 'healthy',
  });
});

app.get('/health', (c) => {
  return c.json({ status: 'ok' });
});

// API routes
app.route('/api/devices', deviceRoutes);
app.route('/api/zones', zoneRoutes);
app.route('/api/ota', otaRoutes);
app.route('/api/telemetry', telemetryRoutes);

// MQTT webhooks (from EMQX)
app.route('/webhooks', webhookRoutes);

// 404 handler
app.notFound((c) => {
  return c.json({ error: 'Not Found', path: c.req.path }, 404);
});

// Error handler
app.onError((err, c) => {
  console.error('Unhandled error:', err);
  return c.json(
    {
      error: 'Internal Server Error',
      message: c.env.ENVIRONMENT === 'development' ? err.message : undefined,
    },
    500
  );
});

export default app;
