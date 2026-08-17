# Static Analysis Report — `custom/`

> Generated from a full read of every source file in `custom/`.
> 35,377 total lines across C++, QML, headers, tests, and resources.

---

## CRITICAL (runtime failures)

### `Config.fontSizeH4` — undefined property reference
- **File:** `qml/pages/MaintenancePage.qml:137`
- **What:** `font.pixelSize: Config.fontSizeH4` — Config.qml only defines `fontSizeH1`, `fontSizeH2`, `fontSizeH3`. This evaluates to `undefined`.
- **Why it matters:** The Text element gets no font size; falls back to platform default, breaking layout.
- **Fix:** Change to `Config.fontSizeH3` or add `fontSizeH4` to Config.qml.

### 23 missing Q_PROPERTY declarations on TelemetryBridge
- **Files:** `qml/singletons/VehicleTelemetry.qml:68-120`, `qml/pages/PreFlightChecklist.qml:399,500-503,1003`
- **What:** VehicleTelemetry.qml and PreFlightChecklist.qml access properties on `TelemetryProvider` (a `TelemetryBridge*`) that have no Q_PROPERTY declaration in `src/adapters/TelemetryBridge.h`:
  - `gpsFixTypeString`, `servoOutputsString`, `vehicleType`, `ekfStatus`, `ekfAirspeedVariance`
  - `flightTime`, `preArmSeverity`, `gimbalPitch`, `gimbalRoll`, `gimbalYaw`, `gimbalCalibrating`
  - `connectionStatus`, `connectionUrl`, `autopilotType`, `lastLogTimestamp`, `hardwareSetupRequired`
  - `batteryDataQuality`, `batteryStatusString`, `gpsDataQuality`, `gpsStatusString`
  - `imuDataQuality`, `compassDataQuality`, `rcDataQuality`
- **Why it matters:** QML property accesses on non-existent Q_PROPERTY return `undefined`, which coerces to `""` (string) or `0` (int). GPS fix labels, gimbal state, vehicle type, flight time, and 19 other telemetry values silently show empty/zero instead of actual data.
- **Fix:** Add Q_PROPERTY declarations + backing members + change-notification signals to TelemetryBridge.h for each missing property, or redirect VehicleTelemetry.qml to read from sources that actually expose them.

---

## SILENT FAILURES

### ArmingGate periodic timer never started
- **File:** `src/core/ArmingGate.cpp:29-31`
- **What:** `m_gateTimer` is created with `setInterval(EVAL_INTERVAL_MS)` and connected to `updateArmingState()`, but `m_gateTimer->start()` is never called anywhere.
- **Why it matters:** Periodic arming state re-evaluation never fires from this timer. Gate state updates rely solely on reactive signal connections; if any signal path is missed, arming decisions may be based on stale state.
- **Fix:** Call `m_gateTimer->start()` in the constructor or after `setPreflightManager()`, or remove the timer.

### forceArm uses hardcoded 10-second timeout
- **File:** `src/core/ArmingGate.cpp:240`
- **What:** `forceArm()` calls `overrideGate("Operator force arm", 10)`. Override expires after 10 seconds.
- **Why it matters:** Operator-initiated force-arm could expire mid-flight, re-blocking arming unexpectedly.
- **Fix:** Use a configurable timeout or make it non-expiring until explicitly reset.

### Blocking network wait with 3-second timeout
- **File:** `src/utils/WeatherProvider.cpp:565`
- **What:** `QTimer::singleShot(3000, &loop, &QEventLoop::quit)` inside a synchronous HTTP fetch.
- **Why it matters:** If the network is slow, the request silently aborts after 3 seconds with no error surfaced.
- **Fix:** Use async QNetworkAccessManager pattern instead of blocking event loop.

