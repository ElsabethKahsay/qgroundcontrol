## Part A — Prompt for the agent (analysis first, then test cases)

```markdown
# Deep GCS reliability analysis + test case suite (pre–real-quad)

## Context
Skywin GCS = QGroundControl + `custom/`. Hand-held checklist will be used by the developer before any real quad-with-payload flight. You are not writing a client handover doc; you are producing an internal verification pack so the tree is clean, consistent, and testable.

## Phase 1 — Deep analysis (do this before writing tests)
Explore `custom/` (and only the QGC touchpoints it depends on). Produce a precise and detailed analysis report covering:

1. **Module map** — preflight, arming gate, battery/live time, Fly quick actions, status indicator, control surfaces, motors/EDF, airspace/compliance, vehicle profile/DB, telemetry bridge.
2. **Data flow** — Vehicle/MAVLink → TelemetryBridge / Facts → UI; DB/profile → estimators; who owns armed/mode/SOC.
3. **Risk list** — race conditions, binding bugs, force-arm paths, SITL vs hardware assumptions, hardcoded values, dead UI, dual sources of truth (e.g. two arm flags, two capacity numbers).
4. **Code health** — dead code, TODOs, duplicated math, magic numbers, missing clamps, logging noise, schema versions, qmldir/qrc registration gaps.
5. **Testability gaps** — pure functions missing, logic only in QML, no hooks for SITL.

Do not invent features that are not in the tree. Cite paths/symbols.

## Phase 2 — Test cases
From the analysis, write concrete test cases in the structure below. Prefer SITL/ArduPilot first; mark Hardware-only where required. Each case: ID, preconditions, steps, expected result, severity (Blocker/Major/Minor).

## Output
1. `ANALYSIS.md` — Phase 1 findings
2. `TEST_CHECKLIST.md` — full checklist + test cases for the developer
```

---

## Part B — Your checklist (use this; expand after agent analysis)

### 0. Preconditions (before any test)

- [ ] Clean build (Debug + Release) of current tree; zero compile errors
- [ ] `custom/` only changes reviewed (no accidental stock QGC drive-by)
- [ ] DB migrates on fresh install and on upgrade from last schema
- [ ] SITL Copter (and VTOL if you ship MR(vtol)) connects to Skywin
- [ ] MAVLink Inspector: HEARTBEAT, `BATTERY_STATUS`, mode, armed visible
- [ ] Known-good params: `BATT_MONITOR`, `BATT_CAPACITY`, stream rates (`SRx_EXTRA3`)
- [ ] Record software version / git hash on every run log

---

### 1. Code cleanliness & engineering practice

- [ ] No dead QML/C++ referenced in qrc/qmldir
- [ ] No duplicate sources of truth (capacity, armed, mode, SOC)
- [ ] Magic numbers named (reserve 0.2, current gate 3 A, force-arm 21196)
- [ ] Battery math in one testable helper (or single clearly owned module)
- [ ] Force Arm / Disarm / setFlightMode go through `Vehicle` APIs where possible
- [ ] UI never assigns over a property binding in a way that kills updates
- [ ] Logs: useful `qCDebug`/`qCWarning` categories; no spam every 50 ms
- [ ] Schema version bumped with idempotent migrations; defaults safe
- [ ] Secrets/paths not hardcoded to one machine
- [ ] Feature flags / vehicle-type guards (FW vs MR vs VTOL) consistent

---

### 2. Connect / disconnect / multi-vehicle resilience

| ID | Case | Expected |
| ---- | ------ | ---------- |
| C1 | Launch app, no vehicle | Disconnected status; no crash; actions disabled |
| C2 | Connect SITL | Active vehicle; telemetry paints within few seconds |
| C3 | Disconnect | UI clears/stales; no stale “Live” battery forever |
| C4 | Reconnect | Profile/DB reload; chip and status recover |
| C5 | Kill SITL mid-flight UI | Comm lost / stale handling; no crash |
| C6 | Rapid connect/disconnect ×10 | No leak crash; DB ok |

---

### 3. Main status indicator (Armed / Flying / Not Ready)

