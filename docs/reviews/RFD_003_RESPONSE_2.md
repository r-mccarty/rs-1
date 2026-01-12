# RFD-003 Response: Requirements Critique Status

**Status:** Draft  
**Author:** Codex review (evidence-based)  
**Date:** 2026-01-12

---

## Method

- Treated the current repo contents as the only source of truth.
- No firmware implementation exists in this repo, so any runtime behavior remains unverified.
- When specs conflict, the issue is considered **Open** until reconciled.

## Status Legend

- **Open**: No spec resolution or conflicting specs remain.
- **Partially addressed**: Specs acknowledge the risk, but validation/enforcement is missing.
- **Addressed in spec**: The spec was updated to cover the gap (implementation still unverified).

---

## 1. Critical Issues (Stop Everything)

1. **OTA memory crisis** — **Partially addressed**  
   Memory budget now accounts for OTA TLS and adds heap checks, but there are no
   measured heap profiles or enforcement details beyond guidance.  
   Evidence: `docs/firmware/MEMORY_BUDGET.md`, `docs/firmware/HARDWAREOS_MODULE_OTA.md`

2. **Coordinate conversion ambiguity** — **Open**  
   COORDINATE_SYSTEM says M11 converts; MQTT contract says cloud converts.  
   Evidence: `docs/firmware/COORDINATE_SYSTEM.md`, `docs/contracts/PROTOCOL_MQTT.md`

3. **TLS memory for Native API** — **Partially addressed**  
   Memory budget includes 8 KB for Noise, but there is no measurement or
   verification of actual heap usage during HA connection.  
   Evidence: `docs/firmware/MEMORY_BUDGET.md`, `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md`

4. **Anti-rollback eFuse limit** — **Open**  
   Limit is stated, but no policy on when to burn, telemetry, or lifecycle
   strategy is defined.  
   Evidence: `docs/REQUIREMENTS_RS1.md`, `docs/firmware/HARDWAREOS_MODULE_SECURITY.md`

5. **NVS wear not enforced** — **Partially addressed**  
   Commit-on-change policy is specified, but no enforcement mechanism or commit
   telemetry exists in the specs.  
   Evidence: `docs/firmware/HARDWAREOS_MODULE_CONFIG_STORE.md`,
   `docs/firmware/HARDWAREOS_MODULE_TIMEBASE.md`

---

## 2. Major Issues (Will Cause Support Tickets)

6. **Kalman filter error recovery** — **Open**  
   No NaN/Inf/singular covariance handling is specified.  
   Evidence: `docs/firmware/HARDWAREOS_MODULE_TRACKING.md`

7. **Point-in-polygon edge cases** — **Open**  
   Boundary policy exists, but margin handling is described as a stub and not
   reflected in the algorithm.  
   Evidence: `docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md`

8. **Confirmed track visibility undefined** — **Addressed in spec**  
   M03 assumes confirmed tracks; track struct includes `confirmed`.  
   Evidence: `docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md`

9. **Sensitivity formula mismatch** — **Open**  
   Glossary default vs formula outputs do not match.  
   Evidence: `docs/firmware/GLOSSARY.md`, `docs/firmware/HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md`

10. **MQTT rate limits unenforceable** — **Open**  
    Rate limits listed without defining enforcement point or queue.  
    Evidence: `docs/contracts/PROTOCOL_MQTT.md`

11. **Telemetry contract mismatch** — **Open**  
    Contract uses single `telemetry` topic; service spec uses typed topics.  
    Evidence: `docs/contracts/PROTOCOL_MQTT.md`, `docs/cloud/SERVICE_TELEMETRY.md`

12. **Wi-Fi reconnection backoff UX** — **Open**  
    Exponential backoff to 60s is specified; no stable-connection reset rule.  
    Evidence: `docs/firmware/DEGRADED_MODES.md`

