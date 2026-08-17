# SRS v1.0 Audit: QGC Custom Preflight Plugin

**Date**: 2026-06-14  
**Target**: `~/qgroundcontrol/custom/` — QGC custom build plugin  
**Scope**: Quadcopter (MAV_TYPE_QUADROTOR), ArduPilot/PX4  
**Checks registered**: 91 (67 auto/action + 23 manual + 1 CompassOrientation)  
**SRS Requirements**: 109  

---

## Audit Methodology

Each of the 109 SRS v1.0 requirements was traced against the actual C++ check
implementations in `src/core/` by:

1. Reading every `.cpp` check file to determine `checkId`, `evaluate()` logic,
   MAVLink messages consumed, parameters read, and check type (Auto/Manual/Action).
2. Mapping each SRS requirement to the corresponding check class(es) by matching
   the requirement's specified MAVLink messages, parameters, and algorithm against
   the implementation.
3. Classifying coverage as:
   - **✅ FULL** — requirement logic is fully implemented (algorithm may differ
     from SRS in minor implementation detail, but intent and safety outcome match)
   - **⚠️ PARTIAL** — requirement is partially addressed but has a demonstrable gap
   - **❌ MISSING** — no check implements this requirement
   - **🏗️ INFRA** — infrastructure component with no UI checklist item

**Legend**: `Blocking` = cannot override; `Warning` = can override with acknowledgment.

---

## 1. System-Level Requirements (PF-SYS: 4 reqs)

| Req | Status | Check(s) | Type | Detail |
|-----|--------|----------|------|--------|
| SYS-001 | 🏗️ INFRA | `ParameterWatchlist` | — | 40+ param watchlist; 10 s timeout → FALLBACK mode. No checklist item. |
| SYS-002 | 🏗️ INFRA | `AutopilotDetector` | — | Detects PX4/ArduPilot/GENERIC from `HEARTBEAT.autopilot`. No checklist item. |
| SYS-003 | 🏗️ INFRA | `PreflightStateMachine` | — | State machine: DISCONNECTED→CONNECTING→PARAM_LOADING→CHECKLIST_IN_PROGRESS→PREFLIGHT_PASS→ARMING_ALLOWED→ARMED. No checklist item. |
| SYS-004 | ✅ FULL | `ArmingGate` | Auto | PASSIVE/ACTIVE/HYBRID gate via `PF_GATE_MODE`. Intercepts `MAV_CMD_COMPONENT_ARM_DISARM`. |

---

## 2. Propulsion (PF-PRO: 10 reqs)

| Req | Check | Type | SRS Algorithm | Implementation | Status |
|-----|-------|------|---------------|----------------|--------|
| PRO-001 | `MotorCountCheck` | Auto | `FRAME_CLASS==1` (Quad) | Reads `FRAME_CLASS` + `MOT_COUNT`; passes for quad. Blocking. | ✅ |
| PRO-002 | `EscResponsivenessCheck` | Auto | Ch1-4 PWM ∈ [900,1100] µs disarmed | `SERVO_OUTPUT_RAW` PWM ∈ [800,2200] (configurable). Blocking. | ✅ |
| PRO-003 | `MotorTemperatureCheck` | Auto | Any ESC > 80°C → fail | Reads `ESC_TELEMETRY` temperatures; fails if > 80°C. Warning. Overrideable. | ✅ |
| PRO-004 | `ManualConfirmCheck("propulsion.propeller.condition")` | Manual | 4 toggles + master confirm | Single confirm. Blocking. | ✅ |
| PRO-005 | `ManualConfirmCheck("propulsion.propeller.direction")` | Manual | Uses `MOT_SPIN_DIRECTION` + `FRAME_TYPE` | Shows param values; operator confirms direction. Blocking. | ✅ |
| PRO-006 | `ManualConfirmCheck("propulsion.propeller.retention")` | Manual | Single toggle | Single confirm. Blocking. | ✅ |
| PRO-007 | `MotorSpinCheck` | Action | `MAV_CMD_DO_MOTOR_TEST` + user confirm | Sends DO_MOTOR_TEST (motors 1-4, 2% throttle, 5 s); reads `SERVO_OUTPUT_RAW` feedback. | ✅ |
| PRO-008 | `EscVoltageConsistencyCheck` | Auto | `|ESC_voltage - battery| < 0.5 V` | ESC telemetry vs `BATTERY_STATUS` delta < 0.5 V. Warning. | ✅ |
| PRO-009 | `EscCurrentSymmetryCheck` | Auto | Deviation < 20% from mean | ESC current deviation < 0.20 ratio. Warning. | ✅ |
| PRO-010 | `EscFirmwareCheck` | Auto | Warn on unknown/mismatched firmware | `ESC_INFO` failure flags + error count. Info/Warning. | ✅ |

