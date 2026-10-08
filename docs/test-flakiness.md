# Test stability notes

One home for why a test is fixed, excluded, or known-failing, so the rationale does
not end up living only in CI YAML, commit messages, and comments inside test files.

Companion to `mavlink-parser-wedge.md`, which covers the MAVLink parser findings.

## Always run the suite serially

`ctest -j1`. This is not a preference, it is a measured result on the 2-CPU dev box:

| Invocation | Full suite (76 tests) | Result |
|---|---|---|
| `ctest -j1` | ~412–460 s | stable |
| `ctest -j2` | ~1500 s | slower **and** unstable |

Parallel runs are slower *and* red, because the tests below already assume a quiet
machine. `.github/workflows/preflight.yml` passes `-j1` explicitly — `ctest`
otherwise defaults to the runner's core count.

Each test runs in its own process via CTest, which is what keeps per-class results
deterministic at all (a single `--unittest` process is order-dependent: the QML
smoke tests deliberately leak engines and windows to dodge a Qt 6 AOT teardown crash).

## Fixed

### `MissionControllerTest::_testGimbalRecalc` — timing, fixed

**Symptom:** `missionGimbalYaw()` returned `nan` where `0.0` was expected.

**Mechanism:** the test slept a fixed `QTest::qWait(500)` waiting for
`MissionController::_recalcMissionFlightStatusSignal`. That signal is a
`Qt::QueuedConnection` *and* registered as compressed
(`MissionController.cc:66`, `QGCApplication::compressEvent`), so delivery is
event-loop dependent. The assertion read `missionGimbalYaw()`, which only that
recalc populates (`VisualMissionItem.cc:149-151`).

**Evidence it was not shared state:** 5/5 pass unloaded, **3/3 fail** under 6-way CPU
contention on a 2-CPU box. The fixture resets cleanly and the settings the test reads
are set by the test itself.

**Fix:** replaced all three fixed `qWait(500)` calls with a bounded poll
(`waitForRecalc`, 10 s ceiling, 25 ms steps) that waits for the recalc's effect.
The ceiling keeps a real regression a fast failure rather than a hang.
After: 3/3 pass at load 8.2, runtime unchanged.

### `VehicleLinkManagerTest` — two mechanisms, both fixed

**(a) Fixed waits too tight.** `QSignalSpy::wait()` was called with 3000 ms for
`Vehicle::initialConnectComplete` and 1000 ms for
`MultiVehicleManager::activeVehicleChanged`. Under contention these are simply not
enough — `initialConnectComplete` fires only after the full parameter /
component-information handshake (`InitialConnectStateMachine.cc:401`).

Reproduced directly: under 6-way contention a *different* sub-test failed each run
(`_simpleLinkTest`, `_multiLinkSingleVehicleTest`, `_connectionRemovedTest`,
`_highLatencyLinkTest`), matching what the full suite showed. The failing runs took
~33 s while the passing run took ~42 s — i.e. ~9 s of capped-out waits.

**Fix:** named constants (`vehicleCreateTimeoutMSecs`,
`initialConnectTimeoutMSecs`, `vehicleDeleteTimeoutMSecs`, all 15 s) used at every
fixed-wait call site. `QSignalSpy::wait()` returns the instant the signal arrives, so
a generous ceiling costs nothing when healthy.

Deliberately left alone: the `wait(_heartbeatMaxElpasedMSecs * 2)` calls. Those
encode product semantics (one of them asserts that a signal does **not** arrive),
not an arbitrary sleep.

**(b) An assertion the product never guaranteed.** `_multiLinkSingleVehicleTest`
asserted `linkNames()[0] == "Mock 1"`, i.e. link *creation* order. But
`VehicleLinkManager::mavlinkMessageReceived()` appends to `_rgLinkInfo` on a link's
**first heartbeat** (`VehicleLinkManager.cc:49-50`), so `linkNames()` and
`linkStatuses()` are ordered by heartbeat arrival. The test already documented that
exact scheduling dependence for `primaryLink` two lines above, then asserted creation
order anyway.

**Fix:** derive `primaryIndex` / `secondaryIndex` from the configuration names and
index every status assertion from those, instead of assuming index 0 is `"Mock 1"`.

### `MockLink` worker timers packing on a freed MAVLink channel — SIGSEGV, fixed

