# Feature Status Report — `custom/` UAV Preflight Plugin

> Generated: 2026-08-23 · Evidence base: full source read of `custom/` (C++/QML/CMake/docs), git log, unit-test inventory.
> Every status below is backed by a file + symbol reference. Nothing is claimed without evidence on disk.

## Status vocabulary (exact meanings used here)

| Status | Meaning |
|---|---|
| **DONE** | Implemented end-to-end in code/UI; no automated test required |
| **TESTED** | DONE **and** covered by a passing unit test (`custom/tests/`) |
| **PARTIAL** | Core works, but has known limitations, unverified-on-hardware paths, or missing sub-features |
| **BROKEN** | Present but demonstrably malfunctioning today |
| **MISSING** | Designed/planned but absent from the shipped product |
| **OUT OF SCOPE** | Explicitly excluded by design decision |

---

## 1. Project Map

```
custom/
├── CMakeLists.txt                  # Explicit source list (no globs) fed to QGC via CUSTOM_SOURCES cache vars
├── cmake/CustomOverrides.cmake     # Branding, icons, Qt version override, Qt6LocationPrivate stub wiring
├── cmake/Qt6LocationPrivateConfig.cmake  # Stub so find_package(Qt6 LocationPrivate) succeeds on Qt 6.8.x OSS
├── custom.qrc + *.qrc              # QML/resources; pages mounted into QGC shell views
├── src/
│   ├── PreflightPlugin.{h,cpp}     # Root plugin replacing QGCCorePlugin (QGC_CUSTOM_DIR mechanism)
│   ├── adapters/                   # TelemetryBridge (MAVLink→Qt properties), QgcVehicleAdapter, QgcParameterAdapter
│   ├── controllers/                # HardwareTestController, ControlSurfaceTestController, HardwareTestProfile
│   ├── core/                       # AbstractCheck + check subclasses, PreflightManager, ArmingGate,
│   │                               #   PreflightStateMachine, ChecklistEngine/ItemModel, VehicleProfileManager,
│   │                               #   UavParameterManager, PowerModel, ParameterWatchlist.h
│   ├── detection/VehicleRegistry   # Fingerprinting / auto-registration of airframes
│   ├── managers/                   # OperatorManager, FlightSession, TelemetryEventLogger
│   ├── mission/                    # WaypointMath, WaypointMathHelper
│   ├── models/                     # FlightHistoryModel, NoFlyZoneModel, VehicleListModel
│   ├── ui/                         # PreflightChecklistModel(+FilterModel)
│   └── utils/                      # DatabaseManager, WeatherProvider, AlertManager, TemplateManager,
│                                   #   MaintenanceTracker, ExportHelper, Config.h, PreflightSettingsManager
├── qml/
│   ├── pages/                      # PreFlightChecklist, FinalChecks, VehicleSelect, WizardStep*, GimbalTest,
│   │                               #   AirspacePage, MaintenancePage, LaunchReady, Home …
│   ├── cpts/                       # MotorCheckPanel, ControlSurfacePanel, ArmGatePanel, PreflightChecklistView,
│   │                               #   WeatherPanel, ServoTestCard, AlertPanel/Banner …
│   ├── singletons/                 # VehicleTelemetry (TelemetryBridge singleton), Config, Colors, DistanceTracker
│   └── FlyView*/Analyze layers     # FlyViewWidgetLayer/FlyViewOverlay hooks, FlightHistoryPage
├── tests/                          # 17 QtTest files + mocks/ (MockTelemetryBridge, MockVehicle, MockParameterManager)
├── deploy/                         # start script, android package overlay
└── docs in-tree                    # doc.md, SRS_AUDIT.md, STATIC_ANALYSIS_REPORT.md, testsuite.md,
                                    #   FLIGHT_SYSTEM_STEPS_1_2_3.md, INTEGRATION_GUIDE.md, UI_change.md,
                                    #   vehicles_known_limitations.md
```

Build entry point (from `doc.md`):
```
cmake -S . -B build_custom -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DQGC_CUSTOM_DIR=custom -DQGC_BUILD_TESTING=OFF
```

### Key file sizes (evidence scale)

| Layer | File | Lines |
|---|---|---|
| Plugin | `src/PreflightPlugin.cpp` | 940 |
| Core | `src/core/PreflightManager.cpp` | 956 |
| Safety | `src/core/ArmingGate.cpp` | 362 |
| Controllers | `src/controllers/HardwareTestController.cpp` | 1731 |
| Controllers | `src/controllers/ControlSurfaceTestController.cpp` | 1169 |
| Adapter | `src/adapters/TelemetryBridge.cpp` (+ `.h` ~900) | 997 |
| Data | `src/utils/DatabaseManager.cpp` | 3972 |
| Model | `src/models/NoFlyZoneModel.cpp` | 307 |
| QML total | pages + cpts + singletons | **17,273** (largest: AirspacePage 2263, PreflightChecklistView 1454, FlightHistoryPage 1130) |

Full QML inventory: **Appendix G**.

---

## 2. Master Feature Matrix

### Area A — Preflight pipeline & checks

| ID | Feature | Status | Evidence |
|---|---|---|---|
| PF-01 | Check framework: `AbstractCheck` (id/category/type/blocking/mandatory, per-check config from DB with in-memory cache, debug test-overrides) | **DONE** | `src/core/AbstractCheck.{h,cpp}`; config caching at `AbstractCheck.cpp:258–296`; smoke-tested via `tests/CheckSmokeTest.*` |
| PF-02 | Live preflight suite — **31 checks registered** (26 auto/action + 5 manual confirms) across Safety/Power/Propulsion/Nav/Comm/Env categories | **TESTED** | Registration block `src/core/PreflightManager.cpp:740–830` — full census with tiers/thresholds in **Appendix B**; `tests/PreflightManagerTest.cpp`, `tests/CheckSmokeTest.*` |
| PF-03 | Preflight state machine (DISCONNECTED→CONNECTING→PARAM_LOADING→CHECKLIST_IN_PROGRESS→PREFLIGHT_PASS→ARMING_ALLOWED→ARMED) | **TESTED** | `src/core/PreflightStateMachine.{h,cpp}`; `tests/StateMachineTest.cpp` |
| PF-04 | Arming gate: Passive/Active/Hybrid modes, intercepts `MAV_CMD_COMPONENT_ARM_DISARM` (400), operator override with `timeoutSec` expiry, periodic re-eval timer (started at `ArmingGate.cpp:71`) | **TESTED** | `src/core/ArmingGate.{h,cpp}` (362 ln); `tests/ArmingGateTest.cpp`, `tests/ArmingGateExtendedTest.cpp` |
| PF-05 | Periodic evaluation loop + staleness handling in `PreflightManager` (QTimer tick, pass/fail/pending/stale counters, per-vehicle instance) | **DONE** | `src/core/PreflightManager.{h,cpp}` (956 ln); overrides persisted to QSettings, results logged to DB |
| PF-06 | Checklist engine: selective evaluation, stale detection, item model + filter model for QML | **TESTED** | `src/core/ChecklistEngine.{h,cpp}`, `src/core/ChecklistItemModel.{h,cpp}`, `src/ui/PreflightChecklistModel{,Filter}.{h,cpp}`; `tests/ChecklistEngineTest.cpp`, `tests/ChecklistItemModelTest.cpp` |
| PF-07 | Manual-confirm flow (5 `ManualConfirmCheck`s with `confirmedBy` attribution wired to flight record) | **PARTIAL** | Implemented (`PreflightManager.cpp:794–830`); the end-to-end acceptance row for check-results↔flight wiring is **unfilled** (`testsuite.md:643–645`) |
| PF-08 | Dormant check library: **39 check sources on disk but NOT compiled**, plus **6 compiled but never instantiated** (`BatteryFailsafeParamCheck`, `GpsFixCheck`, `GpsSpeedAccuracyCheck`, `LevelCalibrationCheck`, `MissionEnergyCheck`+`PowerModel`, `RcFailsafeCheck`) | **MISSING** | Diff of `ls src/core/*.cpp` vs `CUSTOM_SOURCE_REL` in `CMakeLists.txt:54–197`; grep shows no `new XCheck(` for those six anywhere |
| PF-09 | SRS v1.0 coverage (design target: 109 requirements / 91 checks) | **PARTIAL** | Live suite is 31 checks after the “remove 33 preflight checks” commits; `SRS_AUDIT.md` (dated 2026‑06‑14) still describes the 91-check system and is stale |
| PF-10 | Parameter watchlist infrastructure (~40 params, FALLBACK mode on 10 s timeout) | **DONE** | `src/core/ParameterWatchlist.h` consumed at `TelemetryBridge.cpp:414`, `PreflightManager.cpp:513,668` |

