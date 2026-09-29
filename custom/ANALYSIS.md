# GCS Reliability Analysis — Phase 1: Deep Analysis

> **Scope**: `custom/` folder (Skywin GCS = QGroundControl + custom preflight suite)
> **Date**: 2026-09-03
> **Status**: Analysis complete — Phase 2 (test cases) pending

---

## 0. Session tracker (append-only)

- **Ship-blocker #2 reopened (2026-09-28).** The stock arm path bypasses the ArmingGate: `GuidedActionsController.qml:636` `_activeVehicle.armed = true` → `Vehicle::setArmedShowError` → `Vehicle::setArmed` → `sendMavCommand(MAV_CMD_COMPONENT_ARM_DISARM)` never consults the gate. Proven live at the wire with the battery check FAILED (gate closed) — vehicle still armed, `cmd 400 result 0` ACKed. Evidence: `build/evidence-live-bypass.txt` (full run), `build/evidence-attribution.txt` (`executeAction(actionArm)` only). Regression test `custom/tests/ArmSendTest.cpp::testStockSetArmedBypassesGate` with `QEXPECT_FAIL(..., Continue)` reproduces as XFAIL (raw output: `build/evidence-armtest-xfail.txt`).
- **Retracted:** the earlier QML rows marking the FlyView arm path "fixed" are retracted — the UI arm path is ungated.
- **Relabeled:** `MainStatusIndicatorSmokeTest` (`custom/tests/QmlSmokeTest.cpp`) exercises `custom/qml/MainStatusIndicator.qml`, a custom non-shipping component; it does not prove the stock toolbar/arm path.

---

## 1. Module Map

### 1.1 Core Modules (`custom/src/core/`)

| Module | File(s) | Responsibility | Lines (approx) |
|--------|---------|----------------|-----------------|
| **AbstractCheck** | `AbstractCheck.h/.cpp` | Base class for all preflight checks. Status lifecycle, override/confirm, telemetry accessors with fallbacks, configurable thresholds via DatabaseManager. | ~450 |
| **ArmingGate** | `ArmingGate.h/.cpp` | Gate controller: Passive/Active/Hybrid modes, `processArmRequest()`, `acknowledgeOverride()`, periodic evaluation timer, `forceArm()` support. | ~300 |
| **ChecklistEngine** | `ChecklistEngine.h/.cpp` | Signal-driven evaluator: binds to TelemetryBridge, iterates ChecklistItemModel rows, maps bindProperty → telemetry value, emits `allPassed()`. | ~200 |
| **ChecklistItemModel** | `ChecklistItemModel.h/.cpp` | `QAbstractListModel` wrapping `ChecklistItemData[]` for QML. Roles: Id, Label, BindProperty, RequiredValue, Tolerance, Unit, IsManual, Status, Message. | ~120 |
| **PreflightManager** | `PreflightManager.h/.cpp` | Owns ~40 `AbstractCheck` instances, drives `PreflightStateMachine`, persists overrides to DatabaseManager, `startEvaluation()` loop. | ~500 |
| **PreflightStateMachine** | `PreflightStateMachine.h/.cpp` | Linear FSM: Disconnected → Connecting → ParamLoading → ChecklistInProgress → PreflightPass → ManualConfirmPhase → ArmingAllowed → Armed. Emits semantic signals on transitions. | ~90 |
| **PowerModel** | `PowerModel.h/.cpp` | Wh/km defaults per airframe, linear regression calibration, `energyForDistance()` estimation. | ~250 |
| **VehicleProfileManager** | `VehicleProfileManager.h/.cpp` | Device UID tracking, flight session lifecycle, battery serial, live SOC/time estimate, vehicle kind resolution. | ~300 |
| **UavParameterManager** | `UavParameterManager.h/.cpp` | Tracks parameter loading progress (watchlist → received), PX4↔ArduPilot name mapping, timeout→Fallback. | ~200 |
| **ParameterWatchlist** | `ParameterWatchlist.h` | Static definitions of ~50 UAV parameters organized by category (POW, NAV, COM, SAF, ARM) with min/max/default values. | ~120 |
| **ManualConfirmCheck** | (used in tests) | Concrete `AbstractCheck` subclass for manual operator confirmation checks. | — |

### 1.2 Adapters (`custom/src/adapters/`)

