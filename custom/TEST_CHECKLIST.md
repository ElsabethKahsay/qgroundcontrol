# TEST_CHECKLIST.md — Skywin GCS (`custom/`)

**Use before real quad + payload.**
Mark each: **Pass / Fail / N/A** · severity if fail: **Blocker / Major / Minor**.

**Environment:** git hash ______ · SITL Copter / VTOL ______ · bench FC (props off) ______

---

## A. Build & hygiene

| ID | Check | Sev if fail |
|----|--------|-------------|
| A1 | Debug + Release build clean | Blocker |
| A2 | All unit tests under `custom` run (`CheckSmoke`, `ArmingGate*`, `TelemetryBridge`, `DatabaseManager`, `MavLinkIntercept`, `ArmingFlowIntegration`, `VehicleProfileManager`, `FlightSession`, `DatabaseMigration`, `ZoneCompliance`, …) | Blocker |
| A3 | Fresh DB migrates to latest schema; upgrade from prior schema OK | Blocker |
| A4 | Force-arm magics are **named constants** (21196 / 2989), not bare floats only | Major |
| A5 | No duplicate capacity/SOC/armed sources contradicting each other in UI | Major |

---

## B. Connect / telemetry (TelemetryBridge)

| ID | Steps | Expected | Maps to |
|----|--------|----------|---------|
| B1 | No vehicle | Disconnected UI; no 200ms work causing crash | R-08 |
| B2 | Connect SITL | Bridge populates battery/GPS/mode within few seconds | |
| B3 | Disconnect mid-poll | Clean teardown; checks don't keep "Pass" forever on dead link | R-03, R-06 |
| B4 | Reconnect | Re-bind vehicle; evaluation restarts | |
| B5 | `connectionQuality` ~10% | Checks don't flap Pending/eval every cycle | R-09 |
| B6 | Inspector vs bridge | SOC, current, voltage agree (allow EMA lag on current) | |
| B7 | Telemetry stall (bridge stops updating) | Checks transition to Stale within stalenessThresholdMs | R-03 |

---

## C. Preflight checks & ChecklistEngine

| ID | Steps | Expected |
|----|--------|----------|
| C1 | Connect → evaluation running | Check statuses move Pending → Pass/Fail |
| C2 | Fail a critical check (e.g. low voltage threshold) | Fail visible; count in PreflightManager |
| C3 | Manual confirm check | Stays Pending until confirm; no silent Pass |
| C4 | MotorSpinCheck never opened | Stays Pending (document); optional timeout = future work (R-05) |
| C5 | All mandatory pass | `ChecklistEngine.allPassed` / gate can open |
| C6 | Threshold from DB `check_config` | Changing config changes pass/fail without rebuild |
| C7 | BatteryVoltageCheck with known cell count param | Uses param_BAT_CELL_COUNT, not estimate | R-04 |
| C8 | BatteryVoltageCheck with no params | Falls back to estimate (voltage / 3.7) | R-04 |

---

## D. ArmingGate + MAVLink intercept (**safety — Blockers**)

| ID | Steps | Expected | Risk |
|----|--------|----------|------|
| D1 | **Active** mode, critical fail, normal Arm | Command **blocked**; armingDenied | R-01 |
| D2 | **Passive** mode, critical fail, normal Arm | Allowed (log only) | |
| D3 | **Hybrid**, critical fail, Arm | Closed until override ACK with name/reason | |
| D4 | Override once, arm, then gate | One-shot; no permanent open (R-02) | R-02 |
| D5 | Force Arm (21196) from UI | Arms via `forceArm()`; **Vehicle.armed true** in QML | R-01 |
| D6 | Force Arm from MAVLink console | HEARTBEAT armed; toolbar not stuck if D5 path fixed | |
| D7 | Disarm | Disarms; gate/session end paths run | |
| D8 | COMMAND_ACK denied | UI surfaces rejection | |
| D9 | `interceptCommandLong` with non-arm command | Passes through without gate check | |
| D10 | `interceptCommandLong` with disarm (param1=-1) | Passes through regardless of gate | |
| D11 | Mode change Active→Passive mid-evaluation | Gate immediately allows | |
| D12 | ACK consumed → second arm attempt | Blocks again (ACK is one-time) | R-02 |

**Integration (gap):** telemetry → checks fail → gate closed → UI Arm blocked → Force Arm → armed → status Armed.

---

## E. MainStatusIndicator (known pain)

| ID | Steps | Expected |
|----|--------|----------|
| E1 | Debug raw `a/f/l` on **toolbar** label | Matches `Vehicle` |
| E2 | Disarmed + !canArm | Not Ready |
| E3 | Armed on ground | **Armed** (never Not Ready) |
| E4 | Flying | **Flying** |
| E5 | Comm lost | Communication Lost |
| E6 | Confirm custom QML is the toolbar source | Not only stock APM item |

---

## F. Live battery / VehicleProfileManager

| ID | Steps | Expected |
|----|--------|----------|
| F1 | Pack 1P×5000, reserve 20% | Full **5.00 Ah** |
| F2 | SOC 83%, I≈28 A | rem 4.15; usable 3.32; t≈7 min |
| F3 | Disarmed / I < gate | `SOC% · —` |
| F4 | Bat Config edit P/mAh | Detail + chip capacity update live |
| F5 | Stale >5s | Not "Live" |
| F6 | 6S label vs ~12.6 V | Optional warn; don't trust wrong cell math blindly (R-04 related) |
| F7 | Battery config defaults | 6S 1P 5000mAh LiPo 20% reserve | L3 |
| F8 | Capacity = parallel × cellMah / 1000 | 2P × 5000 = 10.0 Ah | L3 |

