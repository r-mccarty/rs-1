/**
 * Zone configuration type definitions
 *
 * Reference: docs/contracts/schemas/zone-config.schema.json
 */

import { z } from 'zod';

// Vertex (point in zone polygon)
export const VertexSchema = z.object({
  x: z.number().min(-6).max(6),  // meters
  y: z.number().min(0).max(6),   // meters
});

export type Vertex = z.infer<typeof VertexSchema>;

// Single zone definition
export const ZoneSchema = z.object({
  id: z.string().min(1).max(32),
  name: z.string().min(1).max(64),
  type: z.enum(['include', 'exclude']),
  vertices: z.array(VertexSchema).min(3).max(8),
  sensitivity: z.number().int().min(0).max(100).default(50),
  enabled: z.boolean().default(true),
});

export type Zone = z.infer<typeof ZoneSchema>;

// Full zone configuration
export const ZoneConfigSchema = z.object({
  version: z.number().int().positive(),
  zones: z.array(ZoneSchema).max(16),
  updated_at: z.string().datetime().optional(),
});

export type ZoneConfig = z.infer<typeof ZoneConfigSchema>;

// Zone configuration record from database
export interface ZoneConfigRecord {
  device_id: string;
  version: number;
  config_json: string;
  updated_at: string;
  updated_by: string | null;
}