| ID | Case | Expected |
| ---- | ------ | ---------- |
| S1 | Disarmed, checks failing | Not Ready (or equivalent) |
| S2 | Disarmed, ready | Ready To Fly |
| S3 | Force Arm on ground | **Armed** (never stuck Not Ready) |
| S4 | Takeoff SITL | **Flying** when `vehicle.flying` true |
| S5 | Land / disarm | Leaves Flying/Armed correctly |
| S6 | Arm via MAVLink console only | Toolbar tracks HEARTBEAT armed |
| S7 | Communication lost | Communication Lost |
| S8 | Debug once: label shows raw a/f/l | Matches Vehicle properties |

**Blocker if:** armed on FC and Disarm enabled but toolbar still Not Ready.

---

### 4. Arm / Disarm / Force Arm / mode

| ID | Case | Expected |
| ---- | ------ | ---------- |
| A1 | Normal Arm when allowed | Arms or clear reject |
| A2 | Force Arm when pre-arm blocks | Arms; status Armed; confirm dialog if designed |
| A3 | Disarm when armed | Disarms; UI enables Force Arm again |
| A4 | Mode toggle MANUAL ↔ AUTO | FC mode changes or clear FLTMODE warning |
| A5 | Mode label | Live `flightMode`, not stuck `--` |
| A6 | Takeoff button | Enabled only when allowed; sends guided takeoff |
| A7 | Return button | RTL / Return when allowed |
| A8 | Stop/cleanup after surface test | Does not leave vehicle armed unexpectedly |

---

### 5. Live battery % and remaining time

**Pack config:** set 1P×5000 mAh, reserve 20% → capacity **5.0 Ah**.

| ID | Case | Expected |
| ---- | ------ | ---------- |
| B1 | Disarmed, I &lt; gate | `SOC% · —` (not huge minutes) |
| B2 | Armed, I high, SOC known | `SOC% · ~N min` |
| B3 | Hand calc | remaining = cap×SOC/100; usable = rem×0.8; t = usable/I×60 |
| B4 | Example: SOC 83%, I 28.1, cap 5 | rem 4.15; usable 3.32; t ≈ 7 min |
| B5 | SOC drop | % tracks FC |
| B6 | Throttle up | minutes decrease |
| B7 | Throttle down | minutes increase |
| B8 | Stale telemetry (&gt;5 s) | Not “Live”; degraded display |
| B9 | Bat Config change P/mAh | Full capacity and detail box update immediately |
| B10 | Detail box | No hardcoded 4 Ah; S/P/mAh match profile |
| B11 | Voltage sanity | 6S config should not silently trust absurd V without warning (optional warn) |
| B12 | Missing SOC | `? · —` or equivalent; no fake % |

---

### 6. Preflight / checklists / arm gate

| ID | Case | Expected |
| ---- | ------ | ---------- |
| P1 | Open preflight | Checks load; counts match UI |
| P2 | Critical fail | Arm gated per design |
| P3 | Force Arm path | Documented bypass only; still sets Vehicle.armed |
| P4 | Operator name / confirm | Required fields behave |
| P5 | Checklist badge | Count/dot consistent with failed items |
| P6 | Post-flight banner | Does not freeze main status forever |

---

### 7. Fly UI chrome (quick actions / panels)

| ID | Case | Expected |
| ---- | ------ | ---------- |
| F1 | Flight time button | Opens same math as chip |
| F2 | FC status panel | Armed, mode, battery snapshot when connected |
| F3 | Events panel | Hidden by default; toggle; scroll; no crash |
| F4 | Bat Config | Persists; survives reconnect |
| F5 | Left rail contrast | Selected item readable (no invisible active state) |
| F6 | Takeoff/Return | Not permanently dead |

---

### 8. Control surfaces / motors (SITL or bench FC — not prop-on payload flight)

| ID | Case | Expected |
| ---- | ------ | ---------- |
| H1 | MR motor test path | Still works; no FW-only break |
| H2 | FW/elevon if in scope | MANUAL + arm; surfaces via override path |
| H3 | Stop all | Neutral + disarm cleanup |
| H4 | Mode fight | Warning if FLTMODE_CH overrides GCS |

