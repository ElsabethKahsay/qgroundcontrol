# FlyQuickActions Restructuring — Detailed Implementation Plan

Companion to `custom/FlyQuickActions Restructuring Spec — RC-Primary Quad FW.docx`.
Scope: quad/FW RC-primary vehicles only. Approach: two independent single-purpose
chains (RTL / Force Arm), no shared code, no path that skips the gate.

## Current-state findings (grounded in code, Sep 29 2026)

- `custom/qml/cpts/FlyQuickActions.qml` currently has **5 QuickButtons + divider**:
  Checklist, Flight time, Bat Config, FC status, Events, then Force Arm, Disarm,
  Takeoff, Return, Mode. `actionTriggered` routes `forceArm`->dialog,
  `takeoff`->`TelemetryProvider.takeoff()`, `rtl`->`TelemetryProvider.rtl()`,
  `disarm`->dialog.
- FlyQuickActions is loaded ONLY via `custom/qml/FlyViewCustomLayer.qml:367` (the
  custom FlyView). It is exported through the `cpts` module (`custom/qml/cpts/qmldir`)
  and aliased in `custom/custom.qrc:81`. No stock counterpart exists, so no
  shadowing concern for this file itself.
- The Force Arm confirm dialog (`FlyQuickActions.qml:368-406`) fires **three**
  parallel paths on accept: `ArmingGate.forceArm()`, `vehicle.forceArm()`, and
  `TelemetryProvider.forceArm()`. This is the exact "no parallel QML-to-C++ paths"
  violation the spec calls out.
- `TelemetryBridge::forceArm()` (TelemetryBridge.cpp:1167-1189) already: opens the
  bounded gate override, logs, then sends `MAV_CMD_COMPONENT_ARM_DISARM`
  param1=1.0 param2=`kForceArmParam2`(2989). This is the path the whole chain
  should converge on.
- `VehicleProfileManager::forceArm()` (VehicleProfileManager.cpp:628-643) is a
  **second path**: opens the override then calls `vehicle->forceArm()` directly
  (no its-own log, no shared funnel). Must be redirected through the funnel.
- `HardwareTestController::_startFixedWingMotorTest` (HardwareTestController.cpp:718)
  is a **third path**: FW auto-arm opens the bounded override (lines 755-759) then
  raw `sendMavCommand(..., 1.0f, kLegacyForceArmParam2=21196)`. Note it already
  opens the override — but keeps a *legacy* force-arm magic number (21196) distinct
  from the 2989 used elsewhere, and bypasses the funnel.
- `ControlSurfaceTestController` pre-sweep arm (ControlSurfaceTestController.cpp:513-527):
  same pattern — opens bounded override, raw send with 21196.
- `TelemetyBridge::takeoff()` (TelemetryBridge.cpp:1195-1211) and `rtl()`
  (1222-1230) share `_vehicle` guard pattern but are separate functions. RTL
  currently routes via `Vehicle::guidedModeRTL(false)` — the same accessor stock
  GCG uses, already mode-gated per stack. Takeoff sends raw `MAV_CMD_NAV_TAKEOFF`
  with fixed 10 m altitude: the two are NOT actually a shared implementation
  today, but the spec wants their contracts separated and RTL's made the single
  hardened entry.
- `MainStatusIndicator.qml` (:317-393) has a press-and-hold Force Arm path wired
  to `ArmingGate.forceArm()` + `TelemetryProvider.forceArm()` (and a
  `mainWindow.forceArmVehicleRequest()` fallback). `ArmedIndicator.qml` and
  `PreflightToolbarIndicator.qml` have **no** force-arm affordance today.
- **MainStatusIndicator shadowing (already documented in QmlSmokeTest.cpp:374-420):**
  the custom `MainStatusIndicator.qml` shares its qrc alias with the stock module
  cache, so the app's single engine can resolve `qrc:` to the STOCK component.
  The smoke test must load the custom file from disk and a sentinel log line must
  prove which file runs in the live app. This finding shapes migration order and
  the sentinel steps below.

## Design decisions locked in

1. **Single funnel for force-arm**: `TelemetryBridge::forceArm()` is THE one
   implementation. VPM and HTC/CST stop calling `vehicle->forceArm()` / raw send
   and call the funnel. Remove the parallel QML calls in FlyQuickActions and
   MainStatusIndicator.
