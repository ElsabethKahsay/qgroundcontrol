Skywin GCS Phase 1 Readiness Plan
20 Oct 2026 · @Arsema

Implementation status (read this first)
Updated 20 Oct 2026 against the working tree on custom-dev-next. Everything below is uncommitted and nothing has been built or run yet; "done" means edited and syntax-checked with qmllint, not tested. The findings further down are left as originally written; this section says which are fixed, which were wrong, and what is still open. Each finding, defect and work item below also carries a "Status 20 Oct" note. Step-by-step instructions for the remaining Steps 0–2 work are in the last section, "Agent instructions: Steps 0–2".

Corrections to this report after checking the current code
• G1: the token-bound, expiring acknowledgment was implemented in commit 2fd409b49 and then removed by 0a71b8d22 ("Simplify preflight roster"). The project copy was not stale; the fix was lost. Restore those hunks rather than rewrite them.
• "Five compiled but unregistered Rc checks" (item 1, F22): no longer true. 0a71b8d22 merged them into RcCheck as sub-checks (src/core/RcCheck.cpp:44).
• AirspeedCheck: deleted in the working tree (along with BatteryTemperatureCheck, GimbalLinkCheck and MotorTemperatureCheck). AccelConsistencyCheck was removed the same way on 20 Oct; the app now registers 29 checks, 24 counted in checklist progress and 5 informational (zone compliance and four weather checks). Planes currently have no airspeed check, so the "AirspeedCheck ground rule" item becomes "add a pitot health check if planes fly in the first field stage".
• B7: confirmed. VehicleProfileManager::disarmVehicle(force=true) sends kForceArmParam2 (2989). Also, Config.h calls 21196 a "legacy" force-arm value; it is ArduPilot's force-disarm value.
• navigatePush (Other gaps): it is not a literal no-op; it pushes onto Window.window.mainStackView, which nothing in the project defines, so it has the same effect. Every call is on one of the eleven unreachable pages.
• F14: confirmed. The v19 migration block exists (utils/DatabaseManager.cpp:1490) but kLatestSchemaVersion is 18.
• params.log (ArduPlane 4.5.7, vehicle 1) is from the user's ArduPilot SITL, not real hardware. Its ARMING_CHECK = 0 and disabled battery/GCS failsafes are normal for a simulator and are not a finding. For item 12, set ARMING_CHECK = 1 in SITL before the "force-arm against a failing pre-arm" scenario, otherwise no pre-arm can fail and the test proves nothing. Real airframes still get the parameter review in item 14.

Done in the working tree (QML only)
• G2: the one-tap mode badge in ArmGatePanel is display-only.
• G3: the "Override" (ArmGatePanel) and "Override Gate" (PriorityFixPanel) buttons are removed. No QML calls overrideGate or sets ArmingGate.mode.
• G4, UI half: Force Arm shows only in a Testing session while the gate is denying, behind a dialog that requires pilot name, reason and "Propellers removed". The pilot and reason only go to the console log until the C++ forceArm accepts them.
• Item 16, UI half: the Fly-view post-flight banner and both "Post-flight summary" buttons are hidden (visible: false), code kept. The bypass itself (every check marked Skipped after disarm) is C++ and still open.
• B6: "Open Motor Test" is greyed out with a tooltip reason when no vehicle is connected or the vehicle is armed. Every entry point (button, tile click, openDialog()) opens a "Before the motor test" confirmation that requires "Propellers are removed" and warns that the fixed-wing test arms the vehicle; only then does the motor dialog open. The inline card keeps its original single-row layout.
• B1, B2, B3, QML stop-gap only: closing the motor dialog, its stop button, STOP ALL on the surface panel, and closing the gimbal page or pressing "Center All" no longer call the force-disarming stop functions while the vehicle is armed and flying. The C++ stop functions still force-disarm whenever the vehicle is armed; the real fix (item 19) is still open.
• Item 18, stop-gap: the gimbal Center and Tilt buttons are disabled with a visible reason until TelemetryBridge has a gimbal command, and the tilt test can no longer report a pass without attitude feedback.
• F3, display side only: VehicleTelemetry.batteryStale (valid battery data seen on this connection, then lost) drives "STALE" in the battery chip and the telemetry strip. This also fixed the chip treating a stale signal as fresh. The arming gate still trusts the last battery value (item 2 is open).
• Item 21: semantic status colours (statusPass/Fail/Warn/Stale/Pending/Info) in Colors.qml with the old names as aliases. PreflightChecklistView has no colour literals; the other safety screens already had none.
• UI cleanup from "After Phase 1": the purple/pink dialog theme now uses the normal surfaces and accent; the five unused colour tokens are deleted; no hex colour literal remains outside the singletons (airspace zone colours moved into Colors.qml as zone* tokens).
• Other gaps: the only reachable navigatePush button ("Test Gimbal" in VideoPreview, which also called a non-existent VideoManager.startGimbalTest()) is hidden.
• Not in the report: the Fly-view Events button and MANUAL/AUTO Mode toggle are removed. The Fly-view Checklist button now opens Analyze → Preflight Checklist (it used to land on Log Download because it reached into ids that are not properties), still asking for a session mode first if none is set. A toolbar chip shows the connected UAV name and session mode and opens the mode dialog when disarmed; the mode dialog has a Cancel button when a mode is already set. The chip is added through PreflightPlugin::toolBarIndicators() (one C++ line), because the stock FlyViewToolBar.qml is AOT-compiled into QGroundControl.Controls and the custom.qrc copy of it never loads; the same applies to any other edit in custom/qml/QGroundControl/Controls/FlyViewToolBar.qml.
• Not in the report: AccelConsistencyCheck removed from the preflight checklist (unregistered, removed from CMakeLists.txt, files deleted).
• Built and partly tested 20 Oct: the incremental build succeeds; QmlShadowTest 5/5, MotorCheckPanelSmokeTest 5/5 and FlyQuickActionsSmokeTest 8/8 pass with no QML runtime errors in the edited files. PreflightManagerTest has 3 failures (testAllMandatoryPassed, testMarkArmedDisarmed, testArmedSurvivesTick) that are not from these changes: they confirm every check and expect arming to be allowed, but confirm() refuses Auto checks and every Auto check becomes mandatory once a vehicle kind is applied. Fix or rewrite them in Step 0/1. Close any running instance of the app before running tests; a second instance hangs at startup.

Still open (all C++ unless noted)
• Item 1: baseline commit, tag and test run.
• Item 15: G1 restore; G4 C++ side (Testing-only refusal, pilot/reason logging, close window on use); G5–G13; one decide() and requestArm(); wiring every Arm button to requestArm() (QML, needs the C++ first).
• Item 16: kPostFlightEnabled in Config.h, resetForNextFlight(), FlightSession back to Idle.
• Items 2–5: data-age freshness, autopilot-only heartbeat, monotonic clocks, armed state from heartbeat.
• Items 6–8: SessionRecorder, battery pack registry and snapshot (including moving the pack modal into the Power section), database startup and migration fixes.
• Item 9 (F16), item 10 (F15 RTL), item 11 (disabled-with-reason controls, needs requestArm()).
• Item 17 (weather W1–W7), item 18 (real gimbal command and feedback), items 19–20 (shared bench disarm helper, Testing-only bench force-arm), item 21 lint test.
• Items 12–14: SITL campaign, Jetson build (F20 start script), real radio and go/no-go.

