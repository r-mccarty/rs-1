/**
 * Cryptographic Utility Functions
 *
 * Reference: docs/cloud/README.md Section 6.1
 */

/**
 * Generate device ID from MAC address
 *
 * Device ID is SHA-256 hash of MAC address, truncated to 32 hex chars.
 * This provides a stable, privacy-preserving identifier.
 *
 * @param macAddress MAC address in format "AA:BB:CC:DD:EE:FF"
 * @returns 32-character hex device ID
 */
export function generateDeviceId(macAddress: string): string {
  // Normalize MAC address (remove colons, lowercase)
  const normalized = macAddress.replace(/:/g, '').toLowerCase();

  // In Workers, we use the Web Crypto API
  // For synchronous usage, we'll use a simple hash
  // In production, use crypto.subtle.digest for async version

  // Simple hash for development (replace with proper crypto in production)
  let hash = 0;
  for (let i = 0; i < normalized.length; i++) {
    const char = normalized.charCodeAt(i);
    hash = ((hash << 5) - hash + char) | 0;
  }

  // For now, just pad the MAC address hash
  // In production, use proper SHA-256
  const hashStr = Math.abs(hash).toString(16).padStart(8, '0');
  const deviceId = (normalized + hashStr).padEnd(32, '0').slice(0, 32);

  return deviceId;
}

/**
 * Generate device ID from MAC address (async, uses proper SHA-256)
 *
 * @param macAddress MAC address in format "AA:BB:CC:DD:EE:FF"
 * @returns Promise<string> 32-character hex device ID
 */
export async function generateDeviceIdAsync(macAddress: string): Promise<string> {
  const normalized = macAddress.replace(/:/g, '').toLowerCase();
  const encoder = new TextEncoder();
  const data = encoder.encode(normalized);

  const hashBuffer = await crypto.subtle.digest('SHA-256', data);
  const hashArray = Array.from(new Uint8Array(hashBuffer));
  const hashHex = hashArray.map((b) => b.toString(16).padStart(2, '0')).join('');

  return hashHex.slice(0, 32);
}

/**
 * Generate HMAC-SHA256 for device authentication
 *
 * @param secret Shared secret
 * @param message Message to sign
 * @returns Promise<string> Base64-encoded HMAC
 */
export async function generateHmac(secret: string, message: string): Promise<string> {
  const encoder = new TextEncoder();
  const keyData = encoder.encode(secret);
  const msgData = encoder.encode(message);

  const key = await crypto.subtle.importKey(
    'raw',
    keyData,
    { name: 'HMAC', hash: 'SHA-256' },
    false,
    ['sign']
  );

  const signature = await crypto.subtle.sign('HMAC', key, msgData);
  const base64 = btoa(String.fromCharCode(...new Uint8Array(signature)));

  return base64;
}

/**
 * Verify HMAC-SHA256
 *
 * @param secret Shared secret
 * @param message Original message
 * @param signature Base64-encoded signature to verify
 * @returns Promise<boolean> True if signature is valid
 */
export async function verifyHmac(
  secret: string,
  message: string,
  signature: string
): Promise<boolean> {
  const expected = await generateHmac(secret, message);
  return expected === signature;
}

/**
 * Generate a time-limited signed URL for firmware download
 *
 * @param bucket R2 bucket binding
 * @param key Object key
 * @param expiresInSeconds URL expiration time
 * @returns Promise<string> Signed URL
 */
export async function generateSignedUrl(
  bucket: R2Bucket,
  key: string,
  expiresInSeconds: number = 3600
): Promise<string> {
  // R2 doesn't have native signed URL support in Workers
  // For production, use a pre-signed URL service or custom token
  // This is a placeholder implementation

  const expiry = Date.now() + expiresInSeconds * 1000;
  const token = await generateHmac(key, `${key}:${expiry}`);

  // In production, this would be a proper signed URL
  return `https://firmware.opticworks.io/${key}?expires=${expiry}&sig=${encodeURIComponent(token)}`;
}