2. **Single magic number**: replace `kLegacyForceArmParam2` (21196) usages in the
   HTC fixed-wing arm and CST pre-sweep arm with the funnel's 2989. Keep
   `kLegacyForceArmParam2` only where it means something distinct (forced disarm,
   HTC.cpp:1389-1395, 832/855/961 disarm sends).
3. **RTL single entry**: `TelemetryBridge::returnToLaunch()` becomes the only RTL
   entry (QML calls it directly). `rtl()` is removed/renamed; no second path via
   VehicleProfileManager.
4. **No-op safety**: all changes that remove a button/dialog must keep the disarm
   path and the checklist/flight-time/FC/events/mode buttons intact.

## Implementation steps

### Phase 1 — Landing RTL hardening first (lowest risk, keep it solid)

1. **TelemetryBridge: `returnToLaunch()` fork-native DO_SET_MODE** (TelemetryBridge.h:726,
   TelemetryBridge.cpp:1237-1326) — landed (Track B, B6). The fork ships with
   APM/PX4 firmware plugins FORCE-disabled (CustomOverrides.cmake:42-45), so the
   stock `Vehicle::guidedModeRTL()` path is unusable; the bridge therefore sends a
   raw `MAV_CMD_DO_SET_MODE` command-long with `param1=MAV_MODE_FLAG_CUSTOM_MODE_ENABLED`
   and `param2=<RTL custom-mode number>` (6 for ArduCopter, 11 for ArduPlane/Rover —
   citations in TelemetryBridge.cpp:52-68), then waits (bounded ≈5 s) for the
   HEARTBEAT to report that custom mode (real feedback via `Vehicle::flightMode()`
   + `FlightModeUtil::planeModeNumber`, NOT "command sent" = success), and requires
   the armed state to be preserved. `RTL_TRIGGERED` timestamped trigger logged
   (altitude, groundspeed, distance-from-home).
2. **FlyQuickActions: rewire Return button** to `TelemetryProvider.returnToLaunch()`
   through a held-press/confirm step showing altitude + distance-to-home.
3. **Tests**: extend `ArmSendTest` and add a QML smoke assertion that Return is
   present, wired to a single function (`returnToLaunch`), Force Arm/Takeoff absent.
4. Sentinel check: with the running app, confirm the edited FlyQuickActions is the
   one loaded (it has no stock twin, so validate the `cpts` export resolves and a
   sentinel property/log line in the file appears at runtime).

### Track B — landed + verified evidence (raw, Sep 30 2026)

Step status: B5 live-bug-repro, B6 fork-native rewrite + tests, B7 mode-ID
citations (ArduCopter/mode.h RTL=6; ArduPlane/mode.h, APMrover2/mode.h RTL=11),
B8 DO_SET_MODE never gate-filtered, B9 armed-state preservation, B10 live QML
sentinel, B11 this evidence, B12 commit on `custom-dev-next`. All harness runs
use `QT_QPA_PLATFORM=offscreen`; live runs use the root user netns loopback
sandbox (`unshare -rn … ip link set lo up … --live-arm`).

- **B5 — unpatched bug reproduced live** (`livearm6.log`):
  ```
  RTL repro outcome: doSetModeWireCount=0 flightModeBefore=Custom:0x10000 flightModeAfter=Custom:0x10000
  RTL repro: ZERO DO_SET_MODE wires (unpatched path bug reproduced)
  returnToLaunch() returned false    RTL NOT confirmed by vehicle
  ```
- **B6 — fix proven live** (`livearm7.log`, `livearm8.log`):
  ```
  returnToLaunch(): sending MAV_CMD_DO_SET_MODE param1=MAV_MODE_FLAG_CUSTOM_MODE_ENABLED param2= 11 (RTL custom-mode number, fork-native path)
  returnToLaunch(): "RTL confirmed via heartbeat (custom mode 11)" armedPreserved= true
  RTL repro outcome: doSetModeWireCount=1 flightModeBefore=Custom:0x10000 flightModeAfter=Custom:0xb
  RTL repro: DO_SET_MODE present on wire
  ```
- **B10 — live productMark sentinel** (`livearm8.log:99-100`):
  ```
  live productMark sentinel: FlyQuickActions-custom-rtl2 present=yes
  live productMark value: FlyQuickActions-custom-rtl2
  ```
