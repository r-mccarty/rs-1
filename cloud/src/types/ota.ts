/**
 * OTA (Over-the-Air) update type definitions
 *
 * Reference: docs/cloud/SERVICE_OTA_ORCHESTRATOR.md
 * Reference: docs/contracts/schemas/ota-manifest.schema.json
 */

import { z } from 'zod';

// OTA manifest sent to device
export const OtaManifestSchema = z.object({
  version: z.string().regex(/^\d+\.\d+\.\d+$/),  // Semantic versioning
  url: z.string().url(),
  sha256: z.string().regex(/^[0-9a-f]{64}$/i),
  size_bytes: z.number().int().positive(),
  rollout_id: z.string().uuid(),
  min_wifi_rssi: z.number().int().min(-100).max(0).default(-70),
  expires_at: z.string().datetime(),
});

export type OtaManifest = z.infer<typeof OtaManifestSchema>;

// Rollout status enum
export type RolloutStatus =
  | 'pending'
  | 'active'
  | 'paused'
  | 'completed'
  | 'aborted';

// OTA rollout record
export interface OtaRollout {
  rollout_id: string;
  firmware_version: string;
  firmware_url: string;
  firmware_sha256: string;
  firmware_size: number;
  status: RolloutStatus;
  target_percent: number;
  created_at: string;
  started_at: string | null;
  completed_at: string | null;
}

// Device OTA status
export type DeviceOtaStatus =
  | 'pending'
  | 'downloading'
  | 'verifying'
  | 'installing'
  | 'success'
  | 'failed';

export interface OtaDeviceStatus {
  device_id: string;
  rollout_id: string;
  status: DeviceOtaStatus;
  progress: number;
  error_message: string | null;
  updated_at: string;
}

// OTA status update from device
export const OtaStatusUpdateSchema = z.object({
  device_id: z.string(),
  rollout_id: z.string().uuid(),
  status: z.enum([
    'pending',
    'downloading',
    'verifying',
    'installing',
    'success',
    'failed',
  ]),
  progress: z.number().int().min(0).max(100),
  error_message: z.string().nullable().optional(),
  timestamp: z.string().datetime(),
});

export type OtaStatusUpdate = z.infer<typeof OtaStatusUpdateSchema>;

// Create rollout request
export const CreateRolloutSchema = z.object({
  firmware_version: z.string().regex(/^\d+\.\d+\.\d+$/),
  firmware_key: z.string(),  // R2 key
  target_percent: z.number().int().min(0).max(100).default(10),
});

export type CreateRolloutRequest = z.infer<typeof CreateRolloutSchema>;
