Skywin GCS Phase 1 Readiness Plan
20 Oct 2026 · @Arsema

Implementation status (read this first)
First written 20 Oct 2026; status updated 22 Oct 2026 (Step 3); latest additions 26 Oct 2026 (GCS failsafe decision, battery-pack waiver, payload item, Fly-view target; the power-type design is in custom/POWER_TYPES_PLAN.md). Steps 1–2 are tagged phase1-step1 / phase1-step2; Step 3 is committed and unit-tested but not yet verified on the aircraft (see "Still open"). The findings further down are left as originally written; this section says which are fixed, which were wrong, and what is still open. Each finding, defect and work item below also carries a "Status 20 Oct" note. Step-by-step instructions for the remaining Steps 0–2 work are in the last section, "Agent instructions: Steps 0–2".

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

Done in Step 1 (C++, committed and tagged phase1-step1, 21 Oct)
• db4e4277b Step 0 baseline (tag phase1-step0). Its baseline-step0.txt only recorded ArmSendTest, not the 18 classes; baseline-step1.txt is the first full run.
• dc6a42757 1.1 G1: token-bound, 15 s expiring acknowledgment restored.
• b8d611ff6 1.2 G2, G3: one-tap controls gone, mode read-only, overrideGate() not invokable, QML guard test.
• a2281c28b 1.3 G4, G9 (part): force arm only in Testing sessions with pilot and reason, logged; HardwareTestController holds the vehicle in a QPointer.
• 15b93be70 1.4 B1–B5, B7: BenchDisarm helper; no bench path force-disarms; tests refuse on an externally armed vehicle; testBlockedReason in the UI; kForceDisarmParam2.
• 9449b5a14 1.5 and 1.6: post-flight parked with resetForNextFlight(); confirmations not restored at startup (F16).
• Test state at phase1-step1 (custom/tests/baseline-step1.txt): 16 of 18 classes fully pass. Known: ArmGateResilienceTest battery-stream freshness test fails until Step 2.2; FlightSessionLifecycleTest segfaults while its third MockLink vehicle connects (stock MockLink lifetime, Step 3 test).
• Build cost lesson: adding BenchDisarm.{h,cpp} to custom/CMakeLists.txt re-ran CMake, which regenerated the precompiled header and forced 509 recompiles (73 minutes), although the dry run beforehand showed 1 step. Adding or removing a source file is not cheap; prefer adding code to an existing .cpp.

Done in Step 2 (C++, tag phase1-step2, 21 Oct)
• 637c32446 2.1–2.3 F1–F4, F17: header-only MonotonicClock (TestClock for tests); per-stream arrival stamps in TelemetryBridge; checks declare the streams they read and go Stale when one stops (seen-then-lost only); every Auto check re-evaluated each tick; autopilot-only heartbeat; connection quality decays.
• 57e9ef29c 2.4 F5, F6 (part), G10: ArmStateTracker — armed transitions only from armedChanged; ACKs label GCS / External / ForceToken.
• 0db57a88f 2.5 G5–G9, G11–G13: one pure ArmingGate::decide() used by armingAllowed, the outbound interceptor, the read-only inbound path and the new requestArm()/previewArm(); acknowledgment bound to the failing checks; force window closes on use; overrideKind; gate refusals and autopilot rejections shown as app messages. User decisions: acknowledgment may override any failing check except the hard blocks; a peer's arm is denied but never mutates the gate.
• Test state at phase1-step2 (custom/tests/baseline-step2.txt): 18 of 19 classes fully pass; FlightSessionLifecycleTest still segfaults on its third MockLink connection (Step 3 test).
• After Step 2 (21 Oct): item 10 done (Return without a nested event loop). RC check fixes from bench testing with a real ArduPilot RC link: RSSI 255 ("not reported", RSSI_TYPE = 0) no longer reads as RC failsafe, so RC Failsafe and RC RSSI no longer fail with a working transmitter; RC calibration no longer judges the throttle channel's trim against 1500 (ArduPilot puts it at the stick bottom); the throttle-minimum check was inverted (passed at any position >= 900 µs) and now passes only in the bottom 10 % of the calibrated range, honouring channel reversal. Gimbal page: the control-surface card is now laid out at full height so all five tests scroll into view; "Back to Checklist" matches the wide "Next: Gimbal Test" button.
• Finding: custom/qml/MainStatusIndicator.qml (same resource path as the AOT-compiled stock file) and custom/qml/ArmedIndicator.qml (nothing references it) never load. QmlShadowTest checks resource content, not what the engine instantiates, so it does not catch this.

Done in Step 3 (22 Oct; code committed and unit-tested, NOT yet verified on the aircraft or in SITL)
• Item 8 (da5a75260): transactional migrations, version 23; storedSchemaVersion() bug that re-ran every migration on every launch fixed; "FLIGHT LOGGING IS OFF" banner.
• Item 7 (da5a75260, ade82ce18): required Battery pack item in the Power section (every session mode, gate hard block), pack registry, 20 % cycle rule, per-flight frozen pack snapshot with mAh/Wh from the FC counters (F7–F10).
• Item 6 (9f39fcae6, ec506bb00): SessionRecorder — connection sessions, one flight per arm (RC arms included), link-loss and crash recovery, 10 s summary, check results keyed (F6, F11, F12); every flight also gets a Flight History row.
• Item 18 (df0791266): real gimbal pitch/roll/yaw tests on measured attitude; gimbal target saves (da5a75260).
• Item 17 W2–W4, W6 and item 21 lint (ade82ce18). Attitude readout on the gimbal page and the Fly view (9f39fcae6). Hidden checklist rows fixed (9f39fcae6).
• Trainer handover logging (ec506bb00, 651dddbef), refusing a trainer channel equal to FLTMODE_CH.
• Disabled motor / surface tests now say why on screen (60e0fdd18): they arm the vehicle, so they need a Testing session.
• Bench-test results linked (23 Oct, v24): motor, surface-sweep and gimbal-axis results were written with flight -1 / 0 / NULL (all 95 hardware_test_events rows on the live database). They now carry the connection session and, when a (bench) flight is open, its Flight History row, so they show in the flight's detail. Two load-time QML warnings removed (a handler for a TelemetryBridge signal that does not exist; anchors inside a layout in MotorCheckPanel).
• One database (23 Oct): the database folder followed the application name, which changes during start-up ("PreflightQGroundControl Daily" -> "Skywin GCS"), so records were split over two files. It is now fixed at <data>/UAVPreflight/Skywin GCS/uav_preflight_data.db whatever the name (unit tests keep their own "_unittest" folder). The old databases were moved, not deleted, to UAVPreflight/backup-before-clean-20261023-094244/ at the user's request for a clean start.
• check_results volume (review note, 23 Oct): a row is written on each status change. Checks are evaluated once a second, so a flapping check writes at most one row per second; no throttle was added, because dropping rows would make the audit trail wrong. Revisit if the database grows too fast on long sessions.
• Review fixes 23 Oct (c84b41fa8): app exit while armed no longer runs the recorder's callbacks on deleted objects; crash-recovered Flight History rows close at the flight's last summary instead of "now"; the gimbal target is also stored on the Flight History row. Reconnect crash in ControlSurfaceTestController fixed (see FlightSessionLifecycleTest below).

Still open (22 Oct)
Verification (operator, no code): see "Bench and SITL checks for Step 3" at the end of this document.
• Not yet seen in the running app: Battery pack dialog, Fly-view ATTITUDE column, trainer chip, weather source line, Flight History rows (the app running on 22 Oct afternoon was the 11:35 build).
• Trainer handover on the GX12 pair: needs a trainer-switch mix on a free channel (CH8 is the plane's flight-mode switch).
• SITL: kill -9 mid-flight leaves a crash_recovered row (item 6); Wh within 5 % of BATTERY_STATUS.energy_consumed (item 7); gimbal axis tests with MNT1_TYPE = 1 (item 18).
• tests/FlightSessionLifecycleTest.cpp: fixed 23 Oct, passes 5/5 with no expected failures or skips. The crash was not MockLink: ControlSurfaceTestController held a raw Vehicle* that dangled after every disconnect, and the next connection disconnected signals from freed memory — on the aircraft that is any reconnect (the plane reconnected twice on 22 Oct). It is a QPointer now, and the plugin hands both bench controllers "no vehicle" on disconnect. The test's scenario (c) was rewritten for the recorder design (two stale QEXPECT_FAILs removed; counts are relative to a baseline because the unit-test database is not emptied between tests).
• Tag phase1-step3 once the bench checks pass.
Aircraft findings from the 22 Oct bench session (fixed wing, CoreWingF405WingV2, ArduPlane 4.5.7, ELRS 16 ch) — fix before flying:
• Roll reads about -46° with the airframe still (pitch follows a tilt): check AHRS_ORIENTATION and redo the accelerometer calibration if the plane sits level.
• "MS4525: no sensor found / Airspeed 1 init failed": connect the airspeed sensor or set ARSPD_TYPE = 0.
• Two GCS-failsafe RTLs from USB link drops (14:32, 14:34): check the cable and port; over the telemetry radio this is what sends the plane home.
• After the RC link was lost, CH3 (throttle) read 1922: check the ELRS receiver failsafe is "No pulses", not a fixed high-throttle position.
• Battery monitor now reads correctly (23.3 V, 1.13 A, mAh and energy counters): the morning's 0 V / "unhealthy" is resolved.
Code still open:
• Item 17 W7 (SITL weather location) and item 14 (real radio and go/no-go; template at the end of this document). Item 12 (SITL campaign) done 24 Oct. Item 13 (Jetson build and its start script) moved to after Phase 1 on 23 Oct.
• Pre-demo audit (24 Oct, report kept outside the repo at ~/skywin-audit/pre-demo-audit-2026-10-24.md). Fixed 24 Oct: C1, the Fly-view Disarm sent ArduPilot's forced disarm (21196) whenever armed, which skips the firmware's "no disarm while flying" refusal; it now always sends a plain disarm. H1, the weather and airspace checks registered as informational became mandatory once the airframe resolved, so a METAR/TAF fetch or a zone result could hold the gate shut; they are now never mandatory, and allMandatoryPassed()/armingBlocker() skip them like the gate's own failing list. Both are mutation-checked (FlyQuickActionsSmokeTest::testDisarmIsNeverForced, PreflightManagerTest::testInformationalChecksNeverGateArming and testMandatorySetPerAirframe). H2 fixed 24 Oct: the arming gate now writes every override and refusal to compliance_logs (same table as the force-arm audit), with pilot, reason, the failing check IDs, vehicle and UTC time: override_acknowledged, override_used, override_expired, gcs_arm_denied (the GCS's own or a stock arm refused by the gate) and peer_arm_denied (an arm from another link refused). Mutation-checked by ArmSendTest::testAckOverrideRecordedWithPilotAndChecks and testGcsArmDenialAndAckExpiryRecorded; peer_arm_denied and the requestArm() refusal are written by the same helper but not pinned by a test. Still open from the audit: H4 Jetson packaging (start script binary name and arch path, Release build, version string; part of item 13). H5 fixed 24 Oct (user decision: turn them all off): custom/qml/FlyView.qml overrides the stock GuidedActionsController so mission start/continue/resume, takeoff, pause, change altitude/speed/heading/loiter radius, Go to, Orbit, ROI, Set home, Set estimator origin and the in-flight Emergency Stop (a forced disarm) are all off; a tap on the Fly-view map now opens nothing. Kept: RTL, Land, Land Abort, Arm/Disarm (gated), Gripper. Guarded by FlyQuickActionsSmokeTest::testGuidedActionsOffExceptRtlAndLand. Smaller audit items fixed 24 Oct: the motor panel's Disarm sends nothing while the vehicle reports flying (M1); an unspent acknowledgment or force window is cleared when the active vehicle changes (M3); the force-arm funnel refuses when no arming gate is registered instead of sending unconditionally (M4, ArmSendTest::testForceArmRefusedWithoutGate, mutation-checked); VehicleProfileManager::forceArm() no longer falls back to an ungated vehicle->forceArm() (M5); the debug summary prints the real roster size instead of "/91"; every QTemporaryFile::open() in the tests is checked (36 warnings gone; custom/ now builds with no warnings); ArmSendTest::testStockSetArmedBypassesGate renamed testStockSetArmedBlockedByGate (it asserts the stock arm is blocked). Audit item M2 was a false alarm: on disconnect VehicleProfileManager resolves the kind to UNKNOWN, which sets m_kindResolved = false. H4 (Jetson packaging and Release build) is with item 13. Full suite after these changes: 33 classes, 0 failures (tests/baseline-step3.txt). H3 fixed 26 Oct (user decision: pack required, with a waiver checkbox that gives up the flight-time estimate): decide() checks the pack before the force window, so a bench force arm needs a pack too. The pack dialog has "Fly without a registered pack" (checkbox + pilot name); VehicleProfileManager::waiveBatteryPack() writes battery_pack_waived to compliance_logs, the Battery pack item shows a Warning (does not block), and the Fly-view battery chip and status-bar estimate show the charge only, no flight time (the stored S/P/mAh may belong to another pack). The waiver is dropped on disarm, on a vehicle change and when a pack is chosen; a waived flight has no serial, so no pack's cycles are counted. Also fixed: compliance_logs.vehicle_id is NOT NULL, so gate events and waivers with no vehicle connected were silently dropped; they are now kept under "unknown". Tests: ArmingGateTest 17/17 (testNoBatteryPackIsHardBlock now expects the force token to be denied; new testBatteryPackWaiverAllowsArmWithoutEstimate); 5 mutants caught (force token past no pack, waiver ignored, waiver never cleared, waiver without a name, waiver not logged). Not pinned by a test: the plugin wiring that clears the waiver on disarm/vehicle change, and the chip hiding the estimate. Not tried in the real app. Changed 26 Oct after bench use: the control-surface and motor tests force-arm and disarm on their own, and each of those disarms cleared the waiver; now only the disarm that ends a non-bench flight clears it (bench = Testing mode, force arm, motor test — the same flag the flight record uses). Fly-view info box: the "Tgt" cells were a fixed "--"; they now show the distance (m/km) and bearing from the vehicle to the target saved on the gimbal page (FlightSession.targetLat/Lon, reloaded when the vehicle or flight changes), "--" with no target or no GPS fix. The box read the vehicle position from gps.latitude/longitude, which do not exist (the GPS facts are lat/lon), so the Plan distance never worked either; it now uses the vehicle coordinate. FlyQuickActionsSmokeTest::testTelemetryStripTargetIsLive (source scan; 2 mutants caught). Live check 26 Oct (new LivePluginBenchTest in QmlSmokeTest.cpp): the real PreflightPlugin against QGC's mock ArduPilot plane. (1) TelemetryBridge::forceArm() in Testing with no pack sends no arm command (the Battery pack item is mandatory on the live roster); after the waiver the same call arms. (2) Two bench force-arm/disarm cycles keep the waiver; an RC arm and disarm in Flight mode clears it. (3) The shipped FlyViewTelemetryStrip.qml, bound to the mock vehicle with a target saved ~400 m away, shows the distance within 1 % and the bearing within 1° of QGeoCoordinate's. Mutants caught: every disarm clears the waiver (the bug seen on the bench), force token past no pack, position read from gps.latitude. Also fixed: the force-arm audit row had the same NOT NULL vehicle_id gap (a refusal with no vehicle was dropped). Not covered: the dialog checkbox and the chip text are only checked by hand. Payload and logo (26 Oct, user request): new "Payload" item in the checklist (Airframe & Physical, both checklist pages; header-only core/PayloadCheck.h, registered by PreflightPlugin like the pack item). The pilot enters what the aircraft carries (kg; "No payload" = 0), not the aircraft's own weight; the item is Pending until entered, never blocks arming, and the entry is cleared on a vehicle change. VehicleProfileManager::enterPayloadWeightKg() refuses negative or non-finite weights; the weight is written on the flight record at arm (flight_sessions.payload_weight_kg, existing column) and feeds the flight-time estimate. The Vehicle Management payload field still sets the weight but does not count as the checklist entry (it saves on focus loss). Toolbar logo: the left logo was /res/QGCLogoFull.svg, which is really the 396x216 CustomAppIcon JPEG (light-blue box) squeezed into the stock button's square icon; the right-hand brand image is removed. The left button now draws qrc:/custom/brand/logo at its own aspect ratio, about 2.2 text lines tall; that resource is a transparent PNG made from the 771x324 logo (white lettering, teal accent, no box). To avoid a custom.qrc edit (CMake re-run) the PNG was written into the existing file behind that alias, custom/res/Images/white_bg_logo.jpg, so the file's name no longer matches its content; rename it with the next qrc change. A MultiEffect drop shadow was tried and dropped: it drew nothing under the software renderer. Tests: ArmingGateTest::testPayloadItemNeedsAnEnteredWeight; LivePluginBenchTest::testPayloadEnteredIsOnTheFlightRecord and testToolbarShowsOneSharpLogo (SKYWIN_TOOLBAR_GRAB=<file> saves a picture). Mutants caught: item passes without an entry, entered weight not stored, entry kept across a vehicle change, negative weight accepted, toolbar back to the old image.

