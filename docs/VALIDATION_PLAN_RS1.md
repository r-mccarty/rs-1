# OpticWorks RS-1 Validation Plan

Version: 0.1
Date: 2026-01-XX
Owner: QA / Product
Status: Draft

## 1. Purpose

Validate RS-1 against product success metrics for setup, accuracy, OTA reliability, and local-first operation.

## 2. Success Metrics (From Product Spec)

- Setup completion within 60 seconds for 80% of users.
- Zone accuracy: < 5% false occupancy rate in typical rooms.
- OTA success rate >= 99%.
- < 1% of users require manual firmware updates.

## 3. Test Matrix (Metric -> Validation)

### 3.1 Setup Time

- Test: Time-to-complete for onboarding flow.
- Method: Lightweight pilot with first-time users.
- Sample: small cohort (TBD), mixed technical background.
- Pass Criteria: >= 80% complete in <= 60 seconds.

### 3.2 Zone Accuracy

- Test: False occupancy rate across common rooms.
- Method: Empirical testing using recorded datasets and tuning analysis.
- Environments: living room, bedroom, kitchen, hallway.
- Pass Criteria: < 5% false occupancy across environments.

### 3.3 OTA Reliability

- Test: OTA update success across network conditions.
- Method: Staged OTA attempts across stable, degraded, and loss-prone networks.
- Pass Criteria: >= 99% success, no bricked devices.

### 3.4 Manual Update Rate

- Test: Manual update fallback usage in beta program.
- Method: Telemetry from beta cohort + support tickets.
- Pass Criteria: < 1% of users require manual update.

## 4. Core Test Suites

### 4.1 Functional

- HA discovery and entity visibility.
- Zone creation, edit, delete flows.
- Sensitivity adjustments reflect in occupancy output.
- Local-only operation without internet.

### 4.2 Reliability

- 72-hour continuous operation without crashes.
- Power cycling and recovery behavior.
- Radar target loss and re-acquisition behavior.

### 4.3 Security

- Signed firmware enforcement.
- OTA transport security (TLS).
- Pairing authentication.

### 4.4 UX

- Onboarding usability.
- Zone editor usability and error recovery.
- AR scan validation (post-beta).

## 5. Test Environments

- HA versions: current stable + previous minor.
- Router types: consumer and prosumer-grade.
- Mobile OS: iOS latest, Android latest.
- Room sizes: small (bedroom), medium (office), large (living room).

## 6. Instrumentation and Data

- Device telemetry (opt-in) for setup time and OTA outcomes.
- App analytics for flow drop-off.
- Support tickets for update failures and accuracy complaints.

## 7. Exit Criteria for Beta Launch

- All critical functional tests pass.
- OTA success >= 99% in internal testing.
- Setup completion rate >= 80% in pilot cohort.
- No P0/P1 defects open.

## 8. Open Questions

- Time threshold for labeling "false occupancy" in recorded datasets.
- Required minimum room count for accuracy validation.
- Beta cohort size and duration.
