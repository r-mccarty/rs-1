# RS-1 Firmware Review (2026-02-03)

## Summary
Massive firmware drop with strong module scaffolding, but integration and security gaps mean the current build will not behave like a complete RS-1 firmware stack yet. The highest-risk issues are missing boot wiring, OTA integrity verification, and incomplete Home Assistant (ESPHome) protocol handling.

## Findings

### Critical
- `firmware/main/main.c`: `app_main` only initializes M01 (radar ingest) and then loops. M02–M12 never initialize, despite the boot-sequence comment listing them. This means tracking, zone engine, presence smoothing, config store, security, OTA, native API, logging, and IAQ never start.
- `firmware/components/ota_manager/ota_manager.c`: Manifest SHA256 is parsed but never verified. `verify_firmware` does not compare the downloaded image hash against `manifest->sha256`, so a compromised transport or wrong artifact is not detected.
- `firmware/components/native_api/native_api.c`: ESPHome Native API is a stub. TODOs remain for protobuf handling, request parsing, and sensor state responses; current implementation will not actually interoperate with Home Assistant.
- `firmware/components/zone_editor/zone_editor.c`: Zone editor does not persist config or apply it to the runtime zone engine. `zone_editor_set_config` updates in-memory state only (TODO for M06), and does not call `zone_engine_load_zones` or equivalent.

### High
- `firmware/components/config_store/config_store.c`: Config encryption uses AES-ECB with a key derived from the device MAC and no integrity/MAC. This is predictable and malleable for security-sensitive blobs (network/passwords, security config). Consider AES-GCM or ESP-IDF NVS encryption with a per-device key in eFuse.
- `firmware/components/logging/logging.c`: Ring buffer logic does not manage `read_pos` during writes or overflows. After wrap, `log_read_recent` can return stale or corrupted entries because `read_pos` is never advanced when overwriting.
- `firmware/components/ota_manager/ota_manager.c`: HTTPS OTA client is configured without a CA bundle or certificate pinning (`esp_http_client_config_t` has no `cert_pem` / `crt_bundle_attach`). This risks TLS failures in production or implicit trust assumptions.

### Medium
- `firmware/components/timebase/timebase.c` + `firmware/main/main.c`: Scheduler requires external calls to `scheduler_tick` to execute tasks, but no task or main-loop integration is present. Scheduled tasks will never run unless a periodic tick is wired in.
- `firmware/components/config_store/config_store.c`: `nvs_get_stats` is called with `CONFIG_NVS_NAMESPACE` rather than a partition name, so stats are likely wrong or misleading.
- `firmware/components/zone_editor/zone_editor.c`: Auth token is stored only in RAM and served over HTTP without TLS. This may be fine for local-only setups, but should be explicitly documented or restricted if exposure is possible.

### Low
- Multiple TODOs remain in core services (`logging.c`, `security.c`, `ota_manager.c`, `native_api.c`, `zone_editor.c`, `iaq.c`). This conflicts with the commit message claiming full implementation and should be reconciled with product readiness.

## Requests For Discussion
- Are we targeting full ESPHome Native API compliance in this milestone, or is a minimal subset acceptable? The current stub will not pass real HA discovery or state updates.
- Should config encryption rely on ESP-IDF NVS encryption instead of custom AES-ECB? That would simplify key management and provide integrity.
- Do we want OTA integrity to be manifest-driven (SHA256 verification) or rely on signed firmware images and secure boot only?

## Test Notes
- Host tests exist only for LD2410/LD2450 parsers in `firmware/test`. No tests cover config store, tracking, zone engine, OTA, security, logging, or native API.
- Tests not run in this review.