Seen as a segfault roughly once per full-suite run, in a *different* test each time:
`VehicleLinkManagerTest::_simpleCommLossTest` crashing in `_sendGpsRawInt()`, then
`ParameterManagerTest` crashing in `_sendADSBVehicles()`. Both traces are identical in
shape:

```
mavlink_finalize_message_buffer(..., status=0x0, ...)   # NULL deref, fault address 0x10
  <- mavlink_finalize_message_chan(..., chan=255, ...)
  <- mavlink_msg_<whatever>_pack_chan(...)
  <- MockLink::_sendXxx()
  <- MockLink::runNHzTasks()          # on the MockLinkWorker thread
```

`mavlink_get_channel_status(255)` returns `NULL`, and `mavlink_finalize_message_buffer`
dereferences it without checking. Channel 255 is `LinkManager::invalidMavlinkChannel()`.

**Mechanism.** `LinkManager::_linkDisconnected()` (`src/Comms/LinkManager.cc:242`) calls
`link->_freeMavlinkChannel()`, which sets `_mavlinkChannel = invalidMavlinkChannel()`
(`src/Comms/LinkInterface.cc:105`). That runs synchronously on the link's thread, driven
by `MockLink::disconnect()`'s `emit disconnected()`. The worker's 1 Hz / 10 Hz / 500 Hz
timers, however, keep firing on their own thread until `MockLink` is destroyed.

So a sender that had *already passed* its `_connected` and `mavlinkChannelIsSet()`
guard a moment earlier packs with a channel that has since been freed. The window is a
few microseconds — narrow enough that it showed up about once per 8-minute suite run and
never in isolation, and narrow enough that a guard placed only at function entry cannot
close it: one run crashed three calls *after* the guard had returned true.

**Fix, two layers:**

1. `MockLink::disconnect()` now stops the worker's timers **before** emitting
   `disconnected()`, using a blocking
   `QMetaObject::invokeMethod(_worker, &MockLinkWorker::stopWork, Qt::BlockingQueuedConnection)`
   so it waits for any in-flight handler to finish. The channel is only freed after no
   sender can be running.
2. `run1HzTasks()` / `run10HzTasks()` / `run500HzTasks()` and `_sendHeartBeat()` also
   check `mavlinkChannelIsSet()`. `_sendHeartBeat()` needs its own check because it is
   now posted to the link thread and can land after teardown has begun.

**Result:** two consecutive full-suite runs with no segfaults, `3 tests failed out of 76`
both times — exactly the three CI-excluded tests below.

## Known failing — pre-existing, not fixed

These fail on stock upstream tests in this fork before any change here. Item 4 of the
reliability campaign chose to document rather than fix them.

| Test | Failure | Location |
|---|---|---|
| `CorridorScanComplexItemTest::_testItemGeneration` | command `16` (`MAV_CMD_NAV_WAYPOINT`) where `4501` expected | `test/MissionManager/CorridorScanComplexItemTest.cc:235` |
| `SurveyComplexItemTest::_testItemGeneration` | same mismatch, `16` vs `4501` | `test/MissionManager/SurveyComplexItemTest.cc:259` |
| `SimpleMissionItemTest::_testEditorFacts` | `QVERIFY(found)` false — expected text-field fact not present | `test/MissionManager/SimpleMissionItemTest.cc:201` |

Related: `TransectStyleComplexItem.cc:1207` (condition-gate path) and
`TransectStyleComplexItemTestBase.cc` (TRACK vehicle default) are the suspected
product-side causes for the two item-generation failures.

### `ControlSurfaceTestControllerTest` — measured, fixed at three layers, both findings closed

Not part of the original two, but it surfaced once the two above were fixed, so it
belongs here rather than being rediscovered later.

#### The experiment

Two conditions, **10 full-suite runs each** (`ctest -j1`, unloaded — contention is
not the trigger for this one, so loads runs were deliberately not used), split by
failure mode because they are not the same bug:

| Failure mode | Baseline (10 runs) | With arm wait (10 runs) |
|---|---|---|
| **A** — sweep never starts; fails at the `activeSurface() >= 0` pump | **0** | **0** |
| **B** — sweep starts, then stops; fails at the auto-stop `QVERIFY2` | **0** | **1** (`fixed_run_2`, `cpp:622`) |
| same missing-wait pattern, *different test* — `testDeadConfigFields`, mode A | **1** (`baseline_run_10`, `cpp:766`) | **0** |