Summary
Verdict: not ready for Phase 1 field testing yet, but close on the arm gate and far on records and freshness. The arm interceptor, force-arm token and RTL path are well designed and heavily tested. The weak points are the layers around them: how the code decides data is fresh, how it decides the vehicle is armed, and how it records flights and batteries.
The five findings that most change the plan:
1. Freshness measures the wrong thing. A check's age is the time since its status last changed, not the time since its data last arrived, and a Passed check is never re-evaluated. A battery or GPS stream can stop while its check keeps passing. [V]
2. Armed state is guessed from command acks. The plugin toggles the vehicle's armed flag and opens or closes flight sessions from COMMAND_ACK, not from the heartbeat. RC arms and RC disarms are invisible to it. [V]
3. Session records are unreliable. For an RC-primary aircraft most flights create no session; a GCS arm followed by an RC disarm leaves a session open that swallows every later flight. Battery identity is one fake serial per vehicle, and the pack the operator entered in Fly view is never stored with the flight. [V]
4. Database failures are silent. Startup ignores a failed database open, migrations record a version even when their SQL failed, the v19 migration never runs, and check-result rows likely fail a foreign key on every write. [V], runtime effect [U]
5. Field robustness is unproven. Nothing has run on a real link, a 3-hour idle, or the Jetson target; the RTL confirm wait runs a nested event loop on a raw vehicle pointer that can be deleted mid-wait. [V] for the code, [R] for the test gaps
The plan below has 21 must-have items for Phase 1, grouped into six workstreams, and a release gate that ends with a props-on, tethered or low-altitude flight on one copter and one plane.
Phase 1 scope
Phase 1 is field testing of ArduPilot quadcopters and fixed-wing aircraft where the RC transmitter arms and flies, and the GCS monitors, gates GCS-initiated arming, runs bench tests and logs. Everything in this plan is judged against that job, not against a full-featured GCS.
The GCS must, for Phase 1:
• Show trustworthy live state: link, heartbeat, battery, GPS, mode, armed state, and say clearly when any of it is stale.
• Never arm the vehicle from the GCS unless the gate is open or a deliberate, logged override is in force; never block disarm.
• Give one reliable Return (RTL) action that confirms the mode change.
• Run motor and servo bench tests safely, props off.
• Record every arm-to-disarm as a flight, whoever armed it, with enough data to reconstruct what happened.
• Survive link loss, link degradation and multi-hour idle without crashing, freezing or quitting.
• Install and run repeatably on the Jetson Orin Nano (JetPack 6.2.2) field laptop or companion target.
The GCS does not, and should say so plainly:
• Block RC-stick arming or arming from other MAVLink peers. ArduPilot's own pre-arm checks remain the last barrier.
• Plan or fly missions, guided goto, takeoff or land in Phase 1.
• Replace the autopilot's failsafes (RC loss, GCS loss, battery, EKF). It checks they are configured; it does not enforce them.
Evidence in this doc is tagged the way your state analysis tags it: [V] read directly in the code with a file:line, [R] reported elsewhere and not re-checked, [U] inferred. All file:line references are to the copy of custom/ in this project's files as of 2026-10-20; nothing was built or run, so no claim here is a test result.
Findings by quality attribute
The gate's decision logic itself has no defect I could find; its inputs do (F1 to F5), and 10 of the 22 findings are record-keeping and database defects (F6 to F15). Paths are relative to custom/src/ unless they start with qml/, tests/ or deploy/. The ID column is what the work items further down refer to.
ID
Attribute
Finding
Evidence
Tag
Phase 1
F1
Safety, reliability
A check's freshness is the time since its status last changed. setStatus returns early when nothing changed, so the timestamp is not refreshed by a re-evaluation that agrees with the last one.
core/AbstractCheck.cpp:139-144
V
Must — Status 20 Oct: open.
F2
Safety, reliability
A Passed check is never re-evaluated by the tick, so a value that degrades after passing is not seen until the check is forced Stale. The Stale rule then fires on every passed mandatory check about 10 s after it passed, the check is re-run about 5 s later and passes again. Expected effect: the gate closes for a few seconds on a roughly 15 s cycle, or a degrading value goes unnoticed for up to 15 s. Not yet observed in a run.
core/AbstractCheck.cpp:59-70, core/PreflightManager.cpp:681, 737-765, core/PreflightManager.h:226
V code, U effect
Must — Status 20 Oct: open.
F3
Safety
batteryDataValid (battery stream stopped for 5 s) is computed but no check and no QML reads it, so the battery check keeps using the last cached voltage.
adapters/TelemetryBridge.cpp:614-619; no other references in src/ or qml/
V
Must — Status 20 Oct: partial. UI shows STALE in the battery chip and telemetry strip (VehicleTelemetry.batteryStale); the gate side is open (item 2).
F4
Safety
The gate's freshness clock ticks on a heartbeat from any component of the vehicle. A companion computer or gimbal heartbeat keeps the gate fresh while the autopilot is silent. Connection quality is computed from the same mixed stream.
adapters/TelemetryBridge.cpp:1031, 1040-1068
V
Must — Status 20 Oct: open.
F5
Consistency
Armed state is inferred from COMMAND_ACK. For a GCS-sent arm the plugin never sees the outbound COMMAND_LONG, so _lastArmDisarmParam stays -1 and the new armed state is guessed as the opposite of the current one, then forced into the vehicle with _updateArmed. A heartbeat that lands before the ack, or a retried ack, flips it the wrong way.
PreflightPlugin.cpp:905, 930-932
V code, U frequency
Must — Status 20 Oct: open.
F6
Data integrity
Flight sessions open and close on that ack, not on armed state: RC arms create no session, an RC disarm never closes one, and while one is open _onGateOpened refuses to start another, so every later flight is folded into it.
PreflightPlugin.cpp:535-555, 585-608, 936-947
V
Must — Status 20 Oct: open.
F7
Data integrity
Battery identity is batt:<sysid>, one fake serial per vehicle, so cycle counts and health trends are per vehicle, not per pack.
PreflightPlugin.cpp:255-256
V
Must — Status 20 Oct: open.
F8
Data integrity
The pack the operator enters in Fly view (type, S, P, cell mAh, reserve) is saved on the vehicle row and overwritten on each change. No flight or session row keeps a copy.
core/VehicleProfileManager.cpp:577-623, 730-735; qml/cpts/FlyQuickActions.qml:558-564
V
Must — Status 20 Oct: open.
F9
Data integrity
Battery cycles have two writers, saveBatteryCycle on disarm after 30 s and incrementBatteryCycle on the gate-close ack; both bump total_cycles.
core/VehicleProfileManager.cpp:1112-1126, PreflightPlugin.cpp:586, utils/DatabaseManager.cpp:3511-3535, 3586-3637
V
Must — Status 20 Oct: open.
F10
Data integrity
Energy per flight uses BAT_CAPACITY/BAT1_CAPACITY, which are PX4 names; ArduPilot's is BATT_CAPACITY, so ArduPilot flights fall back to a 0.45 Ah-per-percent guess. Distance falls back to ground speed at disarm times duration, which is near zero. The pack entered in Fly view is not used.
core/VehicleProfileManager.cpp:1129-1157
V
Must — Status 20 Oct: open.
F11
Data integrity
Check results are written with flight_session_id = -1 and the MAVLink system id as device_uid, while vehicles.device_uid is the hardware UID and foreign keys are on. Likely every insert is rejected; at best none can be joined to a flight.
core/PreflightManager.cpp:1005-1020, utils/DatabaseManager.cpp:205, 465-473
V code, U outcome
Must — Status 20 Oct: open.
F12
Reliability
Crash recovery closes orphaned rows in flights only; open flight_sessions rows stay open forever.
utils/DatabaseManager.cpp:1544-1580
V
Must — Status 20 Oct: open.
F13
Reliability
Startup ignores the result of DatabaseManager::initialize(), so a locked, full or corrupt database leaves the app running with no logging and no warning to the operator.
PreflightPlugin.cpp:221
V
Must — Status 20 Oct: open.
F14
Reliability, maintainability
Migrations log failed SQL as warnings and still record the version, so a failed step is never retried. Migration v19 exists but kLatestSchemaVersion is 18, so it never runs and saveTargetLocation writes columns that do not exist.
utils/DatabaseManager.cpp:997, 1037-1529, 3755-3765
V
Must — Status 20 Oct: open; confirmed (v19 block at utils/DatabaseManager.cpp:1490, kLatestSchemaVersion = 18).
F15
Reliability
RTL waits up to 5 s in a nested QEventLoop and then dereferences the raw _vehicle pointer. Link loss, the most likely moment to press Return, can delete the vehicle during that wait. A second press re-enters the loop.
adapters/TelemetryBridge.cpp:105-131, 1449-1450, adapters/TelemetryBridge.h:916
V code, U crash
Must — Status 20 Oct: open.
F16
Safety
Manual-check confirmations are persisted in QSettings and re-applied at the next startup, before any vehicle connects, so yesterday's "props inspected" can carry into today.
core/PreflightManager.cpp:190, 1043-1084
V
Must — Status 20 Oct: open.
F17
Consistency
All clocks use QDateTime::currentDateTime() (wall clock, 28 sites) instead of a monotonic timer. An NTP or GPS time jump on the Jetson can mark everything stale or keep stale data looking fresh. SQL rows use UTC CURRENT_TIMESTAMP while app-side durations and compliance logs use local time.
e.g. core/ArmingGate.cpp:456, core/PreflightManager.cpp:750, PreflightPlugin.cpp:562-578
V
Must — Status 20 Oct: open.
F18
Reliability
ArduPilot force-arm values: 2989 forces arm and 21196 forces disarm, as you suspected. This matches my reading of ArduPilot's AP_Arming source, not a run. The code comment that 2989 is accepted by PX4 is wrong but harmless for Phase 1.
utils/Config.h:198-204, adapters/TelemetryBridge.cpp:1328-1331
U
Verify — Status 20 Oct: verify in SITL (item 12).
F19
Performance
Every check status change does a synchronous SQLite insert on the GUI thread, there is no retention or pruning of check_results or telemetry events, and the only maintenance is a WAL checkpoint every 5 minutes. Combined with F2's cycling this grows without bound.
core/PreflightManager.cpp:998-1021, utils/DatabaseManager.cpp:114-120
V
Should — Status 20 Oct: open (after Phase 1).
F20
Deployability
The field build is a Debug build, which compiles in test-only hooks (m_testOverrides under QT_DEBUG) and is slower on the Jetson. The start script hard-codes x86_64-linux-gnu, so the AppImage layout will not load on the arm64 Jetson.
core/AbstractCheck.cpp:207-209, deploy/qgroundcontrol-start.sh:3
V
Must — Status 20 Oct: open.
F21
Scalability
One PreflightManager with system id 1 is created for the whole app, and session, battery and gate state are single globals. Fine for one aircraft per GCS in Phase 1; multi-vehicle needs a per-vehicle context.
PreflightPlugin.cpp:153
V
Later — Status 20 Oct: open (after Phase 1).
F22
Maintainability
Large hand-written units (DatabaseManager.cpp 4,388 lines, AirspacePage.qml 2,273), five compiled but unregistered Rc checks, checks reading telemetry by property-name strings, and a blocking 3 s network call in WeatherProvider::lookupIcao. None blocks Phase 1.
utils/DatabaseManager.cpp, core/AbstractCheck.cpp:204-224, utils/WeatherProvider.cpp:641-658
V
Later — Status 20 Oct: partial. The five Rc checks are now sub-checks of RcCheck (commit 0a71b8d22); the rest is after Phase 1.
What is in good shape and should be kept as is: the vehicle-level arm interceptor and its fail-closed default, the caller-scoped one-shot force-arm and ack tokens, force-arm refusal when flying, disarm never being vetoed, exception isolation around each check's evaluate() (core/PreflightManager.cpp:466-485, 681-695), WAL plus a pre-migration backup, and the mutation-checked test discipline.
Sessions and battery records
The current session logic is not logical enough to trust for Phase 1: a flight record should follow the vehicle's armed state, and today it follows the GCS command ack. The redesign below gives one record per connection, one per arm-to-disarm, and a frozen copy of the battery pack on every flight.
How sessions work today
Three components write overlapping records, each with its own trigger. [V]
Writer
Table
Starts on
Ends on
Problem
PreflightPlugin::_onGateOpened/_onGateClosed
flight_sessions, battery_cycles, compliance_logs
COMMAND_ACK accepted for an arm
COMMAND_ACK accepted for a disarm
Never sees RC arms or disarms (F5, F6). An RC disarm leaves the row open and blocks every later session.
FlightSession
flights, flight_telemetry_events
startFlightSession() from FlightFormDialog
closeSession() on vehicle removal
The form is never shown, so this path never writes. Its arm and disarm handling, which uses Vehicle::armedChanged, is the right trigger.
VehicleProfileManager::_onArmedChanged
battery_cycles, flight_sessions energy and distance
Vehicle::armedChanged
Disarm after 30 s
Writes into whichever flight_sessions row the plugin opened, which may be the wrong flight; double-counts cycles with the plugin (F9).
Crash recovery only closes flights (F12), check results cannot be joined to any flight (F11), and the battery is batt:<sysid> for every pack (F7). The pack details entered in Fly view live only on the vehicle row and are overwritten on the next edit (F8).
Target model
One C++ owner, a SessionRecorder, is the only class that writes session, flight, battery-usage and event rows. Everything else emits signals to it. The rules:
1. Connection session: opens when a vehicle's first heartbeat arrives, closes on vehicle removal or app exit. It carries the operator, the session mode (Flight, Testing, Training), the app version and the vehicle UID.
2. Flight: opens on Vehicle::armedChanged(true) and closes on armedChanged(false), whoever armed. The gate state at the moment of arming, and whether the arm came from this GCS, a force-arm token, or elsewhere (RC or another peer), are stored as columns, not used as triggers. Remove the _updateArmed call from the ack handler.
3. Bench runs: a flight that starts while a motor or servo test is active, or in Testing mode, is flagged is_bench and never counts toward flight hours or battery cycles.
4. Link loss while armed: the flight stays open and gets a link_lost event. If the link returns armed, it continues; if it returns disarmed or never returns, it closes with end_reason = link_lost and the last known time.
5. Crash safety: the open flight row is written at arm and its running summary (duration, min voltage, mAh used) is updated every 10 s. On the next start, any open row is closed with end_reason = crash_recovered at its last summary time, for both connections and flights.
6. Time: every row stores UTC ISO-8601 timestamps; durations come from a monotonic QElapsedTimer, not from subtracting wall-clock times (F17).
7. Battery cycles are derived, not counted: a cycle is a non-bench flight that used at least a set share of the pack (default 20% of capacity). A view computes it, so there is no counter to double-increment.
Battery pack on every flight
Decided 2026-10-20: a pack is required before every arm, and the battery configuration modal moves from Fly view into the preflight checklist's Power section. Today the modal is batteryConfigDialog in qml/cpts/FlyQuickActions.qml:429-579, with a second editor in qml/cpts/BatteryTimeEstimatorPanel.qml:209-212. It becomes one shared component, opened by clicking a new "Battery pack" item in the Power category (qml/cpts/ChecklistEngine.qml:9), and Fly view keeps only a read-only display of the selected pack.
• Pack registry (battery_packs): one row per physical pack, with an operator label as its ID ("P03", "4S-5000-A"), chemistry, series cells, parallel cells, cell mAh, rated capacity, reserve percent and a retired flag. If ArduPilot reports a serial for a smart battery, store it too; most packs will not have one.
• Selecting a pack: the operator clicks the "Battery pack" item under Power in the preflight checklist, and the modal opens to pick a pack or add and edit one. The item is mandatory: it stays Pending, and arming stays blocked, until a pack is picked for this connection. Editing pack details changes the registry row, not past flights. Changing the selected pack while armed is refused in the UI; if it happens anyway it is logged as an event and applied at disarm.
• Snapshot per flight (flight_batteries, one row per battery slot so dual-battery planes work): pack ID plus a frozen copy of chemistry, S, P, cell mAh, capacity and reserve as they were at arm time, and the measured start and end voltage, start and end percent, minimum voltage under load, mAh consumed and Wh consumed.
• Measured, not guessed: take mAh and Wh from ArduPilot's BATTERY_STATUS (current_consumed, energy_consumed), use the selected pack's capacity, and fall back to BATT_CAPACITY (the ArduPilot name, F10) only when no pack is selected. Record which source was used.
• No pack selected: cannot happen for a GCS arm, since the item blocks arming. An RC arm without a pick still records the flight, with pack_id = unspecified and the vehicle's default pack copied in, so history is never lost.
Tables
Table
Key columns
Replaces
connection_sessions
id, vehicle_uid, operator_id, mode, app_version, started_utc, ended_utc, end_reason
nothing (new)
flights (extended)
id, connection_session_id, vehicle_uid, armed_utc, disarmed_utc, duration_s, arm_source, gate_open_at_arm, override_used, is_bench, end_reason, max_alt_m, distance_m
flight_sessions rows written by the plugin
battery_packs
pack_id, label, chemistry, series, parallel, cell_mah, capacity_mah, reserve_pct, smart_serial, retired
batteries with batt:<sysid> IDs
flight_batteries
flight_id, slot, pack_id, frozen pack fields, start_v, end_v, min_v, start_pct, end_pct, used_mah, used_wh, capacity_source
battery_cycles
flight_events
id, connection_session_id, flight_id (nullable), utc, type, detail
flight_telemetry_events, gate rows in compliance_logs
check_results (re-keyed)
connection_session_id, flight_id (nullable), vehicle_uid, check_id, status, utc
today's rows keyed by system id and -1
Keep the old tables read-only for history; do not try to back-fill them into the new model. The existing tests/FlightSessionLifecycleTest.cpp already encodes most of these rules as expected failures and skips; turning those into passes is the acceptance test for this work.
Arm gate review
The arm gate is not correct yet: in the project's copy of the code the acknowledge token and its 15 s expiry are not implemented, and three one-tap controls can bypass the gate entirely. The vehicle-level interceptor, fail-closed default and force-arm token are sound; the defects are in the acknowledge path, the override buttons and the inbound path. Reviewed 2026-10-20 from the files, nothing run.
Before anything else, check whether your working tree matches this copy: git diff HEAD -- custom/src/core/ArmingGate.cpp and git log -S m_ackTimer --oneline. ArmingGate.h declares m_ackTimer, m_ackToken and m_ackExpiryMs, and ArmSendTest tests them, but ArmingGate.cpp never uses them. Either the copy is stale or the fix was lost, possibly by the unknown earlier session.
ID
Severity
Defect
Evidence
Fix
G1
Critical
The acknowledgment is not bound to a token and never expires. acknowledgeOverride only sets a flag; m_ackToken is never assigned, so ackTokenForArm() always returns 0; the interceptor's ack branch ignores the call token. Any arm from any path, including stock QGC paths, consumes the ack, and an ack made hours earlier still works. testUnspentAckDeniesStockArmThenAllowsBoundArm and testUnusedAckExpiresAndDenies would fail against this code.
core/ArmingGate.cpp:229-243, 385-393; core/ArmingGate.h:103-106, 175-181; tests/ArmSendTest.cpp:849-919
Mint a fresh token on ack, start a single-shot m_ackTimer of m_ackExpiryMs that clears it, and authorize only the call whose token matches. Re-run both tests and mutation-check them. — Status 20 Oct: open. The fix existed in commit 2fd409b49 and was removed by 0a71b8d22; restore those hunks.
G2
Critical
One tap on the mode badge cycles the gate to Passive, which allows every arm. No confirmation, no log, no reason.
qml/cpts/ArmGatePanel.qml:104; core/ArmingGate.cpp:159-160, 441-447
Remove the click handler. For Phase 1 fix the mode at Active; if a mode change is ever needed, put it in settings behind a confirmation and log it as an event. — Status 20 Oct: done in QML (badge is display-only). Open: make mode read-only in C++.
G3
High
"Override" and "Override Gate" call overrideGate(): one tap opens a 30 s window in which every arm from every path passes, ignoring staleness, Training mode and check results, with no pilot name. This undoes the token design.
qml/cpts/ArmGatePanel.qml:144, qml/cpts/PriorityFixPanel.qml:214; core/ArmingGate.cpp:252-268, 433-438
Remove both buttons and make overrideGate() non-invokable from QML. Every override goes through the acknowledge dialog. — Status 20 Oct: done in QML (both buttons removed). Open: drop Q_INVOKABLE from overrideGate() or delete it.
G4
High
"Force Arm" is one tap, with no confirmation or pilot name, and is shown even when the gate is open. It sends 2989, which also bypasses ArduPilot's own pre-arm checks.
qml/cpts/ArmGatePanel.qml:150-168; adapters/TelemetryBridge.cpp:1332-1390
Show it only in a Testing session (bench), behind a confirmation dialog that requires pilot name and reason, and log each use as a flight event. Close the force window as soon as its token is used, not after 60 s. — Status 20 Oct: partial. QML: Force Arm shows only in a Testing session while the gate denies, behind a pilot/reason/props-off dialog. Open: C++ refusal outside Testing, logging, close window on use.
G5
High
The acknowledgment bypasses everything, including "no telemetry" and "telemetry stale". The toolbar opens the acknowledge dialog for any denial, so an operator can acknowledge a dead link and arm.
qml/PreflightToolbarIndicator.qml:60-64, 115-120; core/ArmingGate.cpp:385-393 runs before the staleness test at :455
Split blocks into hard and overridable. Hard: no fresh autopilot heartbeat, stale data, Training mode, no battery pack. Bind the ack to the set of failing check IDs at ack time and void it if a new check fails. — Status 20 Oct: open.
G6
High
Training mode only blocks inbound arms from other peers. The GCS's own Arm buttons still arm in Training mode when checks pass.
core/ArmingGate.cpp:137-146 (inbound only); no Training test in :360-404 or :431-487
Make Training a hard block in the one decision function below. — Status 20 Oct: open.
G7
Medium
The inbound path disagrees with the outbound one: it allows every arm if no manager is set, lets any arm through during a caller-scoped force window, ignores staleness, and lets a peer's arm consume the operator's acknowledgment. It is also not a real barrier.
core/ArmingGate.cpp:126-187; PreflightPlugin.cpp:902-919
Make it observe-only: log the peer's arm as an event and raise an alert; never consume the ack or change gate state. — Status 20 Oct: open.
G8
Medium
During a force-arm window the gate reports armingAllowed = true, while every normal Arm press is denied by the interceptor. The UI and the decision disagree.
core/ArmingGate.cpp:318-325, 368-381, 433-438
Expose the override kind separately; armingAllowed answers "would a plain arm pass now". — Status 20 Oct: open.
G9
Medium
resetGate() resets the token counter to 0, so token values repeat across windows, and the force-arm token is left on the vehicle after sending. A stale token can match a later window.
core/ArmingGate.cpp:277; adapters/TelemetryBridge.cpp:1374
Never reset the counter (or use a random 32-bit value), and clear the vehicle's token after the command is sent. — Status 20 Oct: open.
G10
Medium
The post-disarm reset only runs on vehicleDisarmed, which is emitted from the command-ack handler, so an RC disarm leaves an ack or override in place.
core/ArmingGate.cpp:51-55; PreflightPlugin.cpp:944-946
Drive it from Vehicle::armedChanged (work item 5). — Status 20 Oct: open.
G11
Low
Three Arm buttons decide differently when to show the dialog: the status drawer only on critical failures, the toolbar on any denial, the Fly view armed indicator never.
qml/MainStatusIndicator.qml:353-361, qml/PreflightToolbarIndicator.qml:60-64, qml/ArmedIndicator.qml:24-30
One ArmingGate.requestArm() that returns arm, needs-acknowledgment or blocked-with-reason, used by every button. — Status 20 Oct: open. All arm call sites are listed in the Agent instructions (Step 2.5 inventory).
G12
Low
ArmingGateTest and part of ArmingGateExtendedTest test processArmRequest, which production never calls; Hybrid and Active behave identically; onCheckFailed and interceptCommandLong write gate state outside the main evaluation; the interceptor mutates the gate through const_cast.
tests/ArmingGateTest.cpp:44-99, tests/ArmingGateExtendedTest.cpp:121; core/ArmingGate.cpp:192-225, 408-420, 374, 386
Delete processArmRequest and its tests, merge Hybrid into Active, and route everything through one decision function. — Status 20 Oct: open.
G13
Verify
QGC resends an unacknowledged command. If a resend passes through the interceptor again, a one-shot token consumed by the first send denies the retry on a lossy link. The Vehicle.cc patch is not in this copy.
src/Vehicle/Vehicle.cc (fork patch, not reviewed)
Read the patch; add a test that drops the first arm ack. — Status 20 Oct: open (verify).
One-tap Acknowledge & Arm already works in this copy: both dialog loaders call TelemetryProvider.arm() from onAccepted (qml/PreflightToolbarIndicator.qml:127-139, qml/MainStatusIndicator.qml:59-75), so the label is accurate once G1 is fixed. Confirm it in SITL, since the custom MainStatusIndicator may be shadowed by the stock one.
Correct behavior, as one decision function
Replace the scattered logic with one pure decide(request) that the interceptor, the armingAllowed property and requestArm() all call:
1. Disarm is always allowed.
2. Hard blocks, never overridable: no vehicle, no autopilot heartbeat within the staleness limit, any mandatory check Stale, Training session, no battery pack selected.
3. All mandatory checks Passed: allow.
4. Otherwise, allow once only if a valid acknowledgment exists: not expired, its token matches this call, it names every currently failing check, and every one of those checks is marked overridable. Consume it and log pilot, reason and check IDs as a flight event.
5. Force-arm (Testing sessions only): allow once for the matching token while the vehicle is not flying; close the window on use.
6. Everything else: deny with the reason the UI shows.
Tests to add, each mutation-checked: stock arm cannot spend an ack; ack expires; a new failure after ack voids it; ack cannot override stale telemetry; Training blocks the GCS arm; one tap cannot change mode or open a window; RC disarm clears an ack; a peer's inbound arm does not consume the ack; dropped first ack still arms once.
Weather, gimbal, flight history and post-flight
All four of your suspicions are right, and the post-flight feature hides a gate bypass that makes parking it urgent. Reviewed 2026-10-20 from the files, nothing run.
Post-flight: park it, and close the bypass it causes
Status 20 Oct: steps 4 (hide entry points) done in QML. Steps 1–3 (kPostFlightEnabled, resetForNextFlight(), FlightSession to Idle) are open, so the bypass still exists.
After every disarm, whoever disarmed and in any session mode, FlightSession emits postFlightChecklistRequired (managers/FlightSession.cpp:404-431, called from :136-143). PreflightManager::activatePostFlightChecks then sets every pre-flight check to Skipped (core/PreflightManager.cpp:585-606), and allMandatoryPassed() counts Skipped as passed (:396-403). No check is ever marked post-flight (setIsPostFlight has no caller), so after the first landing every mandatory check reads as passed and nothing re-evaluates them until the next arm (:679, 744). The gate is open for a re-arm with no checks run. [V] code path, [U] until shown in SITL: arm, disarm, then read ArmingGate.armingAllowed.
How to park post-flight for Phase 1 without breaking anything:
1. Add one switch, kPostFlightEnabled = false, in utils/Config.h. Keep every post-flight class, column and page in the code; only the wiring is gated.
2. With the switch off, on disarm call a new PreflightManager::resetForNextFlight(): leave post-flight mode, reset every check to Pending, clear manual confirmations and the battery-pack selection, and re-evaluate. Do not connect postFlightChecklistRequired to activatePostFlightChecks (PreflightPlugin.cpp:192-193).
3. FlightSession goes from Armed back to Idle (ready for preflight), not to PostFlight.
4. Hide the post-flight entry points: the Fly view banner (qml/FlyViewCustomLayer.qml:562-600) and the two "Post-flight summary" buttons (qml/pages/FinalChecks.qml:283, qml/pages/LaunchReady.qml:194). Leave PostFlightSummary.qml in the resource file so nothing that imports it breaks.
5. Keep the database columns (post_checklist_complete and the post-flight flags) untouched; no migration.
Done when: a test arms, disarms and confirms the gate is closed with every mandatory check Pending; QmlShadowTest, MotorCheckPanelSmokeTest and FlyQuickActionsSmokeTest still load every page; and turning the switch back on restores today's behavior.
Flight history is never saved
Status 20 Oct: open (item 6).
The history page reads the flights table (models/FlightHistoryModel.cpp:109-127 via DatabaseManager::queryFlights). The only writer of flights is FlightSession::startFlightSession, called only from FlightFormDialog, which no page opens; the wizard starts only Testing or Training sessions, which are kept in memory (qml/cpts/WizardStep1Operator.qml:560-565). The gate-driven flight_sessions rows are a different table the page never reads. So the page is always empty, which matches what you saw in SITL. [V]
Fix: covered by work item 6. The SessionRecorder writes flights on every arm-to-disarm, so the existing history page fills without changes to its query, as long as the new rows keep its columns (mode, operator_id, vehicle_id, started_at, duration_sec). Testing flights are stored and flagged is_bench; Training flights are stored and flagged too, rather than kept only in memory.
Weather data is unreliable, for concrete reasons
Status 20 Oct: W1 and W5 done; W2–W4, W6 and W7 open (item 17).
ID
Defect
Evidence
Tag
W1
Open-Meteo returns wind in km/h by default and the request does not set wind_speed_unit, but the code treats it as m/s. Displayed knots are about 3.6 times too high.
utils/WeatherProvider.cpp:119-127, 161, 60
V for code; km/h default from Open-Meteo's docs, from memory — Status 20 Oct: done. The request sets wind_speed_unit=ms (WeatherProvider.cpp); not yet checked against a live response.
W2
An Open-Meteo fetch stamps m_metarTimestamp with the fetch time, so metarFresh() reports a fresh METAR when none was fetched. The gust check then reads windGust = 0 and auto-passes with "No gusts reported in METAR".
utils/WeatherProvider.cpp:166, :45-50; core/WeatherWindGustCheck.cpp:29-37
V — Status 20 Oct: open.
W3
Open-Meteo (model forecast for the GPS point) and METAR (an airport up to 50 km away) write the same fields, last one wins, and the UI does not say which source it shows.
utils/WeatherProvider.cpp:159-166, 240-275
V — Status 20 Oct: open.
W4
The METAR JSON parse likely misreads the current aviationweather.gov format: obsTime is a Unix time number, read here as an ISO string; visib can be the string "10+", read as 0; ceiling comes from clouds[], not a ceiling object. Good weather can show 0 km visibility and 0 ft ceiling.
utils/WeatherProvider.cpp:238-262
U, check against a saved real response — Status 20 Oct: open.
W5
Flight category LIFR is mapped to weather code 95, which displays "Thunderstorm".
utils/WeatherProvider.cpp:265-276
V — Status 20 Oct: done. weatherDescription() names the METAR flight category ("LIFR (low IFR)", …) and LIFR no longer maps to code 95; WeatherProviderTest::testFlightCategoryDescription, mutation-checked.
W6
Missing fields silently become 0 (wind, visibility, temperature), shown as real values.
utils/WeatherProvider.cpp:159-164
V — Status 20 Oct: open.
W7
In SITL the vehicle sits at the simulator's default home, so weather is fetched for that location, not yours.
qml/pages/PreFlightChecklist.qml:57-87
V — Status 20 Oct: open.
Fix plan: request wind_speed_unit=ms and add wind_gusts_10m; keep METAR and forecast in separate structures with their own observation time and source label shown in the UI; parse the current METAR JSON fields from a saved real response used as a test fixture; treat missing fields as unknown, not 0; map flight category to its own label; only the METAR path may auto-pass the gust check; in SITL, fetch for the configured field location instead of the simulated position. Weather checks stay informational and never gate arming.
Gimbal tests do not work at all
Status 20 Oct: stop-gap done: both buttons are disabled with a visible reason until sendGimbalAngles exists, and the tilt test reports FAILED instead of a pass without attitude feedback. Steps 1–5 open.
The page offers only Center and a pitch -30° Tilt test; there is no yaw or roll test. Both buttons call TelemetryProvider.sendMountControl(), which does not exist in TelemetryBridge, so each click throws in QML and sends nothing; the Tilt button then stays on "Tilt Testing…" because its timer is never started. The page also listens for a gimbalCommandResult signal that does not exist, the bridge never updates gimbalPitch, gimbalRoll or gimbalYaw (it only latches "gimbal detected"), and the Tilt test would report Passed after 2 s without checking any feedback. [V] qml/pages/GimbalTest.qml:81-130, 735-766; adapters/TelemetryBridge.cpp:994-1004; no sendMountControl in src/.
Plan to make pitch, roll and yaw tests real:
1. Command: add TelemetryBridge::sendGimbalAngles(pitch, roll, yaw) using the MAVLink gimbal protocol v2 (MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW, or GIMBAL_MANAGER_SET_ATTITUDE when roll is needed), falling back to MAV_CMD_DO_MOUNT_CONTROL for older setups; surface the command ack.
2. Feedback: decode GIMBAL_DEVICE_ATTITUDE_STATUS (quaternion to roll, pitch, yaw) and ArduPilot's MOUNT_STATUS, update the three properties and emit gimbalAttitudeChanged.
3. Limits: read MNT1_TYPE and MNT1_PITCH_MIN/MAX, MNT1_ROLL_MIN/MAX, MNT1_YAW_MIN/MAX; skip an axis whose range is zero (no roll axis on a 2-axis gimbal) and pick test angles inside the limits.
4. Per-axis test: for each of pitch, roll and yaw, command a target angle, pass only if the measured angle is within ±3° of target within 3 s, then return to center and confirm. Show and log a result per axis.
5. Test: a unit test with a mock gimbal stream, and a SITL run with ArduPilot's simulated mount (MNT1_TYPE=1).
Done when each axis shows a measured pass or fail, and disconnecting the gimbal makes the test fail rather than pass.
Bench tests, pages and UI consistency
The motor, control-surface and gimbal tests work on a disarmed bench, which matches what you saw, but three of their stop paths send a forced disarm whenever the vehicle is armed, with no check that it is on the ground. A forced disarm (param2 21196, utils/Config.h:204) stops the motors even in the air. These panels load inside the preflight checklist (qml/cpts/PreflightChecklistView.qml:647, 1037), which the Fly view also opens (qml/FlyViewCustomLayer.qml:603), so they can be reached in flight. [V] code paths; [U] whether the checklist hides these buttons while armed, not run.
Bench test findings
ID
Severity
What happens
Evidence
B1
Critical
Closing the Motor Test dialog calls stopAll(), which force-disarms if armed
qml/cpts/MotorCheckPanel.qml:159, controllers/HardwareTestController.cpp:1403-1408 — Status 20 Oct: partial, QML stop-gap: the dialog close and its stop button skip stopAll() while the vehicle is armed and flying. C++ stopAll() still force-disarms whenever armed (item 19).
B2
Critical
Closing the gimbal/surface page, or its stop button, calls stopAllSurfaces(), which always ends in a forced disarm if armed
qml/pages/GimbalTest.qml:68-70, 818, controllers/ControlSurfaceTestController.cpp:1072, 1090-1097 — Status 20 Oct: partial, QML stop-gap: page close and Center All skip stopAllSurfaces() while armed and flying. C++ fix open (item 19).
B3
Critical
STOP ALL on the control-surface panel calls the same stopAll() as B1
qml/cpts/ControlSurfacePanel.qml:208 — Status 20 Oct: partial, QML stop-gap: STOP ALL is disabled while armed and flying, with a tooltip. C++ fix open (item 19).
B4
High
Starting a motor test while armed force-disarms first, then runs the test
controllers/HardwareTestController.cpp:965-974 — Status 20 Oct: open.
B5
High
The fixed-wing motor test and the surface sweep force-arm with 2989, skipping ArduPilot pre-arm checks, in any session type
controllers/HardwareTestController.cpp:762-774, controllers/ControlSurfaceTestController.cpp:534-550 — Status 20 Oct: open (item 20).
B6
Medium
Props-off is only a text warning, the Open button is always enabled, and the panel says the vehicle stays disarmed, which is wrong for the fixed-wing path
qml/cpts/MotorCheckPanel.qml:114-135, 381 — Status 20 Oct: done in QML: correct fixed-wing text; Open Motor Test is greyed out with a reason when offline or armed, and every entry point goes through a props-off confirmation dialog.
B7
Medium
The Fly view disarm sends 2989 as param2; ArduPilot's force-disarm value is 21196, so this is probably a plain disarm [U]
qml/cpts/FlyQuickActions.qml:399-425, core/VehicleProfileManager.cpp:645-664 — Status 20 Oct: open; confirmed (VehicleProfileManager.cpp:651-655 sends kForceArmParam2).
Fix (must-have items 19 and 20): one disarm helper shared by both test controllers. It sends a plain disarm, only when this test armed the vehicle, and refuses unless the vehicle reports landed and ground speed is under 1 m/s. Closing a dialog or page stops outputs and clears RC overrides but never disarms. Bench force-arm is allowed only in a Testing session, behind a props-off confirmation.
Other gaps found in the review
Status 20 Oct: pages and navigatePush unchanged except the hidden Test Gimbal button; MaintenanceTracker and zone compliance open.
• Maintenance does nothing. MaintenanceTracker is never created or registered in PreflightPlugin, so the maintenance page and airframe hours are dead. [V] no instantiation in src/.
• Eleven pages cannot be opened. Config.navigatePush is a no-op (qml/singletons/Config.qml:100-107) and nothing else loads Home, TemplateManager, TemplateEditor, MaintenancePage, VehicleSelect, VehicleManagement, LaunchReady, FinalChecks, PostFlightSummary, PayloadCheck or PreflightSettings. The gimbal button in qml/cpts/VideoPreview.qml:100 uses it too, so it does nothing.
• Zone compliance protects nothing in Phase 1. core/ZoneComplianceCheck.cpp is advisory, checks only mission waypoints against circles (no legs, no live position), and reports "No mission loaded" on RC flights. The seeded zones are Addis Ababa samples (utils/DatabaseManager.cpp:4240-4266). Record airspace by hand on the go/no-go sheet for Phase 1.
• Mission planning is stock QGC. The Plan view should be reachable from the toolbar's tool selector (FlyViewToolBar.qml:59, showToolSelectDialog) [U] not run. It is outside the custom suite and Phase 1 is RC-primary, so it is not reviewed here.
UI consistency and the singletons
The theme singletons are the cause of most of the inconsistency, and they are also the cheapest place to fix it. [V] qml/singletons/Colors.qml and Config.qml read in full; counts by grep over qml/.
• One meaning, several colours. Failure is error #fb7185, danger #ef4444, fail/stateFail #F87171, checkFailLight #EF9A9A and pastelPink. Success is success #34d399, pass/statePass #4ADE80, checkPassLight and pastelGreen. Warning is warning #fbbf24, testing #FFD700 and checkWarn. Screens pick different ones, so "failed" is not the same red everywhere.
• Clashing themes. Dialogs use a purple palette (dialogBg #1A0A2E, dialogAccent #9B59B6, dialogFocus #E91E63) next to the steel-blue accent #3B82A0, and bgSecondary #F3F4F6 is a light grey in a dark theme.
• Dead tokens. bgSecondary, checkAccent, checkFailLight, checkPassLight and tealLight are never used.
• Bypassing the singletons. 122 hex colour literals in QML outside the singletons, most in PreflightChecklistView (30), AirspacePage (13), GimbalTest (6, including a pink gradient), MaintenancePage (5). 154 literal pixelSize values against 620 that use Config or ScreenTools.
• Two scales and copied constants. Config.qml keeps its own font and spacing sizes beside ScreenTools, and copies maintenance and PWM constants from Config.h, which can drift apart.
Fixes, in order:
Status 20 Oct: 1 and 2 done; 4 done for every QML file (no hex literals outside the singletons); the lint test in 4 and steps 3 and 5 are open.
1. In Colors.qml, define one semantic set (statusPass, statusFail, statusWarn, statusStale, statusPending, statusInfo, accent), checked for contrast on the dark background. Turn every old name into an alias of the matching token, so nothing breaks.
2. Point the dialog tokens at the normal surface and accent colours, make bgSecondary dark, and delete the five unused tokens.
3. In Config.qml, derive font sizes from ScreenTools.defaultFontPixelHeight so there is one scale, and read the C++ constants from Config.h through a context property instead of copying them.
4. Replace literals file by file, starting with PreflightChecklistView, ArmGatePanel and the arm controls. Add a QML lint test that fails on a new #rrggbb or numeric pixelSize outside the singletons.
5. Remove navigatePush and the buttons that call it, or implement it against the main window when those pages come back.
Steps 1 and the safety-screen part of step 4 are must-have item 21, because operators read go/no-go from those colours. The rest can wait.
Phase 1 must-have work
Twenty-one items. Do items 15 and 16, the arm gate corrections and the post-flight bypass, straight after item 1; the rest in table order. Flight history is fixed by item 6. Items 2 to 5 change what the gate trusts and should land together, before any new SITL evidence is gathered, because they change the behavior that evidence would measure. Every item ends with a test that is mutation-checked in the way you already work: break the fix, see the test fail, restore.
#
Workstream
Work item
Fixes
How
Done when
1
Baseline
Re-baseline the repository and tests
State analysis section 8
git status -sb, git log --oneline -10; review the three uncommitted files (the AirspeedCheck.cpp diff first); commit the roster cut and dependency pins; rebuild detached; run the gate suites one class at a time. Register or delete the five unregistered Rc checks.
A tagged commit, and raw PASS/FAIL/Totals lines for every gate suite pasted against it — Status 20 Oct: open. The five Rc checks no longer need registering (merged into RcCheck).
2
Freshness
Freshness from data age, not status age
F1, F2, F3
TelemetryBridge stamps a last-received time per stream (autopilot heartbeat, SYS_STATUS, BATTERY_STATUS, GPS_RAW_INT, EKF_STATUS_REPORT, RC_CHANNELS). Each check declares the streams it reads. The tick re-evaluates every Auto check (29 registered checks, 24 counted in the checklist, at 2 Hz is cheap) and marks it Stale when any of its streams is older than that stream's limit. Drop the "status unchanged for 10 s" rule.
Test: heartbeats continue, battery stops, battery check goes Stale and the gate closes within 5 s, then recovers. Test: no gate close over a 30-minute steady SITL idle. — Status 20 Oct: open. Display side of F3 done in QML.
3
Freshness
Count only the autopilot's heartbeat
F4
Feed heartbeatTick and connection quality only from the autopilot component (vehicle->defaultComponentId(), not a GCS or companion type). Decay quality on a timer when heartbeats stop instead of freezing at the last value.
Test: companion heartbeats alone leave the gate stale; quality falls to 0 within the staleness limit — Status 20 Oct: open.
4
Freshness
Monotonic clocks for every age and duration
F17
Replace QDateTime arithmetic in the gate, checks, bridge and session code with QElapsedTimer; keep UTC QDateTime only for stored timestamps. Inject a clock so tests can jump it.
Test: a +1 h and a -1 h wall-clock jump change no gate decision — Status 20 Oct: open.
5
Freshness
Armed state from the heartbeat only
F5
Remove _updateArmed and the gateOpened/gateClosed emits from the COMMAND_ACK handler. Acks only surface rejections and tag the arm source on the next armed transition.
Tests: heartbeat-before-ack, duplicate ack, RC arm with no ack; each leaves armed state and records correct — Status 20 Oct: open.
6
Records
SessionRecorder with connection sessions and flights
F6, F11, F12
As in Sessions and battery records: one writer, arm-to-disarm flights from armedChanged, bench flag, link-loss handling, 10 s running summary, crash recovery for both tables, check results keyed to the connection session and flight.
tests/FlightSessionLifecycleTest.cpp passes with no expected failures or skips; a kill -9 mid-flight in SITL leaves a closed, crash_recovered row — Status 20 Oct: open.
7
Records
Battery pack registry and per-flight snapshot
F7, F8, F9, F10
Move the battery modal out of Fly view into a mandatory "Battery pack" item in the preflight Power section that opens it on click; pick or edit a pack there; flight_batteries snapshot at arm; mAh and Wh from BATTERY_STATUS; BATT_CAPACITY fallback; cycles as a derived view.
Test: edit a pack after a flight and the flight's snapshot is unchanged. Test: two flights on two packs give each pack one cycle. SITL: Wh within 5% of BATTERY_STATUS.energy_consumed. — Status 20 Oct: open.
8
Records
Database startup and migrations fail loudly
F13, F14
Check initialize()'s result and show a persistent banner when logging is off. Run each migration step in a transaction; on any failed statement roll back, keep the pre-migration backup, and stop. Fix the schema version constant so v19 (and the new tables) run.
Migration test from saved v12 and v18 database files; test that a read-only database path shows the banner — Status 20 Oct: open.
9
Safety
Manual confirmations do not survive a restart
F16
Stop re-applying QSettings overrides at startup; reset manual and action checks at each new connection session; keep the history in check_results.
Test: confirm a manual check, restart, reconnect; it is Pending — Status 20 Oct: open.
10
Commands
Return without a nested event loop
F15
Send DO_SET_MODE, then confirm asynchronously from flightModeChanged with a 5 s single-shot timer; hold the vehicle in a QPointer; ignore a second press while one is pending; show pending, confirmed or failed.
Test: vehicle deleted during the wait, no crash and a "failed" result; ReturnToLaunchTest still passes — Status 20 Oct: open.
11
Commands
Disabled-with-reason Arm and Return, and the Acknowledge label
State analysis items 3 and 5
Bind enabled and a reason line to the gate's denialReason and to the connection state; never a silent no-op. Keep the one-tap Acknowledge & Arm, which already arms in this copy (see Arm gate review).
Observed in SITL: every disabled control says why — Status 20 Oct: open; needs requestArm() from item 15.
12
Field
SITL campaign, copter and plane
F18, state analysis items 6 to 9
On ArduCopter and ArduPlane SITL: force-arm with 2989 against a failing pre-arm, RTL on plane, link loss while armed, netem loss and latency pass watching parserStalled, a 3-hour idle with the quit caller identified, then two clean run-of-show dry runs.
A written run log per scenario with timestamps and the database rows it produced — Status 20 Oct: open.
13
Field
Jetson build and package
F20
Build RelWithDebInfo for arm64 on JetPack 6.2.2; fix the hard-coded x86_64-linux-gnu library path in deploy/qgroundcontrol-start.sh; run the unit test classes on the Jetson; measure CPU and memory over the 3-hour idle.
The packaged build starts on a clean Jetson, tests pass there, and memory growth over 3 hours is under a limit you set — Status 20 Oct: open.
14
Field
Real radio link and go/no-go
State analysis items 10 and section 10
Props-off bench with the real telemetry radio and RC; record each airframe's ArduPilot arming and failsafe parameters (ARMING_CHECK, FS_GCS_ENABLE, FS_THR_ENABLE, BATT_FS_*); line-by-line go/no-go over risks R-01 to R-17.
Signed go/no-go sheet per airframe — Status 20 Oct: open. (params.log is from SITL, not an airframe; review each real airframe's parameters here.)
15
Safety
Arm gate corrections (do right after item 1)
G1 to G13
Restore the bound, expiring acknowledgment; remove the mode-cycle, Override and Override Gate buttons; put Force Arm behind a confirmation in Testing sessions only; one decision function with hard and overridable blocks; observe-only inbound path; one requestArm() for every Arm button.
Every test listed under Arm gate review passes and is mutation-checked; no QML control can open the gate in one tap — Status 20 Oct: partial. QML done for G2, G3 and the G4 dialog; C++ (G1 restore, decide(), requestArm()) open.
16
Safety
Park post-flight and re-run preflight after every disarm (do with item 15)
Post-flight bypass
kPostFlightEnabled = false; resetForNextFlight() on disarm; FlightSession returns to Idle; hide the banner and summary buttons; keep code, pages and columns.
Test: arm, disarm, gate closed and every mandatory check Pending; QML smoke tests still load every page — Status 20 Oct: partial. Banner and both summary buttons hidden in QML; kPostFlightEnabled, resetForNextFlight() and the FlightSession change open.
17
Field
Reliable weather data
W1 to W7
Correct units, separate METAR and forecast with source and time shown, parse the current METAR JSON, missing means unknown, gust auto-pass only from a real METAR.
Parser tests against saved real Open-Meteo and METAR responses; displayed wind matches the source within 1 kt — Status 20 Oct: open.
18
Commands
Working gimbal pitch, roll and yaw tests
Gimbal review
Real command and attitude feedback in TelemetryBridge, MNT1 limits, per-axis measured test with return to center.
Each axis shows a measured pass or fail in SITL with a simulated mount; no gimbal means fail, not pass — Status 20 Oct: stop-gap only. Buttons disabled with a reason and no pass without feedback; real command and feedback open.
19
Safety
Bench tests never disarm a flying aircraft (do with item 15)
B1 to B4, B7
One disarm helper for both test controllers: plain disarm, only when this test armed the vehicle, refused unless landed and ground speed under 1 m/s. Closing a dialog or page stops outputs and RC overrides but never disarms. Check what the Fly view disarm really sends.
SITL, armed and flying: close Motor Test, close the gimbal page, press STOP ALL; no ARM_DISARM is sent and the vehicle stays armed. Mutation-checked. — Status 20 Oct: partial, QML stop-gap (no stop call while flying); shared C++ disarm helper open.
20
Safety
Bench force-arm only in Testing sessions
B5, B6
Allow the fixed-wing motor test and surface sweep only in a Testing session, behind a props-off confirmation; disable test buttons with a reason while armed by anyone else; fix the "stays disarmed" text.
Test: outside Testing the buttons are disabled with a reason; in Testing no 2989 is sent before the confirmation. — Status 20 Oct: partial. B6 done in QML; Testing-only bench force-arm open.
21
Commands
One set of status colours on the safety screens
UI consistency
Semantic status tokens in Colors.qml with the old names as aliases; PreflightChecklistView, ArmGatePanel and the arm controls use only those tokens.
Pass, fail, warn and stale look the same on every safety screen; a lint test fails on new hex literals in those files. — Status 20 Oct: done except the lint test. Status tokens in Colors.qml; no colour literals on safety screens (or anywhere outside the singletons).
After Phase 1
These improve quality but do not change whether a Phase 1 test is safe or its records trustworthy.
Item
Fixes
Why it can wait
Move database writes off the GUI thread (a writer queue) and add retention for check_results and events
F19
Once item 2 stops the stale cycling and item 6 writes only on transitions, write volume is small for single-aircraft tests. Revisit if the Jetson shows UI stalls.
Per-vehicle context instead of app-wide singletons
F21
Phase 1 is one aircraft per GCS.
Split DatabaseManager into per-table repositories; type-safe telemetry access instead of property-name strings
F22
Pure maintainability; a test that every name a check reads exists on the bridge covers the main risk for now.
Make WeatherProvider::lookupIcao asynchronous
F22
Weather checks are informational and not on the arming path.
MAVLink parser vendoring decision, ASan/UBSan build, the three upstream mission test failures, MainStatusIndicatorSmokeTest teardown crash
State analysis backlog
Mitigated by the parser watchdog; mission features are out of Phase 1 scope.
Remove the AirspeedCheck ground rule and replace it with a pitot health check
Earlier read-through, item 2
Must be fixed before any plane test where airspeed is mandatory; if it cannot be done in time, make it advisory for Phase 1 and say so on the go/no-go sheet. — Status 20 Oct: AirspeedCheck is deleted in the working tree, so planes have no airspeed check; add a pitot health check if planes fly in the first field stage.
Rest of the UI cleanup: remaining hex and pixelSize literals, purple dialog theme, one font scale, unused tokens, constants read from Config.h
UI consistency
Cosmetic once item 21 fixes the status colours operators rely on. — Status 20 Oct: mostly done. Purple dialog theme retired, unused tokens deleted, all hex literals replaced. Open: pixelSize literals, one font scale, constants from Config.h.
Wire or delete the eleven unreachable pages and navigatePush; create and register MaintenanceTracker
Other gaps
Not on the arming path; nothing in Phase 1 needs them. — Status 20 Oct: open. The only reachable navigatePush button (Test Gimbal in VideoPreview) is hidden; MaintenanceTracker still not created.
Zone compliance on live position and mission legs, with polygons and real zones
Other gaps
Advisory today; airspace is recorded by hand on the go/no-go sheet for Phase 1.
Release gate for field testing
Each stage needs the previous one passed and written up; a failure sends the build back one stage, not to the start.
1. Code complete: items 1 to 11 merged, every gate and session test class passing one at a time on the tagged commit, no new quarantines.
2. SITL: item 12 complete on both copter and plane, including the 3-hour idle and the crash-recovery check of item 6.
3. Target hardware: item 13 complete, the same SITL run of show repeated once from the Jetson build.
4. Bench, props off: real radio and RC, motor and servo tests, a GCS arm, an RC arm, a force-arm and an RC disarm; check that each produced exactly one flight row with the right arm source and battery snapshot.
5. Go/no-go: item 14 signed per airframe.
6. First field test: props on, tethered or at low altitude, RC pilot in command, with a written abort plan; review the flight rows and logs before the next sortie.
Freeze and tag the build at stage 3; only fixes for failures found in stages 4 to 6 go in after that.
Open questions
• Acknowledge & Arm: resolved. Both dialog loaders already arm on accept, so the label is right once G1 is fixed; confirm in SITL.
• Pack identity: are your packs labeled physically today? Picking a pack is now required, so each pack needs a visible label such as "P03" that matches its registry entry.
• Battery cycle threshold: is 20% of rated capacity used a sensible definition of one cycle for your packs?
• Airspeed on planes: is the plane test in the first field stage, or can it follow the copter? That decides whether the AirspeedCheck rework is must-have.
• Repository state: resolved 20 Oct 2026. The working tree matches this copy; the G1 fix was lost in commit 0a71b8d22, not missing from a stale copy (see Implementation status).
• Inferred effects to confirm early: the gate flapping in F2 and the rejected check-result inserts in F11 are read from the code, not observed. A 2-minute SITL idle with the debug log on, plus a row count of check_results, settles both.

Agent instructions: Steps 0–2
Paste this section to the implementing agent. Updated 2026-10-21 (after the 20 Oct QML work, the W1/W5 weather fixes, the toolbar-chip fix and the AccelConsistencyCheck removal, all built and partly tested). Sections marked **ALREADY DONE (QML)** describe edits that are already in the working tree; do not redo them, only do the parts marked as yours.

You are working in `/home/lisabeth/Dev/qgroundcontrol`, a QGroundControl fork. **All of your changes go under `custom/`.** Do not edit anything outside `custom/` (in particular, do not touch `src/Vehicle/Vehicle.cc` or other upstream QGC code). The branch is `custom-dev-next`. Do not push, and do not open a PR.

The background is the rest of this document (`custom/STATIC_ANALYSIS_REPORT.md`). Read its "Implementation status" section and the "Status 20 Oct" notes first, then "Findings by quality attribute", "Arm gate review", "Post-flight: park it…" and "Bench test findings". Finding IDs (F1–F17, G1–G13, B1–B7) refer to that report.

The system: ArduPilot copters and planes, RC transmitter is primary. The GCS monitors, gates GCS-initiated arming, runs bench tests and logs. Safety rules that must hold after your work:
- The GCS never arms unless the gate allows it, or a deliberate, logged, token-bound override is in force.
- Disarm is never blocked by the gate.
- No bench/test UI path ever disarms a vehicle that is flying.
- No single tap in any QML control opens the gate.

## Working rules

1. **Order:** Step 0 → Step 1 → Step 2. Commit after each numbered sub-item, with a message naming the finding IDs it fixes (e.g. `gate: restore token-bound expiring ack (G1)`). End each commit message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
2. **Tests are compiled into the main binary** (Qt Test, registered with `UT_REGISTER_TEST(ClassName)`; test sources in `custom/tests/` are globbed by `custom/CMakeLists.txt`). Build with `cmake --build build -j2` (Debug, Ninja, ccache; an incremental build takes 1–3 minutes, a full one about 2.5 hours, so never clean the build directory). The binary is `build/Debug/PreflightQGroundControl`. Run one class at a time, headless, from the repository root: `QT_QPA_PLATFORM=offscreen timeout 300 build/Debug/PreflightQGroundControl --unittest:ArmSendTest > build/test-logs/ArmSendTest.log 2>&1`. Close any running instance of the app first: a second instance hangs at startup with no output. Run only one build at a time in `build/`. Never run the whole suite in one process: `MainStatusIndicatorSmokeTest` has a known, unrelated teardown segfault (see `custom/doc.md:265`). Skip it, and mention that you skipped it.
3. **Build budget: no full rebuild in Steps 0 and 1, and none in Step 2 unless a step below says so.** A full build of this tree takes about 2.5 hours on this laptop; the tree was fully built and partly tested on 20 Oct. Rules:
   - Only ever run the incremental `cmake --build build -j2`. Never delete or clean `build/`, never run `cmake --fresh`, never re-run `cmake -S . -B build` with new `-D` options, never change `CMAKE_BUILD_TYPE`, and never edit the top-level `CMakeLists.txt` or `cmake/`. Editing `custom/CMakeLists.txt` to add or remove a source file is fine: it re-runs CMake configure (about a minute), not a full build.
   - Before every build, run `ninja -C build -n | grep -c '^\['` (a dry run) and note the step count. If it is above 300, do not build: stop and report what caused it.
   - Do not run git commands that rewrite working-tree files: `git stash`, `git checkout`/`switch` of another branch or commit, `git restore` of source files, `git reset --hard`, `git clean`. They reset file timestamps and make ninja rebuild everything. `git add`, `git commit`, `git tag`, `git diff`, `git log` and `git show` are fine. To look at an old version of a file, use `git show <rev>:<path>` and read the output; never check it out.
   - Expected recompile sizes when a header changes (objects that include it): `TelemetryBridge.h` 64, `AbstractCheck.h` 55, `Config.h` 27, `ArmingGate.h` 17, `PreflightManager.h` 15, `FlightSession.h` 11, `WeatherProvider.h` 9, `HardwareTestController.h` 8, `ControlSurfaceTestController.h` 5. A `.cpp` change recompiles one object plus the link (1–3 minutes). Group the header edits of one sub-item so they share one build, and make mutation-check mutations in a `.cpp` file, not a header, whenever possible.
   - Tests are cheap (seconds per class); rebuilds are not. Re-run tests freely; build only after a source change.
4. **Mutation-check every new or restored test:** break the fix, confirm the test fails, restore it, confirm it passes. In your final report, list each test with the mutation you used.
5. Match the surrounding style: Qt6, `QStringLiteral`, `qWarning().noquote()`, Doxygen `///` comments on public API, "FORK PATCH:" comments in QML when you remove or hide a control, and only `Colors.*` tokens for colours in QML (no hex literals outside `qml/singletons/`).
6. Do not add features beyond what is listed. Do not start Step 3 work (`SessionRecorder`, new tables, battery packs). Where Step 2 needs a hook for that later work, leave a `// TODO(step3):` comment only.
7. If a test that passed at baseline fails after your change and the failure is not an intended behaviour change, stop and fix it before continuing. If a test asserts behaviour this task deliberately removes (for example, the one-tap override or Passive mode), update or delete it and say so in the commit message.
8. When something is ambiguous, choose the option that fails closed (denies the arm), and record the decision in your final report.

---

## Step 0 — Verify the existing work and record the baseline (no build)

The tree was fully built on 20 Oct (`build/Debug/PreflightQGroundControl`, linked 20 Oct 16:58) and part of it was tested. Step 0 must not rebuild anything. It checks that the work already in the working tree is correct, records a test baseline against the existing binary, and commits.

1. **Confirm the build is current.** `ninja -C build -n | grep -c '^\['` must print 0. If it does not, run the incremental `cmake --build build -j2` once (it should be small) and report what it rebuilt. Do not change any source file in Step 0 except to fix a defect you find in item 2.

2. **Verify, don't redo, the work already in the working tree.** `git status` in `custom/` shows four sets of uncommitted changes. Read each diff (`git diff HEAD -- <path>`) and check it against the items below. Report anything that is wrong, incomplete or inconsistent with this document. Fix only a defect that is local and obvious (for example a typo, a wrong property name, or a missing import), and report every fix you make.
   - **The roster cut (the user's):** AirspeedCheck, BatteryTemperatureCheck, GimbalLinkCheck, MotorTemperatureCheck and AccelConsistencyCheck deleted, plus edits to `PreflightManager.{h,cpp}`, `AbstractCheck.h`, `RcFailsafeCheck.cpp`, `PreflightChecklistFilterModel.cpp`, `PreflightManagerTest.cpp`, `custom/CMakeLists.txt`, and parts of `ChecklistEngine.qml`, `PreflightChecklistView.qml`, `WeatherInfoPanel.qml` and `GimbalTest.qml`. Check: no source still includes or names a deleted check; `custom/CMakeLists.txt` lists none of them; the app log line "PreflightManager: 29 unique checks registered" appears in `build/test-logs/PreflightManagerTest.log`.
   - **The QML safety and UI work** (about 29 QML files, `custom.qrc`, `qml/cpts/qmldir`, new `qml/cpts/SessionStatusChip.qml`) and one C++ line in `PreflightPlugin::toolBarIndicators()`. Check against the "Done in the working tree" list in "Implementation status":
     - `grep -rnE "overrideGate|ArmingGate\.mode\s*=[^=]" custom/qml` finds nothing (`ArmingGate.mode === …` display comparisons are expected and fine);
     - Force Arm in `ArmGatePanel.qml` is visible only for `FlightSession.isTesting && !gateOpen` and opens `forceArmConfirm`;
     - in `MotorCheckPanel.qml`, every path to `motorDialog.open()` goes through `propsConfirm`, and `_safeStopAll()` guards every `stopAll()` (the only direct call is inside `_safeStopAll()` itself);
     - `GimbalTest.qml` routes `stopAllSurfaces()` through `_safeStopAllSurfaces()`, and the STOP ALL button in `ControlSurfacePanel.qml` is `enabled: … && !_flying`;
     - `SessionStatusChip.qml` has `showIndicator` and is listed first in `toolBarIndicators()`;
     - `custom/qml/QGroundControl/Controls/FlyViewToolBar.qml` has no diff (that file never loads because the stock one is AOT-compiled);
     - no `"#rrggbb"` colour literal exists in `custom/qml` outside `qml/singletons/`.
   - **Weather fixes W1 and W5:** `src/utils/WeatherProvider.{h,cpp}` sets `wind_speed_unit=ms` and names the METAR flight category in `weatherDescription()`; the new `WeatherProviderTest::testFlightCategoryDescription` exists and was mutation-checked on 20 Oct.
   - **Docs:** `STATIC_ANALYSIS_REPORT.md`, `TEST_CHECKLIST.md` and `UI_change.md`.
   Do not revert any of it.

3. **Record the baseline against the existing binary.** With the app closed, run each class below separately (command in rule 2) and save the raw PASS/FAIL/SKIP/Totals lines to `custom/tests/baseline-step0.txt`, one block per class, with the binary's link time at the top: ArmSendTest, ArmingGateTest, ArmingGateExtendedTest, ArmGateResilienceTest, ArmingFlowIntegrationTest, MavLinkInterceptTest, PreflightManagerTest, StateMachineTest, FlightSessionTest, FlightSessionLifecycleTest, HardwareTestControllerTest, ControlSurfaceTestControllerTest, ReturnToLaunchTest, TelemetryBridgeTest, WeatherProviderTest, QmlShadowTest, MotorCheckPanelSmokeTest, FlyQuickActionsSmokeTest.
   - There is no class called QmlSmokeTest: `tests/QmlSmokeTest.cpp` registers MotorCheckPanelSmokeTest, FlyQuickActionsSmokeTest and MainStatusIndicatorSmokeTest. Skip MainStatusIndicatorSmokeTest; it skips its own tests.
   - Already run against this tree on 20 Oct: QmlShadowTest 5/5, MotorCheckPanelSmokeTest 5/5, FlyQuickActionsSmokeTest 8/8, WeatherProviderTest 8/8, PreflightManagerTest 11 passed / 3 failed. Re-run them anyway (seconds each) so the baseline file comes from one binary.

4. **Commit, in three commits:**
   1. The roster cut, including the AccelConsistencyCheck removal.
   2. The QML work plus the `PreflightPlugin::toolBarIndicators()` line. Where one file has changes from both (`ChecklistEngine.qml`, `PreflightChecklistView.qml`, `WeatherInfoPanel.qml`, `GimbalTest.qml`), put the whole file in this commit and say so in its message. Commit `STATIC_ANALYSIS_REPORT.md`, `TEST_CHECKLIST.md` and `UI_change.md` with it.
   3. The weather fixes (`weather: Open-Meteo wind in m/s; name METAR flight category (W1, W5)`) plus `custom/tests/baseline-step0.txt`.
   Tag the last one `phase1-step0`. Leave `REQUIREMENTS.md` uncommitted unless it is already tracked. Committing does not touch file timestamps, so it costs no rebuild.

Expected baseline failures:
- The ArmSendTest ack tests (`testUnspentAckDeniesStockArmThenAllowsBoundArm`, `testUnusedAckExpiresAndDenies`) probably fail. That is G1. Record it; it is fixed in 1.1.
- PreflightManagerTest fails 3 tests (`testAllMandatoryPassed`, `testMarkArmedDisarmed`, `testArmedSurvivesTick`). They confirm every check and expect arming to be allowed, but `confirm()` refuses Auto checks and every Auto check is mandatory once a vehicle kind is applied. Record them; fix them in Step 1 (see 1.6) by driving the Auto checks to Passed through telemetry (MockTelemetryBridge), never by weakening `confirm()`.
- If any other class fails, record the exact failure and say whether the 20 Oct work could have caused it. Fix it in Step 0 only if the fix is a local QML or test correction that needs no header change.

---

## Step 1 — Quick safety fixes

No full rebuild is needed or allowed here (rule 3). Every sub-item is a handful of `.cpp` edits plus at most one header, so each build should be 1–20 minutes. To keep header rebuilds to a minimum:
- Do 1.4 and 1.5 back to back and make both `Config.h` edits (`kForceDisarmParam2` rename, `kPostFlightEnabled`) in the first of them, so `Config.h` is rebuilt once (27 objects).
- In 1.1 and 1.2, put both `ArmingGate.h` changes (the ack members if any change, dropping `Q_INVOKABLE` from `overrideGate`, the read-only `mode`) into one build (17 objects).
- 1.3 changes the `TelemetryBridge::forceArm` signature in `TelemetryBridge.h` (64 objects). Make it once and get the signature right first time.
- Do not edit `AbstractCheck.h` in Step 1 (55 objects); its pending comment fix moves to 2.2.

### 1.1 Restore the bound, expiring acknowledgment (G1)
The fix already existed. Commit `2fd409b49` added it, and `0a71b8d22` removed about 20 `m_ack*` lines from `custom/src/core/ArmingGate.cpp`. `ArmingGate.h` still declares `m_ackTimer`, `m_ackToken` and `m_ackExpiryMs` (lines ~171–174), and `ackTokenForArm()` (line ~99) returns `m_ackToken`, which is never set.
- Inspect `git show 2fd409b49 -- custom/src/core/ArmingGate.cpp custom/src/adapters/TelemetryBridge.cpp custom/src/adapters/TelemetryBridge.h` and `git show 0a71b8d22 -- custom/src/core/ArmingGate.cpp`.
- Re-apply the ack logic by hand onto the current file. Do not cherry-pick wholesale: `0a71b8d22` made other, intentional changes that must stay.
- Required behaviour:
  - `acknowledgeOverride(...)` mints a fresh, non-zero token and starts a single-shot `m_ackTimer` of `m_ackExpiryMs` (default `kAckExpirySec * 1000`). The timer clears the ack.
  - The interceptor's ack branch (~`ArmingGate.cpp:385-393`) authorizes only a call whose token equals `m_ackToken`, then consumes the ack.
  - A stock QGC arm (token 0) never spends the ack.
- Done when `testUnspentAckDeniesStockArmThenAllowsBoundArm` and `testUnusedAckExpiresAndDenies` in `custom/tests/ArmSendTest.cpp` (~849–919) pass and are mutation-checked.

### 1.2 Remove the one-tap gate controls (G2, G3)
**ALREADY DONE (QML):** the mode-badge `MouseArea` in `qml/cpts/ArmGatePanel.qml`, the "Override" button there, and "Override Gate" in `qml/cpts/PriorityFixPanel.qml` are removed. No QML references `overrideGate` or assigns `ArmingGate.mode`.

**Yours (C++):**
- In `ArmingGate.h`, remove `Q_INVOKABLE` from `overrideGate()`. If no C++ caller remains, delete `overrideGate()` and its window logic (`ArmingGate.cpp` ~252–268, ~433–438). The tests that call it directly (`ArmingGateTest.cpp:91`, `ArmingGateExtendedTest.cpp:60`, `ArmingFlowIntegrationTest.cpp:357`, `MavLinkInterceptTest.cpp:182`) must then be updated or removed.
- Make the `mode` property read-only from QML (`CONSTANT` or no WRITE), default Active.
- Add a guard test (in QmlShadowTest or FlyQuickActionsSmokeTest, or a source-text test over the qrc files) asserting that `ArmGatePanel.qml` and `PriorityFixPanel.qml` load and contain no `overrideGate` reference and no `mode =` assignment.

### 1.3 Force Arm only in Testing sessions, behind a confirmation (G4, part of B5/B6)
**ALREADY DONE (QML):** in `ArmGatePanel.qml`, Force Arm is visible only when `FlightSession.isTesting && !gateOpen`. It opens the `forceArmConfirm` Dialog (pilot name, reason, and a "Propellers removed" box are all required). `onAccepted` calls `root.requestForceArm()`, which calls the zero-argument `TelemetryProvider.forceArm()`, marked `TODO(step1.3)`.

**Yours (C++, then one QML line):**
- Change `TelemetryBridge::forceArm` (~`TelemetryBridge.cpp:1332-1390`) to `forceArm(const QString &pilot, const QString &reason)`:
  - refuse (return a reason string) outside a Testing session, and when pilot or reason is empty;
  - log pilot, reason and time with `qInfo` and through the existing compliance/event logging path;
  - close the force window as soon as its token is used, not after 60 s;
  - clear the vehicle's force token right after the command is sent (G9, ~line 1374).
- Update `requestForceArm()` in `ArmGatePanel.qml` to pass `pilotField.text.trim()` and `reasonField.text.trim()`, and remove the TODO.
- Test: outside Testing, `forceArm()` returns a refusal and sends no `COMPONENT_ARM_DISARM`. In Testing with a name and reason, exactly one command with param2 = `kForceArmParam2` is sent, and a second call needs a new confirmation.

### 1.4 Bench tests never disarm a flying aircraft (B1–B5, B7)
**ALREADY DONE (QML stop-gap):** closing the motor dialog and its stop button (`MotorCheckPanel.qml`, via `_safeStopAll()`), STOP ALL on `ControlSurfacePanel.qml` (disabled in flight), and closing `GimbalTest.qml` or pressing "Center All" (via `_safeStopAllSurfaces()`) skip the stop call while `activeVehicle.armed && activeVehicle.flying`. "Open Motor Test" is disabled with a tooltip reason when offline or armed, and every entry point (button, tile click, `openDialog()`) goes through the `propsConfirm` dialog ("Propellers are removed" required) before `motorDialog` opens. The fixed-wing warning text (B6) is fixed. Keep these QML guards as defence in depth.

**Yours (C++):**
- In `custom/src/utils/Config.h:198-204`:
  - rename `kLegacyForceArmParam2` to `kForceDisarmParam2` (value 21196);
  - fix the comment: in ArduPilot, 2989 forces **arm** and 21196 forces **disarm**;
  - remove the claim about PX4.
- Create one shared helper, `custom/src/controllers/BenchDisarm.{h,cpp}`, added to `custom/CMakeLists.txt`:
  - `static bool BenchDisarm::requestDisarm(Vehicle*, bool testArmedVehicle, QString *reason)`;
  - sends a **plain** disarm (param2 = 0), and only when `testArmedVehicle` is true;
  - the vehicle reports landed (`vehicle->flying()` is false and, if available, the landed state is on-ground);
  - and ground speed is under 1 m/s;
  - otherwise it sends nothing and fills `reason`.
- Replace every forced or unconditional disarm in `HardwareTestController.cpp` (~843, ~866, ~972, ~1403–1408) and `ControlSurfaceTestController.cpp` (~1072, ~1090–1097) with this helper.
  - Each controller tracks a `_weArmed` flag, set only when that controller sent the arm.
  - `stopAll()` and `stopAllSurfaces()` stop outputs and clear RC overrides but **never** disarm. Only the normal end of a test that armed the vehicle may disarm, through the helper.
- `HardwareTestController.cpp:965-974` (B4): if the vehicle is already armed when a motor test starts, refuse the test with a reason. Do not disarm it.
- B5: the fixed-wing motor test (~762–774) and the surface sweep (~`ControlSurfaceTestController.cpp:534-550`) may force-arm only in a Testing session. Expose a `QString testBlockedReason` property on each controller ("Testing session required", "Vehicle armed by another source", …), and bind the QML start buttons' `enabled` and a visible reason line to it.
- B7: `custom/src/core/VehicleProfileManager.cpp:645-664`. `disarmVehicle(force=true)` sends `kForceArmParam2`. Change it to `kForceDisarmParam2`. This Fly-view operator disarm is intentionally forced; leave it forced.
- Tests in `HardwareTestControllerTest` / `ControlSurfaceTestControllerTest` using the existing mocks (`custom/tests/mocks/MockVehicle.h`):
  - armed and flying: `stopAll()`, `stopAllSurfaces()` and test start send no `COMPONENT_ARM_DISARM`;
  - armed by this test, landed, below 1 m/s: the end of the test sends one plain disarm with param2 = 0.
  - `HardwareTestControllerTest.cpp:287` ("STOP ALL must disarm") asserts the old behaviour; update it.

### 1.5 Park post-flight and reset preflight after every disarm (post-flight bypass)
**ALREADY DONE (QML):** the Fly-view banner (`qml/FlyViewCustomLayer.qml`, `postFlightBanner`) and the "Post-flight summary" buttons in `qml/pages/FinalChecks.qml` and `qml/pages/LaunchReady.qml` are `visible: false` with FORK PATCH comments that name `kPostFlightEnabled`.

**Yours (C++):** the bug is that after every disarm `FlightSession` emits `postFlightChecklistRequired` (`managers/FlightSession.cpp:404-431`). `PreflightPlugin.cpp:192-193` connects that to `PreflightManager::activatePostFlightChecks` (`PreflightManager.cpp:584`), which sets every check to Skipped, and `allMandatoryPassed()` counts Skipped as passed. The gate is then open for a re-arm with no checks run.
- Add `constexpr bool kPostFlightEnabled = false;` to `custom/src/utils/Config.h`.
- When the flag is false:
  - do not make the connection at `PreflightPlugin.cpp:192-193`;
  - on disarm, call a new `PreflightManager::resetForNextFlight()`. It leaves post-flight mode, sets every check to Pending, clears manual confirmations and manual/action check state, and calls the normal re-evaluation;
  - in `FlightSession`, go from Armed back to Idle, not to `PostFlight` (`FlightSession.cpp:~414`).
- Keep `PostFlightSummary.qml` in the resources; add no DB migration. With the flag true, today's behaviour must be unchanged.
- Test: pass every check, arm, then disarm through the mock. `ArmingGate.armingAllowed` is false and every mandatory check is Pending.

### 1.6 Manual confirmations do not survive a restart (F16)
- `custom/src/core/PreflightManager.cpp:190` and `:1043-1084`: stop re-applying QSettings overrides at startup. Writing them can stay for history (add `TODO(step3)` to move them to `check_results`).
- Test: confirm a manual check, destroy and re-create `PreflightManager`, and the check is Pending.
- Also fix the 3 PreflightManagerTest baseline failures (`testAllMandatoryPassed`, `testMarkArmedDisarmed`, `testArmedSurvivesTick`). Keep what they test (all mandatory checks green → arming allowed; arm/disarm FSM transitions), but drive the Auto checks to Passed with a `MockTelemetryBridge` (`custom/tests/mocks/MockTelemetryBridge.h`) feeding passing values, or mark only the checks under test mandatory. Never weaken `confirm()` and never make Auto checks confirmable. This is a test-only change (one `.cpp`).

Run the full Step 0 test list again, write `custom/tests/baseline-step1.txt`, commit, and tag `phase1-step1`.

---

## Step 2 — Gate core rewrite

These items touch the same four units (`ArmingGate`, `AbstractCheck`, `PreflightManager`, `TelemetryBridge`). Do them in this order; each builds on the previous.

### 2.1 Injectable monotonic clock (F17)
- Add `custom/src/utils/MonotonicClock.h`:
  - an interface `IClock { virtual qint64 nowMs() const = 0; }`;
  - a default `SteadyClock` backed by a static `QElapsedTimer`;
  - a `TestClock` with `advance(ms)`.
- Provide one app-wide default, plus a setter for tests, on the classes that need it.
- Replace every **age or duration** computed from `QDateTime::currentDateTime()` with `IClock::nowMs()` in the gate, the checks, the bridge, `PreflightManager` and `FlightSession`. There are 28 call sites (`grep -rn "QDateTime::currentDateTime()" custom/src`), for example `ArmingGate.cpp:456`, `PreflightManager.cpp:749/757`, `PreflightPlugin.cpp:562-578` and `TelemetryBridge.cpp:614-615`.
- Keep `QDateTime::currentDateTimeUtc()` only where a timestamp is **stored** or displayed. Convert stored local times to UTC.
- Test: with `TestClock`, a ±1 h wall-clock jump changes no gate decision, and advancing `TestClock` past the staleness limit does mark checks stale.

### 2.2 Freshness from data age, not status age (F1, F2, F3)
- This sub-item edits `AbstractCheck.h` anyway, so do the pending comment fix in the same build: around line 244, the new `m_isInformational` member took over `m_isPostFlight`'s `///<` comment. Give each member its correct comment.
- In `TelemetryBridge`, stamp a last-received time (`IClock` ms) for each stream: autopilot HEARTBEAT, SYS_STATUS, BATTERY_STATUS, GPS_RAW_INT, EKF_STATUS_REPORT, RC_CHANNELS, plus any other message a registered check reads. Expose `qint64 streamAgeMs(Stream) const` (an enum).
- `AbstractCheck`:
  - add `virtual QList<TelemetryStream> requiredStreams() const` (default empty) and a per-stream limit (default from `Config.h`; 5 s for battery and the `PreflightManager` staleness default otherwise);
  - override `requiredStreams()` in every Auto check according to what its `evaluate()` reads.
- `AbstractCheck::setStatus` (`AbstractCheck.cpp:137-144`): record the evaluation time on **every** call, before the early return. Rename `m_lastEvalTime` to `m_lastEvalMs` (an `IClock` value).
- `PreflightManager` tick (~`PreflightManager.cpp:620-765`):
  - re-evaluate **every** Auto check on each tick, including Passed ones, keeping the exception isolation at ~681–695;
  - before evaluating, if any required stream is older than its limit, set the check Stale with the stream name and age;
  - delete the "status unchanged for `m_stalenessThresholdMs`" rule (~736–765). Keep the setter and getter if tests use them, repurposed as the default stream limit.
- `BatteryVoltageCheck` (and any other battery check) depends on the BATTERY_STATUS stream.
- **ALREADY DONE (QML), keep consistent:** `qml/singletons/VehicleTelemetry.qml` exposes `batteryDataValid` and `batteryStale` (true when valid data was seen on this connection, then lost), used by `BatteryStatusChip.qml` and `FlyViewTelemetryStrip.qml`. They read `TelemetryBridge.batteryDataValid`, which is still computed with the wall clock at `TelemetryBridge.cpp:614-619`. Move that computation onto the new stream age and `IClock`; keep the property and its signal so the QML keeps working.
- Tests:
  - heartbeats continue and BATTERY_STATUS stops: the battery check goes Stale and `armingAllowed` becomes false within 5 s of `TestClock` time; it recovers when BATTERY_STATUS resumes;
  - a Passed check whose value degrades fails on the next tick;
  - steady streams over 30 simulated minutes produce no gate closure (no 15 s flapping).

### 2.3 Count only the autopilot's heartbeat (F4)
- `TelemetryBridge.cpp:1026-1068`: emit `heartbeatTick`, update the HEARTBEAT stream stamp and feed connection quality only when `message.sysid == vehicle->id()` and `message.compid == vehicle->defaultComponentId()`. Other components' heartbeats may still be used for gimbal or companion detection elsewhere.
- Make connection quality decay on a timer towards 0 when autopilot heartbeats stop, instead of freezing at the last value.
- Test: companion (compid 191) and gimbal (compid 154) heartbeats alone leave the gate stale, and quality reaches 0 within the staleness limit.

### 2.4 Armed state from the heartbeat only (F5, G10)
- `PreflightPlugin.cpp:~900-955` (COMMAND_ACK handling): remove the `vehicle->_updateArmed(...)` call and the `emit _armingGate->gateOpened()` / `gateClosed(...)` emits. Keep `_lastArmDisarmParam` only to record **arm source**: an accepted GCS arm ack sets `pendingArmSource = Gcs` (or `ForceToken` when the force token was used), which the next `Vehicle::armedChanged(true)` consumes; with no pending source, the source is `External` (RC or another peer). Acks only surface rejections to the UI.
- Connect `Vehicle::armedChanged` to:
  - the gate's post-arm and post-disarm reset, so ack and force state are cleared on **any** disarm, including RC (G10, `ArmingGate.cpp:51-55`);
  - `resetForNextFlight()` from 1.5;
  - the existing `_onGateOpened` / `_onGateClosed` session bookkeeping, as a stop-gap until Step 3, with `// TODO(step3): replace with SessionRecorder`.
- Tests:
  - heartbeat-armed arrives before the ack: state is correct and there is one transition;
  - a duplicate ack causes no extra transition;
  - an RC arm with no ack is detected with source External;
  - an RC disarm clears an unspent ack.

### 2.5 One decision function (G5, G6, G7, G8, G9, G11, G12)
- In `ArmingGate`, add a pure function `ArmDecision decide(const ArmRequest&) const`.
  - `ArmRequest` holds: isArm, callToken, now (`IClock`), vehicle present, autopilot heartbeat age, mandatory check states, whether a Training session is active, whether a Testing session is active, whether the vehicle is flying, and `bool batteryPackSelected = true` marked `TODO(step3)`.
  - `ArmDecision` holds: `{ Allow, NeedsAck, Deny }`, a reason string, the failing check IDs, and `kind ∈ {Normal, Ack, Force}`.
- Rules, in order:
  1. Disarm → Allow.
  2. Hard blocks, never overridable → Deny with the reason: no vehicle; autopilot heartbeat older than the limit; any mandatory check Stale; Training session; no battery pack.
  3. All mandatory checks Passed → Allow (Normal).
  4. Otherwise, allow once (Ack) if a valid ack exists, meaning all of these hold:
     - it has not expired;
     - its token equals callToken;
     - the set of check IDs recorded at ack time ⊇ the IDs failing now;
     - every failing check has `canOverride()` true.
     Consume the ack and log pilot, reason and check IDs. Otherwise return NeedsAck if every failing check is overridable, and Deny if any is not.
  5. Force (Testing session only, vehicle not flying, matching force token) → Allow once (Force), and close the window.
  6. Else → Deny.
- `acknowledgeOverride(pilot, reason)` also records the current set of failing mandatory check IDs. If a **new** check fails after the ack, the ack is void (G5).
- Route the interceptor (`ArmingGate.cpp:~360-404`), the `armingAllowed` property and two new invokables through `decide()`:
  - `Q_INVOKABLE QVariantMap requestArm()`: decides and, on Allow, arms;
  - `Q_INVOKABLE QVariantMap previewArm()`: decides with no side effect. The toolbar chip needs it.
  - `armingAllowed` answers "would a plain arm with token 0 pass now", so it is false during a force window (G8).
  - Add a separate `overrideKind` property for the UI.
  - Remove the `const_cast` mutation (~374, ~386); do the consumption in a non-const wrapper after `decide()` returns.
- Inbound path (`ArmingGate.cpp:126-187`, `PreflightPlugin.cpp:902-919`) becomes observe-only (G7): it logs a peer's arm as an event and posts an alert through `AlertManager`, and never consumes the ack, opens a window or changes gate state.
- G9: never reset the token counter in `resetGate()` (~line 277), or switch to `QRandomGenerator::system()->generate()` with 0 excluded.
- G12: delete `processArmRequest` and its tests (`ArmingGateTest.cpp:44-99`, `ArmingGateExtendedTest.cpp:~121`, about 12 references). Merge Hybrid into Active (keep the enum value as an alias if QML or the DB references it; `ArmGatePanel.qml` and `PreFlightChecklist.qml:752` still display the mode name). Move the gate-state writes in `onCheckFailed` and `interceptCommandLong` (~192–225, ~408–420) into the single evaluation path.
- G11: wire every Arm button through `requestArm()`, as in the inventory below.
- G13 (verify only): read how upstream `src/Vehicle/Vehicle.cc` resends an unacknowledged `COMMAND_LONG` and whether the resend passes through the interceptor again. Do not edit `Vehicle.cc`. If a retry would hit a consumed one-shot token, make the token survive retries of the **same** command until an ack or timeout. Add a test that drops the first arm ack and confirms the retry still arms exactly once.

#### Arm-button inventory (G11)
Each must call `ArmingGate.requestArm()` (or `previewArm()` where noted) and branch on the result:
- `Allow` → arm;
- `NeedsAck` → open `ArmGateDialogLoader`, and on accept call `TelemetryProvider.arm()`;
- `Deny` → show `reason` and send nothing.

Remove the per-button decision logic listed in the table.

| # | File:line | Today | Change |
|---|---|---|---|
| 1 | `qml/MainStatusIndicator.qml:353-381` (status-drawer Arm) | Opens the dialog only if `failedChecks > 0 && !armingAllowed && !overrideActive`; otherwise calls `arm()` | Pending, manual and stale denials arm silently. Use `requestArm()` |
| 2 | `qml/MainStatusIndicator.qml:59-75` (dialog accept) | `arm()` | Keep |
| 3 | `qml/PreflightToolbarIndicator.qml:60-64, 115-120` (toolbar chip) | Opens the dialog on any denial, including stale or no telemetry (G5); otherwise opens Analyze | Use `previewArm()`: NeedsAck opens the dialog, Deny shows the reason, and the chip never arms |
| 4 | `qml/PreflightToolbarIndicator.qml:122-139` (dialog accept) | `arm()` | Keep |
| 5 | `qml/ArmedIndicator.qml:24-41` (Fly-view armed dropdown) | Calls `arm()` directly; never offers the dialog | Full branch; add an `ArmGateDialogLoader` to this file |
| 6 | `qml/cpts/WizardStep4Review.qml:339-351` ("✓ Arm Vehicle") | `arm()` directly, or the weather dialog first | Keep the weather dialog as a step before the arm, then branch |
| 7 | `qml/cpts/WizardStep4Review.qml:92-101` (weather "Proceed with Caution") | `arm()` directly | Same as 6 |
| 8 | `qml/cpts/ArmGatePanel.qml` (Force Arm) | `forceArm(...)` after confirmation (Testing only) | Not a `requestArm()` path. It stays separate (rule 5) |

Notes:
- `TelemetryBridge::arm()` returns `void`; `requestArm()` is the source of the reason. Optionally have `arm()` return the denial `QString` too.
- The stock QGC arm paths (`src/UI/toolbar/ArmedIndicator.qml:37`, `src/QmlControls/MainStatusIndicator.qml:210` via `mainWindow.armVehicleRequest()`) are outside `custom/`. They reach the interceptor with token 0 and must stay denied unless the gate is fully open. The custom `MainStatusIndicator.qml` may be shadowed by the stock one (shared qrc alias). Add a QmlShadowTest assertion that the custom file wins, or report that it does not.
- Disarm paths (`mainWindow.disarmVehicleRequest()`, `VehicleProfileManager.disarmVehicle(isFlying)` in `FlyQuickActions.qml`, and `HardwareTestController.disarmVehicle()` in `MotorCheckPanel.qml`) never go through `requestArm()`.

### 2.6 Tests for Step 2.5 (each mutation-checked)
Add these to `ArmSendTest` / `ArmingGateExtendedTest` / `ArmGateResilienceTest`, whichever fits best:
1. A stock arm (token 0) cannot spend an ack.
2. An ack expires after `m_ackExpiryMs`.
3. A new check failure after the ack voids it.
4. An ack cannot override a stale-telemetry block.
5. An ack cannot override a non-overridable failing check.
6. Training blocks the GCS's own arm.
7. No QML control can change the mode or open a window in one tap (the 1.2 guard test).
8. An RC disarm clears an ack.
9. A peer's inbound arm does not consume the ack and does not change gate state.
10. A dropped first ack still arms exactly once.
11. Disarm is allowed in every gate state, including no telemetry, stale data and Training.
12. `armingAllowed` is false during a force window while `overrideKind == Force`.

Run the full test list again, write `custom/tests/baseline-step2.txt`, commit, and tag `phase1-step2`. Then update this document: move every item you finished from "Still open" to "Done" in "Implementation status", update the matching "Status 20 Oct" notes (add the date of your change), and commit that too.

Stop after Step 2. Do not start Step 3 (SessionRecorder, battery packs, database migrations); it gets its own instructions once Steps 0–2 are reviewed.

---

## Final report (your last message)
- The commits and tags you created.
- For each sub-item (0, 1.1–1.6, 2.1–2.6): what changed (files), the tests added or changed, and the mutation used for each test.
- Raw PASS/FAIL/Totals for every test class at `phase1-step2`, compared with `phase1-step0`.
- Every test you deleted or whose expectation you changed, and why.
- Decisions you made where this prompt was ambiguous, and anything you could not finish.
- What you found for G13 in `Vehicle.cc`.
- Do not claim SITL behaviour; nothing in this task is verified in SITL.