| Module | File | Responsibility | Lines |
|--------|------|----------------|-------|
| **TelemetryBridge** | `TelemetryBridge.h/.cpp` | Central adapter wrapping QGC `Vehicle`. Exposes 120+ `Q_PROPERTY` values (battery, GPS, IMU, EKF, RC, vibration, etc.). Decodes MAVLink messages. 200ms polling cycle. `_loadParameters()` populates dynamic properties from vehicle params. | ~1500 |

### 1.3 Controllers (`custom/src/controllers/`)

| Module | File | Responsibility |
|--------|------|----------------|
| **HardwareTestController** | `HardwareTestController.h/.cpp` | Motor test coordination: per-motor status, PWM control, servo sweep, RC override, cooldown timer. |
| **ControlSurfaceTestController** | `ControlSurfaceTestController.h/.cpp` | Servo sweep for fixed-wing/VTOL, MANUAL mode gate. |

### 1.4 Utilities (`custom/src/utils/`)

| Module | File | Responsibility |
|--------|------|----------------|
| **DatabaseManager** | `DatabaseManager.h` | Singleton SQLite with 15+ tables (flight_sessions, compliance_logs, vehicle_configs, check_config, battery_cycles, no_fly_zones, etc.). Migration system. |
| **Config** | `Config.h` | Spacing, font size, radius constants for QML. |
| **ExportHelper** | `ExportHelper.h` | Data export utilities. |
| **WeatherProvider** | `WeatherProvider.h` | METAR/TAF weather data fetch. |

### 1.5 Plugin Entry (`custom/src/`)

| Module | File | Responsibility |
|--------|------|----------------|
| **PreflightPlugin** | `PreflightPlugin.h/.cpp` | `QGCCorePlugin` subclass. Singleton glue: owns all managers, wires vehicle lifecycle, registers QML context properties and singletons, intercepts MAVLink arm/disarm commands, palette theming. |

### 1.6 QML UI (`custom/qml/`)

| Component | File | Purpose |
|-----------|------|---------|
| **ArmGateDialog** | `qml/cpts/ArmGateDialog.qml` | Modal dialog for reviewing preflight failures and override. Requires pilot name + reason. |
| **BatteryStatusChip** | `qml/cpts/BatteryStatusChip.qml` | Live SOC chip in Fly view, reads TelemetryProvider + VehicleProfileManager. |
| **ControlSurfacePanel** | `qml/cpts/ControlSurfacePanel.qml` | Servo sweep launcher for FW/VTOL. MANUAL mode gate + RC override + servo sweep controls. |
| **MotorCheckPanel** | `qml/cpts/MotorCheckPanel.qml` | Full motor test dialog: per-motor PWM sliders, test buttons, safety confirmation, cooldown. |
| **FlyQuickActions** | `qml/cpts/FlyQuickActions.qml` | Quick action buttons including Force Arm. |
| **MainStatusIndicator** | `qml/MainStatusIndicator.qml` | Toolbar status: integrates ArmingGate/PreflightManager state. |
| **PreflightToolbarIndicator** | `qml/PreflightToolbarIndicator.qml` | Connection/status dot with state machine labels. |

---

## 2. Data Flow

### 2.1 Initialization Sequence

```
PreflightPlugin::init()
  ├─ Create PreflightManager (owns ~40 AbstractCheck instances)
  ├─ Create TelemetryBridge
  ├─ Create ArmingGate, wire to PreflightManager + TelemetryBridge
  ├─ Create ChecklistItemModel, populate from PreflightManager checks
  ├─ Create ChecklistEngine, bind to ChecklistItemModel + TelemetryBridge
  ├─ Create VehicleProfileManager, bind to TelemetryBridge
  ├─ Create PowerModel, WeatherProvider, HardwareTestController
  ├─ Initialize DatabaseManager (SQLite)
  ├─ Wire vehicle registry signals (known/new vehicle)
  └─ Connect MultiVehicleManager::activeVehicleChanged → _setupForVehicle()
```

### 2.2 Vehicle Connection Flow

```
MultiVehicleManager::activeVehicleChanged(vehicle)
  └─ PreflightPlugin::_setupForVehicle(vehicle)
       ├─ TelemetryBridge::setVehicle(vehicle)  ← starts 200ms polling
       ├─ PreflightManager::startEvaluation(1000)  ← 1s check cycle
       ├─ ChecklistEngine::start()
       └─ Weather refresh (if enabled)
```