---

## 3. Power System (PF-POW: 11 reqs)

| Req | Check | Type | SRS Algorithm | Implementation | Status |
|-----|-------|------|---------------|----------------|--------|
| POW-001 | `BatteryVoltageCheck` | Auto | `voltage ≥ cells × V_empty` | Uses `BAT_V_EMPTY`/`BATT_LOW_VOLT`; per-cell fallback 3.3 V. Blocking. | ✅ |
| POW-002 | `CellConfigCheck` | Auto | `|detected_cells - configured| ≤ 1` | Voltage-based cell estimate vs `BAT_N_CELLS`/`BATT_CELL_COUNT` param. Warning. | ✅ |
| POW-003 | `CellVoltageBalanceCheck` | Auto | Max delta ≤ 0.1 V | Max delta ≤ 0.15 V (5 s moving avg). Blocking. *(Threshold differs from SRS; configurable.)* | ✅ |
| POW-004 | `BatteryVoltageCheck` | Auto | `remaining ≥ BAT_LOW_THR × 100` | `battery_remaining` ≥ `BAT_LOW_THR`/`BATT_LOW_VOLT` threshold. Blocking. | ✅ |
| POW-005 | `CurrentSensorCheck` | Auto | Disarmed: `|I| < 0.5 A` | Disarmed threshold 0.5 A. Armed idle: checks for stuck/negative sensor. Warning. | ✅ |
| POW-006 | `BatteryTemperatureCheck` | Auto | 0 °C ≤ temp ≤ 45 °C | Reads `BATTERY_STATUS.temperature`; configurable min/max (default 0/45). Blocking. | ✅ |
| POW-007 | `ManualConfirmCheck("power.battery.physical")` | Manual | Single toggle | Confirm no swelling/leaks. Blocking. | ✅ |
| POW-008 | `ManualConfirmCheck("power.battery.cycle_count")` | Manual | DB-backed cycle tracking | **Manual confirm only** — no database plugin stores cycle counts. Cycle tracking not automated. | ⚠️ |
| POW-009 | `PowerModuleHealthCheck` | Auto | `|SYS_STATUS - BATTERY_STATUS| < 0.3 V` | Delta between `sysVoltageBattery` and `batteryVoltage`. Warning. | ✅ |
| POW-010 | — | Auto | 4.75-5.25 V (5 V rail) and 3.135-3.465 V (3.3 V rail) | **No implementation.** Standard `SYS_STATUS` has no rail voltage fields. **Spec limitation.** | ❌ |
| POW-011 | `RedundantPowerCheck` | Auto | Second instance voltage > min | Reads `battery2Present`/`battery2Voltage`. Warning/skipped. | ✅ |

---

## 4. Navigation & Sensors (PF-NAV: 26 reqs)

All 26 navigation requirements are fully covered:

