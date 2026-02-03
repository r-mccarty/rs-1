/**
 * Telemetry type definitions
 *
 * Reference: docs/cloud/SERVICE_TELEMETRY.md
 * Reference: docs/contracts/schemas/telemetry.schema.json
 */

import { z } from 'zod';

// Log level enum
export const LogLevelSchema = z.enum(['E', 'W', 'I', 'D', 'V']);
export type LogLevel = z.infer<typeof LogLevelSchema>;

// Single log entry
export const LogEntrySchema = z.object({
  level: LogLevelSchema,
  tag: z.string().max(16),
  message: z.string().max(256),
  timestamp: z.string().datetime().optional(),
});

export type LogEntry = z.infer<typeof LogEntrySchema>;

// Metric value (can be number or boolean)
export const MetricValueSchema = z.union([z.number(), z.boolean()]);
export type MetricValue = z.infer<typeof MetricValueSchema>;

// Telemetry payload from device
export const TelemetryPayloadSchema = z.object({
  device_id: z.string(),
  timestamp: z.string().datetime(),
  metrics: z.record(z.string(), MetricValueSchema),
  logs: z.array(LogEntrySchema).max(100).optional(),
});

export type TelemetryPayload = z.infer<typeof TelemetryPayloadSchema>;

// Common metric names
export const METRIC_NAMES = {
  // System
  UPTIME: 'system.uptime_s',
  FREE_HEAP: 'system.free_heap_kb',
  MIN_FREE_HEAP: 'system.min_free_heap_kb',
  WIFI_RSSI: 'system.wifi_rssi',

  // Radar
  RADAR_FRAMES: 'radar.frames_received',
  RADAR_INVALID: 'radar.frames_invalid',
  RADAR_TARGETS_AVG: 'radar.targets_per_frame',
  RADAR_FRAME_RATE: 'radar.frame_rate_hz',

  // Zones
  ZONE_COUNT: 'zones.count',
  ZONE_OCCUPIED: 'zones.occupied_count',

  // Native API
  API_CONNECTIONS: 'api.connections',
  API_MESSAGES_TX: 'api.messages_tx',
  API_MESSAGES_RX: 'api.messages_rx',

  // OTA
  OTA_IN_PROGRESS: 'ota.in_progress',
  OTA_PROGRESS: 'ota.progress_percent',
} as const;

// Aggregated metrics for dashboard
export interface MetricsAggregate {
  device_id: string;
  period_start: string;
  period_end: string;
  metrics: {
    [key: string]: {
      min: number;
      max: number;
      avg: number;
      count: number;
    };
  };
}