### 2.3 Telemetry → Check Evaluation → Gate

```
TelemetryBridge (200ms poll)
  ├─ Reads Vehicle properties (battery, GPS, IMU, etc.)
  ├─ Decodes MAVLink messages → updates Q_PROPERTY values
  └─ Emits propertyChanged signals

AbstractCheck subclasses (1s evaluation cycle)
  ├─ Read TelemetryBridge properties via getTelemetryDouble()/getTelemetryBool()
  ├─ Compare against thresholds (effectiveMinVoltage(), etc.)
  ├─ Call setStatus() → emits statusChanged/checkPassed/checkFailed
  └─ Read configurable thresholds from DatabaseManager::getCheckConfig()

ChecklistEngine
  ├─ Reads ChecklistItemModel rows
  ├─ Maps bindProperty → TelemetryBridge property
  ├─ Evaluates against requiredValue ± tolerance
  └─ Emits allPassed() when all mandatory checks pass

ArmingGate
  ├─ Queries PreflightManager for critical/manual fail counts
  ├─ processArmRequest(criticalFails, manualFails)
  │   ├─ Passive → always GATE_OPEN (log only)
  │   ├─ Active → GATE_CLOSED if any critical fail
  │   └─ Hybrid → GATE_CLOSED if critical, but acknowledgeOverride() → ALLOW_WITH_ACK
  └─ Emits armingAllowed/armingDenied
```

### 2.4 Arm/Disarm Command Interception

INBOUND ONLY — this path handles MAVLink messages arriving from link peers
(companion/external GCS). Outbound commands this GCS sends go through
`Vehicle::sendMavCommand -> sendMessageOnLinkThreadSafe` and never reach
`mavlinkMessage()`. The outbound arm path is gated solely by `TelemetryBridge::arm()`.

```
MAVLink COMMAND_LONG (MAV_CMD_COMPONENT_ARM_DISARM) [INBOUND]
  └─ PreflightPlugin::mavlinkMessage()
       ├─ Check param1=1 (arm request)
       ├─ Check param2=21196 or 2989 (force arm magic values)
       │   └─ ArmingGate::interceptCommandLong — an inbound force-arm does NOT
       │       auto-open the gate (only a local operator override does)
       ├─ If gate closed AND no override → BLOCK message, emit armingDenied()
       └─ If gate open → let message through

MAVLink COMMAND_ACK
  └─ PreflightPlugin::mavlinkMessage()
       ├─ If ACCEPTED → update vehicle armed state, emit vehicleArmed/Disarmed
       └─ If DENIED → _surfaceCommandRejection()
```

### 2.5 Flight Session Lifecycle

```
ArmingGate::gateOpened
  └─ PreflightPlugin::_onGateOpened()
       └─ DatabaseManager::startFlightSession(fingerprint)

ArmingGate::gateClosed
  └─ PreflightPlugin::_onGateClosed()
       ├─ Calculate duration
       ├─ DatabaseManager::saveComplianceLog(...)
       ├─ DatabaseManager::endFlightSession()
       └─ DatabaseManager::incrementBatteryCycle()
```

---

## 3. Risk List

### 3.1 Critical Risks