Skip full prop spin on a real payload aircraft until this list is green on SITL/bench.

---

### 9. DB / profile / persistence

| ID | Case | Expected |
| ---- | ------ | ---------- |
| D1 | Fresh DB | Schema latest; defaults |
| D2 | Upgrade old DB | Migration; no data loss on battery columns |
| D3 | Vehicle pack config | Round-trip S/P/mAh/reserve |
| D4 | UAV/payload weight | Persist if feature claims it |
| D5 | Corrupt JSON thrust table | Safe fallback; no crash |

---

### 10. Telemetry / performance / stability

| ID | Case | Expected |
| ---- | ------ | ---------- |
| T1 | 30 min SITL session | No growing leak crash; UI responsive |
| T2 | Battery chip 1 Hz+ | No UI freeze |
| T3 | Inspector vs chip | SOC/current agree within smoothing lag |
| T4 | High rate EXTRA3 | Link remains usable |

---

### 11. Consistency / UX

- [ ] Same SOC on chip, detail, FC status
- [ ] Same armed state on status, Disarm, FC status
- [ ] Same mode on mode button and FLIGHT panel
- [ ] Error strings actionable (not empty fail)
- [ ] Disabled buttons have a reason (tooltip or status)

---

### 12. Security / safety hygiene (software)

- [ ] Force Arm always confirm or clearly dangerous styling
- [ ] No param write that sets `ARMING_CHECK=0` permanently as a “fix fix”
- [ ] Disarm available when armed
- [ ] Estimator never encourages flight on stale data (no “Live” when stale)

---

### 13. Pre–real-quad gate (payload aircraft)

**Do not fly payload quad until:**

- [ ] All **Blocker** cases pass on SITL
- [ ] Status Armed/Flying verified with raw `a/f` once
- [ ] Battery: idle gate + live minutes hand-checked
- [ ] Arm/Disarm/Force Arm/mode verified on **bench FC** (props off / safe)
- [ ] Takeoff/Return not dead in UI
- [ ] Known battery monitor calibration plan for real X6/pack
- [ ] Crew briefing: force arm is bench/emergency only

**Real flight still needs:** calibrated current, real hover current vs estimate, RC failsafe, geofence/ops rules — outside pure GCS code checklist.

---

### 14. How to run a session (suggested order)

1. Build + fresh DB
2. SITL connect + Inspector
3. Status + arm/disarm/mode
4. Battery config + live chip math
5. Fly panels (events, FC status)
6. Preflight gate
7. Long soak
8. Bench FC (props off)
9. Only then outdoor quad + payload

---

### 15. Log template (per session)

```text
Date / git hash / SITL or bench:
Blockers found:
Major:
Minor:
Status a/f observed:
Battery sample (SOC, I, cap, t_calc, t_ui):
Arm path used (UI / force / console):
Sign-off: ready for real quad? Y/N
```

---

**How to use this:** run **Part A** and write an analysis md with in the custom folder. include a table of how much each feature is implemented if there are missing implementations.

---

# Part C — Phase 1 deep analysis results (agent + field findings)

**How this works together with Part B:** every defect below is tagged with the checklist ID(s) in **Part B** that would have caught it. After the analysis there is an implementation-completeness table and a **Part D** section of new checklist rows to append to the Part B tables. SITL-finding line numbers are current as of this run; re-verify after any refactor.

## C.1 Module map (custom tree = ~127 files, ~31.5k lines)

