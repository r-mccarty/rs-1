/**
 * Cloudflare Worker Environment Bindings
 *
 * Reference: docs/cloud/INFRASTRUCTURE.md
 */

export interface Env {
  // D1 Database
  DB: D1Database;

  // R2 Buckets
  FIRMWARE_BUCKET: R2Bucket;
  TELEMETRY_BUCKET: R2Bucket;

  // Environment variables
  ENVIRONMENT: string;
  MQTT_BROKER: string;
  MQTT_PORT: string;

  // Secrets (set via wrangler secret)
  MQTT_USERNAME?: string;
  MQTT_PASSWORD?: string;
  API_SECRET?: string;
}