| ID | Risk | Module | Impact | Mitigation Status |
|----|------|--------|--------|-------------------|
| **R-01** | **Arming gate bypass via MAVLink force-arm magic values** | `PreflightPlugin.cpp:955-970` | Motors can spin without preflight checks if magic param2 values (21196, 2989) are sent | Mitigated: inbound force-arm is routed via `_armingGate->interceptCommandLong()` (PreflightPlugin.cpp:955-970) which never auto-opens the override — only operator override/one-time ACK. `ArmingGate::forceArm()` (ArmingGate.cpp:272-288) opens a bounded 60s window, not a permanent bypass. Covered by `testInboundForceArmDoesNotBypassGate` in `tests/MavLinkInterceptTest.cpp` and wire-level `tests/ArmSendTest.cpp` |
| **R-02** | **ArmingGate override is one-shot but not re-gated** | `ArmingGate.cpp:106-155` | `acknowledgeOverride()` allows one arm, then gate re-closes — but if vehicle stays armed, subsequent arm commands pass through without re-evaluation | Partially mitigated: `interceptCommandLong` (ArmingGate.cpp:106-155) re-checks `allMandatoryPassed()` on every inbound arm command, and `isOverrideActive()` only passes while the bounded 60s override window is open (`forceArm()`, ArmingGate.cpp:272-288). One-time ACK is consumed once (`m_ackReceived=false`, ArmingGate.cpp:129). Wire-level `tests/ArmSendTest.cpp::testForceArmExpiresThenArmBlocked` verifies the window expires and blocks again |
| **R-03** | **No telemetry timeout → stale check data** | `AbstractCheck.cpp:59-71` | `requiresReevaluation()` only triggers for Auto checks and returns false for Passed checks; if telemetry stream stops mid-flight a Passed check keeps its last status | Reid at `PreflightManager.cpp:700-724`: `tick()` now PROACTIVELY sweeps mandatory checks each 1000ms eval cycle (`PreflightPlugin.cpp:679`) and sets `CheckStatus::Stale` when last eval > `m_stalenessThresholdMs` (default 10000ms, `PreflightManager.h:217`); stale counts as failed for arming. Remaining gap: sweep covers only MANDATORY checks; non-mandatory Passed checks are not swept |
| **R-04** | **BatteryVoltageCheck cell count estimation** | `BatteryVoltageCheck.cpp:119-126` | `estimateCellCount()` uses `qRound(voltage / 3.7)` (line 123) and can misidentify cells on discharged/full packs when no battery param is present | Mitigated in `effectiveCellCount()` (`BatteryVoltageCheck.cpp:100-108`): prefers `param_BAT_CELL_COUNT` then `param_BATT_CELL_COUNT`, bounded to 1..16; `estimateCellCount()` fallback is only reached when neither param exists and is itself bounded 1..16 (conservative) |
| **R-05** | **MotorSpinCheck relies on QML confirm() — no auto-safety** | `MotorSpinCheck.cpp:31-49` | If QML panel crashes or is never opened, check stays Pending forever; no automatic failure after timeout | Manual check by design, but no timeout/fallback path |

### 3.2 High Risks

| ID | Risk | Module | Impact |
|----|------|--------|--------|
| **R-06** | **ChecklistEngine has no evaluation cycle timeout** | `ChecklistEngine.h/.cpp` | If TelemetryBridge stops updating, engine continues evaluating stale values without detecting the stall |
| **R-07** | **PreflightStateMachine allows forward-only transitions** | `PreflightStateMachine.cpp` | No backward transitions (e.g., from ArmingAllowed back to ChecklistInProgress if a check starts failing mid-evaluation) |
| **R-08** | **TelemetryBridge 200ms poll is fixed** | `TelemetryBridge.h/.cpp` | High-frequency polling even when vehicle is disconnected; no adaptive interval based on link quality |
| **R-09** | **AbstractCheck::hasTelemetry() checks connectionQuality >= 10** | `AbstractCheck.cpp:167` | Hardcoded threshold (10%) — if link quality oscillates around 10%, checks may rapidly toggle between Pending and evaluation |
| **R-10** | **DatabaseManager singleton has no thread safety** | `DatabaseManager.h` | If any check evaluation runs off the main thread (not observed currently but possible), SQLite access could race |

### 3.3 Medium Risks

| ID | Risk | Module | Impact |
|----|------|--------|--------|
| **R-11** | **PowerModel defaults are hardcoded per airframe** | `PowerModel.cpp` | No calibration persistence — defaults may not match actual vehicle efficiency |
| **R-12** | **VehicleProfileManager battery serial fallback** | `VehicleProfileManager.cpp` | Synthetic serial `"batt:<sysId>"` means multiple flights on same battery aren't tracked together |
| **R-13** | **ArmingGate periodic evaluation timer** | `ArmingGate.h` | Timer interval not documented; if too long, gate state may be stale during rapid parameter changes |
| **R-14** | **No check dependency ordering** | `PreflightManager.cpp` | Checks evaluate in insertion order; no mechanism to declare that one check depends on another (e.g., battery check depends on param load) |
| **R-15** | **QML MotorCheckPanel safety dialog is first-test-only** | `MotorCheckPanel.qml:592` | `firstTestDone` flag is per-session — if user closes and reopens dialog, safety dialog won't show again |

---

## 4. Code Health

### 4.1 Architecture Strengths