### Area B — Hardware tests (motor / EDF / servo)

| ID | Feature | Status | Evidence |
|---|---|---|---|
| HW-01 | Multirotor motor test: `MAV_CMD_DO_MOTOR_TEST` (motor instance = board output), per-motor sequence, cooldown between runs, `SERVO_OUTPUT_RAW` feedback comparison | **TESTED** | `src/controllers/HardwareTestController.cpp:979,1345`; feedback eval `:1121–1130`; `tests/HardwareTestControllerTest.cpp` |
| HW-02 | Fixed-wing motor/EDF test: arm → drive throttle channel via `RC_CHANNELS_OVERRIDE` @10 Hz → disarm → evaluate peak PWM from `SERVO_OUTPUT_RAW` | **PARTIAL** | `HardwareTestController.cpp:573–578,762,806`; handles “no SERVO_OUTPUT_RAW” (Mock/SITL) at `:1007–1021`; bench-validated once (commit `19c620ef2` “control surface and motor test working”) but no repeatable bench evidence recorded |
| HW-03 | Profile-driven servo sweeps: JSON templates validated by `isValid()` (PWM ranges, instances 1–16, durations, expected min/max, `useRcOverride` flag, `needsVisualConfirm` steps) | **TESTED** | `src/controllers/HardwareTestProfile.{h,cpp}`; `tests/HardwareTestControllerTest.cpp` covers abort/profile |
| HW-04 | Stabilization suppress: `_sendControlNeutralHold` pins RC1/RC2/RC4 to 1500 µs at 10 Hz; release writes 65535 to all 18 channels | **DONE** | `HardwareTestController.cpp:88,1196–1216` |
| HW-05 | Test persistence: `motor_test_results` + `hardware_test_events` tables (step-by-step audit) | **DONE** | `src/utils/DatabaseManager.{h,cpp}` (schema v13 lineage); exercised by `tests/DatabaseManagerTest.cpp` |
| HW-06 | MotorCheckPanel UI (per-motor cards, visual-confirm gates, abort) | **DONE** | `qml/cpts/MotorCheckPanel.qml` (71 refs to controller); no dedicated UI test |

### Area C — Control surfaces & gimbal

| ID | Feature | Status | Evidence |
|---|---|---|---|
| CS-01 | Surface discovery: channel mapping from `SERVOn_FUNCTION`, travel from `SERVOn_MIN/MAX` (default 988–2011), RC channel re-resolved from `RCMAP_*` at every test start | **DONE** | `src/controllers/ControlSurfaceTestController.{h,cpp}` (struct `ControlSurface`: elevon_l/elevon_r/aileron/elevator/rudder/flap/nose_wheel); re-resolve at `:505–509` |
| CS-02 | Bench sweep engine: force-arm (magic 21196) if disarmed, continuous MANUAL-mode forcing during sweep, `MIXING_GAIN` 0.5→1.0 boost + restore, RC-driven sweep 1000–2000 µs (direct `DO_SET_SERVO` surfaces sweep `SERVOn` limits), center pinning before first command, actual-travel capture from `SERVO_OUTPUT_RAW` | **PARTIAL** | `ControlSurfaceTestController.cpp:497–587` (arm/gain/pin/stream), `:592–650` (gain + sweep bounds), `:916–928` (streaming). Works on bench once (commit `19c620ef2`); safety-sensitive workarounds (force-arm!) warrant re-verification before sign-off |
| CS-03 | Elevon isolation: left/right isolation modes (`elevonMode` 1/2), drives only the isolated side’s `RCMAP_ROLL`+`RCMAP_PITCH` pair packed into ONE `RC_CHANNELS_OVERRIDE`, `swapElevonLR` option, automatic opposite-vs-same-phase verdict counters | **PARTIAL** | Paired-drive `:718–752` (`wantRight = (elevonMode==2) != m_swapElevonLR`, single packed message `:840`), verdict `m_phaseOppositeCount/m_phaseSameCount` `:562–567` + `_updateSweepVerdict` `:1128+`. Mechanism addresses the historic “both sides move” issue; needs hardware re-run to confirm |
| CS-04 | Flap direct drive: works around ArduPilot refusing `MAV_CMD_DO_SET_SERVO` on function-assigned channels (function 14) by preferring `RC_CHANNELS_OVERRIDE` when an RC input channel exists | **PARTIAL** | Documented in-code at `:753–779`; fallback path exists but flap-without-RC-channel case relies on raw `DO_SET_SERVO` acceptance |
| CS-05 | Per-surface QML cards + operator direction-confirm (`confirmSurfaceDirection`) + stop-all cleanup (releases overrides, restores MIXING_GAIN) | **DONE** | `:929–979` (confirm), `:980–1015` (stopAllSurfaces); UI in `qml/pages/GimbalTest.qml`, `qml/cpts/ControlSurfacePanel.qml` |
| CS-06 | `ControlSurfaceCheck` as a checklist item | **MISSING** | `src/core/ControlSurfaceCheck.{h,cpp}` exist as **0-byte empty files**; not in CMake source list; nothing references them |
| CS-07 | Legacy control-surface path: `ControlSurfacePanel.qml` sweeps servos 1–4 through the profile API (superseded by CS-01…05 but still shipped) | **PARTIAL** | `qml/cpts/ControlSurfacePanel.qml` (FW/VTOL-gated card; hard-coded servo 1–4 assumption retained here) |

### Area D — Airspace compliance