13. **Multi-connection API undefined** — **Open**  
    M05 notes multiple connections as future work only.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md`

14. **Safe mode recovery steps** — **Open**  
    Safe mode is defined but no user-facing recovery steps exist.  
    Evidence: `docs/firmware/BOOT_SEQUENCE.md`

15. **NVS corruption recovery UX** — **Partially addressed**  
    Behavior is described (reset to defaults), but re-provisioning flow is not.  
    Evidence: `docs/firmware/DEGRADED_MODES.md`

16. **OTA cooldown storage** — **Open**  
    Cooldown policy exists, but no storage/persistence mechanism is specified.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_OTA.md`

---

## 3. Significant Issues (Technical Debt)

17. **Telemetry opt-in mechanism missing** — **Addressed in spec**  
    `telemetry_enabled` default false is defined.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_LOGGING.md`, `docs/REQUIREMENTS_RS1.md`

18. **LD2450 frame sync recovery missing** — **Addressed in spec**  
    Resync on invalid header/footer is defined.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_RADAR_INGEST.md`

19. **Track retirement undefined** — **Addressed in spec**  
    `occlusion_timeout_frames` and `tentative_drop` parameters exist.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_TRACKING.md`

20. **Zone evaluation load assumed** — **Partially addressed**  
    Performance budget is stated without validation evidence.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_ZONE_ENGINE.md`

21. **mDNS collision** — **Open**  
    No unique instance naming is specified.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md`

22. **Config downgrade handling** — **Open**  
    Migration is listed as an open question.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_CONFIG_STORE.md`

23. **Noise PSK provisioning/rotation** — **Partially addressed**  
    Key storage and pairing are specified, but HA PSK handoff/rotation details
    are not.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_SECURITY.md`,
    `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md`

24. **Zone editor auth** — **Addressed in spec**  
    Pairing token + cloud auth are specified.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_ZONE_EDITOR.md`

25. **Kalman tuning parameters undefined** — **Addressed in spec**  
    Parameters are listed in M02.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_TRACKING.md`

26. **Signature block verification order** — **Partially addressed**  
    Verification flow exists; `block_hash` is defined but not validated.  
    Evidence: `docs/firmware/HARDWAREOS_MODULE_SECURITY.md`

---

## 4. Documentation Issues

27. **Assumptions staleness not enforced** — **Open**  
28. **RFD-001 not linked** — **Open**  
29. **No API stability guarantee** — **Open**  
30. **Test plan lacks coverage metrics** — **Open**  
31. **Integration test framework unspecified** — **Open** (referenced scripts are absent)  
32. **Ground truth sensor not named** — **Open**  

Evidence across: `docs/firmware/*`, `docs/testing/*`

---

## 5. Contradictions

33. **Unlimited zones vs 16 cap** — **Open**  
    Evidence: `docs/REQUIREMENTS_RS1.md`, `docs/contracts/SCHEMA_ZONE_CONFIG.json`

34. **<50ms latency vs 100ms throttle** — **Open**  
    Evidence: `docs/REQUIREMENTS_RS1.md`, `docs/firmware/README.md`,
    `docs/firmware/HARDWAREOS_MODULE_NATIVE_API.md`

35. **Mobile app vs web MVP** — **Open**  
    Evidence: `docs/REQUIREMENTS_RS1.md`, `docs/firmware/README.md`

36. **Telemetry privacy vs target stream** — **Open**  
    Evidence: `docs/REQUIREMENTS_RS1.md`, `docs/contracts/PROTOCOL_MQTT.md`

37. **Sensitivity table vs formula** — **Open**  
    Evidence: `docs/firmware/GLOSSARY.md`,
    `docs/firmware/HARDWAREOS_MODULE_PRESENCE_SMOOTHING.md`

---

## 6. Missing Specifications

38. **Device naming collision resolution** — **Open**  
39. **Zone migration strategy** — **Open**  
40. **Factory reset procedure (user-facing)** — **Open**  
41. **OTA RSSI re-check during download** — **Open**  

---

## Summary

Several RFD-003 issues were addressed in specs after the critique (notably NVS
commit policy, tracking retirement parameters, and telemetry opt-in), but most
issues remain **Open** or **Partially addressed** due to unresolved conflicts,
missing definitions, or lack of validation. No firmware implementation exists
in the repo to confirm runtime behavior.