---

## G. Fly UI / quick actions

| ID | Check | Expected |
|----|--------|----------|
| G1 | Flight time | Same math as chip |
| G2 | FC status | Armed, mode, battery snapshot |
| G3 | Events | Hidden default; toggle works |
| G4 | Takeoff / Return | Clickable when allowed; not dead |
| G5 | Mode MANUAL↔AUTO | Works or clear FLTMODE warning |

---

## H. Hardware tests (SITL / props-off bench only)

| ID | Check | Expected |
|----|--------|----------|
| H1 | Motor panel safety dialog | Shows first test; document firstTestDone behavior (R-15) |
| H2 | Stop / cooldown | Motors stop; cooldown enforced |
| H3 | Control surface FW path | MANUAL gate; cleanup disarm |
| H4 | Panel close mid-test | Safe stop; no stuck PWM |

---

## I. DB / session / compliance

| ID | Steps | Expected | Risk |
|----|--------|----------|------|
| I1 | Gate open → session start row | flight_sessions row | |
| I2 | Gate close → end session + compliance log | Rows written | |
| I3 | Battery cycle increment | If implemented, +1 | R-12 |
| I4 | Override persistence | Survives restart if designed | |
| I5 | Migration vN→latest | No crash; columns present | |
| I6 | Multiple sessions for same vehicle | Unique IDs; no collision | L4 |
| I7 | Compliance log round-trip | JSON survives save/load | L4 |
| I8 | Check config round-trip | Key-value persists | L5 |
| I9 | Vehicle config JSON persistence | Blob survives save/load | L5 |

---

## J. State machine (R-07)

| ID | Steps | Expected |
|----|--------|----------|
| J1 | Happy path to ArmingAllowed | Forward transitions OK |
| J2 | After ArmingAllowed, force a critical fail | Document actual behavior (may stay forward-only — known debt) |
| J3 | Disconnect from mid-state | Reset toward Disconnected |

---

## K. Stability

| ID | Steps | Expected |
|----|--------|----------|
| K1 | Connect/disconnect ×20 | No crash |
| K2 | 30 min SITL soak | UI responsive; no unbounded growth |
| K3 | Telemetry storm (high EXTRA3) | No freeze |

---

## L. Unit tests to **add** before handover (from gaps)

Priority order:

1. ~~**Blocker:** `PreflightPlugin` MAVLink arm block + force-arm + ACK~~ **DONE** → `MavLinkInterceptTest.cpp`
2. ~~**Blocker:** Integration arm flow with MockTelemetryBridge + gate~~ **DONE** → `ArmingFlowIntegrationTest.cpp`
3. ~~**Major:** VehicleProfileManager SOC/time + capacity~~ **DONE** → `VehicleProfileManagerTest.cpp`
4. ~~**Major:** Session start/end + compliance log~~ **DONE** → `FlightSessionTest.cpp`
5. ~~**Major:** DB migration~~ **DONE** → `DatabaseMigrationTest.cpp`
6. ~~**Medium:** No-fly / zone compliance if in tree~~ **DONE** → `ZoneComplianceTest.cpp`
7. **Medium:** QML smoke (ArmGateDialog enable rules) if you have QML test harness

---

## M. Pre–real-quad gate

**Do not fly payload quad until:**

- [ ] All **D\*** and **E3** Pass
- [ ] **F1–F5** Pass on SITL
- [ ] **G4** Pass
- [ ] Bench FC props-off: Force Arm → Armed status → Disarm
- [ ] No reliance on "arm only from console" for ops
- [ ] Battery monitor calibration plan for real pack

---

## N. Session log (copy per run)

```text
Hash:
SITL/bench:
Blockers:
D-arm path (UI/force/console):
Status a/f after arm:
Battery sample (SOC, I, cap, t_ui, t_hand):
Sign-off real quad: Y/N
```

---

### Against Phase 1 — coverage map

| Analysis gap | Checklist section |
|--------------|-------------------|
| No full arming integration test | D + L1–L2 |
| No mavlinkMessage tests | D5–D8, L1 |
| Status stuck Not Ready | E |
| Force-arm / override risks R-01/R-02 | D |
| Stale telemetry R-03/R-06 | B3, B7, F5 |
| Session lifecycle | I |
| Motor QML safety R-15 | H1 |
| FSM one-way R-07 | J |
| Battery cell estimation R-04 | C7, C8, F6 |
| DB schema not verified | I5, L5 |
| Zone compliance untested | L6 |
| VehicleProfileManager untested | F7, F8, L3 |
| Config cache invalidation | C6 |

---

### Test files created (Phase 2)

| File | Section | Risk(s) |
|------|---------|---------|
| `tests/MavLinkInterceptTest.cpp` | L1 | R-01, R-02 |
| `tests/ArmingFlowIntegrationTest.cpp` | L2 | R-01, R-02, R-03, R-06 |
| `tests/VehicleProfileManagerTest.cpp` | L3 | R-04, R-12 |
| `tests/FlightSessionTest.cpp` | L4 | R-12 |
| `tests/DatabaseMigrationTest.cpp` | L5 | R-10 |
| `tests/ZoneComplianceTest.cpp` | L6 | — |

**Next for the agent:** implement remaining **L7** (QML smoke tests) if QML test harness is available, then you execute **D + E + F** on SITL until Blockers are zero. Phase 1 architecture notes stay valid; Phase 2 is what you tick before a real quad.