Baseline for `testNoAutoStopOnExternalArmOrModeChange` was therefore **0/10 on both
modes** — too rare in a full suite to verify anything against, so the mutation check
was run where the signal actually is: the test binary in isolation.

| Condition | Isolation runs | Mode A | Mode B | Arm-wait timeout | Total fails |
|---|---|---|---|---|---|
| **mutated** (arm waits removed) | 12 | **1** | 1 | n/a | **2** |
| **fixed** (arm waits added) | 40 | **0** | 2 | **3** | **5** |

#### What the fix actually did — and did not do

**Applied:** an explicit `_pumpUntil(isArmed(), 10000)` before the
`activeSurface() >= 0` wait in Tests 2 and 3 of
`testNoAutoStopOnExternalArmOrModeChange`, matching what the first sweep already does
at `cpp:559-564`.

**Mutation-verified both ways.** Removing the two waits restores mode A (1/12).
But the fix does **not** make the test deterministic, and reporting it as fixed would
be wrong:

- **Mode A is gone** (0/40 vs 1/12) — the ordering hypothesis was correct.
- **Mode A was not eliminated, it was relocated.** With the wait in place the same
  underlying event now fails *at the arm wait* instead of at the sweep wait:
  `_pumpUntil(isArmed(), 10000)` false at `cpp:610`, 3 times in 40 runs.
  Mutated arm-failure-equivalent rate ≈ 8.3 % (1/12); fixed arm-wait rate ≈ 7.5 %
  (3/40). **The same failure rate, now correctly named** — the vehicle does not arm
  within 10 s, and the old assertion could not tell you that.
- **Mode B is unaffected** (2/40 vs 1/12) — the arm wait has no bearing on it.

So the arm wait is kept: it converts an ambiguous "the sweep never started" into
"the vehicle never armed", which is the actual diagnosis. It is a diagnostic fix, not
a stability fix. The intermittency it exposed was real, was measured here at 20 %, and
has since been root-caused and fixed — see the next finding.

#### Named finding: mode B was unreachable in this build flavour — root cause found, and now fixed

**Not a flakiness quarantine — a distinct finding with a distinct cause, now closed.**

> ~~Sweep auto-stop verification is unreachable in this build flavour, because
> `Vehicle::setFlightMode()` is inert with the APM plugin compiled out.~~
> **Resolved.** The APM plugin is re-enabled (decision reversed below), and the
> blocker turned out to sit one layer deeper than the plugin switch.

**Stage 1 — what the old note got right.** With `QGC_DISABLE_APM_PLUGIN` /
`QGC_DISABLE_APM_PLUGIN_FACTORY` forced on (`custom/cmake/CustomOverrides.cmake:42-44`),
`Vehicle::setFlightMode()` never leaves the generic `FirmwarePlugin` base, which
returns false with `FirmwarePlugin::setFlightMode called on base class, not supported`
(`src/FirmwarePlugin/FirmwarePlugin.cc:84-90`). That made
`testManualModeReassertReachesAutopilot` `QSKIP` and left Test 3 of
`testNoAutoStopOnExternalArmOrModeChange` unverifiable in either direction.

**Stage 2 — flipping the switch was necessary but not sufficient.** Both flags set to
`OFF`; the tree rebuilt clean — **480/480 targets, `BUILD_RC=0`, zero compiler errors**,
`APMFirmwarePluginFactory` present. The suite then failed *differently*:
`12 passed, 3 failed, 0 skipped`.

| Test | Assertion that failed |
|---|---|
| `testManualModeForcedAndReasserted` | `_pumpUntil(planeModeNumber(flightMode()) == 2, 5000)` |
| `testManualModeReassertReachesAutopilot` | same assertion, same 5 s timeout |
| `testNoAutoStopOnExternalArmOrModeChange` | `lastErrorMessage().contains("left MANUAL")` |

All three are *mode-decode* assertions, not mode-send assertions. The log carried
`current: "Unknown"` three times and `called on base class` zero times: the request
was reaching the plugin, the decode was wrong.

**Root cause — measured, not inferred.** `InitialConnectStateMachine` requests
`AVAILABLE_MODES` on every connect and feeds the answer straight back into the plugin
through `StandardModes::gotMessage` → `FirmwarePlugin::updateAvailableFlightModes()`
(`src/Vehicle/StandardModes.cc:96`), which **clears and refills** `_modeEnumToString`
*and* `_flightModeList` (`src/FirmwarePlugin/FirmwarePlugin.cc:456-460`). MockLink
answered that request with one static table for every firmware type
(`src/Comms/MockLink/MockLink.cc:30`), and the table was PX4-flavoured.