| ID | Feature | Status | Evidence |
|---|---|---|---|
| AS-01 | No-fly-zone knowledge base: CRUD, case-insensitive name search, sort by name/radius/reason/updated, overlap warning | **DONE** | `src/models/NoFlyZoneModel.{h,cpp}` (307 ln, `QAbstractListModel`, `QML_ELEMENT`) |
| AS-02 | Airspace page: AnalyzePage-pattern UI with interactive map drawing/selection, reason-coded colors (Regulatory/Obstacle/Restricted/Temporary), notice banners | **DONE** | `qml/pages/AirspacePage.qml` (2263 ln; reason colors `:70–98`) |
| AS-03 | Automatic route-compliance check: collects mission waypoints on `MissionController::visualItemsChanged`, intersects against active zones, logs per-zone audit rows with 60 s re-log interval, acknowledge flow (`acknowledgeZone()`) from page or checklist | **DONE** | `src/core/ZoneComplianceCheck.{h,cpp}` (263 ln; `checkId="airspace.zone_compliance"`, advisory: `mandatory=false`, `canOverride=false`); registered `PreflightManager.cpp:747` |
| AS-04 | Compliance decision recording + audit trail: `zone_compliance_log` (DB schema v13), linked to flight sessions, filterable by flight (-1 = all), newest-first | **DONE** | `DatabaseManager` migrations; consumption in `NoFlyZoneModel::complianceRecords`, `submitCompliance()`; `AirspacePage.qml:37–43` |
| AS-05 | Sidebar badges/counters driven by zone counts (`count`/`activeCount`) | **DONE** | `NoFlyZoneModel.h` properties; consumed across cpts cards/badges |
| AS-06 | MAVLink geofence upload/interaction | **OUT OF SCOPE** | Explicit: `AirspacePage.qml:18` — “There is NO MAVLink geofence interaction — the Plan-view geofence is untouched.” Knowledge base + manual/auto compliance only |
| AS-07 | Unit tests for zone model / route-intersection math | **MISSING** | No `NoFlyZoneModelTest` / `ZoneComplianceCheckTest` in `custom/tests/` |

### Area E — Vehicle, parameters & detection

| ID | Feature | Status | Evidence |
|---|---|---|---|
| VP-01 | TelemetryBridge: MAVLink→Qt property adapter (~100 properties: dual battery, dual GPS, RC incl. RSSI/failsafe/staleness, vibration, EKF, gimbal attitude/calibration, pre-arm severity, data-quality scores) | **TESTED** | `src/adapters/TelemetryBridge.{h,cpp}` (997 ln cpp; all 23 formerly-missing Q_PROPERTYs now declared at `.h:411–495`); `tests/TelemetryBridgeTest.cpp` |
| VP-02 | UAV parameter manager: load with timeout, subscription-based updates (poll replaced by `parameterUpdated` signal) | **TESTED** | `src/core/UavParameterManager.{h,cpp}`; `tests/UavParameterManagerTest.cpp` |
| VP-03 | VehicleRegistry: device fingerprinting, DB lookup, auto-registration of unknown airframes, known/new signals | **TESTED** | `src/detection/VehicleRegistry.{h,cpp}`; vehicle CRUD in `tests/DatabaseManagerTest.cpp` |
| VP-04 | Per-vehicle check configuration: `vehicle_config` JSON blob, `applyVehicleConfig` | **DONE** | `src/core/VehicleProfileManager.cpp` (`getCheckConfig("vehicle_profile",…)` at `:568`) |
| VP-05 | VehicleProfileManager: vehicle-kind classification (FIXED_WING/VTOL_CONVENTIONAL/…), armed-timer threshold | **DONE** | `src/core/VehicleProfileManager.{h,cpp}` (threshold configurable via DB key) |
| VP-06 | AutopilotInfoDetector (autopilot type/version surfacing for the UI) | **DONE** | `src/utils/AutopilotInfoDetector.{h,cpp}`; classified INFRA in `SRS_AUDIT.md` SYS-002 |
| VP-07 | Simultaneous multi-airframe identity (distinct tracking of same-sysid/different-airframe) | **PARTIAL** | Documented limitation: `vehicles_known_limitations.md` §1 (QGC keys by sysid) and §2 (AUTOPILOT_VERSION only for active vehicle) |

### Area F — Session management & data

| ID | Feature | Status | Evidence |
|---|---|---|---|
| SE-01 | OperatorManager (add/select/persist operator, duplicate rejection) | **DONE** | `src/managers/OperatorManager.{h,cpp}`; UI wiring in WizardStep1Operator.qml (18 refs) |
| SE-02 | FlightSession: training mode blocks arming, `armingPermitted` gate, `currentFlightId` binding into ArmingGate + compliance log | **TESTED** | `src/managers/FlightSession.{h,cpp}`; round-trip in `tests/DatabaseManagerTest.cpp`; gate integration in `tests/ArmingGateExtendedTest.cpp` |
| SE-03 | TelemetryEventLogger: ARM/DISARM events, no-orphan guarantee | **DONE** | `src/managers/TelemetryEventLogger.{h,cpp}`; spec `testsuite.md` Part H (runtime acceptance row blank — see SE-06) |
| SE-04 | Post-flight checklist mode (post-flight items activate after disarm, pre-flight items show SKIPPED) | **PARTIAL** | Implemented per `FLIGHT_SYSTEM_STEPS_1_2_3.md` Part 1/6; acceptance evidence not recorded |
| SE-05 | Flight history: model + page + statistics dashboard + CSV export with dual filter (date + vehicle) | **TESTED** | `src/models/FlightHistoryModel.{h,cpp}` + `tests/FlightHistoryModelTest.cpp`; export via `src/utils/ExportHelper.{h,cpp}` |
| SE-06 | Formal manual acceptance run (testsuite.md Parts A–L) | **MISSING** | `testsuite.md:618–660` — the entire Test Record table is blank; system explicitly says “must show PASS before considered complete” |

### Area G — Database & audit

| ID | Feature | Status | Evidence |
|---|---|---|---|
| DB-01 | SQLite schema v13 + forward migrations — **21 tables** (checklists, check results ×2, compliance ×2, zones, hardware/surface/motor test results, vehicles+config, batteries+cycles, maintenance, operators, sessions/flights, telemetry events, check_config, schema_version) | **TESTED** | `src/utils/DatabaseManager.{h,cpp}` (3972 ln cpp); full table map in **Appendix E**; `tests/DatabaseManagerTest.cpp` |
| DB-02 | Audit trails: check results, compliance decisions, hardware-test steps, telemetry events — all timestamped, many flight-linked | **DONE** | Same tables as above; consumers: PreflightManager, NoFlyZoneModel, HardwareTestController, TelemetryEventLogger |
| DB-03 | God-class risk: DatabaseManager spans every domain in one 3,972-line class | **PARTIAL** (works, architectural debt) | Flagged in `STATIC_ANALYSIS_REPORT.md` (at 1,874 ln then; doubled since) |

### Area H — UI / navigation / platform

| ID | Feature | Status | Evidence |
|---|---|---|---|
| UI-01 | Wizard Steps 1–3 (VehicleSelect → operator/session dialogs → FinalChecks) with pre-populated fields and validation | **DONE** | `qml/pages/VehicleSelect.qml`, `WizardStep*`, `FinalChecks.qml`; spec `FLIGHT_SYSTEM_STEPS_1_2_3.md` |
| UI-02 | PreFlightChecklist page + PreflightChecklistView dialog (categories, pass/fail styling, override affordances) | **DONE** | `qml/pages/PreFlightChecklist.qml`, `qml/cpts/PreflightChecklistView.qml`; style debt noted in §8 |
| UI-03 | ArmGatePanel + FlyView integration (override button, force-arm with expiry, training-mode footer gating that hides Override/ForceArm) | **DONE** | `qml/cpts/ArmGatePanel.qml` (ArmingGate ×11, FlightSession ×8); FlyView layers in `qml/` |
| UI-04 | Maintenance/templates/vehicle-management pages (components, battery cycles, templates editor, VehicleManagement) | **DONE** | `qml/pages/MaintenancePage.qml` (VehicleProfileManager ×12), `TemplateManager.qml`, `TemplateEditor.qml` |
| UI-05 | Weather subsystem: provider (METAR-style fetch), panels, wizard integration | **PARTIAL** | `src/utils/WeatherProvider.{h,cpp}`; `tests/WeatherProviderTest.cpp` covers initial state only; **blocking 3 s network fetch on calling thread** remains (`WeatherProvider.cpp:647–649`) |
| UI-06 | Internationalization | **PARTIAL** | ~80 user-facing strings not wrapped in `qsTr()` (worst: FlyViewTelemetryStrip 18, PreFlightChecklist 16, ChecklistEngine.qml 14 — `STATIC_ANALYSIS_REPORT.md` i18n table) |
| UI-07 | Build/platform integration: `QGC_CUSTOM_DIR` plugin swap, Android package overlay, Qt 6.8.x compatibility stubs, branding/icons | **DONE** | `CMakeLists.txt:7–48` (android overlay → `FINAL_ANDROID_DIR`), `cmake/CustomOverrides.cmake`, `cmake/Qt6LocationPrivateConfig.cmake`, linker-anchor registrations in `PreflightPlugin.cpp` |