| Area | Modules | Job |
|---|---|---|
| Plugin glue | `PreflightPlugin.cpp` (`src/`) | Owns all managers, QML context, MAVLink arm gating/interception, sessions |
| Preflight FSM | `PreflightStateMachine`, `PreflightManager` (`core/`) | Linear lifecycle Disconnected→…→ArmingAllowed; owns ~26 runtime checks; time-driven evaluation |
| Arming gate | `ArmingGate` (`core/`) | Enforces arm policy, operator overrides, force-arm; DB preflight status |
| Checklist | `ChecklistEngine`, `ChecklistItemModel` (`core/`) | Reactive telemetry→rows evaluator; item model for QML |
| Checks | ~74 `*Check` classes (`core/`) | Individual preflight verifications |
| Params | `UavParameterManager`, `ParameterWatchlist` (`core/`) | Param arrival tracking + ~58 watched params |
| Battery/live | `VehicleProfileManager` (`core/`), `PowerModel` | SOC%, live minutes, pack config, thrust→current, mission Wh/km |
| Adapters | `TelemetryBridge`, `QgcVehicleAdapter`, `QgcParameterAdapter` (`adapters/`) | MAVLink/Vehicle → Q_PROPERTY stream; QML wrappers |
| Controllers | `HardwareTestController`, `ControlSurfaceTestController`, `HardwareTestProfile` | Motor/servo/surface tests (MAV_CMD / RC override) |
| Managers | `FlightSession`, `OperatorManager`, `TelemetryEventLogger` (`managers/`) | Session FSM, operator CRUD, event audit + distance |
| Detection | `VehicleRegistry` (`detection/`) | HW-UID/SYSID fingerprinting |
| DB | `DatabaseManager` (`utils/`) | SQLite singleton, v1→v17 migrations, 15+ tables, CSV/JSON export |
| Utils | `MaintenanceTracker`, `PreflightSettingsManager`, `TemplateManager`, `WeatherProvider`, `AlertManager`, `AutopilotInfoDetector`, `ExportHelper`, `ClipboardHelper`, `Config.h`, `Hysteresis.h` | Cross-cutting services |
| Models | `NoFlyZoneModel`, `FlightHistoryModel`, `VehicleListModel` (`models/`) | QAbstractListModels for QML |
| Mission | `WaypointMath`, `WaypointMathHelper` (`mission/`) | Haversine distance/bearing/destination (pure) |
| UI | 77 QML files (`qml/`) + `QmlModuleInit.cpp`; `custom.qrc` etc. | All screens |

## C.2 Data flow & owners of truth

- **Telemetry:** `Vehicle` → `TelemetryBridge` (raw facts, ~15 msg types, ~120 props) → QML singletons (`VehicleTelemetry.qml` re-publishes with null guards) → chips/panels. Three parallel paths exist: `TelemetryBridge`, `VehicleTelemetry.qml`, `QgcVehicleAdapter` — authority is `TelemetryBridge`.
- **Armed — triplicated/quad-owner:** `Vehicle::armed()` (`TelemetryBridge.cpp:84`), `ArmingGate::vehicleArmed/Disarmed`, `FlightSession::SessionState::Armed` (`FlightSession.cpp:337`), and `PreflightPlugin::mavlinkMessage` manually mutates `vehicle->_updateArmed` (`PreflightPlugin.cpp:927`). **`PreflightStateMachine::Armed` is never reached** — no code calls `transitionTo(Armed)`; the FSM tops out at `ArmingAllowed`.
- **SOC — two divergent sources:** `batterySocPct` (live chip, `VehicleProfileManager.cpp:717`) vs `batteryPercent` (power-model/cycle path, `VehicleProfileManager.cpp:1072/1084/1096`).
- **Battery minutes (single source of truth, good):** `VehicleProfileManager::_updateLiveEstimate()` (`VehicleProfileManager.cpp:710-745`), 2 s timer, EMA α=0.095, armed+`I≥3A` gate, `Ah=SOC/100×cap×(1-reserve)`. Consumers: `BatteryStatusChip.qml:37-42`, `BatteryTimeEstimatorPanel.qml:32-33`. `FcStatusPanel.qml` reads raw TelemetryBridge (no time calc). `PowerModel` is mission-prediction only, not live.
- **Arm gating:** `TelemetryBridge::arm()` silently drops if gate closed (`TelemetryBridge.cpp:1103-1111`); `PreflightPlugin::mavlinkMessage` blocks incoming external COMMAND_LONG arm (`:907-913`).
- **DB→profile→estimators:** `VehicleProfileManager` resolves UID→upsert vehicle→load history/weight/pack/thrust table→`startFlightSession`→2 s live timer (`VehicleProfileManager.cpp:295-339`).