- **B6/B7/B8/B9 — ReturnToLaunchTest** (`rtl_test_run3.log`):
  ```
  PASS : ReturnToLaunchTest::testNoVehicleGuard()
  PASS : ReturnToLaunchTest::testDistanceToHomeNoFix()
  PASS : ReturnToLaunchTest::testDistanceToHomeComputed()
  PASS : ReturnToLaunchTest::testRtlPlaneSendsDoSetModeAndConfirms()
  PASS : ReturnToLaunchTest::testRtlModeIdsPerVehicleType()
  PASS : ReturnToLaunchTest::testRtlSendsRegardlessOfArmGateState()
  PASS : ReturnToLaunchTest::testRtlPreservesArmedState()
  Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted, 15379ms
  ```
- **No regressions** (same binary, filtered suites):
  ```
  ArmSendTest        Totals: 9  passed, 0 failed  (23404ms)   (includes A2 production-wiring test)
  TelemetryBridgeTest   Totals: 7  passed, 0 failed
  ArmingGateTest       Totals: 8  passed, 0 failed
  ArmingFlowIntegrationTest Totals: 11 passed, 0 failed
  ArmingGateExtendedTest Totals: 10 passed, 0 failed
  FlyQuickActionsSmokeTest Totals: 5 passed, 0 failed (tests the same productMark sentinel)
  ```

### Phase 2 — Remove Takeoff from default FlyView (pure deletion) — DONE

5. Delete the Takeoff QuickButton block and the `takeoff` branch in
   `onActionTriggered` (FlyQuickActions.qml:251-266, 587-588). Remove
   `TelemetryBridge::takeoff()` and its header decl (TelemetryBridge.h:724,
   .cpp:1195-1211). Do NOT build a gated autonomous version (Open Question).
6. Sentinel check as above (verify no `takeoff` reference remains reachable and
   the app builds + QML smoke green).

**Landed (Oct 1 2026):** Takeoff QuickButton (`takeoffBtn`), the `action ===
"takeoff"` branch, the dead `takeoffEnabled` compat property, and
`TelemetryBridge::takeoff()` (decl + impl) all removed. Stock ToolStrip takeoff
is untouched (RC-primary spec). New `testTakeoffRemoved()` asserts
`TelemetryProvider.takeoff` is no longer exposed, no "Takeoff" item exists in the
loaded root, and no `TelemetryProvider.takeoff(`/`action: "takeoff"` wiring
remains.
```
FlyQuickActionsSmokeTest Totals: 6 passed, 0 failed   (new testTakeoffRemoved PASS)
TelemetryBridgeTest       Totals: 7 passed, 0 failed
ReturnToLaunchTest        Totals: 9 passed, 0 failed
ArmSendTest               Totals: 9 passed, 0 failed
ArmingFlowIntegrationTest Totals: 11 passed, 0 failed
```

### Phase 3 — Converge force-arm callers on the funnel (prereq for bench panel) — DONE

Evidence (final build, `PreflightQGroundControl`):

```
ArmSendTest                      Totals: 11 passed, 0 failed, 0 skipped, 0 blacklisted, 29955ms
HardwareTestControllerTest       Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 13762ms
ControlSurfaceTestControllerTest Totals: 9 passed, 0 failed, 5 skipped, 0 blacklisted, 21454ms
FlyQuickActionsSmokeTest         Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 479ms
ReturnToLaunchTest               Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted, 14545ms
TelemetryBridgeTest              Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 64ms
ArmingGateTest                   Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 66ms
ArmingGateExtendedTest           Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 242ms
ArmingFlowIntegrationTest        Totals: 11 passed, 0 failed, 0 skipped, 0 blacklisted, 1742ms
VehicleProfileManagerTest        Totals: 11 passed, 0 failed, 0 skipped, 0 blacklisted, 78ms
```

New tests, all PASS:

| Test | Asserts |
|---|---|
| `ArmSendTest::testBenchForceArmFunnelSingle2989Command` | Closed gate + funnel → bounded override opens, exactly ONE `ARM_DISARM` on the wire, param1=1, param2=**2989** (never 21196), vehicle arms |
| `ArmSendTest::testForceArmRefusedAirborne` | Airborne (ground speed > `kForceArmMaxGroundSpeedMs`) → non-empty refusal, **no** override opened, 0 commands; grounded → accepted, 2989 on the wire |
| `HardwareTestControllerTest::testFixedWingMotorTestArmsViaFunnelWith2989` | FW auto-arm goes through the funnel: 1 command, param2=2989 ≠ 21196, `gate.isOverrideActive()` |
| `ControlSurfaceTestControllerTest::testPreSweepArmViaFunnelWith2989` | Pre-sweep arm via funnel: 1 command, param2=2989 ≠ 21196, override open |
| `FlyQuickActionsSmokeTest::testSingleForceArmPath` | Fly view wires only `TelemetryProvider.forceArm()`; the `ArmingGate.forceArm()` / `v.forceArm()` parallel paths are gone from the QML |

