# RFD: Delta Issues vs Anvil/Synthesis (2026-02-03)

**Scope:** Items present in Anvil/Synthesis reviews that were not in my `RFD-20260203-code-review.md`.

## Findings

### 1. HIGH: Signature verification trusts any key when none configured
- **Location:** `firmware/components/security/security.c:405-434`
- **What:** `security_is_trusted_key` returns true for any public key if no trusted keys are configured.
- **Impact:** In production builds with an empty trusted key list, any attacker-generated key can pass signature verification. This defeats the trust model and makes firmware signature checks ineffective.
- **Recommendation:** Gate permissive behavior behind a dev-only flag (e.g., `CONFIG_RS1_ALLOW_UNTRUSTED_KEYS`). Default to rejecting untrusted keys in production. Add a hard failure (or explicit warning + refusal) when the trusted key store is empty unless the dev override is set. Add a CI guard to fail release builds if trusted keys are not configured.

### 2. MEDIUM: WebSocket clients never removed on disconnect
- **Location:** `firmware/components/zone_editor/zone_editor.c:681-760` and `firmware/components/zone_editor/zone_editor.c:982-1013`
- **What:** WebSocket clients are added on connect but there is no handler to clear entries on close or error. `client_count` only increases; stale entries are left active.
- **Impact:** Client slots can be exhausted over time and broadcasts continue attempting sends to closed sockets, increasing CPU usage and dropped frames.
- **Recommendation:** Handle `HTTPD_WS_TYPE_CLOSE` and socket errors to prune clients, decrement `client_count`, and free slots when a client disconnects. Consider pruning on send failure as a fallback.

## Test Notes
- Tests not run.