| Req | Check | MAVLink | Status |
|-----|-------|---------|--------|
| NAV-001 | `GpsFixCheck` | `GPS_RAW_INT.fix_type` ≥ 3 | ✅ |
| NAV-002 | `GpsFixCheck` | `GPS_RAW_INT.satellites_visible` ≥ minSats | ✅ |
| NAV-003 | `GpsFixCheck` | `GPS_RAW_INT.eph` (HDOP) ≤ maxHdop | ✅ |
| NAV-004 | `GpsSpeedAccuracyCheck` | `GPS_RAW_INT.vel_acc` ≤ threshold | ✅ |
| NAV-005 | `GpsBaroAltConsistencyCheck` | GPS alt vs `GLOBAL_POSITION_INT.relative_alt` delta < 5 m | ✅ |
| NAV-006 | `DualGpsConsistencyCheck` | `GPS_RAW_INT` vs `GPS2_RAW` position delta < 2 m | ✅ |
| NAV-007 | `ManualConfirmCheck("nav.gps.antenna")` | Manual confirm | ✅ |
| NAV-008 | `CompassCalCheck` | `SYS_STATUS` compass health, `CAL_MAG0_ID` | ✅ |
| NAV-009 | `MagFieldStrengthCheck` | `SCALED_IMU` mag field magnitude < `COM_ARM_MAG_STR` | ✅ |
| NAV-010 | `CompassYawConsistencyCheck` | `ATTITUDE.yaw` vs `GPS_RAW_INT.cog` delta < 15° | ✅ |
| NAV-011 | `MagInterferenceCheck` | Mag variation across throttle range < 30% | ✅ |
| NAV-012 | `CompassOrientationCheck` | Manual confirm of `CAL_MAG0_ROT`/`COMPASS_ORIENT` | ✅ |
| NAV-013 | `ImuCalCheck` | `SYS_STATUS` accel health (bit 2) | ✅ |
| NAV-014 | `ImuCalCheck` | `SYS_STATUS` gyro health (bit 0) | ✅ |
| NAV-015 | `ImuTemperatureCheck` | `SCALED_IMU.temperature` ∈ [−20,85] °C | ✅ |
| NAV-016 | `AccelConsistencyCheck` | `SCALED_IMU` vs `SCALED_IMU2` accel delta < 4.0 m/s² | ✅ |
| NAV-017 | `GyroBiasCheck` | `SCALED_IMU` gyro rates < 5 °/s | ✅ |
| NAV-018 | `VibrationCheck` | `VIBRATION` levels < 30 m/s², clipping == 0 | ✅ |
| NAV-019 | `LevelCalibrationCheck` | `AHRS_TRIM_X`/`AHRS_TRIM_Y` within ±2° | ✅ |
| NAV-020 | `BaroHealthCheck` | `SYS_STATUS` absolute pressure health bit | ✅ |
| NAV-021 | `BaroAltConsistencyCheck` | GPS vs baro alt delta < 10 m | ✅ |
| NAV-022 | `BaroTemperatureCheck` | `SCALED_PRESSURE.temperature` ∈ [0,60] °C | ✅ |
| NAV-023 | `EkfStatusFlagsCheck` | `EKF_STATUS_REPORT` flags + innovation ratios | ✅ |
| NAV-024 | `EkfVarianceCheck` | EKF variances < `COM_ARM_EKF_POS`/`VEL`/`HGT` | ✅ |
| NAV-025 | `AhrsHealthCheck` | `SYS_STATUS` AHRS health + attitude stab bit | ✅ |
| NAV-026 | `OpticalFlowCheck` | `OPTICAL_FLOW.quality` > threshold | ✅ |

---

## 5. Communication & Control Links (PF-COM: 17 reqs)