Seen with `--logging:qgc.firmwareplugin.firmwareplugin` — the only route that works,
because `QGCLoggingCategoryRegister::setFilterRulesFromSettings()` overwrites
`QT_LOGGING_RULES` and the unit tests `settings.clear()` on every run:

| `_updateFlightModeList` call | Before the fix | After the fix |
|---|---|---|
| 1st (plugin self-registration) | ArduPlane 31 modes (`Manual` 0 … `Autoland` 26), then ArduCopter 30 | identical |
| every later call | **15 PX4 rows** — `Manual` 65536, `Stabilized` 458752, `Offboard` 393216, `MockLink Mode` 524288, `(Mode not available)` 117702656 | **the same 26-row ArduPlane list, byte-identical every time** |

The last registration wins, so the plugin's ArduPlane map was overwritten by the PX4
one. `APMFirmwarePlugin::flightMode()` (`APMFirmwarePlugin.cc:114`) is gated on
`MAV_MODE_FLAG_CUSTOM_MODE_ENABLED` and does
`_modeEnumToString.value(custom_mode, "Unknown")`; no PX4 key is `0`, so ArduPlane
`MANUAL` decoded to `"Unknown"` and `FlightModeUtil::planeModeNumber("Unknown")` is
`-1`. The mode *list* was clobbered as well, so `setFlightMode("MANUAL")` matched
PX4's `Manual` (65536) and put the wrong `custom_mode` on the wire.

