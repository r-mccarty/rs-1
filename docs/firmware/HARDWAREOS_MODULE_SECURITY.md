# HardwareOS Security Module Specification (M10)

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Provide security services for RS-1 including firmware signature validation, secure transport, device authentication, and key management. This module ensures firmware integrity, protects OTA updates, and secures local/cloud communications.

## 2. Assumptions

> **Staleness Warning**: If any assumption changes, revisit the requirements in this document.

| ID | Assumption | Impact if Changed |
|----|------------|-------------------|
| A1 | ESP32-C3 Secure Boot V2 is available | Boot chain security |
| A2 | Flash encryption is optional for MVP | Key provisioning complexity |
| A3 | TLS 1.2+ is required for all network traffic | mbedTLS configuration |
| A4 | ECDSA P-256 for firmware signing | Signature verification code |
| A5 | Device identity derived from eFuse MAC | Provisioning flow |
| A6 | Pre-shared keys for API encryption | Key distribution method |
| A7 | No hardware security module (HSM) on device | Software key storage |

## 3. Security Domains

### 3.1 Boot Security

Secure boot chain ensuring only signed firmware executes.

### 3.2 Firmware Integrity

Signature validation for OTA and local updates.

### 3.3 Transport Security

TLS for MQTT, HTTPS, and API connections.

### 3.4 Device Authentication

Unique device identity and credential management.

### 3.5 API Security

Authentication for Native API and local HTTP endpoints.

## 4. Boot Security

### 4.1 Secure Boot V2 (ESP32-C3)