Implementation notes:

- `TelemetryBridge::forceArm()` now returns `QString`: empty = sent, non-empty = displayable refusal.
  Ground-safety precondition (item 11): refuses when ground speed >
  `kForceArmMaxGroundSpeedMs` (1.0 m/s, `custom/src/utils/Config.h`), when
  `vehicle->flying()`, or during takeoff/landing. The refusal is checked **before**
  the override window is opened, so a refused force-arm leaves no gate window open.
- `MockLink` gained `lastArmDisarmParam2()` (reset by
  `clearReceivedMavCommandCounts()`) so tests can prove the wire carried 2989 rather
  than only counting commands.
- `VehicleProfileManager::forceArm()` now calls `m_telemetry->forceArm()` when a
  bridge is injected; the old `m_armingGate->forceArm()` + `vehicle->forceArm()`
  body survives only as a no-bridge fallback.
- `HardwareTestController` / `ControlSurfaceTestController` take a new
  `setTelemetryBridge()` (wired in `PreflightPlugin`) and route their arm through
  the funnel when present; the raw fallback now also uses `kForceArmParam2` instead
  of the legacy 21196. The remaining `kLegacyForceArmParam2` uses in those files
  are **disarm** paths (param1=0), out of scope for this phase.
- `FlyQuickActions.qml` force-arm dialog: only `TelemetryProvider.forceArm()`,
  with the returned refusal surfaced via `_rtlResultLabel`.

7. **VehicleProfileManager::forceArm()** → replace body with a call into the
   funnel: `TelemetryBridge::forceArm()` semantics (open override, log, send 2989).
   Prefer injecting/calling the plugin's bridge (`_telemetryBridge`) rather than
   reimplementing; keep the load-bearing `m_armingGate->forceArm()` open inside the
   funnel (which the funnel already does).
8. **HardwareTestController**: change `_startFixedWingMotorTest` to route the arm
   through the funnel instead of `sendMavCommand(..., 1.0f, kLegacyForceArmParam2)`
   (HardwareTestController.cpp:760-764). Keep the bounded override open (already
   present at :755-759, now inside the funnel).
9. **ControlSurfaceTestController**: same for pre-sweep arm
   (ControlSurfaceTestController.cpp:523-526).
10. Delete/neutralize the parallel force-arm trio in
    FlyQuickActions.qml:397-405 (keep only `TelemetryProvider.forceArm()`).