## C.3 Observed failures this session (root causes, SITL)

| Symptom | Root cause | Evidence / fix done |
|---|---|---|
| "5 planes" phantom icons + ADSB targets | Saved MockLink "stil" (type 4, `IncrementVehicleId=true`) auto-started with sysid 128; MockLink itself streams ADSB ids 3039-3042+. **Not real vehicles, no TCP socket.** | `Skywin GCS.ini` `[LinkConfigurations] Link0`; set `auto=false`, `IncrementVehicleId=false` (backup `/tmp/opencode/Skywin_GCS.ini.bak-*`) |
| Takeoff greyed / `NAV_TAKEOFF: FAILED` | Vector was **already airborne at ~5 m**, GUIDED+armed ⇒ ArduCopter `do_user_takeoff` refuses (`!land_complete` "can't takeoff again"), stock `GuidedActionsController.qml:151` hides Take Off while `vehicleFlying`. | Not a bug; `GUIDED→ARM→NAV_TAKEOFF 5→NAV_LAND` verified clean from landed/disarmed |
| Battery always 0% | SITL-side: `BATT_MONITOR=4`, `BATT_VOLT_PIN=13/12`, `SIM_BATT_VOLTAGE=25.2` (6S) vs 12.6 V measured (3S), `BATT_LOW/CRT_MAH=0`. stil reports rem=0, QGC faithful. | No change (user directive) |

## C.4 Risk register (severity-ordered, cross-ref to Part B)

