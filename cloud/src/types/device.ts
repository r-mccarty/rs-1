/**
 * Device-related type definitions
 *
 * Reference: docs/cloud/SERVICE_DEVICE_REGISTRY.md
 */

import { z } from 'zod';

// Device ID is a 32-character hex string derived from MAC
export const DeviceIdSchema = z.string().regex(/^[0-9a-f]{32}$/i);
export type DeviceId = z.infer<typeof DeviceIdSchema>;

// Device record from database
export interface Device {
  device_id: string;
  mac_address: string;
  firmware_version: string | null;
  last_seen: string | null;
  online: boolean;
  config_version: number;
  created_at: string;
}

// Device ownership
export interface DeviceOwnership {
  device_id: string;
  user_id: string;
  role: 'owner' | 'viewer';
}

// Device status from MQTT
export const DeviceStatusSchema = z.object({
  device_id: z.string(),
  firmware_version: z.string(),
  uptime_s: z.number().int().nonnegative(),
  free_heap_kb: z.number().int().nonnegative(),
  wifi_rssi: z.number().int().min(-100).max(0),
  config_version: z.number().int().nonnegative(),
  timestamp: z.string().datetime(),
});

export type DeviceStatus = z.infer<typeof DeviceStatusSchema>;

// Device registration request
export const DeviceRegisterSchema = z.object({
  mac_address: z.string().regex(/^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/),
  firmware_version: z.string().optional(),
});

export type DeviceRegisterRequest = z.infer<typeof DeviceRegisterSchema>;