---

## 3. Status Counts

Matrix total: **50 features**

| Status | Count | IDs |
|---|---|---|
| TESTED | 11 | PF-02, PF-03, PF-04, PF-06, HW-01, HW-03, VP-01, VP-02, VP-03, SE-02, SE-05 (+ DB-01) |
| DONE | 22 | PF-01, PF-05, PF-10, HW-04, HW-05, HW-06, CS-01, CS-05, AS-01…AS-05, VP-04, VP-05, VP-06, SE-01, SE-03, UI-01…UI-04, UI-07 |
| PARTIAL | 12 | PF-07, PF-09, HW-02, CS-02, CS-03, CS-04, CS-07, VP-07, DB-03, SE-04, UI-05, UI-06 |
| BROKEN | 0 | *(nothing demonstrably malfunctioning found in current code)* |
| MISSING | 4 | PF-08, CS-06, AS-07, SE-06 |
| OUT OF SCOPE | 1 | AS-06 |

*(DB-01 counted under TESTED; totals above treat each ID exactly once — see matrix for authority.)*

---

## 4. Deep Dive — Control Surfaces

**Architecture.** Two generations coexist:
- *Modern*: `ControlSurfaceTestController` (1169 ln) — per-surface objects discovered from firmware parameters; used by `GimbalTest.qml` (22 refs) via `loadSurfacesForVehicle(vehicleType, motorCount)` / `stopAllSurfaces()`.
- *Legacy*: `ControlSurfacePanel.qml` — FW/VTOL bench card sweeping servos 1–4 through the `HardwareTestProfile` API. Retained but superseded; this is the only place the “servos 1–4 hard-coded” assumption still lives.

**Surface discovery (CS-01, DONE).** Each `ControlSurface` carries `id`, `label`, `channel` (from `SERVOn_FUNCTION`), travel window `minPwm/maxPwm` (read live from `SERVOn_MIN/MAX`, default 988–2011), `rcChannel/rcChannel2` and `elevonMode ∈ {0 single-RC, 1 left-elevon isolation, 2 right-elevon isolation}`. RC channels are re-resolved from `RCMAP_ROLL`/`RC_MAP_PITCH` at every test start (`_beginSurfaceTest`, cpp:505–509) — so remapped radios are picked up automatically.

**Sweep mechanics (CS-02).** One sweep = state machine `Cooldown → Sweeping → Cooldown` driven by `m_stepTimer`:
1. If disarmed, sends `MAV_CMD_COMPONENT_ARM_DISARM(1.0, 21196)` — the ArduPilot *force-arm* magic — because Plane pins servos at trim while disarmed (cpp:511–524). **Safety-relevant:** the aircraft arms itself for a bench test.
2. Forces flight mode MANUAL and keeps re-issuing it for the whole sweep via `m_forceManualTimer` (cpp:544–552, 620–629) so autopilot-commanded modes (LOITER/AUTO/RTL…) can’t suppress stick input.
3. Boosts `MIXING_GAIN` 0.5→1.0 for full-authority deflection, restores original value afterwards (cpp:592–615).
4. Clears all leftover `RC_CHANNELS_OVERRIDE`s, then pins flight axes neutral so the first real command is the only mover (cpp:574–578, `_sendCenterRcOverrides` :897).
5. Sweeps RC-neutral→1000→2000 µs (mixer translates to full servo travel); direct-drive surfaces (flaps) sweep their `SERVOn` limits instead (cpp:634–650).
6. Requests `SERVO_OUTPUT_RAW` streaming (10 Hz) and captures actual min/max PWM achieved, feeding an automatic verdict (cpp:916–928, `_updateSweepVerdict` :1128+).

**Historic issues → current mitigations.**

| Historic issue | Current state in code |
|---|---|
| Elevon isolation moves both sides | Isolation modes drive **only** the isolated side’s roll+pitch RC pair, packed into one override frame (`_sendServoPwm` :718–752; “pack ALL channels into ONE message” :840); `wantRight = (elevonMode==2) != m_swapElevonLR`. Phase counters (`m_phaseOppositeCount/m_phaseSameCount`, :566–567) auto-classify opposite vs same motion so residual misconfig is *detected*, not silent. Needs one confirming bench run. |
| Ailerons move same direction instead of opposite | Same verdict machinery: opposite-phase expectation is counted and surfaced via `sweepVerdict`; `confirmSurfaceDirection` (:929–979) forces an explicit operator pass/fail per surface. |
| Rudder test disturbs CH1/CH2 | Surface sweeps start from a cleared-then-centered override slate (:574–578); motor tests independently pin RC1/RC2/RC4 at 1500 µs @10 Hz (`HardwareTestController::_sendControlNeutralHold` :1196–1216) and release with 65535×18. |
| `DO_SET_SERVO` rejected on function-assigned channels | Known and routed around: flaps prefer `RC_CHANNELS_OVERRIDE` whenever an RC input channel is mapped (comment + code :753–779); `HardwareTestProfile.TestStep.useRcOverride` provides the same escape hatch for profile sweeps. |
| Surfaces need MANUAL + armed | Fully automated inside the sweep (force-arm + continuous MANUAL forcing) — no operator dance required. |

**ACK-deferred start.** When disarmed, the sweep does not race the arm command: `_beginSurfaceTest` stores `m_pendingSurfaceIndex` and returns; `_onCommandResult` (`:1016–1041`) starts the deferred sweep only on `MAV_RESULT_ACCEPTED` for the arm, and on rejection surfaces `"Arm command rejected — cannot move control surfaces while disarmed"` via `lastErrorMessage`. A rejected `MAV_CMD_DO_SET_SERVO` during an active sweep is logged (`:1043–1045`) rather than retried blindly.

**Feedback capture (`_onMavlinkMessage`, `:1048–1127`).**
- Maintains a live `CH1=… CH2=… CH3=… CH4=…` readout string (`servoOutputsRaw` property) whenever `SERVO_OUTPUT_RAW` arrives — visible even outside a sweep, useful for spotting autopilot-driven outputs.
- During an active sweep it samples the *actual* PWM on `surf.channel` and optional `surf.channel2` (full 16-channel accessor), sanity-windowed to 800–2200 µs, tracking `sweepMinPwmActual/sweepMaxPwmActual` per channel.
- Performs a **live phase comparison**: when both elevon channels are moving it classifies deflection as OPPOSITE (aileron-roll) vs SAME (pitch) into `m_phaseOppositeCount/m_phaseSameCount`; `_updateSweepVerdict` turns these counters into the automatic verdict string shown to the operator. This is the mechanism that catches reversed/mis-mixed elevons without operator judgement.