| # | Sev | Finding | Evidence | Catches |
|---|---|---|---|---|
| R1 | HIGH | **PreflightPlugin construction-order bug:** `connect(_armingGate,…)` at :166-169 runs while `_armingGate` is still `nullptr` (created `:211`); `telemetryLogger->setDependencies(_, _armingGate, _checklistEngine)` `:163` passes nulls (`_checklistEngine` created `:233`). FlightSession↔gate signals and logger gate/engine hooks are **permanently dead**. | `PreflightPlugin.cpp:163-169, 211, 233` | B/P/S, F2 |
| R2 | HIGH | **~51 of 74 check classes never instantiated** — only 22 types constructed in `PreflightManager.cpp:743-813` (26 rows incl. 4× ManualConfirm). EkfFailsafe/BatteryHealth/Geofence/PreArm/ESC checks etc. never run ⇒ gate not exercising intended coverage. | `PreflightManager.cpp:743-813` + grep `new *Check` vs `class *Check` (74 files) | P2, S1 |
| R3 | HIGH | **Param watchlist never enabled:** `UavParameterManager::setWatchlist` never called (`m_watchlist` empty) ⇒ `notifyParamReceived` early-returns (`:69-70`), `isReady()` never true, `ParamLoading` FSM leg dead (only `parametersReadyChanged` conn saves it). | `UavParameterManager.cpp:47-174`, `PreflightManager.cpp:530-532, 160-168` | P1, P2 |
| R4 | HIGH | **Force-arm bypasses gate:** `VehicleProfileManager.cpp:613-626` `forceArm()`/`disarmVehicle` sends magic `2989`; `TelemetryBridge.cpp:1126-1136` same; `ArmingGate.cpp:248-261` `forceArm()` unconditionally opens; and **external** COMMAND_LONG `param2==21196||2989` auto-calls `_armingGate->forceArm()` (`PreflightPlugin.cpp:903-906`) — an unauthenticated external GCS can self-open the gate. | ArmingGate.cpp, TelemetryBridge.cpp, PreflightPlugin.cpp:903, VehicleProfileManager.cpp:613 | A2, S3, B1, item 12 |
| R5 | HIGH | **Unguarded `_activeVehicle` QML derefs** → TypeError on disconnect: `MainStatusIndicator.qml:103-104, 122-123, 142, 369, 374, 382, 393, 402`; `FlyViewWidgetLayer.qml:369-374`. | QML above | S7, C5, T1 |
| R6 | HIGH | **DB upsert passes `{}` empty strings** (`VehicleProfileManager.cpp:307-309` → `upsertVehicleEx` INSERT binds empty `friendly_name`/`motor_layout`). Vehicles table is `friendly_name TEXT NOT NULL DEFAULT ''` (`DatabaseManager.cpp:378`); observed runtime NOT NULL/FK failures block `startFlightSession` (`:431` FK on `device_uid`). Self-heal v14 exists but tests never cover empty-name insert. | `VehicleProfileManager.cpp:307`, `DatabaseManager.cpp:378, 431, 1619-1701` (+ self-heal `:1332-1350`) | D1-D5 |
| R7 | MED | **5 unresettable singletons** — `s_instance` set in ctor, never cleared ⇒ stale pointer if QML engine recreated / tests don't reset. `VehicleRegistry` (Q_APPLICATION_STATIC) is safe. | `FlightSession.cpp:12,22`, `OperatorManager.cpp:6,16`, `VehicleProfileManager.cpp:29,57`, `WeatherProvider.cpp:15,33`, `PreflightSettingsManager.cpp:3,14` | T1, T5 |
| R8 | MED | **Dead/ghost state-machine terminal:** `Armed` state defined but unreachable; `TransitionTo(Armed)` never called. Toolbar Armed comes only from `_updateArmed` mutation. | `PreflightStateMachine.cpp:35-89`, `PreflightPlugin.cpp:925-933` | S3-S6 |
| R9 | MED | **NAV/geofence-failsafe "disabled" detection uses exact-zero compare.** `qFuzzyCompare(param, 0.0)` is *not* always-false (Qt special-cases zero → true iff |param|<1e-12), but tolerance is ultra-tight: any non-zero residue fails the "disabled" path. Prefer `qFuzzyIsNull` clarity. | `EkfFailsafeCheck.cpp:32`, `GeofenceParamCheck.cpp:39`, `BatteryFailsafeCheck.cpp:45` | P2 |
| R10 | MED | **Duplicate logic (will drift):** identical 28-entry flight-mode QHash `HardwareTestController.cpp:28-56` vs `ControlSurfaceTestController.cpp:21-49`; haversine `TelemetryEventLogger.cpp:12-21` vs `DualGpsConsistencyCheck.cpp:19-29`; battery cell-count estimation ×3 sites; battery-capacity Wh ×2 sites. | files above | H1-H4, T1 |
| R11 | MED | **Magic numbers:** fallback 14.8 V (4S assumption) `MainStatusIndicator.qml:203`; `kMinGateA: 3.0` `BatteryStatusChip.qml:28`; airframe defaults/`avgSpeed 30`, `+5%/kg`, `2 min hover` `PowerModel.cpp:122,160-174`; force-arm magic `21196/2989` `TelemetryBridge.cpp:1136`; energy `pctUsed×voltage×0.45` `VehicleProfileManager.cpp:1103`. `Config.h` centralizes ~15 PWM/thresholds but they're compile-time only. | files above | B11, item 1 |
| R12 | MED | **Debug/log noise:** `console.log`×8 (debug `MARKER-20260804` left in `AnalyzeView.qml:137`); preflight debug dump every 5 s `PreflightManager.cpp:590-607`; `armingBlocked` emitted each tick `:652-654`; `checksForCategory` always qDebug `:234-235`. | QML/C++ above | item 1 |
| R13 | MED | **Timer lifecycle:** ~18 QML timers, only 1 with `Component.onDestruction` (`GimbalTest.qml:62`). E.g. `PostFlightSummary.qml:18`, `PreFlightChecklist.qml:87,115`, `FlyQuickActions.qml:273` start unconditionally. | QML files | T1, F3 |
| R14 | MED | **24 unguarded `Window.window.mainStackView.push/pop`** calls (checked only in `GimbalTest.qml:1173`, `VideoPreview.qml:99`). Safe today (always loaded via AnalyzeView loader); fragile. | e.g. `PayloadCheck.qml:45-51`, `LaunchReady.qml:164-188`, `FinalChecks.qml:262-274` | C1 |
| R15 | MED | **`NaN` reads pass checks:** `getTelemetryDouble` returns NaN on missing prop; many checks compare without guard ⇒ false "Pass" without data. | `AbstractCheck.cpp:195-256`; e.g. `GpsFixCheck.cpp:66`, `MotorCountCheck.cpp:51` | S1-S3, P2 |
| R16 | MED | **State-machine safety window:** `createPhase1Checks` sets `mandatory=false` (`PreflightManager.cpp:842-844`) and `updateCriticalChecks` early-returns pre-kind-resolution (`:713-714`) ⇒ `allMandatoryPassed()` trivially true until vehicle kind resolves ⇒ gate open before enforcement. | PreflightManager.cpp | S1, P2 |
| R17 | LOW | Hardcoded export/cache paths (`ExportHelper.cpp:25-28`, `WeatherProvider.cpp:27-30`); `AirspacePage.qml:746` default map center Addis Ababa `(9.03,38.74)`; 3 parallel telemetry access paths; naming inconsistencies (`_fwFlightModeNumber` vs `surfaceFlightModeNumber`). | files above | F5, T1 |