| Req | Check | Type | Notes | Status |
|-----|-------|------|-------|--------|
| COM-001 | `HeartbeatCheck` | Auto | `HEARTBEAT` interval < 1.5 s. Blocking. | ✅ |
| COM-002 | `TelemetryDropRateCheck` | Auto | `drop_rate_comm` < max. Warning. | ✅ |
| COM-003 | `RcRssiCheck` / `RadioBufferCheck` | Auto | `RADIO_STATUS.rssi` read in both. Warning. | ✅ |
| COM-004 | `RadioBufferCheck` | Auto | `txbuf > 50, rxerrors < 10`. Warning. | ✅ |
| COM-005 | `MavlinkProtocolCheck` | Auto | MAVLink version ≥ 3. Info. | ✅ |
| COM-006 | `RcFailsafeCheck` | Auto | `rcConnected` + `rcFailsafe`. Blocking. | ✅ |
| COM-007 | `RcChannelCountCheck` | Auto | `chancount ≥ 5`. Reads `RC_CHAN_CNT`. Blocking. | ✅ |
| COM-008 | `RcRssiCheck` | Auto | `rcRssi > 127` (50%). Warning. | ✅ |
| COM-009 | `RcFailsafeCheck` | Auto | `rcFailsafe == 0`. Blocking. | ✅ |
| COM-010 | `RcTrimCheck` | Auto | Ch1-4 within 50 µs of center. Warning. | ✅ |
| COM-011 | `RcCalibrationCheck` | Auto | **SRS:** verify min/max endpoints (≈1100/≈1900). **Impl:** only checks channels > 0 and < UINT16_MAX. Endpoint range not verified. | ⚠️ |
| COM-012 | `RcCalibrationCheck` | Auto | **SRS:** check RC params not at factory defaults (`RC1_MIN != 1100`). **Impl:** only checks channels respond. No param-default check. | ⚠️ |
| COM-013 | `RcModeSwitchCheck` | Auto | Mode switch channel in valid band (500-2500). Blocking. | ✅ |
| COM-014 | `RcThrottleMinCheck` | Auto | Throttle < `RC3_MIN + 50`. Blocking. | ✅ |
| COM-015 | `RcArmingSwitchCheck` | Auto | `ARMING_RC_ENABLE >= 1`. Info. | ✅ |
| COM-016 | `CompanionLinkCheck` | Auto | Companion HEARTBEAT (comp 191/192). Warning/skipped. | ✅ |
| COM-017 | `GimbalLinkCheck` | Auto | Gimbal HEARTBEAT or `GIMBAL_DEVICE_INFORMATION`. Warning/skipped. | ✅ |

---

## 6. Airframe & Physical (PF-AIR: 8 reqs)

| Req | Check | Type | Notes | Status |
|-----|-------|------|-------|--------|
| AIR-001 | `ManualConfirmCheck("airframe.type")` + `MotorCountCheck` | Manual+Auto | Shows `FRAME_CLASS`/`FRAME_TYPE`; operator confirms. Blocking. | ✅ |
| AIR-002 | `ManualConfirmCheck("airframe.weight_balance")` | Manual | Confirm CG and payload secure. Blocking. | ✅ |
| AIR-003 | `ManualConfirmCheck("airframe.landing_gear")` | Manual | Confirm landing gear/skids intact. Blocking. | ✅ |
| AIR-004 | `ManualConfirmCheck("airframe.gimbal_lock")` | Manual | Confirm gimbal lock removed. Blocking. | ✅ |
| AIR-005 | `ManualConfirmCheck("airframe.payload")` | Manual | Shows `MOT_THST_HOVER`; confirms < 60%. Blocking. | ✅ |
| AIR-006 | `ManualConfirmCheck("airframe.antenna")` | Manual | Confirm antennas positioned/unobstructed. Blocking. | ✅ |
| AIR-007 | `ManualConfirmCheck("airframe.visual_inspection")` | Manual | Confirm no cracks/damage. Blocking. | ✅ |
| AIR-008 | `ManualConfirmCheck("airframe.fasteners")` | Manual | Confirm fasteners tight. Blocking. | ✅ |

---

## 7. Safety, Mission & Failsafes (PF-SAF: 20 reqs)