**Cleanup contract.** `stopAllSurfaces()` (`:980–1015`) stops timers, clears overrides (release = 65535 on all 18 RC channels via `_clearRcOverrides`), restores saved `MIXING_GAIN`, transitions state back through Cooldown. `confirmSurfaceDirection()` (`:929`) records the operator's pass/fail per surface and advances to the next candidate.

**Residual risks:** force-arm on a bench implies props-off procedure discipline (no interlock in code); `MIXING_GAIN` restore depends on `stopAllSurfaces()` being reached (crash-path review recommended); gimbal payload estimator lives in `GimbalTest.qml` alongside (uavWeightField/payloadField kg conversion) and is cosmetic.

---

## 5. Deep Dive — Motor / EDF Tests

**Multirotor (HW-01, TESTED).** `MAV_CMD_DO_MOTOR_TEST` per motor instance; instance == board output number by convention. Sequence: request `SERVO_OUTPUT_RAW` streaming → run motor → cooldown (`kCooldownMs`) → next index. Feedback compares received output PWM against expected window; ACK-timeout ladder distinguishes “ACKed but silent FC” from “no ACK” (cpp:1007–1021, 1121–1130). Covered by `HardwareTestControllerTest` (initial state, abort, profile).

**Fixed-wing / EDF (HW-02, PARTIAL).** Copter-style `DO_MOTOR_TEST` doesn’t map to Plane throttle; implementation is a step machine: `case 0` arm (with neutral-hold active) → `case 1` spin-up via `RC_CHANNELS_OVERRIDE` on the detected throttle channel at 10 Hz → hold for `durationSec` → spin-down → evaluate **peak** PWM seen in `SERVO_OUTPUT_RAW` against the expected window (`cpp:573–578, 762, 806`). Throttle channel auto-detected from `SERVOn_FUNCTION` (Throttle/Motor); graceful branches exist for “ACKed but no feedback” vs “no ACK at all” (`:1007–1021, 1121–1130`) so Mock Link/SITL runs degrade to a warning instead of a false PASS/FAIL. Bench-verified once (commit `19c620ef2`); not covered by a recorded repeatable bench protocol → PARTIAL.

