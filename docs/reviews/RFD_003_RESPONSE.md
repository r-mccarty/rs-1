# RFD-003 Response: Requirements Critique Review

**Status:** Draft
**Author:** Systems Review (Docs-Only Audit)
**Date:** 2026-01-12

---

## Scope and Method

- Source of truth: current repo contents only (docs/specs). No firmware code exists in this repo.
- Classification:
  - **Open**: Contradiction or missing behavior still present in current docs.
  - **Partial**: Some mitigation or definition added, but gap remains or is only stated at a high level.
  - **Resolved (doc-level)**: Current docs now specify the missing behavior clearly.

---

## Issue Status (RFD-003 Appendix)

| ID | Issue (short) | Status | Evidence in repo |
|----|---------------|--------|------------------|
| 1 | OTA memory crisis | Partial | `docs/firmware/MEMORY_BUDGET.md` defines OTA heap checks and mitigation but still 30KB free during OTA; `docs/firmware/HARDWAREOS_MODULE_OTA.md` does not mention heap gating or MQTT disconnect. |
| 2 | Coordinate conversion ambiguity | Open | `docs/firmware/COORDINATE_SYSTEM.md` says M11 is the only conversion point; `docs/contracts/PROTOCOL_MQTT.md` says cloud converts before publish. |
| 3 | TLS memory budget for M05 | Partial | `docs/firmware/MEMORY_BUDGET.md` assigns 8KB for Noise without measurement; no profiling evidence in repo. |
| 4 | Anti-rollback eFuse limit (32) | Open | `docs/REQUIREMENTS_RS1.md` and `docs/firmware/HARDWAREOS_MODULE_SECURITY.md` describe 32-count eFuse with no burn policy or lifecycle plan. |
| 5 | NVS wear not enforced | Partial | `docs/firmware/HARDWAREOS_MODULE_CONFIG_STORE.md` and `docs/firmware/HARDWAREOS_MODULE_TIMEBASE.md` prohibit periodic commits, but no commit-count telemetry or enforcement is defined. |
| 6 | Kalman divergence recovery missing | Open | `docs/firmware/HARDWAREOS_MODULE_TRACKING.md` has no NaN/Inf or covariance-singular handling. |
| 7 | Point-in-polygon edge handling | Partial | `docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md` states boundary policy, but margin logic is not actually applied in the provided pseudocode. |
| 8 | Confirmed track visibility undefined | Resolved (doc-level) | `docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md` defines `track_t.confirmed` and assumes confirmed-only input. |
| 9 | Sensitivity formula mismatch | Open | `docs/firmware/GLOSSARY.md` defines defaults that do not match the formula; `docs/firmware/HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md` repeats the formula. |
| 10 | MQTT rate limits unenforceable | Open | `docs/contracts/PROTOCOL_MQTT.md` lists rates but no enforcement point or queue definition. |
| 11 | Telemetry contract mismatch | Open | `docs/contracts/PROTOCOL_MQTT.md` uses `telemetry` single topic; `docs/cloud/SERVICE_TELEMETRY.md` uses per-category topics. |
| 12 | Wi-Fi backoff behavior | Partial | `docs/firmware/DEGRADED_MODES.md` caps at 60s and resets on success, but no stability window or immediate retry after network recovery. |
| 13 | Multi-connection API undefined | Open | `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md` marks multiple connections as future/open question. |
| 14 | Safe mode recovery steps | Partial | `docs/firmware/BOOT_SEQUENCE.md` defines safe mode boot, but no user-facing recovery procedure. |
| 15 | NVS corruption loses Wi-Fi | Resolved (doc-level) | `docs/firmware/DEGRADED_MODES.md` documents erase + reconfigure behavior. |
| 16 | OTA cooldown storage undefined | Open | `docs/firmware/HARDWAREOS_MODULE_OTA.md` requires cooldown but does not specify persistence or storage. |
| 17 | Telemetry opt-in mechanism missing | Partial | `docs/firmware/HARDWAREOS_MODULE_LOGGING.md` has `telemetry_enabled` flag but no defined consent flow. |
| 18 | LD2450 frame sync recovery missing | Partial | `docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md` says "discard and resync" without algorithm or framing strategy. |
| 19 | Track retirement criteria missing | Resolved (doc-level) | `docs/firmware/HARDWAREOS_MODULE_TRACKING.md` defines `tentative_drop` and `occlusion_timeout_frames`. |
| 20 | Zone evaluation load assumed | Partial | `docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md` estimates 3*16*8 checks but no measurement or CPU budget validation. |
| 21 | mDNS naming collision | Open | `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md` shows static service name without unique instance rule. |
| 22 | Config downgrade handling | Open | `docs/firmware/HARDWAREOS_MODULE_CONFIG_STORE.md` lists migration as open question. |
| 23 | Noise PSK management | Partial | `docs/firmware/HARDWAREOS_MODULE_SECURITY.md` defines PSK storage and pairing but not how HA receives/derives PSK. |
| 24 | Zone editor auth | Partial | `docs/firmware/HARDWAREOS_MODULE_ZONE_EDITOR.md` mentions pairing token/auth but no concrete protocol or threat model. |
| 25 | Kalman tuning parameters undefined | Resolved (doc-level) | `docs/firmware/HARDWAREOS_MODULE_TRACKING.md` defines Q/R parameters and defaults. |
| 26 | Signature block verification order | Resolved (doc-level) | `docs/firmware/HARDWAREOS_MODULE_SECURITY.md` specifies verification flow. |
| 27 | Assumptions staleness enforcement | Partial | Several modules include "Staleness Warning," but no repo-wide enforcement mechanism exists. |
| 28 | RFD-001 not linked | Partial | Many docs reference RFD-001 issues, but linking is inconsistent and not cataloged. |
| 29 | API stability guarantee missing | Open | `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md` lacks stability/compatibility policy beyond ESPHome protocol mention. |
| 30 | Test plan coverage metrics | Open | `docs/testing/VALIDATION_PLAN_RS1.md` has no coverage targets. |
| 31 | Integration test framework unspecified | Open | `docs/testing/INTEGRATION_TESTS.md` describes scenarios but no actual framework/scripts in repo. |
| 32 | Ground truth sensor not named | Open | `docs/testing/VALIDATION_PLAN_RS1.md` references a "depth sensor" without specific model or method. |
| 33 | Unlimited zones vs 16 cap | Open | `docs/REQUIREMENTS_RS1.md` says unlimited (practical 16); `docs/contracts/SCHEMA_ZONE_CONFIG.json` and `docs/firmware/MEMORY_BUDGET.md` cap at 16. |
| 34 | <50ms latency vs 100ms throttle | Open | `docs/REQUIREMENTS_RS1.md` targets <50ms to HA; `docs/firmware/README.md` states 10 Hz (100ms) output. |
| 35 | Mobile app vs web MVP | Open | `docs/REQUIREMENTS_RS1.md` calls for iOS/Android onboarding; `docs/firmware/README.md` lists mobile app as non-goal. |
| 36 | Telemetry privacy vs target stream | Open | `docs/REQUIREMENTS_RS1.md` forbids target positions; `docs/contracts/PROTOCOL_MQTT.md` defines target stream topic. |
| 37 | Sensitivity table vs formula | Open | `docs/firmware/GLOSSARY.md` examples do not match formula; formula repeated in `docs/firmware/HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md`. |
| 38 | Device naming collision resolution | Open | No doc defines unique device naming for mDNS/HA discovery. |
| 39 | Zone migration strategy | Open | Not specified; flagged as open question in `docs/firmware/HARDWAREOS_MODULE_CONFIG_STORE.md`. |
| 40 | Factory reset procedure | Open | No user-facing reset procedure defined; only internal config API in `docs/firmware/HARDWAREOS_MODULE_CONFIG_STORE.md`. |
| 41 | OTA RSSI re-check during download | Open | `docs/firmware/HARDWAREOS_MODULE_OTA.md` checks RSSI at start only. |

---

## Summary

- **Still open or partial:** The majority of issues remain open or only partially addressed in the current docs.
- **Resolved at doc-level:** Items 8, 15, 19, 25, and 26 now have explicit spec detail.
- **Highest-risk open contradictions:** unit conversion ownership, telemetry contract mismatch, latency target vs output rate, and mobile app scope.

---

## Recommended Next Steps (Doc Updates Only)

1. Resolve unit conversion ownership in one place and remove the conflicting statement.
2. Align telemetry topic structure between `PROTOCOL_MQTT.md` and `SERVICE_TELEMETRY.md`.
3. Decide on HA output rate vs latency requirement and update either spec to match.
4. Define a concrete opt-in flow and surface it in logging/telemetry specs.
5. Add a user-facing factory reset procedure and a config migration/downgrade policy.