## C.5 Code health

- **TODO/FIXME/HACK:** none in `custom/src` or `custom/qml` (only `//??` marker at `PreflightManager.cpp:449`; "N-XXXXX" tail placeholder in `FinalChecks.qml:161` is intentional).
- **Strengths:** every header Doxygen-commented; signal-driven checklist engine (no polling); deadbanded telemetry (1e-7° pos, 0.1 m alt); clean C++/QML split (logic in C++ except noted); centralized `Config.qml`/`Colors.qml` tokens.
- **Gaps:** 3 QML files >1000 lines (`AirspacePage.qml` 2263, `PreflightChecklistView.qml` 1200, `GimbalTest.qml` 1200); interpolation from data tables fine but `PowerModel::fitLinear` unclamped (collinear X ⇒ /0 risk at `PowerModel.cpp:96`); `qt6_meta_fixes.h` has **no `#if QT_VERSION` guard** (fragile vs Qt internals); `Qt6LocationPrivateConfig.cmake` is a silent stub.

## C.6 Implementation-completeness table

| Feature | Status | Evidence | Missing / gaps |
|---|---|---|---|
| Preflight state machine | Partial | `PreflightStateMachine.cpp:35-89` | `Armed` terminal unreachable (R8); `ParamLoading` leg dead (R3) |
| Arming gate | Partial | `ArmingGate.cpp:25-359` | Gate↔FlightSession wiring dead (R1); 3 force-arm bypasses + external magic force-arm (R4); no unit tests |
| Check coverage | **Partial (~30%)** | 22/74 classes constructed (`PreflightManager.cpp:743-813`) | ~51 checks dead (R2); NaN pass-through (R15); mandatory=false window (R16) |
| Checklist engine | Complete-ish | `ChecklistEngine.cpp:20-307` | Reactive OK but only QML-reachable; never wired to gate/logger (R1) |
| Param watch | **Stub** | `UavParameterManager.cpp:47-174` | `setWatchlist` never called (R3) |
| Battery/live SOC + minutes | Complete | `VehicleProfileManager.cpp:710-745` | Dual SOC sources (R8); magic 0.45 (R11) |
| Mission power model | Complete (standalone) | `PowerModel.cpp:82-207` | Never fed to a check; `MissionEnergyCheck` unregistered (R2); `fitLinear` unclamped |
| Flight sessions / DB | Partial | `DatabaseManager.cpp`, `VehicleProfileManager.cpp:234-343` | empty-name upsert ⇒ NOT NULL/FK failures (R6); singleton reset (R7) |
| Pre-flight wizard / review | Complete | `PreFlightChecklist.qml`, `WizardStep*` | Debug `console.log` noise (R12) |
| Motor/servo/surface tests | Partial | `HardwareTestController.cpp`, `ControlSurfaceTestController.cpp` | Force-arm paths untested; duplicated mode parser (R10) |
| Weather / airspace / history | Complete | `WeatherProvider`, `NoFlyZoneModel`, `AirspacePage.qml`, `FlightHistoryPage.qml` | hardcoded map center (R17); tile/history logic in QML (JS, untested) |
| Mission math | Complete | `WaypointMath.cpp:13-68` | bearing/destination untested |
| Tests | Partial | 22 files / ~5158 lines, all compile into main binary via glob (`custom/CMakeLists.txt:248-259`) | No tests for A-R1, A-R4, R5, R7-to-be, H1/H2, weather HTTP, bearing/destination, empty-name DB insert |