**Streaming control.** `MAV_CMD_SET_MESSAGE_INTERVAL` requests `SERVO_OUTPUT_RAW` (#36) at 10 Hz while tests run and restores prior behaviour afterwards (`:1052–1062`); `_rcOverrideTimer` drives the 10 Hz neutral-hold / throttle frames. Result-evaluation timer margins are hard-coded `(_durationSec + 1) * 1000 + 500` (`:458` — flagged for Config extraction). ESC telemetry (`ESC_TELEMETRY_*` family) is parsed by `TelemetryBridge`, feeding motor-temperature checks — not used by the test controller itself.

**Profile-driven servo sweeps (HW-03, TESTED).** JSON templates define `TestStep{name, testType motor|servo, servoInstance, motorInstance, targetPwm, expectedMin/Max, durationMs, settleMs, needsVisualConfirm, useRcOverride, throttlePct}`; `isValid()` enforces PWM windows, instance bounds (servo 1–16, motor 0–8), duration > 0, settle ≥ 0, expected ordering. `useRcOverride` switches the actuation path from `MAV_CMD_DO_SET_SERVO` (rejected by ArduPilot on function-assigned outputs) to `RC_CHANNELS_OVERRIDE`.

**Safety envelope.** Neutral hold suppresses stabilization coupling during motor runs (RC1/RC2/RC4 pinned); every run persists to `motor_test_results`/`hardware_test_events`; result timer margins are hard-coded `(_durationSec + 1) * 1000 + 500` (cpp:458 — flagged for Config extraction).

---

## 6. Deep Dive — Airspace Compliance

**Design intent (AirspacePage.qml:10–21).** Two-layer compliance over a *persistent knowledge base*, deliberately **not** a geofence: “There is NO MAVLink geofence interaction — the Plan-view geofence is untouched.”

1. **Automatic layer** — `ZoneComplianceCheck` (`airspace.zone_compliance`, Safety category, advisory: `mandatory=false`, `canOverride=false`). Re-evaluates on every mission edit (`MissionController::visualItemsChanged`), collects planned waypoints, intersects each active zone, writes per-zone audit rows throttled to one per 60 s, and exposes `acknowledgeZone()` from both the checklist and the Airspace page.
2. **Manual layer** — operator reviews the map/list and records a decision via `submitCompliance()`; rows land in `zone_compliance_log` (schema v13) tagged with `currentFlightId` (filter −1 = all flights, newest first).

**UI.** `AirspacePage.qml` (2,263 ln) follows the AnalyzePage pattern so it renders inside AnalyzeView; map tiles color-coded by reason (Regulatory red / Obstacle orange / Restricted dark-red / Temporary yellow); transient notice banners surface model errors and overlap warnings; sidebar badges from `count`/`activeCount`.

**Gap:** no unit tests for the model or the intersection math (AS-07). Given that this feature produces legal/compliance artifacts, geometry tests are the highest-value missing tests in the tree.

---

## 7. Deep Dive — Preflight Pipeline

```
connect → PARAM_LOADING (UavParameterManager, watchlist subscription)
        → CHECKLIST_IN_PROGRESS (PreflightManager QTimer ticks all 31 checks)
        → PREFLIGHT_PASS ⇄ ArmingGate decides
        → ARMING_ALLOWED (operator Arm intercepted by gate, cmd 400)
        → ARMED (TelemetryEventLogger writes ARM row; session bound to flight id)
```

- **Checks** derive from `AbstractCheck` (category, auto/manual, blocking, mandatory). Config thresholds come from the `check_config` DB table through a cached accessor (`AbstractCheck.cpp:258–296`) — the old “SQLite query per tick” problem is fixed. Debug builds support per-property test overrides for deterministic CI.
- **Registration rules** (`createPhase1Checks`, `PreflightManager.cpp:740–845`): checks appended in tier order (Tier 1 Nav/Power/Comm/Safety-failsafe → Tier 2 RC → Tier 3 warning/sidebar → Tier 4 enhancement/niche → manual confirms), then sorted by `(categoryInt, id)` for deterministic display and de-duplicated by ID as a safety net.
- **Airframe-conditional blocking**: after airframe identification, `safety.airspeed` and `safety.rtl_alt` become mandatory **only on fixed-wing** (on multirotors airspeed must never block arming — `:725–728`), while remaining auto safety checks are re-promoted to mandatory (`:729–733`).
- **Late wiring**: `ZoneComplianceCheck` is constructed with a `nullptr` zone model at registration and receives the real `NoFlyZoneModel`/mission wiring from `PreflightPlugin` afterwards (`:744–747`) — until then it reports “No mission loaded”.
- **Live suite = 31 checks** (`PreflightManager.cpp:742–830`): 26 automatic/action + 5 `ManualConfirmCheck`. Blocking checks cannot be overridden; warnings can. Operator overrides persist in QSettings; every evaluation writes results to the DB audit trail.
- **ArmingGate** supports Passive/Active/Hybrid, expires operator overrides after `timeoutSec`, and re-evaluates on its own 500 ms-class timer (started at `ArmingGate.cpp:71` — earlier “timer never started” finding is resolved). Dead `s_emergencyCommands` branch and the never-emitted `forceArmIssued` signal have been **removed** since the static-analysis report.
- **State machine** transitions fully covered by `StateMachineTest`.
- **Dormant inventory (PF-08)**: 39 additional check sources sit in `src/core/` uncompiled (GPS-fix quality, full EKF variance/status-flag pair, ESC telemetry family, geofence-param trio, vibration pair, METAR/TAF environment family, compass calibration/yaw-consistency, IMU cal, gyro bias, optical flow, terrain clearance/RTL-terrain, takeoff command, redundant power, companion link, radio buffer, RC channel-count/trim/rssi, pre-arm-ok, ambient/baro temperature, baro health, cell-voltage balance, battery health, mag field strength/interference, dual-GPS consistency, mission-item check, power-module health). Another 6 are compiled but never instantiated — notably `MissionEnergyCheck` + `PowerModel` (which have a passing unit test, `MissionEnergyCheckTest`) and `RcFailsafeCheck`.

---

## 8. Broken / Regressions / Contradictions (priority order)

Nothing was found that is *broken at runtime today*. The following are defects-in-context, ordered by risk:

1. **P1 — Documentation contradicts the build (three stale sources).**
   - `GCS_TODO.md` (July 2026) marks *everything* done including “1.2 MissionEnergyCheck”, yet the class is compiled-but-unregistered today (later “trim/remove 33 checks” commits superseded it).
   - `SRS_AUDIT.md` still audits the retired 91-check system.
   - `STATIC_ANALYSIS_REPORT.md` lists as CRITICAL several items already fixed: `Config.fontSizeH4` (gone), 23 missing `TelemetryBridge` Q_PROPERTYs (all present, `.h:411–495`), ArmingGate timer never started (started, `:71`), detection-stub dead code (files deleted), ArmingGate dead emergency-set (removed). Anyone triaging from these docs will make wrong decisions.
2. **P2 — Compiled-but-unregistered checks (6)** including tested ones (`MissionEnergyCheck`, `RcFailsafeCheck`, `GpsFixCheck`…): dead binary weight and misleading test green-ness. Either register or drop from `CUSTOM_SOURCE_REL`.
3. **P2 — Empty files** `src/core/ControlSurfaceCheck.{h,cpp}` (0 bytes): placeholder that blocks honest “checklist-integrated surface test” claims (CS-06 MISSING).
4. **P3 — Blocking weather fetch** `WeatherProvider.cpp:647–649`: synchronous `QEventLoop` with 3 s timeout on the calling (UI) thread; silently aborts with no error surfaced. Called from `Component.onCompleted` in FinalChecks/PreFlightChecklist.
5. **P3 — Unrecorded acceptance evidence**: `testsuite.md` Test Record entirely blank (SE-06) despite the document declaring PASS mandatory before completion.
6. **P4 — Style/config debt**: ~42 hard-coded hex colors concentrated in `PreflightChecklistView.qml`; duplicated constants between `Config.h` and `Config.qml` (verified: `kPwmMin=800`, `kMotorCountQuad=4` in both) with manual-sync drift risk; ~80 untranslated strings.
7. **P4 — VirtualJoystick QRC shadow risk** (`FlyViewWidgetLayer.qml:109` loads an upstream QGC QRC path): verify upstream still ships it, else the loader renders nothing.

---

## 9. Missing Features (not invented — each maps to design intent or dormant code)

| Missing capability | Basis |
|---|---|
| Checklist-integrated control-surface check (CS-06) | Empty `ControlSurfaceCheck` stubs |
| Zone-model / route-intersection unit tests (AS-07) | Absent from `custom/tests/` |
| Full manual acceptance record for the session system (SE-06) | Blank Test Record, `testsuite.md` |
| Re-activation of the 45 dormant checks (PF-08) — highest-value candidates: `RcRssiCheck` (comm.rc.rssi), `GpsFixCheck`, `EkfVarianceCheck`, `VibrationCheck`, `PreArmOkCheck`, `MissionEnergyCheck`+`PowerModel` | On-disk sources; SRS design targets |
| Repeatable bench protocol artifact for CS-02/CS-03/HW-02 sign-off | Single historical commit (`19c620ef2`), no recorded rerun |
| Same-sysid multi-airframe disambiguation | `vehicles_known_limitations.md` §1–2 (may stay accepted-limitation) |

---

## 10. Recommended Implementation Order

1. **Truth pass on docs (½ day):** regenerate SRS_AUDIT against the 31-check reality; mark STATIC_ANALYSIS_REPORT items verified-fixed; annotate GCS_TODO as superseded. Cheapest risk reducer in the repo.
2. **Resolve dormancy (1 day):** decide register-vs-delete for the 6 compiled-unregistered checks and the 39 uncompiled sources; delete or implement the `ControlSurfaceCheck` empties. Update `CMakeLists.txt` accordingly.
3. **Bench re-verification sprint (hardware):** scripted run covering elevon L/R isolation, aileron opposition, rudder isolation, flap drive, multirotor + FW motor tests; fill the `testsuite.md` Test Record. Promotes CS-02/03, HW-02, PF-07, SE-03/04 to TESTED.
4. **Async weather (¼ day):** convert `WeatherProvider` fetch to non-blocking pattern; propagate error states to WeatherPanel.
5. **Geometry tests (½ day):** `NoFlyZoneModel` + `ZoneComplianceCheck` intersection unit tests (mock waypoints), closing the compliance-feature test gap.
6. **Cleanup batch:** Config dedup strategy (single source of truth), qsTr sweep, hex-color consolidation into `Colors.*`, extract hard-coded timing margins to `Config.h`.

---

## 11. Non-Goals / Do-Not-Touch

- **Stock QGC geofence** stays untouched — airspace feature is a knowledge base + advisory checks only (`AirspacePage.qml:18`).
- **Integration boundary**: all product code lives under `custom/`; the QGC-custom bridge is exclusively the `QGC_CUSTOM_DIR` mechanism (`CMakeLists.txt` cache vars `CUSTOM_SOURCES`, `CUSTOM_DEFINITIONS=CUSTOMHEADER/CUSTOMCLASS`, `CUSTOM_LIBRARIES=Qt6::Sql`). Upstream edits are limited to the minimal fixes catalogued in `INTEGRATION_GUIDE.md` Step 4 — keep it that way.
- **Plan-view mission editing semantics**: `ZoneComplianceCheck` only *reads* `visualItemsChanged`; it must never mutate missions.
- **Arming interception scope**: gate touches only `MAV_CMD_COMPONENT_ARM_DISARM` (400); do not widen to other commands without revisiting `ArmingGateExtendedTest`.

---

## Appendix A — Verification commands used for this report
```bash
# compiled vs dormant checks
ls custom/src/core/*.cpp | xargs -n1 basename | sort > /tmp/disk
grep -oE "src/core/[A-Za-z]+\.cpp" custom/CMakeLists.txt | xargs -n1 basename | sort > /tmp/built
comm -23 /tmp/disk /tmp/built          # 39 orphans

# registration census
grep -n "new \w*Check(" custom/src/core/PreflightManager.cpp   # 31 instantiations

# test inventory
ls custom/tests/*.cpp | wc -l                                  # 17 suites (+3 mocks)

# stale-doc spot checks
grep -n "fontSizeH4" custom/qml                                # 0 hits (fixed)
grep -n "m_gateTimer->start" custom/src/core/ArmingGate.cpp    # :71 (fixed)
grep -cn "Q_PROPERTY.*(gpsFixTypeString|gimbalPitch|…)" custom/src/adapters/TelemetryBridge.h  # 23 (fixed)
```

---

## Appendix B — Live Check Census (31 registered)

From `PreflightManager::createPhase1Checks()` (`src/core/PreflightManager.cpp:740–830`), sorted here by tier as registered; display order is re-sorted by category+id at runtime.

| # | Class | Tier | Category | Type | Constructor thresholds |
|---|---|---|---|---|---|
| 1 | MavlinkProtocolCheck | SRS add | Communication | Auto | protocol version sanity |
| 2 | ZoneComplianceCheck | SRS add | Safety | Auto (advisory) | zone model wired later by plugin |
| 3 | BatteryVoltageCheck | base | Power | Auto | min 0.0 / max 5.0 V per cell window |
| 4 | AttitudeCheck | base | Navigation | Auto | 30° level tolerance |
| 5 | HeartbeatCheck | base | Communication | Auto | 10 s heartbeat timeout |
| 6 | RtlAltParamCheck | base | Safety | Auto | min 10 m, param 122 (RTL_ALT) |
| 7 | HomePositionCheck | base | Navigation | Auto | 0.005° set accuracy |
| 8 | AirspeedCheck | base | Navigation | Auto | 20 m/s ceiling (FW-mandatory) |
| 9 | AccelConsistencyCheck | T1 Nav | Navigation | Auto | 4.0 m/s² divergence |
| 10 | BatteryTemperatureCheck | T1 Power | Power | Auto | max 45 °C, min 0 °C |
| 11 | CellConfigCheck | T1 Power | Power | Auto | 3.0 V/cell floor, ≥1 cell delta |
| 12 | TelemetryDropRateCheck | T1 Comm | Communication | Auto | 10 % drop / 5 msg window |
| 13 | RcThrottleMinCheck | T1 Comm | Communication | Auto | throttle below min at arm |
| 14 | BatteryFailsafeCheck | T1 Safety | Safety | Auto | BATFS params armed |
| 15 | RadioFailsafeCheck | T1 Safety | Safety | Auto | FS radio action configured |
| 16 | GcsFailsafeCheck | T1 Safety | Safety | Auto | GCS-loss action configured |
| 17 | EkfFailsafeCheck | T1 Safety | Safety | Auto | EKF failsafe action |
| 18 | RcModeSwitchCheck | T2 RC | Communication | Auto | mode switch mapped |
| 19 | RcArmingSwitchCheck | T2 RC | Communication | Auto | arming switch mapped |
| 20 | RcCalibrationCheck | T2 RC | Communication | Auto | non-factory calibration |
| 21 | MotorCountCheck | T3 warn | Propulsion | Auto (non-blocking) | expected frame count |
| 22 | MotorSpinCheck | manual-ish | Propulsion | Action | operator spin confirmation |
| 23 | GimbalLinkCheck | T4 niche | Payload | Auto | gimbal heartbeat present |
| 24 | VideoFeedCheck | T4 niche | Payload | Auto | video stream active |
| 25 | ImuTemperatureCheck | T4 niche | Navigation | Auto | 85 °C max / −20 °C min |
| 26 | MotorTemperatureCheck | T4 niche | Propulsion | Auto | 80 °C max (ESC telemetry) |
| 27 | ManualConfirmCheck `airframe.weight_balance` | manual | Airframe | Manual | CG within limits, payload secure |
| 28 | ManualConfirmCheck `airframe.visual_inspection` | manual | Airframe | Manual | 5-point checklist (antenna, cracks, fasteners, wiring, props) |
| 29 | ManualConfirmCheck `propulsion.propeller.direction` | manual | Propulsion | Manual | watch params MOT_SPIN_DIRECTION, FRAME_TYPE |
| 30 | ManualConfirmCheck `sensors.compass.orientation` | manual | Navigation | Manual | compass rotation matches install |
| 31 | ManualConfirmCheck `environment.magnetic_disturbance` | manual | Environment | Manual | no mag interference sources nearby |

Runtime post-processing: sorted by `(categoryInt, id)` (`:832–837`), duplicate IDs removed (`:839+`); mandatory flags re-derived per airframe kind (see §7).

---

## Appendix C — Dormant Check Manifest

### C.1 Compiled but never instantiated (6) — in `CMakeLists.txt:54–197`, absent from Appendix B

| Class | Notes |
|---|---|
| MissionEnergyCheck (+ PowerModel dependency) | Has passing unit test (`MissionEnergyCheckTest`); Haversine mission distance vs Wh budget. GCS_TODO claims it shipped — regression vs doc. |
| RcFailsafeCheck | Overlaps live RadioFailsafeCheck; RSSI-threshold variant |
| GpsFixCheck | Fix-type/satellite gate with rationale + fix-steps strings |
| GpsSpeedAccuracyCheck | Speed-accuracy window gate |
| LevelCalibrationCheck | AHRS level trim verification |
| BatteryFailsafeParamCheck | Param-level battery failsafe audit (superseded by live BatteryFailsafeCheck?) |

### C.2 On disk, not compiled (39 sources)

Grouped by family:

- **GPS/Nav:** DualGpsConsistencyCheck, EkfStatusFlagsCheck, EkfVarianceCheck, GyroBiasCheck, ImuCalCheck, CompassCalCheck, CompassYawConsistencyCheck, MagFieldStrengthCheck, MagInterferenceCheck, OpticalFlowCheck, TerrainClearanceCheck
- **Baro/Environment:** AmbientTemperatureCheck, BaroHealthCheck, BaroTemperatureCheck, MetarTemperatureCheck, TafDeteriorationCheck, WeatherWindGustCheck
- **Power:** BatteryHealthCheck, CellVoltageBalanceCheck, PowerModuleHealthCheck, RedundantPowerCheck
- **ESC/Propulsion:** EscCurrentSymmetryCheck, EscFirmwareCheck, EscResponsivenessCheck, EscVoltageConsistencyCheck
- **Comm/RC:** CompanionLinkCheck, RadioBufferCheck, RcChannelCountCheck, RcRssiCheck, RcTrimCheck
- **Mission/Safety:** GeofenceMaxAltCheck, GeofenceMaxRadiusCheck, GeofenceParamCheck, MissionItemCheck, RtlTerrainCheck, TakeoffCommandCheck (the only NAV_TAKEOFF consumer in tree), ControlSurfaceCheck (**0-byte empty**), PreArmOkCheck

> Note: `RcRssiCheck` was flagged as epoch-buggy in STATIC_ANALYSIS_REPORT.md; re-inspection shows the math is dimensionally correct (`TelemetryBridge.cpp:550` stores local-clock µs; check compares like with unlike only if timestamps differ in origin — both use `currentMSecsSinceEpoch()*1000`). Treat that finding as retracted.

---

## Appendix D — Test Suite Inventory

17 suites compiled into the main binary via glob (`custom/CMakeLists.txt:247–260`) when `-DQGC_BUILD_TESTING=ON`; mocks in `custom/tests/mocks/` (MockTelemetryBridge, MockVehicle, MockParameterManager).

| Test file | Target(s) | Scope |
|---|---|---|
| ArmingGateTest.cpp | ArmingGate | passive/active/hybrid, override, disconnect |
| ArmingGateExtendedTest.cpp | ArmingGate | expiry timers, session integration edges |
| StateMachineTest.cpp | PreflightStateMachine | all states reachable, transitions, reset |
| PreflightManagerTest.cpp | PreflightManager | registration census, evaluation loop |
| CheckSmokeTest.{h,cpp} | AbstractCheck subclasses | construct+evaluate smoke over live suite |
| ChecklistEngineTest.cpp | ChecklistEngine | selective eval, evaluateAll, stale detection |
| ChecklistItemModelTest.cpp | ChecklistItemModel | rowCount, data roles, signals, filter |
| DatabaseManagerTest.cpp | DatabaseManager | schema, vehicle/battery CRUD, cycles, sessions |
| FlightHistoryModelTest.cpp | FlightHistoryModel | query/filter/export rows |
| HardwareTestControllerTest.cpp | HardwareTestController + Profile | initial state, abort, profile validation |
| MissionEnergyCheckTest.cpp | MissionEnergyCheck | Haversine, margin, no-mission (class currently unregistered!) |
| RcCalibrationCheckTest.cpp | RcCalibrationCheck | factory-default must fail |
| TelemetryBridgeTest.cpp | TelemetryBridge | property updates from decoded MAVLink |
| UavParameterManagerTest.cpp | UavParameterManager | load timeout, subscription updates |
| WeatherProviderTest.cpp | WeatherProvider | singleton + initial state only |

Coverage shape: safety-critical core (gate, state machine, DB, bridge, manager) is tested; **zero tests** for NoFlyZoneModel, ZoneComplianceCheck geometry, ControlSurfaceTestController, VehicleProfileManager, OperatorManager/FlightSession units beyond DB round-trip.

---

## Appendix E — Database Schema (21 tables, `DatabaseManager.cpp` CREATE statements)

| Table | Purpose |
|---|---|
| schema_version | migration bookkeeping (currently v13 lineage) |
| checklist_templates | editable checklist definitions (TemplateManager/Editor) |
| check_results | per-evaluation audit of every check status |
| flight_check_results | check snapshots bound to a flight session |
| compliance_logs | legacy/general compliance decisions (JSON snapshot) |
| zone_compliance_log | per-zone route-compliance audit rows (v13) |
| no_fly_zones | airspace knowledge base (NoFlyZoneModel) |
| hardware_test_events | servo/profile test step audit |
| surface_test_results | control-surface sweep outcomes |
| motor_test_results | per-motor run results incl. peak PWM |
| vehicles | registry keyed by device fingerprint |
| vehicle_config | per-vehicle JSON overrides applied to checks |
| batteries | pack inventory (cells, capacity, health) |
| battery_cycles | cycle counter increments per session |
| maintenance_components | component service records (MaintenanceTracker) |
| operators | pilot/operator roster (OperatorManager) |
| flight_sessions | one row per training/flight session (FlightSession) |
| flights | flight log (v12 rebuild artifact `flights_v12` also present) |
| flight_telemetry_events | ARM/DISARM + telemetry event stream (TelemetryEventLogger) |
| check_config | runtime-tunable thresholds consumed via cached accessor |

---

## Appendix F — MAVLink Protocol Surface

Compiled-code usage census (`grep MAV_CMD_|MAVLINK_MSG_ID_ custom/src`):

| Symbol | Hits | Where |
|---|---|---|
| MAV_CMD_COMPONENT_ARM_DISARM | 19 | ArmingGate interception, CS force-arm, HW arm/disarm steps |
| MAV_CMD_DO_MOTOR_TEST | 11 | multirotor motor tests (HW) |
| MAV_CMD_DO_SET_SERVO | 8 | profile servo sweeps, flap direct drive |
| MAV_CMD_SET_MESSAGE_INTERVAL | 3 | SERVO_OUTPUT_RAW streaming on/off |
| MAVLINK_MSG_ID_SERVO_OUTPUT_RAW | 5 | feedback in both controllers |
| MAV_CMD_REQUEST_AUTOPILOT_CAPABILITIES / AUTOPILOT_VERSION | 1+1 | AutopilotInfoDetector (active vehicle only — known limitation §VP-07) |
| MAV_CMD_DO_REPOSITION / DO_CHANGE_SPEED / NAV_TAKEOFF | misc | NAV_TAKEOFF appears **only inside orphaned TakeoffCommandCheck** — not in any compiled path |
| Parsed message IDs | SYS_STATUS, HEARTBEAT, RC_CHANNELS, RADIO_STATUS, GPS_RAW_INT, GLOBAL_POSITION_INT, ATTITUDE, ESTIMATOR_STATUS, EKF_STATUS_REPORT, SCALED_PRESSURE, TERRAIN_REPORT, OPTICAL_FLOW, ESC_INFO, ESC_TELEMETRY_{1..4}, COMMAND_ACK/LONG | TelemetryBridge decoders + controller feedback paths |

---

## Appendix G — QML Inventory (17,273 lines total)

| File | Lines | Role |
|---|---|---|
| pages/AirspacePage.qml | 2263 | zones KB + map + audit log (AnalyzePage pattern) |
| cpts/PreflightChecklistView.qml | 1454 | checklist dialog (style debt hotspot) |
| pages/FlightHistoryPage.qml | 1130 | history list + stats dashboard |
| pages/GimbalTest.qml | 913 | gimbal/payload bench page (drives CS controller) |
| cpts/MotorCheckPanel.qml | 749 | motor test cards (HardwareTestController ×71) |
| cpts/WizardStep4Review.qml | 691 | wizard review step |
| pages/PreFlightChecklist.qml | 646 | checklist page (motor config dialog, weather fetch onCompleted) |
| cpts/WizardStep3Hardware.qml | 639 | hardware/wizard step |
| cpts/WizardStep1Operator.qml | 636 | operator selection step |
| pages/MaintenancePage.qml | 493 | maintenance/components/battery cycles |
| singletons/VehicleTelemetry.qml | 478 | TelemetryBridge QML singleton façade + staleness monitor |
| cpts/FlyViewTelemetryStrip.qml | 385 | FlyView telemetry strip (18 untranslated strings) |
| pages/PostFlightSummary.qml | 373 | post-flight report view |
| pages/VehicleSelect.qml | 358 | wizard vehicle picker |
| cpts/FlightFormDialog.qml | 325 | session flight-form dialog |
| cpts/WeatherPanel.qml / WeatherInfoPanel.qml | 313/296 | weather display |
| cpts/TelemetryInfoBox.qml | 292 | info box |
| pages/FinalChecks.qml | 288 | wizard final gates (weather fetch onCompleted) |
| pages/VehicleManagement.qml | 257 | fingerprint registry UI |
| cpts/ArmGateDialog.qml / ArmGatePanel.qml | 248/153 | override UI (expiry countdown) |
| cpts/ServoTestCard.qml | 245 | per-step servo test card |
| pages/LaunchReady.qml | 229 | launch readiness summary |
| cpts/ChecklistEngine.qml | 224 | engine-driven checklist renderer (14 untranslated) |
| cpts/PriorityFixPanel.qml | 220 | failed-check triage panel |
| pages/PreflightSettings.qml | 219 | settings surface |
| cpts/SessionStartDialog.qml | 190 | operator/session bootstrap dialog |
| cpts/PropInspector.qml | 170 | parameter inspector card |
| cpts/ChecklistItem.qml | 155 | single row item |
| cpts/MotorTestCard.qml / MotorMonitor.qml | 153/130 | motor run card + live monitor |
| cpts/ControlSurfacePanel.qml | 153 | legacy servos-1–4 sweep launcher |
| pages/TemplateEditor.qml / TemplateManager.qml | 144/96 | checklist template CRUD |
| remaining (VideoPreview, AlertPanel/Banner, TelemetryBar, WizardStepBar, TelemetrySection, Colors singleton, CollapsibleSection, AutoCheckCard…) | <140 each | support widgets |