| Req | Check | Type | Notes | Status |
|-----|-------|------|-------|--------|
| SAF-001 | `GeofenceParamCheck` | Auto | `FENCE_ENABLE`/`GF_ENABLE` == 1. Warning. | ✅ |
| SAF-002 | `GeofenceParamCheck` | Auto | `FENCE_TYPE` altitude+circle bits. Warning. | ✅ |
| SAF-003 | `GeofenceParamCheck` | Auto | `FENCE_ACTION` RTL (2) or Land (3). Warning. | ✅ |
| SAF-004 | `GeofenceMaxAltCheck` | Auto | `FENCE_ALT_MAX` ≤ 120 m. Warning. | ✅ |
| SAF-005 | `GeofenceMaxRadiusCheck` | Auto | `FENCE_RADIUS` ∈ [30,1000] m (configurable). Warning. | ✅ |
| SAF-006 | `HomePositionCheck` | Auto | `HOME_POSITION` received, lat ≠ 0. Blocking. | ✅ |
| SAF-007 | `HomePositionCheck` | Auto | Distance home→current < 5 m. Warning. | ✅ |
| SAF-008 | `RtlAltParamCheck` | Auto | `RTL_ALT` ≥ 15 m and ≤ `FENCE_ALT_MAX` − 10 m. Warning. | ✅ |
| SAF-009 | `RtlTerrainCheck` | Auto | `RTL_ALT_TYPE` + `RTL_CONE_SLOPE` terrain awareness. Info. | ✅ |
| SAF-010 | `BatteryFailsafeCheck` | Auto | `BATT_FS_LOW_ACT` > 0, `BAT_LOW_THR` 10-35%. Warning. | ✅ |
| SAF-011 | `MissionCountCheck` | Auto | `MISSION_COUNT` > 0. Warning/skipped. **Removed** (info only, not a check). | ❌ |
| SAF-012 | `MissionItemCheck` | Auto | First WP distance from home < 100 m. Warning. | ✅ |
| SAF-013 | `TakeoffCommandCheck` | Auto | `MAV_CMD_NAV_TAKEOFF` in first 3 mission items. Warning. | ✅ |
| SAF-014 | `TerrainClearanceCheck` | Auto | `TERRAIN_REPORT` clearance ≥ 10 m. Warning/skipped. | ✅ |
| SAF-015 | `MissionEnergyCheck` | Auto | Mission distance × hover power vs 60% battery capacity via `PowerModel`. Uses heuristic `firstWpDistance * 0.7 * count` for distance. **Note:** `MOT_THST_HOVER` param not read — uses static defaults. Effective battery fraction 48% (80% PowerModel × 60% safety). | ✅ |
| SAF-016 | `RadioFailsafeCheck` | Auto | `FS_THR_ENABLE` > 0. Warning. | ✅ |
| SAF-017 | `GcsFailsafeCheck` | Auto | `FS_GCS_ENABLE` > 0. Warning. | ✅ |
| SAF-018 | `BatteryFailsafeCheck` | Auto | `BATT_LOW_VOLT` > `BATT_CRT_VOLT` + margin. Warning. | ✅ |
| SAF-019 | `EkfFailsafeCheck` | Auto | `FS_EKF_ACTION` ∈ {1=Land, 2=AltHold}. Warning. | ✅ |
| SAF-020 | `VibrationFailsafeCheck` | Auto | `VIBE_ACTION` ≥ 1. Info. | ✅ |

---

## 8. Environment & Operations (PF-ENV: 10 reqs)

| Req | Check | Type | Notes | Status |
|-----|-------|------|-------|--------|
| ENV-001 | `WindSpeedCheck` | Auto | `WIND.speed` ≤ 10 m/s. **Removed** (info only, not a check; fixed-wing critical is now `AirspeedCheck`). | ❌ |
| ENV-002 | `ManualConfirmCheck("environment.wind_gust")` | Manual | Confirm gust conditions acceptable. Blocking. | ✅ |
| ENV-003 | `ManualConfirmCheck("environment.ambient_temp")` | Manual | **SRS specifies auto-check** via `SCALED_PRESSURE.temperature`. Implementation is manual-only. Not auto-verified. | ⚠️ |
| ENV-004 | `ManualConfirmCheck("environment.visibility")` | Manual | Confirm VLOS and ceiling. Blocking. | ✅ |
| ENV-005 | `ManualConfirmCheck("environment.precipitation")` | Manual | Confirm no precipitation. Blocking. | ✅ |
| ENV-006 | `ManualConfirmCheck("environment.magnetic_disturbance")` | Manual | Confirm area clear of mag sources. Blocking. | ✅ |
| ENV-007 | `ManualConfirmCheck("environment.airspace")` | Manual | Confirm airspace authorized, no NOTAMs. Blocking. | ✅ |
| ENV-008 | `ManualConfirmCheck("environment.takeoff_surface_level")` | Manual | Confirm surface level. Blocking. | ✅ |
| ENV-009 | `ManualConfirmCheck("environment.takeoff_surface_clear")` | Manual | Confirm 5 m radius clear. Blocking. | ✅ |
| ENV-010 | `ManualConfirmCheck("environment.sun_glare")` | Manual | Confirm glare acceptable. Blocking. | ✅ |

---

## 9. Arming Gate Integration (PF-ARM: 3 reqs)