## C.7 Verified-by-grep line refs (single source of truth)

- Registered checks: `PreflightManager.cpp:743-813` (22 types, 26 instantiations).
- Unregistered (dead) checks — grep `<name>` for zero hits in `custom/src`: AmbientTemperature, BaroHealth, BaroTemperature, BatteryFailsafe, BatteryFailsafeParam, BatteryHealth, CellVoltageBalance, CompanionLink, CompassCal, CompassYawConsistency, ControlSurface, DualGpsConsistency, EkfFailsafe, EkfStatusFlags, EkfVariance, EscCurrentSymmetry, EscFirmware, EscResponsiveness, EscVoltageConsistency, GcsFailsafe, GeofenceMaxAlt, GeofenceMaxRadius, GeofenceParam, GpsSpeedAccuracy, GyroBias, ImuCal, LevelCalibration, MagFieldStrength, MagInterference, MetarTemperature, MissionEnergy, MissionItem, OpticalFlow, PowerModuleHealth, PreArmOk, RadioBuffer, RadioFailsafe, RcChannelCount, RcFailsafe, RcRssi, RcTrim, RedundantPower, RtlTerrain, TafDeterioration, TakeoffCommand, TerrainClearance, Vibration, VideoFeed, WeatherWindGust.

---

# Part D — New checklist rows to append to Part B tables

(Append to the matching tables; IDs continue from Part B.)

**Section 1 (evidence-driven cleanliness):**
- [ ] Re-run `grep -c "class .*Check" custom/src/core/*Check.h` vs `new .*Check` in `PreflightManager.cpp` — dead-check count must be 0 or each gap justified
- [ ] `PreflightPlugin::init` order: after refactor, assert no `connect`/`setDependencies` arg is null (add `Q_ASSERT` at construction sites)
- [ ] No unguarded `_activeVehicle.` access in any custom QML handler/binding

**Section 2 (add after C6):**

| ID | Case | Expected |
| --- | --- | --- |
| C7 | External GCS sends arm `COMMAND_LONG` with `param2=21196` | Gate must NOT auto-open; override requires operator confirm |
| C8 | Recreate QML engine / relaunch plugin | No stale singleton pointer (`FlightSession`, `OperatorManager`, `VehicleProfileManager`, `WeatherProvider`, `PreflightSettingsManager`) |

**Section 3 (after S8):**

| ID | Case | Expected |
| --- | --- | --- |
| S9 | FSM state trace shows `Armed` | `PreflightStateMachine` actually transitions to `Armed` on heartbeat |

**Section 6 (after P6):**

| ID | Case | Expected |
| --- | --- | --- |
| P7 | Param download progress visible | `UavParameterManager::isReady()` goes true (watchlist non-empty) within download; not blocked on dead `setWatchlist` |
| P8 | Check roster = intended | Every check that exists and is expected to gate is registered; list matches UI count |

**Section 5 (after B12):**

| ID | Case | Expected |
| --- | --- | --- |
| B13 | `batterySocPct` and `batteryPercent` agree within tolerance | No dual-source divergence |

**Section 9 (after D5):**

| ID | Case | Expected |
| --- | --- | --- |
| D6 | Upsert vehicle with empty `friendlyName` | INSERT succeeds (no NOT NULL/FK failure); session row created |
| D7 | `startFlightSession` after failed upsert | Graceful error / retry, never silently empty session |
| D8 | Singleton reset on teardown | `instance()` revalid after destroy |

**Section 10 (after T4):**

| ID | Case | Expected |
| --- | --- | --- |
| T5 | 30 min soak with check dump enabled | No per-tick log blow-up; `armingBlocked` not spammed |

**Section 14 (order note):** insert an item 3b — "Dead-check audit: confirm registered checks == intended roster (P7/P8) before arming gate soak."
