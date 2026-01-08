# HardwareOS Tracking Module Specification

Version: 0.1
Date: 2026-01-XX
Owner: OpticWorks Firmware
Status: Draft

## 1. Purpose

Provide robust multi-target tracking under occlusion and multipath using a light-weight association, prediction, and smoothing pipeline. Output stable tracks to the Zone Engine.

## 2. Inputs and Outputs

### Inputs

- Detection list per frame:
  - Position: (x, y)
  - Signal quality (optional)
  - Timestamp (ms)
- Frame rate: assumed 10 Hz (pending confirmation).

### Outputs

- Track list:
  - Track ID
  - Position (x, y)
  - Velocity (vx, vy)
  - Confidence
  - Age, last_seen

## 3. Tracking Pipeline

### 3.1 Predict

- Use constant-velocity model per track:
  - x' = x + vx * dt
  - y' = y + vy * dt
- If velocity is not stable, use alpha-beta filter.

### 3.2 Gate

- Compute distance between each predicted track and detection.
- Reject pairs beyond a configurable max-distance gate.
- Gate should scale with dt and expected target speed.

### 3.3 Associate

- Use a cost matrix of gated distances.
- Assignment options:
  - Greedy nearest-neighbor (preferred for <= 3 targets).
  - Hungarian assignment (optional, if CPU budget allows).
- Unassigned detections spawn new tracks.
- Unassigned tracks are marked "missing."

### 3.4 Update

- Update track state with detection (Kalman or alpha-beta).
- Increase confidence for matched tracks.
- Decrease confidence for missing tracks.

### 3.5 Track Lifecycle

- Confirm tracks after N consecutive matches.
- Retire tracks after M consecutive misses or timeout.
- Maintain a short "hold" period to bridge occlusions.

## 4. Recommended Defaults (Initial)

- Max targets: 3 (LD2450 limit).
- Frame rate: 10 Hz (pending confirmation).
- Gate distance: 0.6 m (tune from datasets).
- Confirm threshold: 2 consecutive matches.
- Drop threshold: 5 consecutive misses.
- Hold time: 0.5-1.0 s.

## 5. Occlusion and Multipath Strategy

- Prediction bridges short dropouts.
- Confidence-based hold reduces false vacancy.
- Zone engine uses track presence, not raw detections.

## 6. Telemetry (Optional)

- Track count per frame.
- Association failures and ID switches.
- Occlusion duration distribution.

## 7. Tuning Plan

- Use recorded datasets to tune gate distance and hold time.
- Compare greedy vs Hungarian for ID stability and CPU cost.
- Evaluate false occupancy vs false vacancy tradeoff.

## 8. Open Questions

- Confirm actual LD2450 frame rate and jitter.
- Choose association method for MVP (greedy vs Hungarian).
- Define acceptable CPU budget for tracking at 10 Hz.