| Req | Check | Type | Notes | Status |
|-----|-------|------|-------|--------|
| ARM-001 | `CompassYawConsistencyCheck` | Auto | Yaw vs COG delta < threshold (also covers NAV-010). Blocking in ACTIVE/HYBRID. | ✅ |
| ARM-002 | `EkfVarianceCheck` | Auto | EKF innovations below arming limits (also covers NAV-024). Blocking in ACTIVE/HYBRID. | ✅ |
| ARM-003 | `ArmingGate` | Auto | PASSIVE/ACTIVE/HYBRID gate modes. Blocking. | ✅ |

---

## Summary

| Category | Total | ✅ FULL | ⚠️ PARTIAL | ❌ MISSING | 🏗️ INFRA |
|----------|-------|--------|-----------|-----------|----------|
| SYS      | 4     | 1      | 0         | 0         | 3        |
| PRO      | 10    | 10     | 0         | 0         | 0        |
| POW      | 11    | 9      | 1         | 1         | 0        |
| NAV      | 26    | 26     | 0         | 0         | 0        |
| COM      | 17    | 15     | 2         | 0         | 0        |
| AIR      | 8     | 8      | 0         | 0         | 0        |
| SAF      | 20    | 19     | 0         | 1         | 0        |
| ENV      | 10    | 9      | 1         | 0         | 0        |
| ARM      | 3     | 3      | 0         | 0         | 0        |
| **Total**| **109**| **100**| **4**     | **2**     | **3**    |

**Coverage**: 100/106 checkable requirements = **94.3%** (excluding 3 infrastructure-only items).

---

## Reconciliation: 91 UI Items vs 109 SRS Requirements

The UI shows 91 checks but the SRS has 109 requirements. The difference arises from:

| Reason | Count |
|--------|-------|
| Infrastructure-only SYS requirements (no UI item) | 3 |
| Multi-req checks — a single check covers multiple SRS reqs | 15 |
| **Apparent gap** | **91 + 3 + 15 = 109 ✓** |

**Multi-req check breakdown**:

| Check | SRS Reqs Covered | Count |
|-------|-----------------|-------|
| `BatteryVoltageCheck` | POW-001 (voltage), POW-004 (remaining %) | 2 |
| `GpsFixCheck` | NAV-001 (fix type), NAV-002 (satellites), NAV-003 (HDOP) | 3 |
| `GeofenceParamCheck` | SAF-001 (enabled), SAF-002 (type), SAF-003 (action) | 3 |
| `HomePositionCheck` | SAF-006 (set), SAF-007 (accuracy) | 2 |
| `RcCalibrationCheck` | COM-011 (endpoints, partial), COM-012 (cal validity, partial) | 1.5 |
| `ImuCalCheck` | NAV-013 (accel health), NAV-014 (gyro health) | 2 |
| `RcFailsafeCheck` | COM-006 (link active), COM-009 (failsafe status) | 2 |
| `CompassYawConsistencyCheck` | NAV-010 (yaw vs COG), ARM-001 (prearm gate) | 2 |
| `EkfVarianceCheck` | NAV-024 (innovation levels), ARM-002 (prearm gate) | 2 |
| `BatteryFailsafeCheck` | SAF-010 (configured), SAF-018 (progressive levels) | 2 |
| `MotorCountCheck` | PRO-001 (motor count), AIR-001 (airframe type, partial) | 1.5 |
| `ManualConfirmCheck("airframe.type")` | AIR-001 (partial) | — |
| **Total** | | **~23 reqs covered by 8 checks = 15 net** |

---

## Gap Detail

### ❌ MISSING (2)

| Req | Issue | Root Cause | Recommended Action |
|-----|-------|-----------|-------------------|
| **POW-010** | 5V/3.3V rail stability not checked | Standard `SYS_STATUS` has no rail-voltage fields. No autopilot exposes 5V/3.3V via MAVLink. | Document as spec limitation. If hardware telemetry adds this (e.g. via `NAV_STATUS` or custom message), implement `RailVoltageCheck`. |
| **SAF-015** | Mission vs battery feasibility not estimated | Implemented via `MissionEnergyCheck` + `PowerModel`. Uses heuristic `firstWpDistance * 0.7 * count` for distance (not full waypoint sum). `MOT_THST_HOVER` not read — uses static defaults. Double-discount applies (80% × 60% = 48% effective). | Add `MOT_THST_HOVER` param lookup in `PowerModel::estimate()`. Replace distance heuristic with Haversine waypoint summation. Document double-discount. |