• SITL session 26 Oct (operator, copter): (1) The Fly-view Tgt cell showed "23 @243°"; the bearing now has its own row, "Brg 243° SW" (degrees + 8-point compass), and Tgt shows the distance only ("23 m"). (2) Dist and Flight stayed 0 because the copter never left home: SITL reported it armed in GUIDED, not AUTO, and the GCS's own mission start is off (H5), so the mission must be started from the RC or MAVProxy. Not a GCS fault. (3) Found in that run's log: every end-of-flight compliance record (session-<id>) was refused, because PreflightPlugin passes no vehicle type and compliance_logs.vehicle_type is NOT NULL. saveComplianceLog now stores "unknown" for a missing vehicle id or type (and "{}" for missing JSON), so no caller loses a record this way. Tests: FlyQuickActionsSmokeTest::testTelemetryStripTargetIsLive (Brg rows bound), LivePluginBenchTest::testFlyViewTargetDistanceFromVehicle (distance-only value, bearing and compass point), DatabaseManagerTest::testComplianceLogKeptWithoutVehicleDetails. Mutants caught: Tgt back to "@" form, compass rounded down, Brg fixed "--", record without type dropped.

• Fly-view info box and logo (operator, 27 Oct): NAVIGATION drops GPS, Sats, Hdg and Brg (on the toolbar already); SYSTEM drops the battery-percent line (on the battery chip); the Tgt row shows only when a target is saved, in both layouts; then (same day) Plan removed (nothing in the app sets the plan location, so it always showed "--") and Trip removed from both layouts (RETURN NOW stays on the battery chip and in the message). NAVIGATION now: Dist (from home now), Flight (flown since arm), Tgt (with a target), Left (the 2-column layout also drops Bat and Brg; its GPS row is kept, as it has no NAVIGATION column). The Skywin toolbar logo is 2.6 text lines tall (was 2.2). Tests: FlyQuickActionsSmokeTest::testTelemetryStripTargetIsLive (Tgt conditional, removed rows stay removed), LivePluginBenchTest::testFlyViewTargetDistanceFromVehicle (rows actually shown); 3 mutants caught.

Flight-time estimate review (23 Oct)
• A — done (live estimate, Fly-view battery chip): the reserve was taken from what is left (remaining × (1 − reserve)), so the time never reached 0 at the reserve and was overstated most near the end (5 Ah pack, 20 % reserve, 20 A: 2.4 min shown at 20 % SOC instead of 0). Now usable = capacity × (SOC − reserve), 0 at or below the reserve, and the chip shows "RESERVE — LAND" in red. VehicleProfileManagerTest::testUsableAhStopsAtTheReserve (mutant: old rule, caught).
• B — done: the live charge is the selected pack's (rated mAh − the FC's mAh used; the FC's own %, relative to its BATT_CAPACITY, only as fallback), so the time no longer depends on BATT_CAPACITY matching the pack. The Battery pack item warns (does not block) when BATT_CAPACITY differs from the pack by more than 5 %, with the value to set. Like ArduPilot, this assumes the pack was full when the FC started counting. Tests: VehicleProfileManagerTest::testPackSocFromSelectedPackNotFcCapacity (mutant caught), ArmingGateTest::testBatteryPackWarnsOnFcCapacityMismatch.
• D–H — done: the pre-flight estimate is built from the selected pack (cells × nominal cell voltage for its chemistry × Ah above the reserve at its current charge; a full pack is assumed, and said, before battery data arrives), uses the vehicle's real airframe type, and gives time = usable Wh ÷ Wh per minute and range = usable Wh ÷ Wh per km, both learned from the vehicle's own flights or per-airframe defaults (range = time × a default cruise speed then). The model learns only from flights that are not bench runs, ended by a normal disarm, flew at least 500 m and lasted at least 60 s. VehicleProfileManager::batteryWh() no longer guesses the cell count from the voltage. Found while doing this: the custom MainStatusIndicator that showed this estimate never loads (the compiled stock toolbar wins), so the estimate was not visible anywhere; it is now on the Fly-view battery chip while disarmed ("87% · est ~25 min", with time / range / source in the chip's detail). Tests: BatteryEstimatorTest::testUsablePackEnergyFromThePack, ::testAirframeDecidesTheDefaults, DatabaseManagerTest::testEnergyModelLearnsOnlyFromRealFlights.
• Test-suite gap found 23 Oct: 34 test classes are registered but the suite list (baseline files) ran only 22. The other 12 had not been run in these sessions; BatteryEstimatorTest had a stale "schema version 18" assertion (fixed). The full list is now run from the registered classes.

Recommendations for Phase 1 (23 Oct), easiest to hardest
1. Label every battery pack physically (PK-01, PK-02, …) to match its registry entry; a pack is now required before arming. Minutes.
2. Back up the database after each field day (copy uav_preflight_data.db); it is now the flight record. Minutes per day.
3. Confirm the Open-Meteo licence before any commercial deployment: the free API is for non-commercial use. One check with Open-Meteo.
4. Keep the flight-mode channel (FLTMODE_CH) for flight modes only: check each airframe's channel map once (mode switch, trainer mix, arm switch) and write it on the go/no-go sheet. On the plane CH8 is the mode switch.
5. Decide the GCS-failsafe policy per airframe and put it on the go/no-go sheet: with RC as primary control, a telemetry dropout currently sends the plane home (FS_GCS_ENABL; it triggered twice on 22 Oct over USB). Either FS_GCS_ENABL = 0 and rely on the RC failsafe, or keep it and make the telemetry link solid.
   Decided 26 Oct: RC-primary, GCS failsafe OFF on both airframes; the check defaults to RC-primary. Operator: set FS_GCS_ENABL = 0 (plane) and FS_GCS_ENABLE = 0 (copter); the GCS never writes them.
   Status 24 Oct — two defects in the recommendation-5 code fixed: (1) the parameters it reads (FS_GCS_ENABL, FS_LONG_TIMEOUT, FS_LONG_ACTN, FS_GCS_TIMEOUT) were not on ParameterWatchlist, and only watched parameters reach the checks on a real vehicle, so on the plane it would have said "not available" while every unit test passed (tests inject values directly) — now watched, with a test that every parameter the safety checks read is watched; (2) the policy was stored per MAVLink system id, which is 1 on every ArduPilot vehicle by default, so a plane and a copter shared one "per-airframe" policy — now stored per hardware UID (the key the flight records use), with the old key as fallback. Decision still the operator's.
6. Fix the plane before any props-on test: ELRS receiver failsafe to "No pulses"; replace the USB cable that dropped twice; airspeed sensor fitted or ARSPD_TYPE = 0; roll reads about -46° at rest — set AHRS_ORIENTATION and redo the accelerometer calibration.
   Status 24 Oct — software done: the Attitude item names AHRS_ORIENTATION and the accelerometer calibration when a level vehicle reads tilted (threshold unchanged); new Airspeed Sensor item (fixed wing: ARSPD_TYPE set but SYS_STATUS differential-pressure bit 0x10 not healthy → Failed "Airspeed sensor configured but not found — connect it or set ARSPD_TYPE = 0"); new GCS Link Drops item (Communication) counting link losses and vehicle-removed connections since the app started; Radio Failsafe reads ArduPlane's THR_FAILSAFE / THR_FS_VALUE / FS_SHORT_ACTN / FS_LONG_ACTN (it read only ArduCopter's FS_THR_ENABLE, so it was Skipped on the plane) and warns when THR_FS_VALUE is not below the throttle minimum. Parameter facts verified in ~/ardupilot at tags Plane-4.5.7 and Copter-4.5.7 (4.5.7 FS_LONG_ACTN has no 5 = AUTOLAND; Copter 4.5.7 FS_THR_ENABLE ends at 6). Operator procedure: "Bench and SITL checks for Step 3", section H. Physical fixes on the aircraft: open (operator).
