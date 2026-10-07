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

### `ControlSurfaceTestControllerTest` — measured, fixed at one layer, one finding open

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
a stability fix. The test remains ~10 % intermittent.

#### Named finding: mode B is unreachable in this build flavor

**Not a flakiness quarantine — a distinct finding with a distinct cause.**

> **Sweep auto-stop verification is unreachable in this build flavor, because
> `Vehicle::setFlightMode()` is inert with the APM plugin compiled out.**

`testNoAutoStopOnExternalArmOrModeChange` Test 3 (`cpp:597-622`) asserts that an
external change to STABILIZE does *not* auto-stop the sweep, while "the 1 s re-assert
timer then tries to force MANUAL back". In this build that re-assert **cannot reach
the autopilot at all**: `QGC_DISABLE_APM_PLUGIN` /
`QGC_DISABLE_APM_PLUGIN_FACTORY` (`custom/cmake/CustomOverrides.cmake`) leave
`Vehicle::setFlightMode()` failing on the generic `FirmwarePlugin` base. The vehicle
therefore sits in STABILIZE indefinitely instead of returning to MANUAL, which is a
state the product would never be left in with the plugin enabled — so the assertion
is being made against conditions production does not produce.

**Identical root cause, already recorded:** `testManualModeReassertReachesAutopilot`
`QSKIP`s for exactly this reason and says so in its skip text
(`ControlSurfaceTestControllerTest.cpp:219`). One build decision, two tests'
worth of safety-relevant coverage unavailable:
`testManualModeReassertReachesAutopilot` is **skipped outright**, and Test 3 of
`testNoAutoStopOnExternalArmOrModeChange` runs but cannot be trusted either way.
That is not zero.

**Consequence for the suspected product bug:** the no-auto-stop-on-external-arm-change
behaviour is **still unproven in either direction**. Mode B is evidence about the
build, not about the product.

Do not add this to the CI exclusion list: that would violate the policy below.

#### The five originally-skipped gaps — what is actually proven

Baseline for this suite, recorded in `custom/FlyQuickActions Implementation Plan.md`
(lines 172 and 294): `9 passed, 0 failed, 5 skipped`. All five QSKIPs were removed by
one commit, `b3c0bbd1a`, which is the only mock-infra commit since. Current status:

| # | Gap / test | Status | Proven on |
|---|---|---|---|
| 1 | `testFlapRcChannelClobber` | **PASS** | **genuine new capability** — MockLink `RC_CHANNELS_OVERRIDE` capture plus `_setParam()` driving `FLAP_IN_CHANNEL` through the real `PARAM_SET` round-trip |
| 2 | `testNoAutoStopOnExternalArmOrModeChange` | **RUNS, ~10 % intermittent** — mode A addressed, mode B open (above) | **genuine new capability** — MockLink APM plane-mode encoding in `setFlightMode()`; the *assertions* are intact, the build flavour limits what they can prove |
| 3 | `testDbResultLoggingFires` | **PASS** | **narrowed** — the old QSKIP demanded a MockLink `injectMavlinkMessage()`; that API was never built and does not exist. The test was rewritten to inject `SERVO_OUTPUT_RAW` via the test's own `_sendServoOutputRaw()` helper |
| 4 | `testManualModeForcedAndReasserted` | **PASS** | **genuine new capability** — APM plane-mode encoding; this is the half of the MANUAL re-assert that does not need the firmware plugin |
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

## Standing build decision: `custom/cmake/CustomOverrides.cmake` disables the APM plugin

No other document tracks this as a decision — it is cited as an established fact in
`custom/FlyQuickActions Implementation Plan.md:76` and `:273`, so it is recorded here
where the costs are actually measurable.

**Decision:** `QGC_DISABLE_APM_PLUGIN` / `QGC_DISABLE_APM_PLUGIN_FACTORY` are forced
on (`CustomOverrides.cmake:42-45`), so `Vehicle::setFlightMode()` and every other
firmware-plugin override fail through to the generic `FirmwarePlugin` base.

**Cost 1 — already known, and it is the reason RTL needed a fork-native path.**
`Vehicle::guidedModeRTL()` is unusable, which is why `TelemetryBridge` sends a raw
`MAV_CMD_DO_SET_MODE` itself.

**Cost 2 — added this round: two tests' worth of safety-relevant control-surface
coverage, not zero.**

| Test | Effect | Reference |
|---|---|---|
| `testManualModeReassertReachesAutopilot` | **skipped outright** on every run of this flavour | `ControlSurfaceTestControllerTest.cpp:219` |
| `testNoAutoStopOnExternalArmOrModeChange`, Test 3 | runs, but mode B ("STABILIZE must not auto-stop the sweep") is **unverifiable** — the re-assert that the test's own comment depends on cannot reach the autopilot | `ControlSurfaceTestControllerTest.cpp:597-622`, finding above |

Both are about the same thing: whether a mode re-assert actually lands on the
autopilot. That is the mechanism the surface-sweep safety interlock depends on, so
this is not cosmetic coverage — it is the assertion that the sweep stays under
control when the pilot or autopilot changes mode underneath it.

**Re-evaluate when:** the fork either ships its own firmware-plugin shim that
implements `setFlightMode()` for ArduPilot, or drops the disable and takes the Qt /
branding consequences. Until one of those happens, neither test can be made to prove
what it was written to prove, and adding either to the CI exclusion list would hide
the gap rather than close it.