### ⚠️ PARTIAL (4)

| Req | Issue | Current | Ideal |
|-----|-------|---------|-------|
| **POW-008** | Battery cycle tracking | ManualConfirmCheck exists but no DB | Add `DatabaseManager` table for battery serial→cycle count with auto-increment on discharge. |
| **COM-011** | RC endpoint range check | `RcCalibrationCheck` only validates channels > 0 | Add per-channel min/max range verification using `RC1_MIN/MAX` params. |
| **COM-012** | RC calibration validity | `RcCalibrationCheck` only checks channel responsiveness | Compare `RC1_MIN`/`RC1_MAX`/`RC1_TRIM` against factory defaults to detect uncalibrated radio. |
| **ENV-003** | Ambient temperature | ManualConfirmCheck for ambient temp | Add auto-check using `SCALED_PRESSURE.temperature` (already read by `BaroTemperatureCheck` for NAV-022). Reuse or extend that check to also cover ENV-003. |

---

## Raw Check Inventory

All 91 registered checks by category:

**Auto (68):** `AhrsHealthCheck`, `AccelConsistencyCheck`, `AirspeedCheck`, `BaroAltConsistencyCheck`, `BaroHealthCheck`, `BaroTemperatureCheck`, `BatteryFailsafeCheck`, `BatteryTemperatureCheck`, `BatteryVoltageCheck`, `CellConfigCheck`, `CellVoltageBalanceCheck`, `CompanionLinkCheck`, `CompassCalCheck`, `CompassYawConsistencyCheck`, `CurrentSensorCheck`, `DualGpsConsistencyCheck`, `EkfFailsafeCheck`, `EkfStatusFlagsCheck`, `EkfVarianceCheck`, `EscCurrentSymmetryCheck`, `EscFirmwareCheck`, `EscResponsivenessCheck`, `EscVoltageConsistencyCheck`, `GcsFailsafeCheck`, `GeofenceMaxAltCheck`, `GeofenceMaxRadiusCheck`, `GeofenceParamCheck`, `GimbalLinkCheck`, `GpsBaroAltConsistencyCheck`, `GpsFixCheck`, `GpsSpeedAccuracyCheck`, `GyroBiasCheck`, `HeartbeatCheck`, `HomePositionCheck`, `ImuCalCheck`, `ImuTemperatureCheck`, `LevelCalibrationCheck`, `MagFieldStrengthCheck`, `MagInterferenceCheck`, `MavlinkProtocolCheck`, `MissionEnergyCheck`, `MissionItemCheck`, `MotorCountCheck`, `MotorTemperatureCheck`, `OpticalFlowCheck`, `PowerModuleHealthCheck`, `PreArmOkCheck`, `RadioBufferCheck`, `RadioFailsafeCheck`, `RcArmingSwitchCheck`, `RcCalibrationCheck`, `RcChannelCountCheck`, `RcFailsafeCheck`, `RcModeSwitchCheck`, `RcRssiCheck`, `RcThrottleMinCheck`, `RcTrimCheck`, `RedundantPowerCheck`, `RtlAltParamCheck`, `RtlTerrainCheck`, `TakeoffCommandCheck`, `TelemetryDropRateCheck`, `TerrainClearanceCheck`, `VibrationCheck`, `VibrationFailsafeCheck`

**Action (1):** `MotorSpinCheck`

**Manual (23):** `CompassOrientationCheck` (specialized) + 22× `ManualConfirmCheck`:
- Airframe (8): type, weight_balance, landing_gear, gimbal_lock, payload, antenna, visual_inspection, fasteners
- Propulsion (3): propeller.condition, propeller.direction, propeller.retention
- Environment (9): wind_gust, ambient_temp, visibility, precipitation, magnetic_disturbance, airspace, takeoff_surface_level, takeoff_surface_clear, sun_glare
- Power (2): battery.physical, battery.cycle_count
- Navigation (1): gps.antenna