**Fix.** `MockLink` now sources its `AVAILABLE_MODES` reply from the firmware plugin
for ArduPilot (`MockLink::_flightModesFor()`), so the round-trip is a no-op instead of
an overwrite. That matches real ArduPilot, which has advertised its own custom modes
since 4.7 (ArduPilot/ardupilot#28566, `standard_mode` left unset). Supporting changes:
`FirmwarePlugin::availableFlightModes()` read accessor; `FlightMode_t` gained a
`delayed` flag so `_availableModesCount()` no longer depends on a hardcoded `-1` for
the trailing synthetic entry; `_sendAvailableMode()` packs through a zeroed 35-byte
buffer because `mav_array_memcpy(packet.mode_name, …, 35)` reads past a short string.

**Result:** `15 passed, 0 failed, 0 skipped`, with 0 occurrences of
`called on base class`, 0 of `Unknown flight Mode`, 0 of `current: "Unknown"`, and
0 PX4 custom modes left in the registration log.

**Consequence for the suspected product bug:** the no-auto-stop-on-external-arm-change
behaviour is now proven on the real plugin path — `setFlightMode()` is a live send,
not a no-op. Both tests above are genuine coverage again.

#### Named finding: the residual intermittency was a MockLink cross-thread heartbeat race — root cause found, and now fixed

**Not flakiness, not a test-ordering artefact. A data race in the mock link, and the
last thing standing between this suite and being deterministic.**

**What was still failing.** After the `AVAILABLE_MODES` fix above, the suite was green
in a single pass but not repeatably. **20 isolated runs of
`ControlSurfaceTestControllerTest`**, same binary, same machine:

| | Runs | Failures | Failure signature |
|---|---|---|---|
| before the fix | 20 | **4 (20 %)** | all `testNoAutoStopOnExternalArmOrModeChange`, all at `_pumpUntil(isArmed(), 10000)` |
| after the fix | 20 | **0** | — |

**The signature that made it diagnosable.** Counting
`Sweep auto-stopped: "Vehicle left MANUAL for Loiter …"` per run gave a perfect
correlation, 20/20:

| occurrences in a run | runs | outcome |
|---|---|---|
| 1 (Test 2's legitimate stop) | 16 before / **20 after** | pass |
| 2 (a second, spurious stop) | **4 before** / 0 after | **fail, 4/4** |

The two failure modes recorded above are the same defect seen at two different
instants. Test 3 of the test starts a sweep, then ~40 ms later a stale LOITER mode
change lands and auto-stops it; if that happens while the test is still in its
`isArmed()` poll (which the auto-stop's disarm then cancels) you get the arm-wait
failure, and if it happens after `setFlightMode("STABILIZE")` you get mode B's
`"STABILIZE incorrectly auto-stopped the sweep"`. Same stale event, two landing zones.

**Mechanism — measured from the log, then confirmed against the code.** In a failing
run the vehicle's mode went `Loiter → Manual → Loiter` inside 100 ms:

```
32.894  Sweep auto-stopped: "… left MANUAL for Loiter …"      (Test 2, legitimate)
32.949  SURFACE TEST … mode "Manual"                          (Test 3's _startSurfaceSweep
                                                               forced MANUAL — it found "Loiter")
32.991  Sweep auto-stopped: "… left MANUAL for Loiter …"      (spurious)
```

Only one line in the test ever writes `custom_mode = 12`, and it runs once. So the
vehicle was shown `12` *after* it had already been shown `0`. Threading explains it:

- `MockLink::_mavCustomMode` is written **only on the link's own thread** — by
  `setFlightMode()` (called directly by the test) and by the receive path
  (`_writeBytes` → `writeBytesQueuedSignal`, a `Qt::QueuedConnection` →
  `_writeBytesQueued` → `_handleSetMode`/`_handleCommandLong`).
- It was **read on the worker thread**, by `MockLink::run10HzTasks()` →
  `_sendHeartBeat()`. A plain `uint32_t`, no mutex, no atomic — `_mavlinkAuxMutex`
  only guards the parser.
- `respondWithMavlinkMessage()` does a *direct* `emit bytesReceived`. `Vehicle` lives
  on the link thread, so an emit from that thread is handled **synchronously**, while
  an emit from the worker is **queued**. A tick that read a stale mode is therefore
  not merely late — it is delivered out of order, after the fresh one.

A 10 Hz tick at ~32.95 read `12` written a millisecond earlier by the link thread,
packed it, and queued it; the link thread's `0` had already been applied
synchronously at 32.949. The vehicle then bounced back to LOITER.

**Fix** (`src/Comms/MockLink/MockLink.cc`). The two mode-carrying emitters reachable
from the worker — `_sendHeartBeat()` in `run1HzTasks()`/`run10HzTasks()` and
`_sendHighLatency2()` — are now marshalled onto the link thread with
`QMetaObject::invokeMethod(…, Qt::QueuedConnection)`. Packing then always happens on
the thread that owns the fields, so the value read is never stale, and every
heartbeat is emitted from one thread, so delivery order is preserved. The worker
still drives the timers; only the build-and-send of these two messages moves.

**Result.** 20/20 isolated runs green, every one showing exactly one LOITER auto-stop
(the legitimate one). The `isArmed()` timeout and the `"STABILIZE incorrectly
auto-stopped the sweep"` assertion were both casualties of this one race and both
disappeared with it.


#### The five originally-skipped gaps — what is actually proven

Baseline for this suite, recorded in `custom/FlyQuickActions Implementation Plan.md`
(lines 172 and 294): `9 passed, 0 failed, 5 skipped`. All five QSKIPs were removed by
one commit, `b3c0bbd1a`, which is the only mock-infra commit since. Current status:

| # | Gap / test | Status | Proven on |
|---|---|---|---|
| 1 | `testFlapRcChannelClobber` | **PASS** | **genuine new capability** — MockLink `RC_CHANNELS_OVERRIDE` capture plus `_setParam()` driving `FLAP_IN_CHANNEL` through the real `PARAM_SET` round-trip |
| 2 | `testNoAutoStopOnExternalArmOrModeChange` | **PASS, 20/20 isolated runs** — mode A addressed, mode B **proven** on the real plugin path, and the residual 20 % intermittency root-caused and fixed (both findings above) | **genuine new capability** — MockLink APM plane-mode encoding in `setFlightMode()`; with the APM plugin enabled the build flavour no longer limits what the assertions can prove |
| 3 | `testDbResultLoggingFires` | **PASS** | **narrowed** — the old QSKIP demanded a MockLink `injectMavlinkMessage()`; that API was never built and does not exist. The test was rewritten to inject `SERVO_OUTPUT_RAW` via the test's own `_sendServoOutputRaw()` helper |
| 4 | `testManualModeForcedAndReasserted` | **PASS** | **genuine new capability** — APM plane-mode encoding plus, since the plugin was re-enabled, the firmware-plugin half of the MANUAL re-assert as well |
| 5 | `testDeadConfigFields` | **PASS** (but failed mode A once in 10 baseline runs) | **narrowed** — no elevon-isolation or `channel2Invert` infra was ever built; the test now asserts the *reachable* facts (`channel2 == -1`, `Cooldown == 4`) instead of exercising the dead field |

Gaps 1, 2 and 4 rest on real infrastructure. **Gaps 3 and 5 rest on assertions that were
rewritten to fit what the mock can do** — they are green, but they are not the coverage
the original audit asked for, and they should not be counted as such in any coverage
roll-up.

Separately, `testDeadConfigFields` carries the *same* missing arm wait that Tests 2 and 3
had (`cpp:778-781`, unguarded `activeSurface()` pump) and produced the one baseline
mode-A failure of this round. It has been left as-is deliberately — changing two tests
at once would have made the experiment above uninterpretable.

## CI exclusion list

`preflight.yml` passes `-E '^(...)$'` with this policy, stated there too:

> Remove an entry once it is fixed — do not add new ones.

Current entries (three):

| Excluded | Status |
|---|---|
| `CorridorScanComplexItemTest` | known failing, table above |
| `SimpleMissionItemTest` | known failing, table above |
| `SurveyComplexItemTest` | known failing, table above |

`GpsTest` and `MissionControllerTest` were **removed** from the list this round: both
are fixed and both pass in a full serial run (`GpsTest` 0.40 s,
`MissionControllerTest` 12.68 s). Exclusions that are never pruned silently become a
permanent allow-list of failures, which is why the removal is part of the fix rather
than a follow-up.

## Standing build decision: `custom/cmake/CustomOverrides.cmake` no longer disables the APM plugin

No other document tracks this as a decision — it was cited as an established fact in
`custom/FlyQuickActions Implementation Plan.md:76` and `:273`, so it is recorded here
where the costs are actually measurable. Those two references have been annotated to
point back at this section.

**Decision, reversed:** `QGC_DISABLE_APM_PLUGIN` / `QGC_DISABLE_APM_PLUGIN_FACTORY`
are now forced **off** (`CustomOverrides.cmake:43-44`), so `Vehicle::setFlightMode()`
and the rest of the firmware-plugin overrides resolve to `APMFirmwarePlugin` instead
of falling through to the generic `FirmwarePlugin` base. `QGC_DISABLE_PX4_PLUGIN_FACTORY`
stays **on**.

**Why it was ever disabled: never stated.** The flag was introduced in `6432bf6a0`
with a commit body that does not mention it. The mechanism behind it is now
measurable and is recorded in the finding above: with the plugin enabled, MockLink's
PX4-flavoured `AVAILABLE_MODES` reply overwrote the plugin's own mode map,
`flightMode()` decoded every ArduPlane mode as `"Unknown"`, and three
control-surface tests failed. The symptom was real; the disable was aimed at the
wrong layer.

**Cost 1 — retired, but not yet re-validated on hardware.**
`Vehicle::guidedModeRTL()` was unusable, which is why `TelemetryBridge` sends its own
raw `MAV_CMD_DO_SET_MODE` (`custom/src/adapters/TelemetryBridge.cpp:1322`). The stock
path should now work, so that workaround is redundant-but-equivalent. It has been left
in place deliberately: dropping it before RTL has been re-tested on hardware would
trade a known path for an unproven one, and the bridge does exactly what the plugin
now does.

**Cost 2 — retired.** The two tests it removed from coverage are back:

| Test | Was | Now |
|---|---|---|
| `testManualModeReassertReachesAutopilot` | `QSKIP` on every run | **PASS** — `DO_SET_MODE` reaches MockLink with `custom_mode == 0` (`APMPlaneMode::MANUAL`) through the real plugin |
| `testNoAutoStopOnExternalArmOrModeChange`, Test 3 | ran, mode B unverifiable | **PASS** — mode B ("STABILIZE must not auto-stop the sweep") is now an actual assertion |

**What the flip cost:** one fix in `MockLink` (firmware-appropriate `AVAILABLE_MODES`,
see the finding above) plus three test comments that asserted the old story. No Qt,
branding, link or compile consequences: 480/480 targets, `BUILD_RC=0`, zero errors.

**Do not flip it back** without re-running the control-surface suite. The failure mode
is silent — a wrong mode table, not a crash — and it surfaces as
`flightMode() == "Unknown"` rather than as a build error.