- **Clean separation of concerns**: AbstractCheck → concrete checks → PreflightManager → ArmingGate → QML is a well-defined ownership chain
- **Testable design**: MockTelemetryBridge (410 lines) provides comprehensive property mocking; existing tests (CheckSmokeTest, ArmingGateTest) demonstrate the pattern
- **Signal-driven evaluation**: ChecklistEngine uses Qt signals for re-evaluation rather than polling, which is efficient
- **Configurable thresholds**: `configDouble()`/`configInt()` in AbstractCheck read from DatabaseManager, allowing per-vehicle customization
- **MAVLink interception (inbound-only)**: PreflightPlugin::mavlinkMessage correctly intercepts and blocks INBOUND arm commands (from companion/external GCS on the link) at the message level. Its scope is strictly inbound: outbound arm commands sent via Vehicle::sendMavCommand -> sendMessageOnLinkThreadSafe never pass through mavlinkMessage(), so the outbound arm path is gated solely by the application-level check in TelemetryBridge::arm() (Layer 1).

### 4.2 Code Smells / Technical Debt

| Issue | Location | Severity |
|-------|----------|----------|
| **Duplicated property names** | `BatteryVoltageCheck.cpp:60-63` reads both `batteryCurrentAmps` and `batteryCurrent` with fallback | Low — defensive but suggests naming inconsistency |
| **Magic numbers in force-arm** | `Config.h:195-196` — `kForceArmParam2` (2989) and `kLegacyForceArmParam2` (21196) ARE now named constants; used at PreflightPlugin.cpp:966, TelemetryBridge.cpp, VehicleProfileManager.cpp | Resolved — constants exist; verify all call sites use them (no raw literals) |
| **QML type checking via string comparison** | `MotorCheckPanel.qml:251` — `typeof ControlSurfaceTestController !== "undefined"` | Low — fragile; should use QML singletons properly |
| **Config cache is mutable** | `AbstractCheck.cpp:260-291` — `m_configCache` is `mutable` and modified in const methods | Low — works but unconventional |
| **No enum for check status in QML** | `ArmGateDialog.qml:102-103` — `s === 2 || s === 4` uses magic ints | Medium — should use named enum values |
| **Large TelemetryBridge** | ~1500 lines, 120+ properties | Medium — god object; consider splitting into domain-specific bridges |

### 4.3 Documentation Quality

- **Good**: Core modules have file-level `@file`/`@brief` docstrings (AbstractCheck, ChecklistItemModel, PreflightStateMachine)
- **Good**: Inline comments explain non-obvious logic (e.g., `BatteryVoltageCheck::effectiveMinVoltage()`)
- **Missing**: No architecture decision records (ADRs) explaining why certain design choices were made
- **Missing**: No API documentation for QML-facing properties and methods

---

## 5. Testability Gaps

### 5.1 Existing Test Coverage

| Test File | What It Covers | Approach |
|-----------|----------------|----------|
| `CheckSmokeTest.cpp` | AbstractCheck lifecycle, ManualConfirmCheck, ChecklistItemModel load/set/reset, ChecklistEngine allPassed | Unit tests with mock check subclasses |
| `ArmingGateTest.cpp` | Passive/Active/Hybrid modes, override, disconnect reset | Unit tests with MockTelemetryBridge |
| `ArmingGateExtendedTest.cpp` | Extended gate scenarios | Unit tests |
| `ChecklistEngineTest.cpp` | Engine evaluation | Unit tests |
| `PreflightManagerTest.cpp` | Manager initialization, check registration | Unit tests |
| `ChecklistItemModelTest.cpp` | Model data roles, load, status update | Unit tests |
| `StateMachineTest.cpp` | PreflightStateMachine transitions | Unit tests |
| `DatabaseManagerTest.cpp` | SQLite operations | Unit tests |
| `BatteryEstimatorTest.cpp` | PowerModel estimation | Unit tests |
| `MissionEnergyCheckTest.cpp` | Mission energy calculation | Unit tests |
| `UavParameterManagerTest.cpp` | Parameter loading, PX4↔ArduPilot mapping | Unit tests |
| `FlightHistoryModelTest.cpp` | Flight history model | Unit tests |
| `WeatherProviderTest.cpp` | Weather data fetch | Unit tests |
| `RcCalibrationCheckTest.cpp` | RC calibration check | Unit tests |
| `HardwareTestControllerTest.cpp` | Motor test controller | Unit tests |
| `TelemetryBridgeTest.cpp` | Telemetry bridge | Unit tests |