11. Harden `TelemetryBridge::forceArm()` with the bench precondition: refuse when
    `vehicle->groundSpeed() > threshold` or a landed/takeoff state mismatch
    (defense against force-arming airborne). Return a structured refusal the UI
    can display. (This is where the new panel's ground-safety check lives.)
12. **Tests**: extend `ArmSendTest` with a bench-panel force-arm override scenario
    (mirror scenario (b) through the funnel); extend `HardwareTestControllerTest`
    and `ControlSurfaceTestControllerTest` to assert the bounded override + single
    param 2989 on the wire; assert the airborne-precondition refusal.

### Phase 4 — Bench Test panel with hardened Force Arm — SCOPED DOWN

**Decision (user):** the Bench Test panel was NOT built. The intended host,
`custom/qml/pages/MaintenancePage.qml`, is **dead UI** — it is registered in
`custom/custom.qrc:51` but nothing navigates to it: no `Config.navigatePush()`
target names it, and no QML or C++ file references it anywhere. Building the
panel would have put a hardened force-arm affordance into unreachable UI.
Phase 4 was therefore spent **hardening the force-arm paths that ARE reachable**
instead.

Reachability audit of every force-arm call site:

| Call site | Reachable via | Phase 4 action |
|---|---|---|
| `FlyQuickActions.qml:459` | Fly view | already single-path (Phase 3) |
| `MainStatusIndicator.qml:353` | custom toolbar (aliased over stock) | **hardened** |
| `WizardStep4Review.qml:294` | `PreFlightChecklist.qml` step 3 | **hardened** |
| `ArmGatePanel.qml:148` | `PreflightChecklistView.qml:1323` | **hardened** |
| `ControlSurfacePanel.qml:270` | unreferenced, but shipped in qrc | **hardened** |
| `MaintenancePage.qml` | **nothing** | not touched (dead) |

Defects found and fixed:

1. **`ArmGatePanel.qml` "Force Arm" armed nothing.** `ArmingGate.forceArm()`
   (`ArmingGate.cpp:284`) only sets `_overrideActive` and starts the bounded
   timer — it sends **no command**. That button was the whole of its
   `onClicked`, so pressing it opened an override window and nothing else.
   Now calls the funnel, which opens the override *and* sends.
2. **`WizardStep4Review.qml` sent a plain arm after forcing.** It called
   `ArmingGate.forceArm()` then `TelemetryProvider.arm()`, i.e. `param2=0` with
   no force magic — the autopilot was still free to reject the arm on its own
   pre-arm checks, so the operator's "force" could silently no-op. Now the funnel
   is called **first** and the audit `Database.insertTelemetryEvent(...,
   "FORCE_ARM", ...)` plus the success overlay only happen if it was accepted; a
   refusal shows a red panel and logs nothing.
3. **`MainStatusIndicator.qml` kept the Phase 3 trio** — gate override only, then
   the force-arm, with a `mainWindow.forceArmVehicleRequest()` fallback that
   routes to stock `GuidedActionsController` and so through the APM/PX4 firmware
   plugins this fork disables (`CustomOverrides.cmake:42-45`). Collapsed to the
   one funnel call; the stock fallback is gone. On refusal the popup stays open
   and the reason is logged rather than swallowed.
4. **`ControlSurfacePanel.qml` called raw `v.forceArm()`**, bypassing the funnel:
   no 2989 magic, and none of the Phase 3 ground-speed precondition. Now routed
   through the funnel with the refusal rendered inside the dialog.

Standing regression check (item 16 substitute):
`FlyQuickActionsSmokeTest::testNoQmlCallsGateOnlyForceArm` walks the whole
`custom/qml` tree, strips line comments, and fails if any file makes a live call
to `ArmingGate.forceArm()` or `mainWindow.forceArmVehicleRequest()`, and asserts
the funnel is wired in more than one panel. It is proven live: stashing the
`MainStatusIndicator.qml` hardening back to its pre-Phase-4 state makes it fail
with both offenders named, and restoring the fix makes it pass.

Evidence (final build):

```
FlyQuickActionsSmokeTest         Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 961ms
ArmSendTest                      Totals: 11 passed, 0 failed, 0 skipped, 0 blacklisted, 33587ms
HardwareTestControllerTest       Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 14668ms
ControlSurfaceTestControllerTest Totals: 9 passed, 0 failed, 5 skipped, 0 blacklisted, 23915ms
ReturnToLaunchTest               Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted, 16212ms
TelemetryBridgeTest              Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 52ms
ArmingGateTest                   Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 62ms
ArmingGateExtendedTest           Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 272ms
ArmingFlowIntegrationTest        Totals: 11 passed, 0 failed, 0 skipped, 0 blacklisted, 2243ms
VehicleProfileManagerTest        Totals: 11 passed, 0 failed, 0 skipped, 0 blacklisted, 97ms
```

Live-app regression (`--live-arm`, Track A driver, exit 0): phase A
`armCount=0 armed=false vetoDenied=true`; phase B
`armCount=1 lastParam1=1.0 armed=true overrideActive=true`; `SUCCESS`; RTL repro
`doSetModeWireCount=1` `Custom:0x10000`→`Custom:0xb`; productMark sentinel present.

Still open from the original Phase 4: the Bench Test panel itself (items 13-17)
needs a reachable host page first.

13. Extend `custom/qml/pages/MaintenancePage.qml` with the bench section
    (decision 1 above; it already hosts vehicle profile/history and is the
    natural home next to MotorCheckPanel, currently loaded from
    PreFlightChecklist.qml:522, and GimbalTest, :644). No new page.
14. New panel layout: explicit **"Ground Test Mode"** banner; Force Arm with
    **two-step confirm** (hold-to-confirm or type-to-confirm, not single tap);
    live groundspeed + vehicle state shown; button calls
    `TelemetryProvider.forceArm()`; surfaces the funnel's refusal text.
15. Wire the panel into navigation (MaintenancePage or new page) and the plugin
    context properties.
16. **Smoke test**: BenchTestPanel — force-arm present, two-step required, refuses
    with non-zero groundspeed.
17. Manual bench verification: force-arm with a failing mandatory check opens the
    bounded override and sends exactly one arm command (mirror ArmSendTest (b)
    through the new UI path, against the running app, not just the harness).

### Phase 5 — Remove Force Arm from Fly view + residuals

18. Delete Force Arm QuickButton + its confirm dialog from FlyQuickActions
    (lines 225-234, 367-406); update `FlyViewCustomLayer.qml` action routing
    (`forceArm` branch at :377-379) → no longer handled (remove branch).
19. MainStatusIndicator: remove the press-and-hold force-arm affordance
    (MainStatusIndicator.qml:317, 353-366); the Arm/Disarm button becomes a plain
    single-action arm (through `TelemetryProvider.arm()`) + disarm
    (mainWindow.disarmVehicleRequest()).
20. ArmedIndicator.qml / PreflightToolbarIndicator.qml: no force-arm present
    today; add a sentinel/load trace to *prove* the loaded files have no
    affordance (standing regression check, not a one-off).
21. Sentinel checks at every QML edit as per spec migration note — confirm with a
    sentinel log line that the edited file is the one actually running (given the
    MainStatusIndicator shadowing already documented).

### Phase 6 — Monitoring surface + rollout

22. Add/extend telemetry-strip widgets per the Monitoring Surface table as
    capacity allows (incremental, non-blocking). Read-only from the UI's
    perspective; only the RTL row issues a flight command.
23. Update the operator runbook / `custom/doc.md` to reflect the new Fly view and
    where bench testing now lives.
24. Full regression: run the full custom suite per-class via
    `./build/Debug/PreflightQGroundControl --unittest:<Name>` (full-suite run is
    OOM-killed in this environment; always per-class). Confirm ArmSendTest,
    HardwareTestControllerTest, ControlSurfaceTestControllerTest, all Arming*
    tests, QmlSmokeTest (with the known Qt6 AOT teardown caveat), plus the new
    smoke tests.

## Test plan summary (deferred to implementation)

| Suite | Must show |
|---|---|
| ArmSendTest (a/b/c) | Unchanged — veto at Vehicle chokepoint, independent of QML caller |
| ArmSendTest (new) | Bench force-arm override: exactly one 2989-param2 command via funnel; airborne-precondition refusal |
| HardwareTestControllerTest | FW auto-arm opens bounded override, single funnel send, no legacy 21196 |
| ControlSurfaceTestControllerTest | Pre-sweep arm opens bounded override, single funnel send |
| MavLinkInterceptTest | Unaffected (inbound-only) |
| FlyQuickActions QML smoke (new) | Force Arm + Takeoff absent for default quad/FW; Return present, single `returnToLaunch()` |
| BenchTestPanel smoke (new) | Two-step confirm; ground-safety refusal; exactly one command |
| Sentinel/load-trace | Every edited QML is the one actually running, not a stock twin |
| Manual bench/live | Real app: gate-closed → zero wire commands + denial; override → exactly one; via production `TelemetryBridge::forceArm()` wiring |

## Check-in decisions (locked 2026-09-29)

1. **Bench panel placement**: extend `MaintenancePage.qml` (recommended option).
   Phase 4 step 13-15 builds the Ground Test Mode Force Arm section into
   MaintenancePage, next to vehicle profile/history; it also consolidates where
   MotorCheckPanel/GimbalTest are reached if that navigation is rerouted here.
2. **RTL authorization**: anyone with the Fly view open — no role gate. RTL stays
   a single, always-enabled-on-connection button usable by pilot or observer.
3. **Override window**: reuse the flight-context window (`kForceArmOverrideSec`)
   for bench force-arm; no new knob (spec Open Question 5 deferred).
4. Compliance-log RTL event: use TelemetryEventLogger's existing schema
   (`flightId, eventType, triggeredBy, batteryV, altitudeM, sats, flightMode`),
   writing event type `RTL_TRIGGERED` with triggeredBy populated — consistent
   with in-place logging, no schema change. (Ties into Phase 1 step 1 logging.)
5. Scope: VTOL excluded as per spec line 11.

## Remaining open questions (deferred, non-blocking)

- Does any current ops profile need GCS-commanded takeoff (VTOL transition,
  scripted survey launch)? → separate "Autonomous Ops" follow-up spec if ever.
- Should ground testing get a longer/re-triggerable override window after all?
  (Deferred by decision 3.)