```
┌─────────────────────────────────────────────────────────────┐
│                     Secure Boot Chain                        │
│                                                              │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌────────┐ │
│  │  ROM     │───▶│ Bootloader│───▶│   App    │───▶│ OTA    │ │
│  │ (eFuse)  │    │ (signed) │    │ (signed) │    │(signed)│ │
│  └──────────┘    └──────────┘    └──────────┘    └────────┘ │
│       │                │               │              │      │
│       ▼                ▼               ▼              ▼      │
│    Burned           Verify          Verify         Verify    │
│    at mfg           RSA/ECDSA       RSA/ECDSA     RSA/ECDSA │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 Key Storage (eFuse)

| eFuse Block | Purpose | Write Protection |
|-------------|---------|------------------|
| Block 0 | Secure boot enable flags | Yes (permanent) |
| Block 1 | Flash encryption key | Yes (permanent) |
| Block 2 | Secure boot public key digest | Yes (permanent) |

### 4.3 Secure Boot Configuration

```c
typedef struct {
    bool secure_boot_enabled;
    bool flash_encryption_enabled;
    uint8_t key_digest[32];         // SHA-256 of public key
    uint8_t rollback_index;         // Anti-rollback counter
} secure_boot_config_t;
```

## 5. Firmware Signing

### 5.1 Signature Scheme

- **Algorithm**: ECDSA with P-256 (secp256r1)
- **Hash**: SHA-256
- **Key Size**: 256-bit private, 512-bit public (uncompressed)

### 5.2 Signature Block Format

Appended to firmware binary:

```c
typedef struct __attribute__((packed)) {
    uint8_t magic[4];               // "OPFW"
    uint32_t version;               // Signature format version
    uint8_t fw_hash[32];            // SHA-256 of firmware
    uint8_t signature[64];          // ECDSA signature (r || s)
    uint8_t public_key[64];         // Public key (for verification)
    uint32_t fw_version;            // Firmware version (anti-rollback)
    uint32_t build_timestamp;       // Build time
    uint8_t reserved[32];           // Future use
    uint8_t block_hash[32];         // SHA-256 of this block (minus this field)
} firmware_signature_block_t;
```

### 5.3 Verification Flow

```c
esp_err_t security_verify_firmware(const uint8_t *fw_data, size_t fw_size) {
    // 1. Locate signature block at end of firmware
    firmware_signature_block_t *sig = (void*)(fw_data + fw_size - sizeof(*sig));

    // 2. Verify magic
    if (memcmp(sig->magic, "OPFW", 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 3. Verify public key matches trusted key
    if (!security_is_trusted_key(sig->public_key)) {
        return ESP_ERR_INVALID_STATE;
    }

    // 4. Compute firmware hash
    uint8_t computed_hash[32];
    sha256(fw_data, fw_size - sizeof(*sig), computed_hash);

    // 5. Verify hash matches
    if (memcmp(computed_hash, sig->fw_hash, 32) != 0) {
        return ESP_ERR_INVALID_CRC;
    }

    // 6. Verify ECDSA signature
    if (!ecdsa_verify(sig->public_key, sig->fw_hash, sig->signature)) {
        return ESP_ERR_INVALID_MAC;
    }

    // 7. Anti-rollback check
    if (sig->fw_version < security_get_min_version()) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}
```

### 5.4 Trusted Keys

```c
// Embedded in firmware (can be updated via signed OTA)
typedef struct {
    uint8_t key[64];
    uint32_t valid_from;
    uint32_t valid_until;
    bool revoked;
} trusted_key_t;

#define MAX_TRUSTED_KEYS 4
trusted_key_t trusted_keys[MAX_TRUSTED_KEYS];
```

## 6. Transport Security

### 6.1 TLS Configuration

```c
typedef struct {
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca_cert;       // CA certificate for server validation
    mbedtls_x509_crt device_cert;   // Device certificate (optional)
    mbedtls_pk_context device_key;  // Device private key (optional)
} tls_context_t;
```

### 6.2 Cipher Suites (Priority Order)

1. `TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256`
2. `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256`
3. `TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384`

### 6.3 Certificate Pinning

For cloud connections, pin to OpticWorks CA:

```c
// SHA-256 fingerprint of OpticWorks CA certificate
const uint8_t PINNED_CA_FINGERPRINT[32] = { ... };

bool security_verify_server_cert(const mbedtls_x509_crt *cert) {
    uint8_t fingerprint[32];
    sha256(cert->raw.p, cert->raw.len, fingerprint);
    return memcmp(fingerprint, PINNED_CA_FINGERPRINT, 32) == 0;
}
```

### 6.4 Connections Requiring TLS

| Connection | TLS Required | Cert Validation |
|------------|--------------|-----------------|
| MQTT (OTA) | Yes | CA + pinning |
| HTTPS (CDN) | Yes | CA |
| Native API | Noise preferred | PSK |
| Local HTTP | No (LAN only) | N/A |

## 7. Device Authentication

### 7.1 Device Identity

```c
typedef struct {
    uint8_t device_id[16];          // Derived from MAC
    uint8_t device_secret[32];      // Provisioned at manufacture
    uint8_t mqtt_username[32];      // device_id as username
    uint8_t mqtt_password[64];      // Derived credential
} device_identity_t;
```

### 7.2 Identity Derivation

```c
void security_derive_identity(device_identity_t *id) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    // Device ID: SHA-256(mac || "opticworks-rs1")[:16]
    uint8_t hash[32];
    sha256_init();
    sha256_update(mac, 6);
    sha256_update("opticworks-rs1", 14);
    sha256_final(hash);
    memcpy(id->device_id, hash, 16);

    // Format as hex string for MQTT username
    hex_encode(id->device_id, 16, id->mqtt_username);
}
```

### 7.3 MQTT Authentication

```c
// MQTT password: HMAC-SHA256(device_secret, device_id || timestamp)
void security_generate_mqtt_password(device_identity_t *id, uint32_t timestamp) {
    uint8_t data[20];
    memcpy(data, id->device_id, 16);
    memcpy(data + 16, &timestamp, 4);

    uint8_t hmac[32];
    hmac_sha256(id->device_secret, 32, data, 20, hmac);

    base64_encode(hmac, 32, id->mqtt_password);
}
```

## 8. Provisioning Security

### 8.1 AP Mode Security

When the device operates in provisioning AP mode, special security considerations apply:

```c
typedef struct {
    char ap_ssid[32];           // "OpticWorks-XXXX"
    bool ap_open;               // true (no password)
    uint32_t ap_timeout_sec;    // 600 (10 minutes)
    bool captive_portal;        // true
} provisioning_ap_config_t;
```

**Security decisions:**

| Risk | Decision | Rationale |
|------|----------|-----------|
| Open AP | Accept | Reduces setup friction; no sensitive data on AP network |
| Password exposure | Mitigate | WiFi password only stored after successful connection |
| Rogue provisioning | Accept | Requires physical access to see QR code |

### 8.2 Provisioning API Security

The local provisioning API (`192.168.4.1`) has limited exposure:

```c
typedef struct {
    bool allow_device_info;     // true - public info only
    bool allow_network_scan;    // true - user-initiated
    bool allow_provision;       // true - requires full payload
    bool require_https;         // false - local network only
} provisioning_api_config_t;
```

**API exposure:**

| Endpoint | Authentication | Rate Limit |
|----------|---------------|------------|
| `GET /api/device-info` | None | 10/min |
| `GET /api/networks` | None | 2/min |
| `POST /api/provision` | None | 3/min |
| `GET /api/provision/status` | None | 60/min |
| `WS /api/provision/ws` | None | 1 connection |

### 8.3 Credential Storage

WiFi credentials are stored after successful WiFi connection (before cloud registration):

```c
// Commit after WiFi connection success
if (wifi_connected) {
    config_store_set_network(ssid, password);
    config_store_set_provisioned(true);
    config_store_commit();
}

// This ensures device works locally even if cloud is unreachable
```

---

## 9. API Security

### 9.1 Native API (ESPHome Compatible)

| Method | Security |
|--------|----------|
| Password | Simple password in ConnectRequest |
| Noise | Noise_NNpsk0 with PSK |

### 9.2 Noise Protocol Implementation

```c
typedef struct {
    uint8_t psk[32];                // Pre-shared key
    noise_handshakestate hs;        // Handshake state
    noise_cipherstate tx;           // Encrypt state
    noise_cipherstate rx;           // Decrypt state
    bool handshake_complete;
} noise_session_t;

esp_err_t security_noise_init(noise_session_t *session, const uint8_t *psk);
esp_err_t security_noise_handshake(noise_session_t *session, const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len);
esp_err_t security_noise_encrypt(noise_session_t *session, const uint8_t *plaintext, size_t len, uint8_t *ciphertext);
esp_err_t security_noise_decrypt(noise_session_t *session, const uint8_t *ciphertext, size_t len, uint8_t *plaintext);
```

### 9.3 Device Password Authentication

Local access to the device (Zone Editor, settings) uses standard HTTP authentication:

```c
typedef struct {
    char username[16];              // Always "admin"
    uint8_t password_hash[32];      // SHA-256 of password
    uint8_t password_salt[16];      // Random salt
    bool password_changed;          // true if user changed default
} device_auth_t;

// Default password is unique per device, generated at manufacturing
// and stored in a read-only flash partition
typedef struct {
    char default_password[9];       // 8 chars + null terminator
} manufacturing_data_t;
```

**Authentication Flow:**
```
1. User connects to device on LAN (http://{device_ip}/)
2. Device returns 401 Unauthorized with WWW-Authenticate: Basic
3. User enters admin / {password from device label}
4. Device validates credentials, returns session cookie
5. Subsequent requests include session cookie (valid 24 hours)
6. User can change password via /settings endpoint
```

**Password Requirements:**
- Default: 8-character alphanumeric (printed on device label)
- Custom: Minimum 8 characters
- Stored: SHA-256 hash with random salt

**Password Reset:**
- Factory reset restores default password
- Default password is in read-only flash (survives reset)

## 10. Key Management

### 10.1 Key Types

| Key | Storage | Purpose |
|-----|---------|---------|
| Secure boot key | eFuse (burned) | Boot chain |
| Flash encryption key | eFuse (burned) | Flash encryption |
| Firmware signing key | Firmware (public only) | OTA validation |
| Device secret | NVS (encrypted) | Cloud auth |
| API PSK | NVS (encrypted) | API encryption |
| Device password | NVS (hashed) | Local API auth |
| Default password | Flash (read-only) | Factory default |

### 10.2 Key Rotation

| Key | Rotation Method | Frequency |
|-----|-----------------|-----------|
| Firmware signing | OTA with new trusted key | As needed |
| Device secret | Provisioning or support | Rare |
| API PSK | User-initiated or OTA | As needed |

### 10.3 Key Provisioning

Manufacturing provisioning:
1. Generate device secret on secure workstation.
2. Generate unique 8-char device password.
3. Write device secret to NVS via serial during manufacture.
4. Write default password to read-only flash partition.
5. Print password on device label (next to QR code).
6. Register device_id, secret_hash, and MAC in cloud database.
7. Record MAC → order mapping when device ships.

## 11. Anti-Rollback

### 11.1 Version Tracking

```c
// Stored in eFuse (one-way counter)
typedef struct {
    uint8_t major;
    uint8_t minor;
    uint16_t security_version;      // Monotonic security version
} rollback_version_t;

// On OTA: reject if new.security_version < current.security_version
```

### 11.2 eFuse Counter

ESP32-C3 provides 32 bits for anti-rollback. Each security update burns one bit:

```c
uint32_t current_version = esp_efuse_read_field_cnt(ESP_EFUSE_SECURE_VERSION);
if (new_security_version <= current_version) {
    return ESP_ERR_INVALID_VERSION;
}
// After successful boot:
esp_efuse_write_field_cnt(ESP_EFUSE_SECURE_VERSION, new_security_version);
```

## 12. Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `secure_boot_enabled` | bool | true | Enable secure boot |
| `flash_encryption` | bool | false | Enable flash encryption |
| `tls_min_version` | uint8 | TLS1.2 | Minimum TLS version |
| `api_encryption_required` | bool | true | Require Noise for API |
| `pairing_code_timeout_sec` | uint16 | 300 | Pairing code validity (5 min) |
| `session_token_timeout_sec` | uint32 | 3600 | Session token validity (1 hour) |
| `ap_mode_timeout_sec` | uint32 | 600 | AP mode timeout (10 min) |
| `provision_rate_limit` | uint8 | 3 | Max provisioning attempts per minute |

## 13. Security Events

### 13.1 Logged Events

| Event | Severity | Description |
|-------|----------|-------------|
| `SEC_BOOT_VERIFIED` | Info | Firmware signature valid |
| `SEC_BOOT_FAILED` | Error | Signature verification failed |
| `SEC_AUTH_FAILED` | Warning | API auth failure |
| `SEC_PAIRING_ATTEMPT` | Info | Pairing code entered |
| `SEC_ROLLBACK_BLOCKED` | Error | Downgrade attempt blocked |
| `SEC_PROVISION_START` | Info | Provisioning AP mode started |
| `SEC_PROVISION_SUCCESS` | Info | Device provisioned successfully |
| `SEC_PROVISION_FAILED` | Warning | Provisioning failed |

### 13.2 Telemetry (M09)

| Metric | Type | Description |
|--------|------|-------------|
| `security.auth_failures` | Counter | Failed authentications |
| `security.pairing_attempts` | Counter | Pairing attempts |
| `security.tls_handshakes` | Counter | TLS connections |
| `security.provision_attempts` | Counter | Provisioning attempts |
| `security.provision_success` | Counter | Successful provisioning |

## 14. Testing Strategy

### 14.1 Unit Tests

- Signature verification with valid/invalid signatures.
- Key derivation consistency.
- Noise protocol handshake.
- Provisioning state machine transitions.

### 14.2 Integration Tests

- OTA with signed/unsigned firmware.
- TLS connection to test server.
- API authentication flow.
- Full provisioning flow (AP mode → WiFi → cloud).

### 14.3 Security Tests

- Attempt rollback to older firmware.
- Attempt connection with invalid certificate.
- Fuzz API authentication.
- Provisioning rate limit enforcement.

## 15. Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| mbedTLS | 3.x | TLS, crypto |
| ESP-IDF Secure Boot | 5.x | Boot chain |
| Noise-C | latest | API encryption |

## 16. Open Questions

- Flash encryption for production (performance impact)?
- Hardware key storage options for future revision?
- Certificate rotation strategy for long-lived devices?
- Remote key revocation mechanism?

## 17. References

| Document | Purpose |
|----------|---------|
| `../contracts/PROTOCOL_PROVISIONING.md` | Complete provisioning protocol |
| `../contracts/PROTOCOL_MQTT.md` | MQTT authentication details |
| `HARDWAREOS_MODULE_CONFIG_STORE.md` | Credential storage |
