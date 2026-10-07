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

### `ControlSurfaceTestControllerTest` — newly found, root cause not yet found

Not part of the original two, but it surfaced once the two above were fixed, so it
belongs here rather than being rediscovered later.

**Symptom:** `testNoAutoStopOnExternalArmOrModeChange` fails at
`custom/tests/ControlSurfaceTestControllerTest.cpp:602` —
`_pumpUntil(activeSurface() >= 0, 15000)` returns false, i.e. Test 3's
`ctrl.testAileron()` never starts a sweep.

**What this is *not*:** it is not a too-tight timeout. `_pumpUntil`
(`...cpp:821`) is already a correctly written bounded poll with a 15 s ceiling that
spins `processEvents` + `qWait`. And it is **not** load-related — measured the
opposite way round from the two above:

| Condition | Result |
|---|---|
| full ctest run | **fail** |
| isolation, unloaded | **fail**, then 3/3 pass |
| isolation under 6-way contention | **pass** |

That is roughly 2 failures in 6 runs, and slower runs do *better*, which points at a
settle/ordering race rather than a deadline.

**Leading hypothesis (unconfirmed):** Test 3 calls `ctrl.testAileron()` immediately
after Test 2 ends with an external disarm + LOITER mode change, reusing the same
`ctrl` instance without an intervening settle. The very first sweep in the test
(lines 559-564) waits for `isArmed()` *before* waiting for `activeSurface() >= 0`;
Tests 2 and 3 (lines 582-584, 602-604) skip that and only wait for
`activeSurface()`, so they depend on arming having completed implicitly.

**Status:** open. This lives in `custom/tests/`, i.e. the fork's own surface rather
than stock upstream, so it should be fixed there rather than excluded — but it needs
the control-surface sweep lifecycle confirmed before the fix is trustworthy.
Do not add it to the CI exclusion list: that would violate the policy below.

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