### RcRssiCheck epoch arithmetic always passes
- **File:** `src/core/RcRssiCheck.cpp:45`
- **What:** `qint64 elapsed = QDateTime::currentMSecsSinceEpoch() * 1000 - lastUsec` — multiplying epoch ms by 1000 produces an astronomically large number.
- **Why it matters:** This always exceeds any staleness threshold, making the check always think RC data is fresh regardless of actual age.
- **Fix:** Use `QDateTime::currentMSecsSinceEpoch()` directly (it's already in ms).

### Database queries on every evaluation tick
- **File:** `src/core/AbstractCheck.cpp:231,241`
- **What:** `DatabaseManager::instance().getCheckConfig(m_id, key)` is called inside `evaluate()` on every timer tick for every check.
- **Why it matters:** Repeated SQLite I/O on every tick (potentially 50+ checks × 1 Hz = 50+ DB queries/sec) blocks the main thread.
- **Fix:** Cache config values on first read or use a config snapshot per evaluation cycle.

### Connection quality hardcoded to 100
- **File:** `src/adapters/TelemetryBridge.cpp:219`
- **What:** `_connectionQuality = 100` is set on vehicle connect and never updated based on actual link quality.
- **Why it matters:** Connection quality indicator always shows 100% regardless of actual link conditions.
- **Fix:** Compute from RSSI, packet loss, or heartbeat timing.

### Motor test result timer uses hardcoded margins
- **File:** `src/controllers/HardwareTestController.cpp:458`
- **What:** `_resultTimer.start((_durationSec + 1) * 1000 + 500)` — the `+1` second and `+500ms` are not derived from configuration.
- **Why it matters:** Timing margins are fragile and may be insufficient on slow links.
- **Fix:** Make margins configurable via Config.h constants.

---

## DEAD CODE

### Empty stub files (0 bytes each)
| File | Lines | Safe to delete? |r
|---|---|---|
| `src/detection/DetectionBridge.h` | 0 | Yes |
| `src/detection/DetectionBridge.cpp` | 0 | Yes |
| `src/detection/DetectionModel.h` | 0 | Yes |
| `src/detection/DetectionModel.cpp` | 0 | Yes |

### Stub files with only comments/includes
| File | Lines | Description | Safe to delete? |
|---|---|---|---|
| `src/utils/AutopilotDetector.h` | 4 | Header comment + `#pragma once`, no class | Yes |
| `src/utils/AutopilotDetector.cpp` | 11 | Includes only, no functions | Yes |
| `src/utils/VideoManager.h` | 6 | Header comment + `#pragma once`, no class | Yes |
| `src/utils/VideoManager.cpp` | 4 | Comments only | Yes |
| `src/QmlModuleInit.cpp` | 2 | Comment explaining registrations are in PreflightPlugin | Yes (intentional linker anchor) |

### Dead logic in ArmingGate
- **File:** `src/core/ArmingGate.cpp:99-100`
- **What:** `if (s_emergencyCommands.contains(command)) return true;`
- **Why dead:** This is reached only when `command == m_armCommandCode` (400). The `s_emergencyCommands` set contains `{21, 92, 2050, 3000, 4000}` — none of which is 400. The check can never be true.
- **Safe to delete:** Yes.

### Never-emitted signal
- **File:** `src/core/ArmingGate.h:100`
- **What:** `void forceArmIssued(const QString &reason);` — declared but never emitted. `forceArm()` calls `overrideGate()` instead.
- **Safe to delete:** Yes.

### TestPage.qml development artifact
- **File:** `res/TestPage.qml` (38 lines) — registered in QRC, appears to be a development test page.
- **Safe to delete:** If not needed for QA.

---

## HARDCODED VALUES

### Numeric literals not in Config.h

| File:line | Value | Represents | Should be |
|---|---|---|---|
| `src/core/ArmingGate.h:136` | `400` | MAV_CMD_COMPONENT_ARM_DISARM | `kMavCmdArmDisarm` in Config.h |
| `src/core/ArmingGate.h:137` | `500` | Gate eval interval (ms) | `kArmingGateEvalMs` in Config.h |
| `src/core/ArmingGate.cpp:281` | `10` | Telemetry staleness (sec) | Match `m_stalenessThresholdMs` or use named constant |
| `src/adapters/TelemetryBridge.cpp:29` | `200` | Telemetry poll interval (ms) | `kTelemetryUpdateMs` in Config.h |
| `src/core/UavParameterManager.cpp:39` | `10000` | Param load timeout (ms) | `kParamLoadTimeoutMs` in Config.h |
| `src/core/AbstractCheck.cpp:55` | `5000` | Default stale timeout (ms) | `kDefaultStaleTimeoutMs` in Config.h |
| `src/core/VehicleProfileManager.cpp:323` | `30000` | Armed timer threshold (ms) | `kArmedTimerThresholdMs` in Config.h |
| `src/core/MissionEnergyCheck.cpp:148` | `3.7` | Nominal cell voltage (V), used 4x | `kNominalCellVoltage` in Config.h |
| `src/core/MissionEnergyCheck.cpp:157` | `5000.0` | Default battery capacity (mAh) | `kDefaultBatteryCapacityMah` in Config.h |
| `src/core/DualGpsConsistencyCheck.cpp:27` | `6371000.0` | Earth radius (m) for Haversine | `kEarthRadiusM` in Config.h |
| `src/core/MagInterferenceCheck.cpp:38` | `1000.0` | Expected mag field strength | `kExpectedMagField` in Config.h |
| `src/core/PreflightManager.cpp:766` | `10000.0` | Default takeoff altitude (m) | `kDefaultTakeoffAltitude` in Config.h |
| `src/core/ArmingGate.cpp:240` | `10` | forceArm timeout (sec) | `kForceArmTimeoutSec` in Config.h |
| `src/controllers/HardwareTestController.cpp:458` | `1` and `500` | Timer margin (ms) | Named constants in Config.h |

### Hardcoded hex colors in QML (not referencing Colors.*)

| File:line(s) | Color(s) | Count | Should be |
|---|---|---|---|
| `qml/cpts/PreflightChecklistView.qml:26` | `#0a0a1a` | 1 | `Colors.background` |
| `qml/cpts/PreflightChecklistView.qml:66,193,194,195,740` | `#2D1B4E`, `#4A1942`, `#6B1D5E` | 5 | `Colors.dialogSurface` and gradient constants |
| `qml/cpts/PreflightChecklistView.qml:73,208,759,769,773,783,787,813` | `#F8BBD0` | 8 | `Colors.dialogHighlight` |
| `qml/cpts/PreflightChecklistView.qml:74,223,228` | `#CE93D8` | 3 | `Colors.checkAccent` |
| `qml/cpts/PreflightChecklistView.qml:112,124,170,241` | `#fff` / `#FFFFFF` | 4 | `Colors.dialogText` |
| `qml/cpts/PreflightChecklistView.qml:123,169,235` | `#9333ea` | 3 | `Colors.dialogAccent` |
| `qml/cpts/PreflightChecklistView.qml:808` | `#E91E63` | 1 | `Colors.dialogFocus` |
| `qml/cpts/PreflightChecklistView.qml:213` | `#EF9A9A`, `#A5D6A7` | 2 | `Colors.checkFailLight`, `Colors.checkPassLight` |
| `qml/cpts/VideoPreview.qml:14,43` | `#1a1a1a`, `#0f0f0f` | 2 | `Colors.background` |
| `qml/cpts/CustomButton.qml:15` | `#94a3b8` | 1 | `Colors.textDisabled` |
| `qml/pages/MaintenancePage.qml:154,378,399,435,456` | `#e6a817` | 5 | `Colors.warning` |
| `qml/pages/GimbalTest.qml:13-14` | `#e879f9`, `#d946ef` | 2 | `Colors.dialogAccent` |
| `qml/pages/GimbalTest.qml:166-167` | `#0D47A1`, `#3E2723` | 2 | Named constants |
| `qml/pages/GimbalTest.qml:184,239,240` | `#22334155` | 3 | `Colors.border` with alpha |
| **Total hardcoded hex colors** | | **~42** | |

### Duplicate Config constants
- `Config.h` and `Config.qml` both define `kPwmMin`, `kPwmMax`, `kPwmRangeMin`, `kPwmRangeMax`, `kMotorCountQuad`, `kMotorCountHexa`, `kMotorCountOcta`, `kMotorCountFixedWing`, `kMotorCountVtolQuad`, `kEkfVarianceMax`, `kMaintWarningPercent`, `kMaintCriticalPercent`, `kTelemetryPanelRatio`, `kChecklistPanelRatio`, `kMinRowHeight`, `kCategoryVerticalSpacing`, `kHealthGridColumns`, `kFooterHeight`.
- These must be kept in sync manually. Drift between C++ and QML constants will cause inconsistent behavior.

---

## ARCHITECTURAL ISSUES

### DatabaseManager is a 1,874-line god class
- **File:** `src/utils/DatabaseManager.cpp` (1,874 lines), `src/utils/DatabaseManager.h` (173 lines)
- **Responsibilities:** Templates, compliance logs, hardware test events, component maintenance, vehicle profiles, vehicle registry, battery CRUD, battery cycles, flight sessions, check config, check results, calibrated power models.
- **Risk level:** High — changes to any domain risk breaking others.
- **Fix:** Split into domain-specific repository classes (VehicleRepository, BatteryRepository, TemplateRepository, etc.) sharing a connection.

### TelemetryBridge has 100+ Q_PROPERTY declarations
- **Files:** `src/adapters/TelemetryBridge.h` (524 lines), `src/adapters/TelemetryBridge.cpp` (754 lines)
- **Responsibilities:** Battery (2 batteries), GPS (2 receivers), RC, vibration, EKF, ESC telemetry, accelerometers (2), magnetic field, wind, gimbal, mission, pre-arm, motor outputs, connection quality — all in one class.
- **Risk level:** Moderate — hard to test in isolation, difficult to extend.
- **Fix:** Split into domain-specific bridges or use composition.

### Duplicate code paths in ArmingGate
- **File:** `src/core/ArmingGate.cpp:112-123` vs `125-136` (identical Hybrid/Active blocks in `interceptCommandLong`)
- **File:** `src/core/ArmingGate.cpp:160-168` vs `171-179` (identical Active/Hybrid blocks in `processArmRequest`)
- **Risk level:** Low — maintenance burden, inconsistency risk.

### Singleton patterns are inconsistent
- DatabaseManager: Meyers' singleton (`static DatabaseManager instance`)
- AlertManager: Meyers' singleton
- PreflightSettingsManager: `qmlRegisterSingletonType<C++>`
- Colors/Config/VehicleTelemetry: `qmlRegisterSingletonType(QUrl)`
- VehicleRegistry: `instance()` pattern
- **Risk level:** Low — inconsistent lifecycle management.

### Thread ownership not documented
- DatabaseManager is called from main thread, timer callbacks, and potentially UI event handlers. No thread-affinity documentation.
- AlertManager singleton may be accessed from multiple threads.
- **Risk level:** Latent race condition if any check evaluation is moved to a worker thread.

---

## QML ISSUES

### VirtualJoystick.qml QRC path not registered in custom.qrc
- **File:** `qml/FlyViewWidgetLayer.qml:109`
- **What:** `source: "qrc:/qml/QGroundControl/FlightDisplay/VirtualJoystick.qml"` — this path does not appear in `custom/custom.qrc`. It relies on the upstream QGC QRC.
- **Why it matters:** If the custom QRC shadows the upstream path, this Loader will silently show nothing.
- **Fix:** Verify upstream QRC still provides this path, or add it to custom.qrc.

### Network calls in Component.onCompleted
- **File:** `qml/pages/FinalChecks.qml:279-282` — calls `WeatherProvider.fetchWeather(lat, lon)`.
- **File:** `qml/pages/PreFlightChecklist.qml:19-25` — same.
- **Why it matters:** Network calls during UI initialization block the main thread.
- **Fix:** Defer with a short timer (e.g., `Timer { interval: 100; running: true; onTriggered: ... }`).

### Dialog opened during construction
- **File:** `qml/pages/PreFlightChecklist.qml:329-332` — `Component.onCompleted: motorConfigDialog.open()`
- **Why it matters:** Can cause visual glitches if the parent isn't fully laid out yet.

### VehicleTelemetry.qml stale monitor uses Date objects
- **File:** `qml/singletons/VehicleTelemetry.qml:306-310`
- **What:** `telemetryStale` binding creates a new `Date()` on every evaluation.
- **Why it matters:** Minor — evaluated every 1s by `_staleTimer`. Not a performance problem but could use elapsed time tracking instead.

---

## I18N GAPS

User-facing strings NOT wrapped in `qsTr()`:

| File | Untranslated count |
|---|---|
| `qml/cpts/FlyViewTelemetryStrip.qml` | 18 |
| `qml/pages/PreFlightChecklist.qml` | 16 |
| `qml/cpts/ChecklistEngine.qml` | 14 |
| `qml/cpts/ServoTestCard.qml` | 8 |
| `qml/cpts/PreflightChecklistView.qml` | 7 |
| `qml/mission/DmsConverter.qml` | 4 |
| `qml/cpts/WeatherPanel.qml` | 3 |
| `qml/pages/MaintenancePage.qml` | 2 |
| `qml/pages/FinalChecks.qml` | 2 |
| `qml/cpts/TelemetryInfoBox.qml` | 1 |
| `qml/cpts/AlertPanel.qml` | 1 |
| `qml/cpts/MotorCheckPanel.qml` | 1 |
| `qml/cpts/VideoPreview.qml` | 1 |
| `qml/pages/LaunchReady.qml` | 1 |
| `qml/pages/VehicleSelect.qml` | 1 |
| **Total untranslated** | **80** |

---

## TEST COVERAGE GAPS

### Tested source files

| Source file | Test file |
|---|---|
| ArmingGate | ArmingGateTest.cpp |
| ChecklistEngine | ChecklistEngineTest.cpp |
| ChecklistItemModel | ChecklistItemModelTest.cpp |
| CompassOrientationCheck | CompassOrientationCheckTest.cpp |
| DatabaseManager | DatabaseManagerTest.cpp |
| HardwareTestController | HardwareTestControllerTest.cpp |
| MissionEnergyCheck | MissionEnergyCheckTest.cpp |
| RcCalibrationCheck | RcCalibrationCheckTest.cpp |
| PreflightStateMachine | StateMachineTest.cpp |
| WeatherProvider | WeatherProviderTest.cpp |

### Untested source files (grouped by risk)

**Safety-critical — no tests:**
- `AbstractCheck` (base class for all 55+ checks)
- `PreflightManager` (central manager, 971 lines)
- `TelemetryBridge` (MAVLink adapter, 754 lines)
- `UavParameterManager` (parameter cache, 149 lines)
- All 51 individual check subclasses except 4 (AccelConsistencyCheck, AhrsHealthCheck, AirspeedCheck, AmbientTemperatureCheck, AttitudeCheck, BaroAltConsistencyCheck, BaroHealthCheck, BaroTemperatureCheck, BatteryFailsafeCheck, BatteryHealthCheck, BatteryTemperatureCheck, BatteryVoltageCheck, CellConfigCheck, CellVoltageBalanceCheck, CompanionLinkCheck, CurrentSensorCheck, DualGpsConsistencyCheck, EkfFailsafeCheck, EkfStatusFlagsCheck, EkfVarianceCheck, EscCurrentSymmetryCheck, EscFirmwareCheck, EscResponsivenessCheck, EscVoltageConsistencyCheck, GcsFailsafeCheck, GeofenceMaxAltCheck, GeofenceMaxRadiusCheck, GeofenceParamCheck, GimbalLinkCheck, GpsBaroAltConsistencyCheck, GpsFixCheck, GpsSpeedAccuracyCheck, GyroBiasCheck, HeartbeatCheck, HomePositionCheck, ImuCalCheck, ImuTemperatureCheck, LevelCalibrationCheck, MagFieldStrengthCheck, MagInterferenceCheck, ManualConfirmCheck, MavlinkProtocolCheck, MetarCeilingCheck, MetarPrecipitationCheck, MetarTemperatureCheck, MetarVisibilityCheck, MissionCountCheck, MissionItemCheck, MotorCountCheck, MotorSpinCheck, MotorTemperatureCheck, OpticalFlowCheck, PowerModuleHealthCheck, PreArmOkCheck, RadioBufferCheck, RadioFailsafeCheck, RcArmingSwitchCheck, RcChannelCountCheck, RcFailsafeCheck, RcModeSwitchCheck, RcRssiCheck, RcThrottleMinCheck, RcTrimCheck, RedundantPowerCheck, RtlAltParamCheck, RtlTerrainCheck, TafDeteriorationCheck, TakeoffCommandCheck, TelemetryDropRateCheck, TerrainClearanceCheck, VibrationCheck, VibrationFailsafeCheck, VideoFeedCheck, WeatherWindCheck, WeatherWindGustCheck, WindSpeedCheck)

**Non-safety-critical — no tests:**
- AlertManager
- VehicleProfileManager
- PowerModel
- PreflightSettingsManager
- ExportHelper
- MaintenanceTracker
- ClipboardHelper
- TemplateManager
- QgcParameterAdapter
- QgcVehicleAdapter
- VehicleRegistry
- PreflightChecklistModel
- PreflightChecklistFilterModel
- WaypointMath / WaypointMathHelper

---

## SUMMARY TABLE

| Severity | Count | Highest-risk example |
|---|---|---|
| CRITICAL (runtime failures) | 2 | 23 missing Q_PROPERTY declarations on TelemetryBridge — GPS fix type, gimbal state, vehicle type, and 20 other properties silently return empty/zero |
| SILENT FAILURES | 7 | ArmingGate periodic timer never started; RcRssiCheck epoch `*1000` bug always passes |
| DEAD CODE | 9+ | 4 empty detection stubs; dead `s_emergencyCommands` check in ArmingGate; never-emitted signal |
| HARDCODED VALUES | 22+ | ~42 hardcoded hex colors in PreflightChecklistView.qml; 14+ numeric literals missing Config.h constants; duplicate Config.h/Config.qml constants |
| ARCHITECTURAL | 6 | DatabaseManager 1,874-line god class; TelemetryBridge 100+ properties; singleton pattern inconsistency |
| QML ISSUES | 5 | Network calls in Component.onCompleted; VirtualJoystick QRC path not in custom.qrc |
| I18N GAPS | 80 | FlyViewTelemetryStrip (18), PreFlightChecklist (16), ChecklistEngine (14) |
| TEST COVERAGE | 40+ untested | PreflightManager, TelemetryBridge, UavParameterManager, 51 of 55 check subclasses — all safety-critical |