### 5.2 Coverage Gaps

| Gap | Modules Not Covered | Risk |
|----|---------------------|------|
| **No integration tests for full arming flow** | TelemetryBridge → Checks → ArmingGate → MAVLink block | High — end-to-end arming safety is untested |
| **No QML component tests** | ArmGateDialog, MotorCheckPanel, ControlSurfacePanel, BatteryStatusChip | Medium — UI logic (e.g., button enable/disable) untested |
| **No tests for PreflightPlugin::mavlinkMessage()** | MAVLink interception, force-arm detection, ACK handling | High — critical safety path |
| **No tests for flight session lifecycle** | Gate opened → session start → gate closed → session end + compliance log | Medium |
| **No tests for DatabaseManager migration** | Schema upgrades, data integrity across versions | Medium |
| **No tests for VehicleProfileManager** | Device UID, battery serial, SOC estimate | Medium |
| **No tests for WeatherProvider integration** | Weather refresh on vehicle connect, ICAO fallback | Low |
| **No tests for Override persistence** | Override records saved/restored across sessions | Medium |
| **No tests for Check::applyVehicleConfig()** | Per-vehicle threshold customization | Low |
| **No tests for PowerModel calibration** | Linear regression, energy estimation | Low — BatteryEstimatorTest may cover |
| **No tests for NoFlyZoneModel/ZoneComplianceCheck** | No-fly zone CRUD, compliance evaluation | Medium |
| **No stress tests** | Rapid connect/disconnect, multiple vehicles, telemetry storms | Medium |
| **No tests for AbstractCheck config cache** | `configDouble()`/`configInt()` caching, cache invalidation | Low |

### 5.3 Test Infrastructure Observations

- **MockTelemetryBridge** is comprehensive (410 lines, 70+ mock properties with setters that emit signals) — excellent foundation for unit tests
- **MockVehicle** is minimal (19 lines) — only exposes `id()` and `isConnected()`, insufficient for testing vehicle-dependent flows
- **MockParameterManager** exists but was not examined in detail
- **Test framework**: Qt Test (`QTest`, `QSignalSpy`) with `UnitTest` base class and `UT_REGISTER_TEST` macro — consistent across all test files
- **No test helper factories** — each test manually creates mock objects and wires them; a `TestHarness` or fixture class would reduce boilerplate

---

## 6. Summary of Findings

### What Works Well

1. **Arming gate design** is sound: three modes (Passive/Active/Hybrid) provide appropriate safety levels for different operational contexts
2. **AbstractCheck abstraction** is clean — adding new checks requires only implementing `evaluate()` and optionally `getRationale()`/`getFixSteps()`
3. **Signal-driven architecture** — most components communicate via Qt signals/slots, making the system loosely coupled
4. **TelemetryBridge** successfully abstracts away the QGC Vehicle API, providing a stable interface for all checks
5. **Configurable thresholds** via DatabaseManager allow per-vehicle customization without code changes

### What Needs Improvement

1. **Force-arm safety**: Magic values 21196/2989 should be named constants; post-force-arm gate state needs re-validation
2. **Telemetry staleness detection**: No proactive detection of telemetry stream interruption — checks can operate on stale data
3. **State machine directionality**: PreflightStateMachine only moves forward; no mechanism to revert if conditions deteriorate
4. **QML enum exposure**: Magic integers (status codes 0-4) in QML should be replaced with named constants
5. **Integration test coverage**: Critical arming flow (telemetry → checks → gate → MAVLink) has no end-to-end test

### Recommended Next Steps

1. **Write integration tests** for the full arming flow: connect → telemetry → checks pass → gate opens → arm command allowed
2. **Write QML tests** for ArmGateDialog (button enable logic, override validation) and MotorCheckPanel (safety dialog flow)
3. **Add telemetry staleness detection** — if no telemetry update for N seconds, transition checks to Stale status
4. **Replace magic integers in QML** with named enum constants from C++
5. **Consider splitting TelemetryBridge** into domain-specific adapters (BatteryBridge, GPSBridge, IMUBridge) to reduce the god-object problem

---

*Analysis complete. Phase 2 (test cases) will produce `TEST_CHECKLIST.md` covering unit tests, integration tests, and QML tests for identified gaps.*
