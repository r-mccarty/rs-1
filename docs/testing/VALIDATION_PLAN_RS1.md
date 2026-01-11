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

### 3.2.1 Tracking Accuracy (Ground Truth)

- Test: Track-level accuracy against ground truth trajectories.
- Method: Record synchronized LD2450 + depth sensor data, track targets in depth data, and compare to RS-1 tracks.
- Environments: wall-mounted setup (initial), ceiling-mounted setup (future).
- Pass Criteria: RS-1 track error within 5-10% of ground truth error baseline.

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

## 6.1 Ground Truth Dataset Pipeline

- Collect synchronized LD2450 frames and depth sensor frames.
- Calibrate both sensors into a shared floor-plane coordinate system.
- Generate ground truth tracks from depth data using a reference tracker.
- Store datasets with placement metadata (wall-mounted vs ceiling-mounted).

## 6.2 Optimization Loop (Tracking)

- Parameters to tune: gating distance, hold time, smoothing constants, confidence decay, z-score thresholds.
- Objective: minimize weighted tracking loss (position error + ID switches + misses).
- Search strategy: initial grid/random search, then Bayesian optimization.
- Output: tuned parameter sets for wall-mounted and ceiling-mounted placements.

## 6.3 Calibration Procedure (Depth -> Radar Frame)

- Establish a shared floor-plane coordinate system with known scale.
- Place 3+ calibration targets at known positions in the room.
- Capture depth sensor point cloud and radar detections simultaneously.
- Fit a rigid transform (rotation + translation) from depth coordinates to radar coordinates.
- Validate by checking residual error at calibration points (target < 10 cm).
- Save transform per placement mode (wall vs ceiling) and reuse for dataset alignment.

## 6.4 Tracking Loss Function

Define a per-frame loss across all tracks:

```
L = w_p * mean_position_error
  + w_m * missed_detection_rate
  + w_s * id_switch_rate
  + w_l * latency_penalty
```

Recommended defaults:

- w_p = 0.5 (position error is primary)
- w_m = 0.2 (missed detections)
- w_s = 0.2 (ID switches)
- w_l = 0.1 (latency)

Compute final score as the average L across the dataset. Lower is better.

## 7. Exit Criteria for Beta Launch

- All critical functional tests pass.
- OTA success >= 99% in internal testing.
- Setup completion rate >= 80% in pilot cohort.
- No P0/P1 defects open.

## 8. Open Questions

- Time threshold for labeling "false occupancy" in recorded datasets.
- Required minimum room count for accuracy validation.
- Beta cohort size and duration.
- Depth sensor selection and ground truth tracker (Luxonis or equivalent).