7. Build the field laptop's Release build (about an hour, overnight) once the bench checks pass, then freeze and tag phase1-step3.
8. Run the SITL campaign (item 12) before the first props-on flight, especially crash recovery, link loss and the 3-hour idle. Scripts can be written now; running them needs ArduPilot SITL on the development machine.
   Status 24 Oct — run on ArduPlane and ArduCopter SITL (ArduPilot master 4.6.0-beta1-6903, not the plane's 4.5.7): every scenario passes; three GCS recording bugs found and fixed (restart while armed lost the rest of the flight; that flight had no energy baseline; a landing during a link blackout closed as an ordinary disarm). Details, run logs and the remaining notes: section "SITL campaign results" at the end of this document. 3-hour idle: PASS (3 h 1 min connected, memory 212 → 213 MB, no quit, no link loss).
9. Bench the copter: the release gate needs one copter and one plane, and only the plane has been on the bench so far. Plan its bench session (the same checks as the plane).
10. Power types and mission check (copter; designed 26 Oct; steps 1–5 of 6 (maths, storage, flight recorder, checklist items and arming, the Power types page on Analyze → Vehicles, the Fly-view time/distance left and RETURN NOW) built and tested 26–27 Oct, and step 6 (ArduCopter SITL flight) passed 27 Oct. Open decision: ArduPilot's synthetic current (BATT_MONITOR = 25) does not follow throttle on copters (POWER_TYPES_PLAN.md section 9). Also fixed 27 Oct: the Fly-view battery estimator panel overwrote each vehicle's stored pack (cells, mAh) with its defaults on every connect; copters cannot be armed from the GCS without a confirmed power type or a logged waiver): its own document, custom/POWER_TYPES_PLAN.md (goal, definitions, decisions, worked example, implementation plan).

Earlier notes on the open items (history; statuses above supersede them)
• Items 6–8: SessionRecorder, battery pack registry and snapshot (including moving the pack modal into the Power section), database startup and migration fixes.
• Step 3 decisions (22 Oct, user): new Step 3 classes go into new source files added to custom/CMakeLists.txt in ONE batched change (one ~70-minute rebuild, then incremental builds); a flight counts as one battery cycle when it uses at least 20 % of the pack's rated capacity; the battery-pack entry lives in the preflight page's Power section (a "Battery pack" item that opens the pack dialog), not in the Fly view. Pack requirement (user, 22 Oct): required in every session mode — done.
• Step 3 addition (22 Oct): gimbal-page target location does not save. Verified on the live database (UAVPreflight/PreflightQGroundControl Daily/uav_preflight_data.db): schema version 18, flights has no target_lat / target_lon / target_source because v19 never ran (F14), and the "Submit Target" button is enabled only when FlightSession.currentFlightId > 0, which nothing in normal use sets (only the never-opened FlightFormDialog). Typed or map-centre targets only update the in-memory distance readout. Fix in Step 3: item 8 runs v19; the target is stored on the connection session (it is set during preflight, before any arm) and copied onto each flight row; Submit is enabled whenever a vehicle is connected; the page reloads the saved target on open. Status 22 Oct: done, commit da5a75260, keyed on the vehicle row (v20 columns) until SessionRecorder adds connection sessions; copied onto each flight session at gate open.
• Hidden checklist rows (found 22 Oct while placing the Battery pack item): PreflightChecklistModel counted its rows with totalChecks() (which skips the informational weather/zone checks) but read row n from the full check list, so the last few checks never appeared on the Analyze checklist — including the Battery pack item, which the plugin adds last. The model now keeps its own list of non-informational checks. Guarded by ChecklistPagesSmokeTest (both checklist pages show "Battery pack" and its select button). The Fly-view checklist page's generic "Run" button would also have confirm()ed the pack item without a pack; it now opens the pack dialog, and BatteryPackCheck reverts any pass without a selected pack.
• Gimbal page attitude card (22 Oct, user): the PITCH / ROLL / YAW card between the restricted-zone card and the gimbal test buttons was bound to TelemetryBridge.gimbalPitch/Roll/Yaw, which nothing set until item 18, so it always read 0.0°. Now (user decision): one card that shows the gimbal's measured angles when a gimbal reports, otherwise the UAV's own attitude (TelemetryBridge.pitch / roll from ATTITUDE, radians → degrees; yaw as heading 0–360°), labelled "gimbal" or "UAV". The same readout is on the Fly view as an ATTITUDE column in the floating telemetry box (header "ATTITUDE · GIMBAL" / "ATTITUDE · UAV"), so operators see it without opening the preflight page. Check on the bench: with no gimbal, tilt the airframe nose-down ~20° and Pitch reads about -20°.
• Item 18 (22 Oct, user priority; done in code 22 Oct, see the item 18 row): real gimbal pitch, roll and yaw tests. Today both gimbal buttons are disabled with a reason (stop-gap) because TelemetryBridge has no gimbal command. To do: TelemetryBridge::sendGimbalAngles(pitch, roll, yaw) via the gimbal protocol v2 (MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW / GIMBAL_MANAGER_SET_ATTITUDE, falling back to MAV_CMD_DO_MOUNT_CONTROL) with the command ack surfaced; decode GIMBAL_DEVICE_ATTITUDE_STATUS (quaternion → roll/pitch/yaw) and ArduPilot MOUNT_STATUS into gimbalPitch/Roll/Yaw with a gimbalAttitudeChanged signal; read MNT1_TYPE and MNT1_PITCH/ROLL/YAW_MIN/MAX and skip an axis with no range; one test per axis (command a target inside the limits, pass only if the measured angle is within ±3° within 3 s, return to centre and confirm), each logged; no gimbal or no feedback = fail. Tests: a mock gimbal stream unit test, and SITL with MNT1_TYPE = 1. The page already listens for gimbalAttitudeChanged and enables itself when sendMountControl/sendGimbalAngles exists.
• Step 3 addition (21 Oct): trainer (master/slave) handover logging. Master/slave is a transmitter feature (EdgeTX/OpenTX Trainer mode): the aircraft sees only the master's link, so ArduPilot and the GCS cannot tell who is flying. To record it, the instructor's transmitter copies the trainer-switch state onto a spare channel (e.g. CH8); the GCS gets a setting for that channel, logs "control → student / instructor" events (time, altitude, battery) as HANDOVER rows on the current flight, and shows who has control in the Fly view during a Training session. Today the wizard records instructor/trainee names and RC links, and flight history/export have a "Trainer handovers" section, but nothing ever writes a HANDOVER event, so it is always empty. Depends on SessionRecorder (item 6) for the flight row. Status 22 Oct: done in code. TrainerSwitchTracker (header-only) reads the trainer channel from RC_CHANNELS (default CH8, changeable from the Fly-view chip; stored in check_config trainer/channel): the value at connection is "instructor", a move of more than 300 µs is "student", back within 150 µs is "instructor" again, so any mix output works (987 → 1500, 1000 → 2000, reversed). In Training sessions each switch is logged as HANDOVER_TO_STUDENT / HANDOVER_TO_INSTRUCTOR with altitude, battery, satellites and flight mode, on the flight and its Flight History row, and shown as an app message; a Fly-view chip shows INSTRUCTOR FLYING / STUDENT FLYING, "(switch not seen yet)" until the channel has moved once, or "not monitored" when the channel has no valid value. Tests: TelemetryBridgeTest::testTrainerSwitchTracker, DatabaseManagerTest::testRecorderWritesFlightHistoryAndHandovers. Bench 22 Oct (fixed wing, ELRS 16 ch): CH8 is the flight-mode switch (ArduPlane FLTMODE_CH = 8; 987/1500/2011 changed MANUAL/STABILIZE/RTL), so the trainer switch must be mixed onto a free channel (e.g. CH9/CH10). The GCS now refuses a trainer channel equal to FLTMODE_CH (chip says so; nothing is logged). Still to do: set the mix on the instructor GX12 and confirm the chosen channel follows the trainer switch.
• Flight History gap (found 22 Oct): Flight History, CSV export and stats read the flights table, which only the never-shown FlightFormDialog path wrote; SessionRecorder wrote flight_sessions only, so recorded flights did not appear in Flight History. Fixed: SessionRecorder also writes a flights row per flight (operator optional, mode, arm source and bench flag in notes, weather and its source, max altitude, min/max voltage, distance, duration), linked by flight_sessions.history_flight_id (v23).
• Item 17: W7 only (SITL weather location); W2–W4 and W6 done 22 Oct. Item 18: SITL / real-mount run. Item 21: done 22 Oct.
• Items 12 and 14: SITL campaign, real radio and go/no-go. (Item 13, Jetson, after Phase 1.)

Summary
Verdict: not ready for Phase 1 field testing yet, but close on the arm gate and far on records and freshness. The arm interceptor, force-arm token and RTL path are well designed and heavily tested. The weak points are the layers around them: how the code decides data is fresh, how it decides the vehicle is armed, and how it records flights and batteries.
The five findings that most change the plan:
1. Freshness measures the wrong thing. A check's age is the time since its status last changed, not the time since its data last arrived, and a Passed check is never re-evaluated. A battery or GPS stream can stop while its check keeps passing. [V]
2. Armed state is guessed from command acks. The plugin toggles the vehicle's armed flag and opens or closes flight sessions from COMMAND_ACK, not from the heartbeat. RC arms and RC disarms are invisible to it. [V]
3. Session records are unreliable. For an RC-primary aircraft most flights create no session; a GCS arm followed by an RC disarm leaves a session open that swallows every later flight. Battery identity is one fake serial per vehicle, and the pack the operator entered in Fly view is never stored with the flight. [V]
4. Database failures are silent. Startup ignores a failed database open, migrations record a version even when their SQL failed, the v19 migration never runs, and check-result rows likely fail a foreign key on every write. [V], runtime effect [U]
5. Field robustness is unproven. Nothing has run on a real link, a 3-hour idle, or the Jetson target; the RTL confirm wait runs a nested event loop on a raw vehicle pointer that can be deleted mid-wait. [V] for the code, [R] for the test gaps
The plan below has 21 must-have items for Phase 1 (20 since 23 Oct: item 13, the Jetson build, moved to after Phase 1), grouped into six workstreams, and a release gate that ends with a props-on, tethered or low-altitude flight on one copter and one plane.
Phase 1 scope
Phase 1 is field testing of ArduPilot quadcopters and fixed-wing aircraft where the RC transmitter arms and flies, and the GCS monitors, gates GCS-initiated arming, runs bench tests and logs. Everything in this plan is judged against that job, not against a full-featured GCS.
The GCS must, for Phase 1:
• Show trustworthy live state: link, heartbeat, battery, GPS, mode, armed state, and say clearly when any of it is stale.
• Never arm the vehicle from the GCS unless the gate is open or a deliberate, logged override is in force; never block disarm.
• Give one reliable Return (RTL) action that confirms the mode change.
• Run motor and servo bench tests safely, props off.
• Record every arm-to-disarm as a flight, whoever armed it, with enough data to reconstruct what happened.
• Survive link loss, link degradation and multi-hour idle without crashing, freezing or quitting.
• Install and run repeatably on the field laptop (x86_64). Jetson Orin Nano deployment is after Phase 1 (decided 23 Oct; item 13 and its setup/start script are kept for then).
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
Must — Status 21 Oct: done, commit 637c32446: every evaluation is stamped (MonotonicClock), and freshness is the age of the telemetry stream the check reads.
F2
Safety, reliability
A Passed check is never re-evaluated by the tick, so a value that degrades after passing is not seen until the check is forced Stale. The Stale rule then fires on every passed mandatory check about 10 s after it passed, the check is re-run about 5 s later and passes again. Expected effect: the gate closes for a few seconds on a roughly 15 s cycle, or a degrading value goes unnoticed for up to 15 s. Not yet observed in a run.
core/AbstractCheck.cpp:59-70, core/PreflightManager.cpp:681, 737-765, core/PreflightManager.h:226
V code, U effect
Must — Status 21 Oct: done, commit 637c32446: every non-informational Auto check is re-evaluated each tick; the 10 s status-age rule is removed.
F3
Safety
batteryDataValid (battery stream stopped for 5 s) is computed but no check and no QML reads it, so the battery check keeps using the last cached voltage.
adapters/TelemetryBridge.cpp:614-619; no other references in src/ or qml/
V
Must — Status 21 Oct: done, commit 637c32446: the battery check goes Stale when BATTERY_STATUS stops for 5 s, which closes the gate if it is mandatory (ArmGateResilienceTest).
F4
Safety
The gate's freshness clock ticks on a heartbeat from any component of the vehicle. A companion computer or gimbal heartbeat keeps the gate fresh while the autopilot is silent. Connection quality is computed from the same mixed stream.
adapters/TelemetryBridge.cpp:1031, 1040-1068
V
Must — Status 21 Oct: done, commit 637c32446: only the autopilot component's HEARTBEAT drives heartbeatTick and quality; quality decays to 0 within 6 s.
F5
Consistency
Armed state is inferred from COMMAND_ACK. For a GCS-sent arm the plugin never sees the outbound COMMAND_LONG, so _lastArmDisarmParam stays -1 and the new armed state is guessed as the opposite of the current one, then forced into the vehicle with _updateArmed. A heartbeat that lands before the ack, or a retried ack, flips it the wrong way.
PreflightPlugin.cpp:905, 930-932
V code, U frequency
Must — Status 21 Oct: done, commit 57e9ef29c: armed state only from Vehicle::armedChanged (ArmStateTracker); ACKs only label the arm source.
F6
Data integrity
Flight sessions open and close on that ack, not on armed state: RC arms create no session, an RC disarm never closes one, and while one is open _onGateOpened refuses to start another, so every later flight is folded into it.
PreflightPlugin.cpp:535-555, 585-608, 936-947
V
Must — Status 22 Oct: done (item 6): SessionRecorder opens one flight per arm-to-disarm from the HEARTBEAT armed state, whoever armed; a second arm is a second flight. The live database had 138 of 255 flight_sessions rows never closed; crash recovery now closes them.
F7
Data integrity
Battery identity is batt:<sysid>, one fake serial per vehicle, so cycle counts and health trends are per vehicle, not per pack.
PreflightPlugin.cpp:255-256
V
Must — Status 22 Oct: done, commit da5a75260: the synthetic batt:<sysid> serial is removed; the operator picks a registered pack (Battery pack item, Power section), required before any GCS arm.
F8
Data integrity
The pack the operator enters in Fly view (type, S, P, cell mAh, reserve) is saved on the vehicle row and overwritten on each change. No flight or session row keeps a copy.
core/VehicleProfileManager.cpp:577-623, 730-735; qml/cpts/FlyQuickActions.qml:558-564
V
Must — Status 22 Oct: done: v22 flight_batteries keeps a frozen copy of the pack at arm (serial, label, chemistry, S, P, cell mAh, rated mAh) plus start/end voltage and percent, minimum voltage, mAh and Wh; SessionRecorder writes it (DatabaseManagerTest::testRecorderBatterySnapshotIsFrozen).
F9
Data integrity
Battery cycles have two writers, saveBatteryCycle on disarm after 30 s and incrementBatteryCycle on the gate-close ack; both bump total_cycles.
core/VehicleProfileManager.cpp:1112-1126, PreflightPlugin.cpp:586, utils/DatabaseManager.cpp:3511-3535, 3586-3637
V
Must — Status 22 Oct: done: one writer, DatabaseManager::recordBatteryFlight() at session end, counting a cycle only when the flight used >= 20 % of rated capacity (FC mAh consumed); the 30 s disarm writer in VehicleProfileManager is removed.
F10
Data integrity
Energy per flight uses BAT_CAPACITY/BAT1_CAPACITY, which are PX4 names; ArduPilot's is BATT_CAPACITY, so ArduPilot flights fall back to a 0.45 Ah-per-percent guess. Distance falls back to ground speed at disarm times duration, which is near zero. The pack entered in Fly view is not used.
core/VehicleProfileManager.cpp:1129-1157
V
Must — Status 22 Oct: done: energy per flight is the FC energy counter (BATTERY_STATUS.energy_consumed) when reported, else mAh used x mean voltage, else unknown (NULL); distance is the vehicle's flown-distance counter. The BAT_CAPACITY guess and the ground-speed x duration distance in VehicleProfileManager are removed.
F11
Data integrity
Check results are written with flight_session_id = -1 and the MAVLink system id as device_uid, while vehicles.device_uid is the hardware UID and foreign keys are on. Likely every insert is rejected; at best none can be joined to a flight.
core/PreflightManager.cpp:1005-1020, utils/DatabaseManager.cpp:205, 465-473
V code, U outcome
Must — Status 22 Oct: done (item 6): check results are written keyed to the connection session and open flight with the vehicles-row UID (DatabaseManager::recordCheckResult). The live database had 0 check_results rows: every earlier insert had been rejected.
F12
Reliability
Crash recovery closes orphaned rows in flights only; open flight_sessions rows stay open forever.
utils/DatabaseManager.cpp:1544-1580
V
Must — Status 22 Oct: done (item 6): crash recovery closes open flight_sessions (at last_summary_at) and connection_sessions (at last_seen_at) with end_reason = crash_recovered.
F13
Reliability
Startup ignores the result of DatabaseManager::initialize(), so a locked, full or corrupt database leaves the app running with no logging and no warning to the operator.
PreflightPlugin.cpp:221
V
Must — Status 22 Oct: done, commit da5a75260: initialize()'s result is checked; DatabaseManager::startupError drives a red "FLIGHT LOGGING IS OFF" Fly-view banner and a startup message.
F14
Reliability, maintainability
Migrations log failed SQL as warnings and still record the version, so a failed step is never retried. Migration v19 exists but kLatestSchemaVersion is 18, so it never runs and saveTargetLocation writes columns that do not exist.
utils/DatabaseManager.cpp:997, 1037-1529, 3755-3765
V
Must — Status 22 Oct: done, commit da5a75260: each step is transactional with its version row and stops on failure; version 20. Also fixed: storedSchemaVersion() answered 0 during initialize(), so every launch re-ran every migration (v12 rebuilt flights each start, zeroing its summary columns; the .bak was rewritten each launch).
F15
Reliability
RTL waits up to 5 s in a nested QEventLoop and then dereferences the raw _vehicle pointer. Link loss, the most likely moment to press Return, can delete the vehicle during that wait. A second press re-enters the loop.
adapters/TelemetryBridge.cpp:105-131, 1449-1450, adapters/TelemetryBridge.h:916
V code, U crash
Must — Status 21 Oct: done (item 10): asynchronous confirmation on a QPointer<Vehicle>, 5 s timeout, vehicle loss reports failure, second press while pending sends nothing; the outcome is shown as an app message.
F16
Safety
Manual-check confirmations are persisted in QSettings and re-applied at the next startup, before any vehicle connects, so yesterday's "props inspected" can carry into today.
core/PreflightManager.cpp:190, 1043-1084
V
Must — Status 21 Oct: done, commit 9449b5a14 (overrides no longer restored at startup).
F17
Consistency
All clocks use QDateTime::currentDateTime() (wall clock, 28 sites) instead of a monotonic timer. An NTP or GPS time jump on the Jetson can mark everything stale or keep stale data looking fresh. SQL rows use UTC CURRENT_TIMESTAMP while app-side durations and compliance logs use local time.
e.g. core/ArmingGate.cpp:456, core/PreflightManager.cpp:750, PreflightPlugin.cpp:562-578
V
Must — Status 21 Oct: done, commit 637c32446: header-only MonotonicClock for ages and durations; stored timestamps UTC. Plugin session duration still UTC-based until Step 3.
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
After Phase 1 for the Jetson part (moved 23 Oct with item 13): the start-script fix (hard-coded x86_64-linux-gnu path) is kept, planned for the Jetson work. Still relevant to Phase 1: the field laptop should run a Release/RelWithDebInfo build, not the Debug build with test hooks. — Status 23 Oct: open.
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
Mint a fresh token on ack, start a single-shot m_ackTimer of m_ackExpiryMs that clears it, and authorize only the call whose token matches. Re-run both tests and mutation-check them. — Status 21 Oct: done, commit dc6a42757: token minted on acknowledge, 15 s single-shot expiry, only the matching call consumes it; ArmSendTest ack tests pass.
G2
Critical
One tap on the mode badge cycles the gate to Passive, which allows every arm. No confirmation, no log, no reason.
qml/cpts/ArmGatePanel.qml:104; core/ArmingGate.cpp:159-160, 441-447
Remove the click handler. For Phase 1 fix the mode at Active; if a mode change is ever needed, put it in settings behind a confirmation and log it as an event. — Status 21 Oct: done: badge display-only (QML) and mode read-only in C++, commit b8d611ff6.
G3
High
"Override" and "Override Gate" call overrideGate(): one tap opens a 30 s window in which every arm from every path passes, ignoring staleness, Training mode and check results, with no pilot name. This undoes the token design.
qml/cpts/ArmGatePanel.qml:144, qml/cpts/PriorityFixPanel.qml:214; core/ArmingGate.cpp:252-268, 433-438
Remove both buttons and make overrideGate() non-invokable from QML. Every override goes through the acknowledge dialog. — Status 21 Oct: done: both buttons removed and overrideGate() no longer invokable from QML, QML guard test, commit b8d611ff6.
G4
High
"Force Arm" is one tap, with no confirmation or pilot name, and is shown even when the gate is open. It sends 2989, which also bypasses ArduPilot's own pre-arm checks.
qml/cpts/ArmGatePanel.qml:150-168; adapters/TelemetryBridge.cpp:1332-1390
Show it only in a Testing session (bench), behind a confirmation dialog that requires pilot name and reason, and log each use as a flight event. Close the force window as soon as its token is used, not after 60 s. — Status 21 Oct: done, commit a2281c28b: forceArm(pilot, reason) refuses outside a Testing session or without pilot/reason, logs every attempt to the compliance log; dialog passes pilot/reason. (Window closes on use: see G9.)
G5
High
The acknowledgment bypasses everything, including "no telemetry" and "telemetry stale". The toolbar opens the acknowledge dialog for any denial, so an operator can acknowledge a dead link and arm.
qml/PreflightToolbarIndicator.qml:60-64, 115-120; core/ArmingGate.cpp:385-393 runs before the staleness test at :455
Split blocks into hard and overridable. Hard: no fresh autopilot heartbeat, stale data, Training mode, no battery pack. Bind the ack to the set of failing check IDs at ack time and void it if a new check fails. — Status 21 Oct: done (Step 2.5): hard blocks (no/stale heartbeat, Stale mandatory check, Training) are never overridable; the ack records the failing checks and a new failure voids it. User decision: any other failing check can be acknowledged.
G6
High
Training mode only blocks inbound arms from other peers. The GCS's own Arm buttons still arm in Training mode when checks pass.
core/ArmingGate.cpp:137-146 (inbound only); no Training test in :360-404 or :431-487
Make Training a hard block in the one decision function below. — Status 21 Oct: done (Step 2.5): Training blocks every GCS arm in decide().
G7
Medium
The inbound path disagrees with the outbound one: it allows every arm if no manager is set, lets any arm through during a caller-scoped force window, ignores staleness, and lets a peer's arm consume the operator's acknowledgment. It is also not a real barrier.
core/ArmingGate.cpp:126-187; PreflightPlugin.cpp:902-919
Make it observe-only: log the peer's arm as an event and raise an alert; never consume the ack or change gate state. — Status 21 Oct: done (Step 2.5), user decision: a peer's inbound arm is still denied when the gate is closed, but judged read-only with token 0: it never spends the ack or a force token and never changes gate state. No manager now fails closed.
G8
Medium
During a force-arm window the gate reports armingAllowed = true, while every normal Arm press is denied by the interceptor. The UI and the decision disagree.
core/ArmingGate.cpp:318-325, 368-381, 433-438
Expose the override kind separately; armingAllowed answers "would a plain arm pass now". — Status 21 Oct: done (Step 2.5): armingAllowed answers 'would a plain arm pass'; overrideKind exposes None/Ack/Force; the force window closes on use.
G9
Medium
resetGate() resets the token counter to 0, so token values repeat across windows, and the force-arm token is left on the vehicle after sending. A stale token can match a later window.
core/ArmingGate.cpp:277; adapters/TelemetryBridge.cpp:1374
Never reset the counter (or use a random 32-bit value), and clear the vehicle's token after the command is sent. — Status 21 Oct: done: vehicle token cleared around the send (a2281c28b); token counter never reset (Step 2.5).
G10
Medium
The post-disarm reset only runs on vehicleDisarmed, which is emitted from the command-ack handler, so an RC disarm leaves an ack or override in place.
core/ArmingGate.cpp:51-55; PreflightPlugin.cpp:944-946
Drive it from Vehicle::armedChanged (work item 5). — Status 21 Oct: done, commit 57e9ef29c: any disarm (RC included) reaches vehicleDisarmed and clears an unspent ack.
G11
Low
Three Arm buttons decide differently when to show the dialog: the status drawer only on critical failures, the toolbar on any denial, the Fly view armed indicator never.
qml/MainStatusIndicator.qml:353-361, qml/PreflightToolbarIndicator.qml:60-64, qml/ArmedIndicator.qml:24-30
One ArmingGate.requestArm() that returns arm, needs-acknowledgment or blocked-with-reason, used by every button. — Status 21 Oct: done for the live custom entry points (Step 2.5): the toolbar chip uses previewArm(), the wizard's Arm buttons use requestArm(); refusals are shown as app messages. The custom MainStatusIndicator.qml and ArmedIndicator.qml never load (stock AOT-compiled copies win / not referenced); the stock status-drawer Arm button is gated by the interceptor's decide() and shows Vehicle's 'Unable to arm' message.
G12
Low
ArmingGateTest and part of ArmingGateExtendedTest test processArmRequest, which production never calls; Hybrid and Active behave identically; onCheckFailed and interceptCommandLong write gate state outside the main evaluation; the interceptor mutates the gate through const_cast.
tests/ArmingGateTest.cpp:44-99, tests/ArmingGateExtendedTest.cpp:121; core/ArmingGate.cpp:192-225, 408-420, 374, 386
Delete processArmRequest and its tests, merge Hybrid into Active, and route everything through one decision function. — Status 21 Oct: done (Step 2.5): processArmRequest() and its tests removed; Hybrid behaves as Active; onCheckFailed() and the inbound path no longer write gate state; no const_cast.
G13
Verify
QGC resends an unacknowledged command. If a resend passes through the interceptor again, a one-shot token consumed by the first send denies the retry on a lossy link. The Vehicle.cc patch is not in this copy.
src/Vehicle/Vehicle.cc (fork patch, not reviewed)
Read the patch; add a test that drops the first arm ack. — Status 21 Oct: verified by reading src/Vehicle/Vehicle.cc: the interceptor runs once in _sendMavCommandWorker before the command is queued; resends from _sendMavCommandFromList do not re-run it, so a consumed one-shot token cannot block a retry. No fix needed. A dropped-ACK test is not possible inside custom/ (MockLink's second-attempt behaviour exists only for its special test commands).
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
Status 21 Oct: done (commit 9449b5a14). Steps 1–4 implemented; FlightSession returns to PreFlight rather than Idle, because Idle here means "no session". The bypass is closed: testResetForNextFlightClosesGate.
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
V — Status 22 Oct: done: only a METAR's own observation time sets metarTimestamp; a forecast fetch stamps a separate forecast time, so the gust check can no longer auto-pass on forecast data (WeatherProviderTest::testForecastNeverMakesMetarFresh).
W3
Open-Meteo (model forecast for the GPS point) and METAR (an airport up to 50 km away) write the same fields, last one wins, and the UI does not say which source it shows.
utils/WeatherProvider.cpp:159-166, 240-275
V — Status 22 Oct: done: a fresh METAR is not overwritten by a forecast, and weatherSource ("METAR HAAB, observed 09:00Z" / "Forecast (Open-Meteo), fetched 12:00Z") is shown in the weather panels and stored with the flight's weather summary.
W4
The METAR JSON parse likely misreads the current aviationweather.gov format: obsTime is a Unix time number, read here as an ISO string; visib can be the string "10+", read as 0; ceiling comes from clouds[], not a ceiling object. Good weather can show 0 km visibility and 0 ft ceiling.
utils/WeatherProvider.cpp:238-262
U, check against a saved real response — Status 22 Oct: done, checked against real responses saved 22 Oct (HAAB, KBOS, ENGM, RJTT; now test fixtures). Worse than suspected: obsTime (Unix seconds) parsed as invalid, so a METAR was never fresh; visib "6+"/"10+" read as 0; gusts are in wgst (code read gust: never found); the category is fltCat (code read flightCategory, so the W5 label never applied to real data); ceiling comes from clouds[]; wdir can be "VRB". All parsed now.
W5
Flight category LIFR is mapped to weather code 95, which displays "Thunderstorm".
utils/WeatherProvider.cpp:265-276
V — Status 20 Oct: done. weatherDescription() names the METAR flight category ("LIFR (low IFR)", …) and LIFR no longer maps to code 95; WeatherProviderTest::testFlightCategoryDescription, mutation-checked.
W6
Missing fields silently become 0 (wind, visibility, temperature), shown as real values.
utils/WeatherProvider.cpp:159-164
V — Status 22 Oct: done: unreported values are NaN (ceiling -1) and display as "--" / "none reported", never 0; unknown wind is not windOk. FlightSession's weather summary also labelled m/s as km/h (fixed).
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
qml/cpts/MotorCheckPanel.qml:159, controllers/HardwareTestController.cpp:1403-1408 — Status 21 Oct: done, commit 15b93be70: stopAll() only disarms through BenchDisarm (plain disarm, only if this test armed the vehicle, not flying, < 1 m/s). The QML flight guard stays as defence in depth.
B2
Critical
Closing the gimbal/surface page, or its stop button, calls stopAllSurfaces(), which always ends in a forced disarm if armed
qml/pages/GimbalTest.qml:68-70, 818, controllers/ControlSurfaceTestController.cpp:1072, 1090-1097 — Status 21 Oct: done, commit 15b93be70: stopAllSurfaces() disarms only through BenchDisarm, only if the controller armed the vehicle.
B3
Critical
STOP ALL on the control-surface panel calls the same stopAll() as B1
qml/cpts/ControlSurfacePanel.qml:208 — Status 21 Oct: done, commit 15b93be70 (same stopAll() path as B1).
B4
High
Starting a motor test while armed force-disarms first, then runs the test
controllers/HardwareTestController.cpp:965-974 — Status 21 Oct: done, commit 15b93be70: a motor test or sweep on a vehicle armed by another source is refused with a reason; nothing is disarmed.
B5
High
The fixed-wing motor test and the surface sweep force-arm with 2989, skipping ArduPilot pre-arm checks, in any session type
controllers/HardwareTestController.cpp:762-774, controllers/ControlSurfaceTestController.cpp:534-550 — Status 21 Oct: done, commits a2281c28b and 15b93be70: force-arm only in a Testing session; a refusal aborts the test; testBlockedReason shown in the UI. (The props-off confirmation is in QML, B6.)
B6
Medium
Props-off is only a text warning, the Open button is always enabled, and the panel says the vehicle stays disarmed, which is wrong for the fixed-wing path
qml/cpts/MotorCheckPanel.qml:114-135, 381 — Status 20 Oct: done in QML: correct fixed-wing text; Open Motor Test is greyed out with a reason when offline or armed, and every entry point goes through a props-off confirmation dialog.
B7
Medium
The Fly view disarm sends 2989 as param2; ArduPilot's force-disarm value is 21196, so this is probably a plain disarm [U]
qml/cpts/FlyQuickActions.qml:399-425, core/VehicleProfileManager.cpp:645-664 — Status 21 Oct: done, commit 15b93be70: the Fly-view force disarm sends kForceDisarmParam2 (21196).
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
A tagged commit, and raw PASS/FAIL/Totals lines for every gate suite pasted against it — Status 22 Oct: done: tags phase1-step1 / phase1-step2 and tests/baseline-step1..3.txt (raw PASS/FAIL/Totals for every suite). The five Rc checks no longer need registering (merged into RcCheck).
2
Freshness
Freshness from data age, not status age
F1, F2, F3
TelemetryBridge stamps a last-received time per stream (autopilot heartbeat, SYS_STATUS, BATTERY_STATUS, GPS_RAW_INT, EKF_STATUS_REPORT, RC_CHANNELS). Each check declares the streams it reads. The tick re-evaluates every Auto check (29 registered checks, 24 counted in the checklist, at 2 Hz is cheap) and marks it Stale when any of its streams is older than that stream's limit. Drop the "status unchanged for 10 s" rule.
Test: heartbeats continue, battery stops, battery check goes Stale and the gate closes within 5 s, then recovers. Test: no gate close over a 30-minute steady SITL idle. — Status 21 Oct: done, commit 637c32446 (and the rewritten ArmGateResilienceTest battery test now passes).
3
Freshness
Count only the autopilot's heartbeat
F4
Feed heartbeatTick and connection quality only from the autopilot component (vehicle->defaultComponentId(), not a GCS or companion type). Decay quality on a timer when heartbeats stop instead of freezing at the last value.
Test: companion heartbeats alone leave the gate stale; quality falls to 0 within the staleness limit — Status 21 Oct: done, commit 637c32446.
4
Freshness
Monotonic clocks for every age and duration
F17
Replace QDateTime arithmetic in the gate, checks, bridge and session code with QElapsedTimer; keep UTC QDateTime only for stored timestamps. Inject a clock so tests can jump it.
Test: a +1 h and a -1 h wall-clock jump change no gate decision — Status 21 Oct: done, commit 637c32446 (plugin session duration moves to monotonic in Step 3).
5
Freshness
Armed state from the heartbeat only
F5
Remove _updateArmed and the gateOpened/gateClosed emits from the COMMAND_ACK handler. Acks only surface rejections and tag the arm source on the next armed transition.
Tests: heartbeat-before-ack, duplicate ack, RC arm with no ack; each leaves armed state and records correct — Status 21 Oct: done, commit 57e9ef29c.
6
Records
SessionRecorder with connection sessions and flights
F6, F11, F12
As in Sessions and battery records: one writer, arm-to-disarm flights from armedChanged, bench flag, link-loss handling, 10 s running summary, crash recovery for both tables, check results keyed to the connection session and flight.
tests/FlightSessionLifecycleTest.cpp passes with no expected failures or skips; a kill -9 mid-flight in SITL leaves a closed, crash_recovered row — Status 22 Oct: done in code. SessionRecorder (core/SessionRecorder.h, header-only: no CMake change) is the only writer; v21 adds connection_sessions, session_events and the flight columns (connection, arm source, gate state, mode, bench, end reason, running summary, min voltage, mAh used). Tested without MockLink in DatabaseManagerTest (flight follows armed state incl. an RC arm, link loss closes at the loss time, crash recovery for both tables, check results keyed; mutant on link-loss timing caught). FlightSessionLifecycleTest still crashes in MockLink (unchanged, unrelated to the recorder); the SITL kill -9 run is still to do. Battery cycles stay a once-per-flight counted row (recordBatteryFlight, non-bench only) rather than a view.
7
Records
Battery pack registry and per-flight snapshot
F7, F8, F9, F10
Move the battery modal out of Fly view into a mandatory "Battery pack" item in the preflight Power section that opens it on click; pick or edit a pack there; flight_batteries snapshot at arm; mAh and Wh from BATTERY_STATUS; BATT_CAPACITY fallback; cycles as a derived view.
Test: edit a pack after a flight and the flight's snapshot is unchanged. Test: two flights on two packs give each pack one cycle. SITL: Wh within 5% of BATTERY_STATUS.energy_consumed. — Status 22 Oct: done in code (da5a75260, ade82ce18); SITL Wh check open. Earlier note: commit da5a75260 + gimbal commit: registry, Power-section item and dialog, gate hard block in every mode, 20 % cycle rule (DatabaseManagerTest, ArmingGateTest). Snapshot and energy done 22 Oct (F8, F10). Open: the SITL check that Wh is within 5 % of BATTERY_STATUS.energy_consumed.
8
Records
Database startup and migrations fail loudly
F13, F14
Check initialize()'s result and show a persistent banner when logging is off. Run each migration step in a transaction; on any failed statement roll back, keep the pre-migration backup, and stop. Fix the schema version constant so v19 (and the new tables) run.
Migration test from saved v12 and v18 database files; test that a read-only database path shows the banner — Status 22 Oct: done, commit da5a75260 (DatabaseMigrationTest 14/14: restart does not re-run, v18 upgrade, failed step rolls back, unopenable path sets the error).
9
Safety
Manual confirmations do not survive a restart
F16
Stop re-applying QSettings overrides at startup; reset manual and action checks at each new connection session; keep the history in check_results.
Test: confirm a manual check, restart, reconnect; it is Pending — Status 21 Oct: done, commit 9449b5a14.
10
Commands
Return without a nested event loop
F15
Send DO_SET_MODE, then confirm asynchronously from flightModeChanged with a 5 s single-shot timer; hold the vehicle in a QPointer; ignore a second press while one is pending; show pending, confirmed or failed.
Test: vehicle deleted during the wait, no crash and a "failed" result; ReturnToLaunchTest still passes — Status 21 Oct: done (21 Oct): see F15. ReturnToLaunchTest 11/11, including vehicle-lost-during-wait and second-press tests.
11
Commands
Disabled-with-reason Arm and Return, and the Acknowledge label
State analysis items 3 and 5
Bind enabled and a reason line to the gate's denialReason and to the connection state; never a silent no-op. Keep the one-tap Acknowledge & Arm, which already arms in this copy (see Arm gate review).
Observed in SITL: every disabled control says why — Status 21 Oct: done for Arm and Return: every refusal and the Return outcome are shown as app messages; Arm entry points use requestArm()/previewArm().
12
Field
SITL campaign, copter and plane
F18, state analysis items 6 to 9
On ArduCopter and ArduPlane SITL: force-arm with 2989 against a failing pre-arm, RTL on plane, link loss while armed, netem loss and latency pass watching parserStalled, a 3-hour idle with the quit caller identified, then two clean run-of-show dry runs.
A written run log per scenario with timestamps and the database rows it produced — Status 24 Oct: done. Scripts in custom/tools/sitl/, run logs in custom/tools/sitl/runs/, results and findings in "SITL campaign results" at the end of this document. The netem part is done by a userspace relay (no root); parserStalled does not exist in the code.
13
Field
Jetson build and package
F20
Build RelWithDebInfo for arm64 on JetPack 6.2.2; fix the hard-coded x86_64-linux-gnu library path in deploy/qgroundcontrol-start.sh; run the unit test classes on the Jetson; measure CPU and memory over the 3-hour idle.
The packaged build starts on a clean Jetson, tests pass there, and memory growth over 3 hours is under a limit you set — Status 23 Oct: moved to after Phase 1 (user decision). Kept here unchanged, including the deploy/qgroundcontrol-start.sh fix, for the Jetson work. For Phase 1 the 3-hour memory check is done on the field laptop as part of item 12.
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
Every test listed under Arm gate review passes and is mutation-checked; no QML control can open the gate in one tap — Status 21 Oct: done (Steps 1 and 2.5): G1–G13 addressed; see each G row.
16
Safety
Park post-flight and re-run preflight after every disarm (do with item 15)
Post-flight bypass
kPostFlightEnabled = false; resetForNextFlight() on disarm; FlightSession returns to Idle; hide the banner and summary buttons; keep code, pages and columns.
Test: arm, disarm, gate closed and every mandatory check Pending; QML smoke tests still load every page — Status 21 Oct: done, commit 9449b5a14: kPostFlightEnabled = false, resetForNextFlight() on disarm, FlightSession back to PreFlight (keeps the session mode), entry points hidden.
17
Field
Reliable weather data
W1 to W7
Correct units, separate METAR and forecast with source and time shown, parse the current METAR JSON, missing means unknown, gust auto-pass only from a real METAR.
Parser tests against saved real Open-Meteo and METAR responses; displayed wind matches the source within 1 kt — Status 22 Oct: done except W7 (W1–W6; WeatherProviderTest uses real responses saved 22 Oct, ade82ce18). W7 (SITL weather location) open.
18
Commands
Working gimbal pitch, roll and yaw tests
Gimbal review
Real command and attitude feedback in TelemetryBridge, MNT1 limits, per-axis measured test with return to center.
Each axis shows a measured pass or fail in SITL with a simulated mount; no gimbal means fail, not pass — Status 22 Oct: done in code (gimbal commit): DO_MOUNT_CONTROL with ack, attitude decoded from GIMBAL_DEVICE_ATTITUDE_STATUS / MOUNT_ORIENTATION / MOUNT_STATUS, MNT1 (or MNT_ANG*) limits, per-axis ±3°/3 s test with return to centre, logged to hardware_test_events (TelemetryBridgeTest decode tests). SITL / real-mount run still to do.
19
Safety
Bench tests never disarm a flying aircraft (do with item 15)
B1 to B4, B7
One disarm helper for both test controllers: plain disarm, only when this test armed the vehicle, refused unless landed and ground speed under 1 m/s. Closing a dialog or page stops outputs and RC overrides but never disarms. Check what the Fly view disarm really sends.
SITL, armed and flying: close Motor Test, close the gimbal page, press STOP ALL; no ARM_DISARM is sent and the vehicle stays armed. Mutation-checked. — Status 21 Oct: done, commit 15b93be70 (BenchDisarm; QML flight guard kept).
20
Safety
Bench force-arm only in Testing sessions
B5, B6
Allow the fixed-wing motor test and surface sweep only in a Testing session, behind a props-off confirmation; disable test buttons with a reason while armed by anyone else; fix the "stays disarmed" text.
Test: outside Testing the buttons are disabled with a reason; in Testing no 2989 is sent before the confirmation. — Status 21 Oct: done, commits a2281c28b, 15b93be70 and the 20 Oct QML props-off confirmation.
21
Commands
One set of status colours on the safety screens
UI consistency
Semantic status tokens in Colors.qml with the old names as aliases; PreflightChecklistView, ArmGatePanel and the arm controls use only those tokens.
Pass, fail, warn and stale look the same on every safety screen; a lint test fails on new hex literals in those files. — Status 22 Oct: done, including the lint test (QmlShadowTest::testNoColourLiteralsOutsideSingletons: no hex colour literal in any custom QML outside qml/singletons, no named colour but "transparent" on the safety screens). Status tokens in Colors.qml; no colour literals on safety screens (or anywhere outside the singletons).
After Phase 1
These improve quality but do not change whether a Phase 1 test is safe or its records trustworthy.
Item
Fixes
Why it can wait
Jetson Orin Nano build and package (item 13): RelWithDebInfo for arm64 on JetPack 6.2.2, the deploy/qgroundcontrol-start.sh library-path fix, tests and a 3-hour run on the Jetson
F20
Moved here 23 Oct (user decision): Phase 1 runs on the field laptop. The setup and start script work is kept for when the Jetson becomes the target.
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
3. Target hardware: the same SITL run repeated once from the field laptop's Release/RelWithDebInfo build (the Jetson, item 13, is after Phase 1).
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
   - Only ever run the incremental `cmake --build build -j2`. Never delete or clean `build/`, never run `cmake --fresh`, never re-run `cmake -S . -B build` with new `-D` options, never change `CMAKE_BUILD_TYPE`, and never edit the top-level `CMakeLists.txt` or `cmake/`. Do not add or remove source files in `custom/CMakeLists.txt` unless unavoidable: on 21 Oct adding two files re-ran CMake, regenerated the precompiled header and forced 509 recompiles (73 minutes) while the dry run beforehand showed 1 step. Put new code in an existing `.cpp`/`.h` instead. If you must, say so and get approval first.
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

---

## Bench and SITL checks for Step 3 (written 22 Oct for the operator)

Safety first, every time: **propellers off** for everything below. The plane may arm during the motor and surface tests.

### A. Start the new build
1. Close the QGC window that is open now (it is an older build from 11:35).
2. Start it again from the build: `~/Dev/qgroundcontrol/build/Debug/PreflightQGroundControl`.
3. Pass if: the app opens normally and there is **no red "FLIGHT LOGGING IS OFF" banner** at the top of the Fly view. (On this first start the database upgrades itself; a backup `uav_preflight_data.db.bak` is made first.)
4. If the red banner appears: write down its text and send it to me — the app still flies, but nothing is being recorded.

### B. Battery pack (Power section)
1. Connect the plane over USB and plug in the flight battery.
2. Open the checklist (Fly view → **Checklist**). Choose any session mode when asked.
3. In the **Power** section look for an item called **Battery pack**, with a button **Select battery pack**.
4. Press it. In the window: Pack = **+ New pack**; Pack ID = what is written on your pack (e.g. `PK-01`; write one on the pack with a marker if it has none); Type, S (cells in series, e.g. 6 for a 6S pack), P (usually 1), Cell capacity in mAh (the number on the pack label). Press **Save & use this pack**.
5. Pass if: the item turns green and shows the pack name and "0 cycles". The button now says **Change battery pack**.
6. Arming check (props off!): with the pack NOT selected (unplug USB and reconnect — the selection clears on every new connection), try to arm from the GCS. Pass if: it is refused with "Select the battery pack before arming".

### C. Attitude readout and the plane's level problem
1. On the Fly view, the floating telemetry box now has a 4th column **ATTITUDE · UAV** (it says GIMBAL instead if a gimbal is connected).
2. Put the plane on a flat table, wings level, nose level.
3. Pass if: Roll and Pitch read close to **0** (within about ±3°).
4. Tilt the nose down a little: Pitch should go negative. Lift the right wing: Roll should change. They should follow you live.
5. **Today the plane read Roll ≈ -46° while still.** If it still does on a flat table, the flight controller does not know how it is mounted:
   - QGC → Vehicle Setup (gear icon) → **Sensors** → set the **Autopilot Orientation** to how the board is mounted (arrow on the board vs. the nose of the plane), then run **Accelerometer** calibration and follow the on-screen positions.
   - Check step 3 again. Do not fly until Roll and Pitch read 0 when level.

### D. Motor and control-surface tests (needs a Testing session)
1. These tests arm the plane, so the GCS only allows them in a **Testing** session. If a session is already running, end it and start a new one choosing **Testing**.
2. If a test is still greyed out, read the orange line under it — it now says why (e.g. "start a Testing session first", or "Vehicle is armed — disarm it").
3. Motor test: props off, open it, confirm the props-off question, test the motor. Pass if the motor spins briefly and the vehicle disarms by itself afterwards.
4. Surface test (gimbal page → Control Surface Test): press **Sweep** on each surface. Pass if each surface moves both ways and comes back to centre.

### E. Gimbal page target location
1. Open the gimbal page, type a target latitude and longitude (or press the map-centre button), press **Submit Target**.
2. Pass if: it says "Target location saved". Close QGC, start it again, reconnect, open the gimbal page: the same target is filled in.

### F. Training mode, step by step (instructor and student radios)

#### F0. What training mode is, in plain words
- **Two people, two radios.** The **instructor** has one GX12, the **student** has the other. They are joined by a **trainer cable** (a 3.5 mm audio-style cable into the trainer port of each radio).
- **Only the instructor's radio talks to the plane.** It is "bound" to the plane's receiver (the ELRS link). The student's radio does **not** transmit to the plane; it only sends its stick positions down the cable to the instructor's radio. That makes the instructor's radio the **master** and the student's the **slave**.
- **The trainer switch decides who flies.** It is a switch on the instructor's radio (often the spring-loaded one, e.g. SH). **Held** = the student's sticks pass through to the plane. **Released** = the instructor's sticks fly. If the student gets into trouble, the instructor just lets go of the switch and has control back instantly.
- **What the GCS does in a Training session:**
  1. It **never arms the plane from the GCS** — the arm button refuses with "Arming is disabled in training mode". Arming is done on the instructor's radio only.
  2. It records **who** is instructing and training (names chosen when the session starts).
  3. It shows on the Fly view **who is flying right now** (INSTRUCTOR FLYING / STUDENT FLYING) and logs every change of control as a **handover** with time, altitude, battery and flight mode. This needs the setup in F3.

You already proved F1–F2 work on 22 Oct morning: the student's sticks moved the plane only while the trainer switch was held.

#### F1. Connect the radios (both radios OFF first)
1. Plug the trainer cable into the **trainer port** of both GX12s (the round socket on the back; check the GX12 manual picture if unsure).
2. Turn on the **instructor's** radio and select the plane's model (the one bound to this plane).
3. Turn on the **student's** radio. On the student's radio, select a model whose **internal/external RF module is OFF** (Model settings → Internal RF → Off, and External RF → Off). The student radio must not transmit, or two radios fight over the link.

#### F2. Set master and slave (once; you have probably done this already)
1. **Instructor radio (master):** press **MDL** → page to the **Trainer** setting → **Mode = Master/Jack** (Jack = through the cable).
2. **Student radio (slave):** press **MDL** → **Trainer** → **Mode = Slave/Jack**.
3. **Instructor radio, the switch:** **MDL** → page to **Special Functions**. There must be a line like `SH  Trainer  Sticks` (switch, function "Trainer", what is passed = sticks). If there is none, add one: choose a free line, press the roller, set the switch (SH is a good choice: it springs back when you let go), function **Trainer**, value **Sticks**, and make sure it is ticked/enabled.
4. **Instructor radio, check the student's sticks arrive:** **SYS** (radio settings) → page to **Trainer**. Move the student's sticks: the numbers on this page must move. If a stick moves the wrong channel or backwards, fix it on this page (each row maps one student channel; the "+/-" or "Inv" option reverses it). Press **Cal** here once with the student's sticks centred.
5. **Check on the plane, props off:** in QGC look at the RC channels (the monitor sees them too). Hold the trainer switch and move the student's sticks: CH1–CH4 change. Release the switch: the student's sticks do nothing and the instructor's sticks move CH1–CH4.

#### F3. Find the channel SH arrives on (the switch already reaches the plane)
Operator convention (23 Oct): the instructor's radio uses switch **SH** for the handover, and SH is already sent to the flight controller. **On these radios SH low = the instructor (master) is flying; SH high = the trainee is flying** (measured on the bench 24 Oct; the first description had it the other way round). No new mix is needed; the GCS only needs to know which channel carries SH.
Easiest: use **Detect** in the GCS (F5) — it finds the channel while you flick SH. By hand instead:
1. With the plane connected and the instructor's radio on, flick SH a few times.
2. Find the channel that jumps (e.g. 987 ↔ 2011): on the radio (**SYS → Channel monitor**) or in QGC (Vehicle Setup → Radio).
3. It must not be the **flight-mode channel** (CH8 on the plane). If it is, the GCS refuses it (see F7).

#### F4. Start a Training session in the GCS
1. Plane powered (battery in), USB connected to the GCS computer, instructor's radio on and linked, student radio connected by cable. **Props off** for this first test.
2. In QGC open the **Checklist** (Fly view). When it asks for the session mode, choose **Training Flight** ("Arming disabled • Full audit").
3. Enter / choose the **instructor** and the **trainee** names when asked.
4. Do the preflight checks as usual (including the **Battery pack**, section B).

#### F5. Tell the GCS which channel carries the switch
1. On the Fly view, at the top centre, a small round-cornered **trainer chip** appears (only in Training sessions with a plane connected).
2. **Tap the chip.** A small window opens: **Trainer switch (SH) channel**.
   - **Detect (recommended):** press **Detect**, then flick SH down and up 2 times within 15 s. The window shows **"Found: CH9"** (for example); press **Use CH9**. If it says "Several channels moved", flick only SH and try again; "No RC data" means the RC is not connected; "Only the flight-mode channel moved" means SH is not sent on its own channel.
   - Or type the channel from F3 into the field.
   - Leave **SH high = instructor** unticked (the default for these radios); tick it only for a radio wired the other way round.
3. The setting is remembered for next time.

#### F6. Test the handover (props off)
1. Put SH **low**. The chip says **INSTRUCTOR FLYING** in green (with "(flick SH once to confirm the channel)" until both positions have been seen on this connection).
2. Put SH **high**. Within a second:
   - the chip turns orange: **STUDENT FLYING**;
   - a short note **"STUDENT has control"** appears under the chip and fades after 3 seconds (nothing to dismiss).
3. SH **low** again: chip green **INSTRUCTOR FLYING**, note **"INSTRUCTOR has control"**.
4. Repeat 2–3 a few times. Each change is recorded.
5. Arm **from the instructor's radio** (the GCS arm button refuses in Training — that is correct), hold/release the trainer switch once or twice, then disarm from the radio.
6. Open **Flight History**, open that flight: a **TRAINER HANDOVERS** section lists each "HANDOVER_TO_STUDENT / HANDOVER_TO_INSTRUCTOR" with the time and altitude.

#### F7. If it does not work
| What the chip says / what you see | What it means | What to do |
|---|---|---|
| No chip at all | Not a Training session, or no plane connected | Start the session as **Training Flight**; check the plane is connected |
| "not monitored — no CHn trainer mix" | No valid value on that channel | Wrong channel: redo F3 and set the channel that moves with SH |
| "CH9 is the flight-mode channel (FLTMODE_CH)" | That channel already changes flight modes | Use another free channel in both F3 and F5 |
| Chip changes but **the plane's flight mode also changes** | The mix is on the mode channel | Stop; move the mix to a free channel |
| STUDENT / INSTRUCTOR are the wrong way round | This radio sends SH the other way | Toggle **SH high = instructor** in the chip's window |
| Student's sticks do nothing while the switch is held | Trainer cable / master–slave / special function | Recheck F1–F2 (the GCS is not involved in that part) |

Safety rules for real training flights: the instructor keeps a finger on the trainer switch at all times; the student only gets control at a safe height; any doubt → release the switch. The GCS only records — it never switches control itself.

### G. Flight History (records)
1. In a Flight or Training session, arm and disarm once (props off; motor test style arm is fine).
2. Open **Flight History**. Pass if: the flight appears with its time and duration. In a Training flight where you flipped the trainer switch, open the flight: a **TRAINER HANDOVERS** section lists each handover with time and altitude.

### H. Fix the plane before flying (operator procedure)
**Propellers off for all of this.** Each fix has a matching item in the checklist that tells you when it is right.

**H1. RC receiver failsafe → "No pulses"**
*Why:* when the RC link is lost, the receiver must stop sending, so the flight controller knows the link is gone. If the receiver keeps sending the last sticks (or a fixed throttle — on 22 Oct throttle read 1922 after the link was lost), the plane can keep flying on them.
1. If the receiver is connected to the flight controller by **PWM** wires: set its failsafe to **No pulses** (ExpressLRS receiver settings — the Lua script on the GX12, SYS → Tools → ExpressLRS, or the receiver's web page, depending on the receiver).
2. If it is connected by a **serial / CRSF** wire (one cable, usually): there may be no such setting; the receiver simply stops sending and the flight controller detects the loss itself. Step 4 is what proves it either way.
3. In QGC (Vehicle Setup → Parameters) check **THR_FAILSAFE = 1** and **FS_LONG_ACTN = 1** (return to launch) — or the action you choose.
4. Confirm: switch the transmitter **off**. QGC must show an **RC failsafe**, and the motor must not spin. Switch it back on.
5. Checklist: the **Radio Failsafe** item (Safety section) shows *"RC failsafe on (THR_FAILSAFE = 1). after 1.5 s: …; after 5 s: return to launch"* in green. Its detail lists these steps.

**H2. Replace the USB cable that dropped twice**
*Why:* each drop is a lost GCS link; with the GCS failsafe enabled the plane flies home (it did twice on 22 Oct, on the bench).
1. Use a short, good-quality USB cable, straight into the computer (no hub), placed so it cannot be pulled.
2. Confirm: leave the plane connected for 10 minutes. The **GCS Link Drops** item (Communication section) must stay green: *"No GCS link drops since the app started"*. Any drop turns it orange with the count. (The count starts again when the app restarts.)

**H3. Airspeed sensor: fit it, or tell the flight controller there is none**
*Why:* the flight controller is set to use an airspeed sensor it cannot find ("MS4525: no sensor found" at boot).
- If the plane **has** an airspeed sensor: check its cable and connector, power-cycle the plane.
- If it **has none**: Vehicle Setup → Parameters → **ARSPD_TYPE = 0**, then reboot the flight controller.
- Confirm: the **Airspeed Sensor** item (GPS/Navigation & Sensors section) is green — *"Airspeed sensor found and healthy"* or *"No airspeed sensor configured (ARSPD_TYPE = 0)…"*. Red *"Airspeed sensor configured but not found"* means it is still wrong.

**H4. Make the flight controller read "level" correctly**
*Why:* with the plane still, it reports about −46° of roll. Either the board is mounted differently from what the flight controller thinks, or its accelerometers were calibrated badly. It would fly as if it were banked.
1. Put the plane on a flat table, wings level.
2. QGC → Vehicle Setup (gear icon) → **Sensors** → **Autopilot orientation**: choose how the board is mounted (compare the arrow on the board with the nose of the plane). This is the parameter **AHRS_ORIENTATION**.
3. On the same page run the **Accelerometer** calibration and follow its positions.
4. Confirm: on the Fly view the **ATTITUDE · UAV** column reads roll and pitch near **0°** with the plane level, and the **Attitude** item in the checklist is green. If the item is red it now says *"…If it is sitting level, the flight controller's orientation (AHRS_ORIENTATION) or accelerometer calibration is wrong"*.

**H5. Battery capacity**
In Vehicle Setup → Parameters set **BATT_CAPACITY** to the pack's mAh. The **Battery pack** item turns orange with the value to set if it does not match the selected pack.

### What to send me
For each section A–H: pass, or what you saw instead (a photo of the screen is fine). Keep QGC's MAVLink forwarding on — the monitor I run reads the plane's messages and shows arm/disarm, modes, CH9 and failsafes as they happen.

### SITL checks (later, at the desk)
- Kill QGC (`kill -9`) while a simulated flight is armed; restart: the flight shows end reason **crash_recovered**.
- Fly a short SITL flight; the flight's Wh should be within 5 % of the simulator's energy counter.
- SITL with `MNT1_TYPE = 1`: run the gimbal Pitch / Roll / Yaw tests; each should PASS.

---

## Agent instructions: Phase 1 recommendations 5, 6 and 8 (written 23 Oct)

You are continuing work on the Skywin GCS fork of QGroundControl at `/home/lisabeth/Dev/qgroundcontrol`, branch `custom-dev-next`. All project code is under `custom/`. Read this whole document first: the "Implementation status", "Recommendations for Phase 1", item 12 and item 14 rows, and "Bench and SITL checks for Step 3". The aircraft facts below come from the 22 Oct bench session (fixed wing, CoreWingF405WingV2, ArduPlane 4.5.7, ELRS 16 channels; CH8 is its flight-mode switch).

### Rules (non-negotiable)
1. Change only files under `custom/`. Never edit upstream QGC (`src/`, `test/`), never push, never open a PR.
2. **No full rebuild.** Build with `cmake --build build -j2` (incremental). Before building, dry-run: `ninja -C build -n | grep -c '^\['`; if more than 300 steps, stop and ask. Do **not** add source files to `custom/CMakeLists.txt` and do **not** edit `custom/custom.qrc` (both re-run CMake: ~70-minute rebuild). Use header-only classes without `Q_OBJECT`, QML inline components, and tests added to existing test files. New non-compiled files (scripts, docs) are fine.
3. **Never edit a header while a build is running** (stale objects → crashes at exit). If it happens, `touch` the header and rebuild.
4. Run tests one class per process, always with `--allow-multiple` (the operator's QGC may be open; without it the test hangs): `QT_QPA_PLATFORM=offscreen timeout -s KILL 300 build/Debug/PreflightQGroundControl --allow-multiple --unittest:<Class>`.
5. Every new test is mutation-checked: break the rule it guards, show it fails, restore, rebuild, show it passes. Report the mutant.
6. **Never touch the operator's flight database** `~/.local/share/UAVPreflight/Skywin GCS/uav_preflight_data.db`. Any run of the real app (SITL) uses a throw-away data folder: `HOME=<tmp> XDG_DATA_HOME=<tmp>/share XDG_CONFIG_HOME=<tmp>/config` plus `--allow-multiple`.
7. **Never write parameters to the real aircraft** or arm it. Hardware steps are written as an operator procedure; the operator does them.
8. Commit after each finished piece with a clear message ending in `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Update this document (status lines with the date and commit) in the same or the next commit.
9. Ask before guessing on anything marked "decision" below.

### Recommendation 5 — GCS-failsafe policy per airframe
Problem: with RC as primary control, a telemetry dropout sends the plane home; it happened twice on 22 Oct over USB. Today's check (`src/core/GcsFailsafeCheck.cpp`, id `safety.failsafe.gcs`) reads `FS_GCS_ENABLE`, which is **ArduCopter's** name; ArduPlane's is **`FS_GCS_ENABL`**, so on the plane the check is Skipped. It also always warns when the failsafe is disabled, which is wrong if the chosen policy is "RC primary, GCS failsafe off".
Do:
1. Confirm the parameter names, values and meanings for ArduPlane 4.5 and ArduCopter 4.5 from the local ArduPilot source (`~/ardupilot`, e.g. `ArduPlane/Parameters.cpp`, `ArduCopter/Parameters.cpp`); include the related timeout/action parameters (e.g. `FS_GCS_TIMEOUT`, `FS_LONG_ACTN` on Plane). Quote what you found in the commit message.
2. Make the check read the right parameter for the vehicle type.
3. Add a per-airframe **policy**, stored in `check_config` (check id `safety.failsafe.gcs`, key `policy`, per vehicle): `rc_primary` (GCS failsafe expected OFF) or `telemetry_primary` (expected ON). Result: Passed when the vehicle's setting matches the policy; Failed with a clear message and fix steps when it does not; Warning "GCS-failsafe policy not chosen for this airframe" when no policy is stored. The GCS never writes the parameter itself.
4. Let the operator choose the policy on the check's card in the checklist (both checklist pages), with one sentence explaining each choice. Keep colours from `Colors` (the lint test enforces it).
5. Tests in an existing test file (e.g. `tests/CheckSmokeTest.cpp` or `PreflightManagerTest.cpp`): Plane name read on a plane, Copter name on a copter, each policy × setting combination, no policy → Warning.
6. Decision for the operator (do not choose): which policy each airframe uses. Write the two options with their trade-off into the go/no-go template (below).

Status 23 Oct: done in commit 6efe022a7. The check reads `FS_GCS_ENABL` on Plane and `FS_GCS_ENABLE` on Copter, stores the chosen policy per vehicle in `check_config` (key `policy`), warns when no policy is stored, and never writes the vehicle parameter. The policy chooser is on both checklist pages. Tests: PreflightManagerTest 24/24 (Plane name, Copter name, each policy × setting, no policy → Warning, storage; five mutants caught), ChecklistPagesSmokeTest 4/4, QmlShadowTest 7/7, CheckSmokeTest 13/13, FlyQuickActionsSmokeTest 8/8. The full suite is re-run at the end (deliverable 4).

Decision 26 Oct (operator): **RC-primary for both airframes — GCS failsafe OFF** (`FS_GCS_ENABL` = 0 on the plane, `FS_GCS_ENABLE` = 0 on the copter); the RC failsafe is the backstop. The check now treats an airframe with no stored policy as RC-primary (Passed when the parameter is 0, Failed otherwise); Telemetry-primary can still be chosen per airframe on the check card. PreflightManagerTest 32/32; mutant (no default) caught by 3 tests.

GCS-failsafe policy options for the go/no-go sheet (operator decision, do not choose for the operator):
- **RC-primary** — `FS_GCS_ENABL` / `FS_GCS_ENABLE` = 0. The RC link remains in control; a GCS or USB telemetry dropout must not trigger RTL. Trade-off: if the RC link also fails, or the aircraft is beyond RC range, there is no GCS-triggered failsafe; rely on the RC failsafe (ELRS "No pulses").
- **Telemetry-primary** — `FS_GCS_ENABL` / `FS_GCS_ENABLE` ≥ 1, with the appropriate timeout/action (Plane: `FS_LONG_TIMEOUT`, `FS_LONG_ACTN`; Copter: `FS_GCS_TIMEOUT` and the action in `FS_GCS_ENABLE`). Loss of the GCS link triggers the configured failsafe. Trade-off: any telemetry or USB dropout (as on 22 Oct) sends the aircraft home; the link and cable must be reliable.

### Recommendation 6 — fix the plane (operator does the physical part)
Findings: roll reads about −46° at rest (pitch follows a tilt); "MS4525: no sensor found / Airspeed 1 init failed"; two USB link drops caused GCS-failsafe RTLs; after RC loss CH3 (throttle) read 1922.
Do (software, so the GCS catches these on any airframe):
1. **Level at rest:** extend the attitude check's failure text/fix steps so a large constant roll or pitch at rest points to `AHRS_ORIENTATION` and the accelerometer calibration (QGC Vehicle Setup → Sensors). Do not loosen its threshold.
2. **Airspeed sensor:** add a check (header-only, registered in `PreflightManager` or the plugin like `BatteryPackCheck`) for fixed-wing only: if `ARSPD_TYPE` ≠ 0 and the autopilot reports the differential-pressure sensor missing/unhealthy (SYS_STATUS sensor bits; verify the bit and ArduPlane behaviour in `~/ardupilot`), Failed with "Airspeed sensor configured but not found — connect it or set ARSPD_TYPE = 0". `ARSPD_TYPE` = 0 → Passed/Skipped with a note.
3. **Link drops:** show on the Fly view (or the checklist Communication section) how many times the GCS link was lost in this connection session (the recorder already logs `link_lost` events), so an unreliable cable is visible before flight.
4. **RC failsafe:** verify that `RcFailsafeCheck`/`RadioFailsafeCheck` read the right ArduPlane parameters (`THR_FAILSAFE`, `THR_FS_VALUE`, `FS_SHORT_ACTN`, `FS_LONG_ACTN`) and that their fix steps mention setting the ELRS receiver failsafe to "No pulses". Correct what is wrong; do not invent behaviour — check `~/ardupilot`.
5. Tests for 2 and 3 (mock telemetry / recorder events), mutation-checked.
6. Write the operator procedure for the physical fixes into "Bench and SITL checks for Step 3" section H (ELRS failsafe setting, USB cable, `ARSPD_TYPE`, orientation + accelerometer calibration, and how to confirm each in the new checks). Plain language: the operator is new to RC equipment.

### Recommendation 8 — SITL campaign (item 12)
Scope from item 12: on ArduCopter and ArduPlane SITL — force-arm with 2989 against a failing pre-arm, RTL on plane, link loss while armed, netem loss and latency watching `parserStalled`, a 3-hour idle with the quit caller identified, then two clean run-of-show dry runs. Plus the Step 3 SITL checks: kill -9 mid-flight leaves a `crash_recovered` flight; flight Wh within 5 % of `BATTERY_STATUS.energy_consumed`; gimbal axis tests with `MNT1_TYPE = 1`.
Do:
1. Put scripts under `custom/tools/sitl/` (Python with pymavlink, which is installed for the user; run without `python3 -I`). One script per scenario plus a runner. Start SITL with `~/ardupilot/Tools/autotest/sim_vehicle.py` (`-v ArduCopter` / `-v ArduPlane`, `--out` to the GCS's UDP port), start the GCS with the throw-away data folder (rule 6) and `--allow-multiple`, `QT_QPA_PLATFORM=offscreen` is acceptable.
2. Drive the vehicle from the script (MAVLink commands, RC overrides) — not by clicking. Where a scenario needs the GCS's own arm button, use the GCS code path that is already unit-tested or say it was not exercised.
3. After each scenario, query the throw-away database and write a run log `custom/tools/sitl/runs/<date>-<scenario>.md`: timestamps, what was commanded, what the vehicle reported, and the rows produced (`connection_sessions`, `flight_sessions`, `flights`, `flight_batteries`, `session_events`, `check_results`, test-result tables). State PASS/FAIL against the expected rows.
4. The 3-hour idle runs in the background; record memory (RSS) every 5 minutes and whether/why the app quit.
5. Weather: SITL sits at its default home, so W7 applies; note it, do not fix it here unless asked.
6. Report every failure you find as a finding in this document; fix only clear GCS bugs, each with a test, and say so.

### Deliverables and order
1. Recommendation 5 (code + tests), commit. 2. Recommendation 6 (code + tests + procedure), commit. 3. Recommendation 8 scripts, then runs (plane first, then copter), commit scripts and run logs. 4. Full test run, written to `custom/tests/baseline-step3.txt` (all classes must still pass). 5. Update this document: the recommendations' statuses, item 12's status, a go/no-go template section per airframe (channel map with the mode channel, GCS-failsafe policy and its trade-off, airspeed, failsafe settings, observed wind, sign-off lines) that item 14 can use.

### Final report (your last message)
Commits; per recommendation what changed (files), the tests and their mutants; the ArduPilot parameter facts you verified and where; each SITL scenario's PASS/FAIL with the run-log path; every bug found and whether fixed; anything that needs the operator (decisions, hardware steps). Do not claim hardware results — nothing here is verified on the aircraft.

## SITL campaign results (item 12, 24 Oct)

How it runs: `custom/tools/sitl/run_all.py` (each scenario is also a script of its own, `sc_*.py`; `sitl_common.py` holds the plumbing, its docstring explains it). SITL starts through sim_vehicle.py with --no-mavproxy, so nothing goes to UDP 14550 and an operator's QGC on the same machine is never touched. A Python relay between SITL and the GCS cuts the stream into MAVLink frames and can drop, delay or black out the link (stands in for tc netem, which needs root). The GCS runs offscreen with --allow-multiple and a throw-away HOME / XDG folder whose settings switch every auto-connect off (so it cannot grab a flight controller on USB); its database lives in that folder and is deleted after the run. The script commands the vehicle on SITL's second serial port. ArduPilot in ~/ardupilot is master (4.6.0-beta1-6903), not the plane's 4.5.7.

| Scenario | Plane | Copter | Run logs (custom/tools/sitl/runs/) |
|---|---|---|---|
| Force-arm 2989 against a failing pre-arm (BATT_ARM_VOLT = 20 V): normal arm refused, force arm accepted, flight recorded as arm_source = external, closed disarmed | PASS | PASS | 2026-10-24-force-arm-*.md |
| Plane RTL (stays in RTL, airborne); 20 s link blackout while armed: link_lost then link_restored, flight stays open | PASS | PASS | 2026-10-24-rtl-link-loss-*.md |
| Copter lands and disarms during a blackout: flight closes as link_lost, ended at the moment of loss | — | PASS (after fix 3) | 2026-10-24-rtl-link-loss-arducopter.md |
| Lossy link 60 s each at 20 %/150 ms, 40 %/400 ms, 80 %/400 ms: GCS stays up, flight row survives, no link_lost at 20 % | PASS | PASS | 2026-10-24-lossy-link-*.md |
| Flight Wh against BATTERY_STATUS.energy_consumed (within 5 %) | — | PASS (12.472 / 12.472 Wh) | 2026-10-24-kill9-energy-arducopter.md |
| kill -9 mid-flight: row closed crash_recovered at next start; rest of the flight recorded | — | PASS (after fixes 1–2) | 2026-10-24-kill9-energy-arducopter.md |
| Two run-of-show dry runs (connect, fly, RTL, land/disarm, close, restart): two flights closed disarmed, two history rows, no link_lost | PASS | PASS | 2026-10-24-run-of-show-*.md |
| Gimbal axes, MNT1_TYPE = 1 (servo): pitch −45°, yaw 60°, back to 0°, roll 20° each reported within 1° and the servo outputs move | — | PASS | 2026-10-24-gimbal-arducopter.md |
| 3-hour idle (RSS every 5 min, quit cause) | PASS: still running after 3 h, RSS 212 → 213 MB (+0.2 %), one connection, no link_lost; no quit, so no quit caller to identify. It ran the build from before fixes 1–3, which do not touch the idle path | — | 2026-10-24-idle-3h-arduplane.md |

GCS bugs found and fixed (each with a test; SITL re-run passes):
1. Restart while armed lost the rest of the flight. A flight row opened only on an arm transition; a GCS that comes up (or is restarted after a crash) while the vehicle is armed never sees one, and neither does one that gets the arm before the vehicle's UID. Now SessionRecorder::joinArmedFlight() opens the flight when the UID becomes known with the vehicle armed (PreflightPlugin::_openConnectionSession), arm_source = already_armed. Test: DatabaseManagerTest::testRecorderJoinsAFlightAlreadyArmed (mutant: no "already open" guard → caught).
2. A flight opened before the first BATTERY_STATUS kept energy, mAh and distance unknown for its whole length, and its minimum voltage stuck at 0 V. Now a baseline unknown at open is taken from the first sample that has it, and a 0 V start reading is ignored. Same test (mutants: no late baseline → caught; min voltage seeded from 0 V → caught). Remaining limit: the joined flight counts from when the GCS has set the vehicle up after its restart (about a minute in SITL: 10.7 Wh recorded of 18.3 Wh flown after the join).
3. A vehicle that disarmed during a link outage was recorded as an ordinary disarm when the link returned. QGC reports the link back on the first message, before the next HEARTBEAT updates the armed flag, so the recorder was told "back, still armed". The plugin now waits 2.5 s (more than one heartbeat) before handling the restore; a disarm heartbeat in between closes the flight as link_lost at the moment of loss. Checked by the SITL scenario (failed before, passes after); no unit test (the timing lives in the plugin's link wiring).

Noted, not changed:
- Closing the GCS with a vehicle connected (SIGTERM, a laptop shutdown, or the window's close) asks "close anyway?" (upstream QGC). Unanswered, the app keeps running until it is killed, and the next start closes that connection as crash_recovered; flights are not affected. On the field laptop: disconnect the vehicle, or answer the question, before shutting down.
- The connection rows record app_version "phase1-step0-4-g7aeb4a40d": the version string is fixed when CMake configures, so it is stale. It is corrected by the next configure (the Release build of recommendation 7).
- parserStalled, which item 12 asked to watch, does not exist in the GCS or upstream QGC. The lossy-link run watches session events, the process and the flight row instead. QGC declares comm lost only after 3.5 s with no message at all, so even 80 % loss gave no link_lost.
- Weather (W7): SITL sits at its default home, so the weather item reports that location; not fixed here.
- Not exercised by the scripts (they need the UI): the GCS's own arm button and gate (arms come from the script, arm_source = external), the checklist confirmations, the battery-pack selection (so flight_batteries has no pack), and the gimbal page's own test buttons (the run shows the simulated gimbal answers the commands they send).

## Go/no-go sheet per airframe (item 14 template)

Print one sheet per airframe and fill it at the props-off bench with the real telemetry radio and RC. Values come from the GCS (Vehicle Setup → Parameters, and the preflight checklist) unless marked "measure". Any "No" in the last column means NO-GO until it is fixed or a named person accepts it in writing on this sheet.

**Airframe:** ________ **Type:** plane / copter **Flight controller + firmware:** ________ **Hardware UID (GCS):** ________ **Date:** ________

### 1. Channel map (RC)
| Channel | Function (from the transmitter and ArduPilot) | Checked moving in the GCS radio page? |
|---|---|---|
| CH1–CH4 | roll / pitch / throttle / yaw (RCMAP_ROLL, RCMAP_PITCH, RCMAP_THROTTLE, RCMAP_YAW) | Yes / No |
| FLTMODE_CH = ___ | flight-mode switch: positions → ______ / ______ / ______ | Yes / No |
| CH__ | arm/disarm switch (RCx_OPTION = 153/154) or none | Yes / No |
| CH__ | trainer switch (SH) — must not equal FLTMODE_CH; GCS "Detect" result: CH__ ; SH low = instructor (default) / reversed | Yes / No |
| CH__ | other switches: ______ | Yes / No |

### 2. GCS-failsafe policy (operator decision)
Chosen (26 Oct): **RC-primary** (FS_GCS_ENABL / FS_GCS_ENABLE = 0) for both airframes. Confirm on each aircraft: parameter = 0: Yes / No
- RC-primary trade-off: a telemetry or USB dropout does not send the aircraft home; if the RC link also fails, only the RC failsafe acts.
- Telemetry-primary trade-off: any telemetry or USB dropout (as on 22 Oct) triggers the failsafe; the link and cable must be reliable.
GCS "GCS Failsafe" item shows the same policy and Passed: Yes / No. GCS "GCS Link Drops" item shows 0 drops after 10 min connected: Yes / No.

### 3. RC failsafe
| Item | Value | OK? |
|---|---|---|
| Receiver failsafe (ELRS: "No pulses"; checked by switching the transmitter off — throttle in the GCS radio page drops to 0 / stops) | ______ | Yes / No |
| Plane: THR_FAILSAFE (1) / THR_FS_VALUE (below RC3_MIN) / FS_SHORT_ACTN / FS_LONG_ACTN / FS_LONG_TIMEOUT | ______ | Yes / No |
| Copter: FS_THR_ENABLE / FS_THR_VALUE | ______ | Yes / No |
| GCS "Radio Failsafe" item Passed | ______ | Yes / No |

### 4. Battery
| Item | Value | OK? |
|---|---|---|
| Pack label on the pack matches the GCS pack registry | ______ | Yes / No |
| BATT_CAPACITY equals the pack's rated mAh (GCS "Battery pack" item not warning) | ______ | Yes / No |
| BATT_FS_LOW_ACT / BATT_LOW_VOLT / BATT_LOW_MAH, BATT_FS_CRT_ACT / BATT_CRT_VOLT | ______ | Yes / No |
| Pre-flight estimate on the battery chip (min) | ______ | — |

### 5. Sensors and attitude
| Item | Value | OK? |
|---|---|---|
| Level at rest: roll and pitch within ±5° on the GCS (else AHRS_ORIENTATION + accelerometer calibration) | roll ___ pitch ___ | Yes / No |
| Plane: airspeed sensor fitted and the GCS "Airspeed Sensor" item Passed, or ARSPD_TYPE = 0 (and flying on the estimate is accepted) | ARSPD_TYPE ___ | Yes / No |
| Compass and GPS items Passed, satellites ___ , HDOP ___ | | Yes / No |
| ARMING_CHECK = 1 (all checks) | ______ | Yes / No |

### 6. Site and weather (measure on the day)
| Item | Value | OK? |
|---|---|---|
| Observed wind (hand anemometer) / gusts, m/s | ___ / ___ | Yes / No |
| GCS weather item (location shown is the field, not a default) | ______ | Yes / No |
| Airspace / site permission recorded | ______ | Yes / No |

### 7. Software
| Item | Value | OK? |
|---|---|---|
| GCS build (commit / tag) | ______ | — |
| SITL campaign (item 12) run logs reviewed for this build: custom/tools/sitl/runs/ | ______ | Yes / No |
| Database backed up after the last field day | ______ | Yes / No |

### Sign-off
Operator: __________________ Signature: __________ Date: ______
Safety pilot / instructor: __________________ Signature: __________ Date: ______
Decision: **GO** / **NO-GO** Reason or accepted exceptions: ______________________________
