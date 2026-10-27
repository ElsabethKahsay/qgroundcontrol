# Power Types and Mission Check

Skywin GCS · copter only · designed 26 Oct 2026 with the operator · **all 6 steps built; copters' in-flight charge counted by the GCS (section 9)**

Status: step 1 (the maths) and step 2 (storage) built and tested 26 Oct, step 3 (flight recorder) and step 4 (checklist items and arming) 27 Oct; step 5 (the Power types page and the Fly view) 27 Oct; step 6 (SITL check) 27 Oct. Decided 27 Oct: on copters the GCS counts the in-flight charge itself (section 9). Each step starts on the operator's go (see "Implementation plan"). Planes keep the existing rough flight-time estimate.

Method (decided 26 Oct): **combined**. The flight controller estimates current from throttle (`BATT_MONITOR = 25`, synthetic current), the charger provides the real total after each flight, and the GCS learns a hover/cruise/payload model from both.

---

## 1. Goal

Answer two questions, before take-off and live in flight:

1. **Can this copter carry X kg out to the target, stay there N minutes, drop the payload, and get back home with the landing reserve intact?**
2. **How long and how far can it still fly right now?**

If the round trip doesn't fit, the GCS refuses to arm; the pilot can waive this, and the waiver is logged. In flight it says "RETURN NOW".

---

## 2. Definitions

| Term | Meaning |
|---|---|
| **Power type** | One way of building a copter's power system, shared by every aircraft built that way. It is identified by **frame + propeller diameter × pitch (inches) + battery cells (S)**, e.g. "Quad X · 15×5.5 · 6S". The motor is a free-text note, not part of the identity. Different props or a different cell count make a different power type. |
| **Link** | In Vehicle Management, each aircraft (by its hardware ID) is linked to one power type. Several aircraft can share a type and its learned numbers. |
| **Hover / cruise** | The flight phase the GCS assigns to each second. Ground speed below 3 m/s is hover; take-off, landing and time at the target count as hover. Anything faster is cruise. |
| **Payload** | What the copter carries on this flight, not its own weight, entered in the preflight checklist. It is dropped at the target: it counts as aboard from take-off until the copter first comes within 20 m of the target. |
| **Time at target** | Minutes the copter stays at the target, entered by the pilot each flight next to the payload. |
| **Landing reserve** | The share of the pack the plan never uses. Adjustable, default 20 % (the existing per-vehicle battery reserve). |
| **Usable charge** | Pack capacity × (charge left − reserve). A full 6S 16 Ah pack with a 20 % reserve gives 12.8 Ah. |
| **Synthetic current** | ArduPilot's current estimate when `BATT_MONITOR = 25`. It is roughly a straight line in throttle: `BATT_AMP_PERVLT` (current at full throttle) × throttle + `BATT_AMP_OFFSET` (idle current), adjusted for voltage. The flight controller adds it up into **mAh used** and sends both over telemetry. |
| **Charger number** | The mAh the charger puts back into the pack after the flight, read off the charger screen. The pack starts the flight full, so this equals the charge the flight really used. |
| **Charger factor** | Charger number ÷ the FC's synthetic mAh for the same flight, averaged over the accepted flights. It corrects the synthetic total until `BATT_AMP_PERVLT` is set right. |
| **Model** | Four numbers per power type, learned from flights: hover current at 0 kg, hover extra per kg, cruise current at 0 kg, cruise extra per kg. |
| **Validated** | A power type with at least 3 accepted flights, where the model predicts each of the last 3 charger numbers within ±10 %. Only a validated type can block arming. |

---

## 3. The combined method: what each source gives

**Why three sources.** None of them is good enough alone:
- **No current sensor** is fitted (working assumption, 26 Oct). So the flight controller has no measured current.
- **The synthetic current** (`BATT_MONITOR = 25`) gives a live current and mAh used every second. ArduPilot's own documentation says it is "inaccurate and not recommended" for multicopters: it treats current as a straight line in throttle, so its total can be off, and so can its split between hover and cruise.
- **The charger number** is the real total for each flight. ArduPilot calibrates its synthetic monitor the same way: it compares "mah returned to batt" with the estimate.
- **The GCS model** turns the charger numbers into hover and cruise currents, plus the payload effect, which is what the mission check needs.

**Where each piece of data comes from.** Speed, voltage, throttle, position and the synthetic current all arrive **live over telemetry** during the flight. This is the same data that ends up in the Mission Planner log, so nobody needs to open a log afterwards. The copters carry a telemetry radio that stays connected during flights (operator, 26 Oct). The only number the operator types is the **charger number**, once per flight.

| Data | Comes from | Used for |
|---|---|---|
| Synthetic current and mAh used | Live telemetry (`BATT_MONITOR = 25`) | Charge used in flight (× charger factor); live current for the in-flight adjustment |
| Ground speed | Live telemetry | Hover (< 3 m/s) or cruise for each second; average cruise speed (time → distance); distance left in flight |
| Voltage | Live telemetry | Pack full at arming (≥ 4.15 V per cell); cell-count check (e.g. 6S vs 4S); "voltage lower than expected" warning |
| Throttle | Live telemetry | Fallback in-flight adjustment when no synthetic current is received |
| Position | Live telemetry | Distance to the target; payload counted as dropped within 20 m of the target |
| Charger number | Typed on the post-flight page | The real total: learning the model, the charger factor, the suggested `BATT_AMP_PERVLT` |

**What the GCS uses for charge used in flight, in order:**

| Copter setting | Charge used in flight | Charger number |
|---|---|---|
| `BATT_MONITOR = 25` (the decided setup) | FC mAh × charger factor | Required every flight |
| Real current sensor later (`BATT_MONITOR = 4`, or ESC current telemetry) | FC mAh as reported | Optional (a cross-check) |
| Neither (e.g. `BATT_MONITOR = 3`) | The GCS's own running sum: model current × (throttle ÷ phase reference throttle)^1.5, limited to ±30 % | Required every flight |

The GCS reads `BATT_MONITOR` from the copter. It labels the current "synthetic" when it is 25, and never treats a synthetic value as a real sensor.

**Calibrating `BATT_AMP_PERVLT`.** After each flight the GCS compares the FC's mAh with the charger number and shows the corrected value, using ArduPilot's formula: new value = old value × (charger mAh ÷ FC mAh). The operator decides whether to set it on the copter; **the GCS never writes parameters to the aircraft.** Until the operator sets it, the GCS applies the charger factor itself, so the estimate is right either way.

---

## 4. Decisions (operator, 26 Oct)

| Question | Decision |
|---|---|
| Current source | Combined: `BATT_MONITOR = 25` synthetic current live, charger number after each flight, GCS model learned from both |
| Telemetry | Every copter carries a telemetry link to the GCS that stays up during flight |
| Power type identity | Frame (read from FRAME_CLASS/FRAME_TYPE) + prop diameter × pitch + cells; motor is a note only |
| Payload at target | Dropped: the return leg is calculated at 0 kg |
| Time at target | Entered by the pilot each flight |
| Take-off / landing | Adjustable, default 1 min each, counted as hover |
| Landing reserve | Adjustable, default 20 % |
| Round trip doesn't fit | Arming blocked unless waived (pilot name, logged in compliance_logs). It only blocks once the type is validated; before that it shows an amber warning |
| "Power type: props fitted match?" checklist item | Required, with a logged waiver |
| No target saved | Check skipped; it shows the max out-and-back distance and does not block |
| Learning | Suggest only: a flight joins the model when the pilot clicks Use |
| Hand-calculated throttle values | Not used for now |

---

## 5. How it works

### 5.1 Once per copter
1. Set `BATT_MONITOR = 25` and a starting `BATT_AMP_PERVLT` on the copter (operator, with QGC's parameter editor or Mission Planner). The starting value only needs to be roughly right, because the charger corrects it.
2. In Vehicle Management, create the power type: the frame is read from the copter, and you enter the props and cells.
3. Link each copter built that way to it.

### 5.2 Every flight: what the GCS records by itself
- Hover minutes and cruise minutes, each split into "with payload" and "without payload".
- The FC's synthetic mAh used, and its average synthetic current per phase.
- The average cruise ground speed, and the average throttle per phase.
- The cell voltage at arming, the pack, payload, power type and time at target.
- How much of the flight had telemetry. A flight with more than 10 % of its time missing is not used for learning.
- Whether the pack was **full at arming** (at least 4.15 V per cell at rest). Only those flights count for learning; otherwise the charger number would include charge used before the flight.

### 5.3 Every flight: what the pilot does
1. Charge **that pack straight after landing**.
2. Read the mAh on the charger and type it on the post-flight page.
3. The page then shows the comparison: "FC said 4,250 mAh, charger 5,100 mAh → suggested BATT_AMP_PERVLT 144 (now 120)".

### 5.4 Learning the model
Each accepted flight gives one equation:

```
charger Ah = Σ over its minutes (phase current at the payload aboard) × time
```

The GCS solves them by least squares in two stages: the 0 kg currents from the payload-free flights, then the extra per kg from what the payload flights used beyond that.

- **First estimate:** needs 2 flights without payload with different hover/cruise mixes (one mostly cruise, one mostly hover), plus 1 flight with payload. With only one payload flight, the payload is assumed to add the same percentage in both phases. A 4th flight (payload, mostly hover) removes that assumption.
- **When the flights are too alike** to separate hover from cruise, the GCS says which flight to fly next, e.g. "fly one mostly-hover flight".
- **The synthetic current is a starting hint.** Its per-phase averages, × the charger factor, seed the model before there are enough flights. They are labelled "from synthetic current, not yet learned".
- **A flight that misses the model by more than 20 %** is flagged (wrong pack charged? payload heavier than entered?). It is used only if the pilot accepts it.
- **Suggest only:** new flights show **[Use] [Dismiss]**. Nothing changes without a click.

### 5.5 Before take-off: the mission check
The trip is split into legs:
- take-off
- cruise out with payload
- time at the target with payload
- cruise back without payload
- landing

Each leg costs model current × time. The total is compared with the usable charge, and the result shows in the checklist, the battery chip and the Fly-view info box.

### 5.6 In flight
- **Charge used:** FC synthetic mAh × charger factor (see the table in section 3 for the other setups).
- **Live adjustment:** the model current for the current phase is scaled by (live synthetic current × charger factor ÷ model current). This ratio is averaged over 30 s and limited to ±30 %, so a headwind or extra drag shows up.
- **Every second**, the rest of the trip is recomputed against what is left above the reserve: the rest of the way out, time at target, the way back and landing. If it no longer fits, the GCS shows **"RETURN NOW — x Ah short"**.
- **Voltage cross-check:** if the cell voltage is lower than expected for the charge used, the GCS warns "voltage lower than expected" and uses the worse of the two.
- **Telemetry lost:** the estimate freezes and shows "no data". The RC failsafe and the flight controller's own battery failsafe are unaffected.

### 5.7 Checks when the copter connects
- `BATT_MONITOR` is not 25 (or 4) → warning: "no current estimate on the copter; GCS fallback in use".
- The flight controller's frame differs from the power type → warning.
- Battery voltage outside 3.3–4.35 V × cells → warning, e.g. "looks like 4S, type is 6S".
- Props can't be checked, which is why the "Power type: props fitted match?" checklist item exists.

---

## 6. Worked example

SKY-03, power type "Quad X · 15×5.5 · 6S", 9 kg empty, `BATT_MONITOR = 25`, `BATT_AMP_PERVLT = 120` to start.

**Learning flights** (charger number typed after each; the FC's synthetic mAh is recorded automatically):

| Flight | Payload | Hover | Cruise | FC synthetic | Charger |
|---|---|---|---|---|---|
| 1 | 0 kg | 3 min | 10 min | 4,250 mAh | 5,100 mAh |
| 2 | 0 kg | 8 min | 4 min | 4,722 mAh | 5,667 mAh |
| 3 | 1.5 kg (kept aboard) | 3 min | 10 min | 5,458 mAh | 6,550 mAh |
| 4 | 1.5 kg (kept aboard) | 8 min | 4 min | 6,056 mAh | 7,267 mAh |

**Calibration:**
- Charger factor = 5,100 ÷ 4,250 = **1.20**: the synthetic current reads about 17 % low.
- After flight 1 the post-flight page shows "suggested BATT_AMP_PERVLT = 120 × 5100 ÷ 4250 = 144".
- If the operator sets 144, the FC's own mAh comes out close to the charger number on the next flights, and the factor drops towards 1.0.

**Model (from the charger numbers):**
- Flights 1 + 2 → **hover 32 A, cruise 21 A**.
  - Check: 32×3/60 + 21×10/60 = 5.10 Ah; 32×8/60 + 21×4/60 = 5.67 Ah.
  - With only flights 1–3, the payload is assumed to add the same percentage in both phases (+19 % per kg), giving +6.07 A/kg hover and +3.98 A/kg cruise.
- Flights 3 + 4 → **+6 A per kg hover, +4 A per kg cruise**.

**Mission:** 6S 16 Ah pack with a 20 % reserve, so 12.8 Ah usable. Payload 0.8 kg, target 5 km away, 2 min at the target, cruise at 15 m/s.

| Leg | Payload | Current | Time | Charge |
|---|---|---|---|---|
| Take-off & climb | 0.8 kg | 32 + 6×0.8 = 36.8 A | 1 min | 0.61 Ah |
| Cruise out 5 km | 0.8 kg | 21 + 4×0.8 = 24.2 A | 5.6 min | 2.24 Ah |
| At target | 0.8 kg | 36.8 A | 2 min | 1.23 Ah |
| Cruise back 5 km | 0 kg | 21.0 A | 5.6 min | 1.94 Ah |
| Land | 0 kg | 32.0 A | 1 min | 0.53 Ah |
| **Total** | | | **15.1 min** | **6.56 Ah** |

**Result:** "Tgt 5.0 km · 0.8 kg dropped · 2 min → OK, 6.2 Ah spare".

**Maximum target distance:**
- The fixed legs use 2.37 Ah, leaving 10.43 Ah.
- Each km of target distance costs 0.45 Ah out + 0.39 Ah back = 0.84 Ah.
- 10.43 ÷ 0.84 = **12.46 km** (shown as 12.4 km).

**Headwind** of 5 m/s on the way out (10 m/s out, 20 m/s back): cruise costs 4.82 Ah instead of 4.18 Ah, for a total of **7.19 Ah, still OK**.

**In flight, 10 minutes in:**
- The FC reports 4,330 mAh used. × 1.20 = 5.2 Ah, so 16 − 5.2 − 3.2 reserve = **7.6 Ah left**.
- The live synthetic current is 23.3 A × 1.20 = 28.0 A, against a model cruise current of 24.2 A. The ratio is 1.16 (within the ±30 % limit), so the rest of the trip is costed at 16 % above the model.
- If the rest of the trip needs more than 7.6 Ah → **RETURN NOW**.

---

## 7. Implementation plan

The operator approves each step before the next one starts.

| Step | What | Time |
|---|---|---|
| 1. Maths **(done 26 Oct)** | Header-only `src/core/PowerTypeModel.h` (pure maths, no Qt object): least-squares fit (2–4 unknowns, too-alike detection, outliers, validation rule), charger factor and suggested `BATT_AMP_PERVLT`, leg sum, max distance, in-flight sum (FC mAh × factor, live-ratio scaling, throttle fallback), cell-count check. Tests reproduce every number in section 6 | ½ day |
| 2. Storage **(done 26 Oct)** | Next database version step: tables `power_types` and `vehicle_power_type`. New flight_sessions columns: power type, hover/cruise minutes with/without payload, cruise speed, FC synthetic mAh, synthetic current per phase, reference throttles, cell volts at arming, telemetry coverage, time at target, charger mAh, learning state. Q_INVOKABLEs on VehicleProfileManager (count the recompiles first; ask if 300 or more) | ½ day |
| 3. Flight recorder **(done 27 Oct)** | Per-second hover/cruise and payload-aboard accounting; FC mAh and synthetic current per phase; telemetry coverage; full-pack test at arming; `BATT_MONITOR` / `BATT_AMP_PERVLT` read on connect | ½ day |
| 4. Checklist and arming **(done 27 Oct)** | `PowerTypeCheck.h` and `MissionCheck.h` (header-only, no Q_OBJECT; the mission check blocks when validated, amber otherwise); `BATT_MONITOR` warning; time-at-target field in the payload dialog; one arming-gate rule + waiver. Unit tests + LivePluginBenchTest | 1 day |
| 5. Screens **(done 27 Oct)** | Vehicle Management: power types, link, the type's flights (charger status, Use/Dismiss, "fly next" advice, Validated badge), built as inline QML in the existing page. Post-flight page: charger mAh entry, FC vs charger comparison and suggested `BATT_AMP_PERVLT`. Fly view: battery chip, info box, RETURN NOW | 1 day |
| 6. Final check **(done 27 Oct)** | SITL flight with `BATT_MONITOR = 25` (this proves the logic only, since SITL's current is simulated); full test suite; report | ½–1 day |

**Total: about 4½–5 days.**

**Step 1 result (26 Oct):**
- `src/core/PowerTypeModel.h` is pure maths, header-only, with no CMake change.
- Six tests in `BatteryEstimatorTest` (`testPowerType…`) reproduce every number in section 6. They also cover the too-alike advice, the ±30 % limit, the validation and outlier rules, and the recorder helpers. BatteryEstimatorTest passes 20/20.
- Seven mutants were caught:
  - return leg costed with the payload aboard;
  - live correction not limited;
  - validation at 20 % instead of 10 %;
  - charger factor inverted;
  - proportional payload ignoring cruise;
  - 3 m/s counted as hover;
  - full-pack check without tolerance.
- One bug found and fixed while testing: 4.15 V × 6 is 24.900000000000002 in floating point, so a pack at exactly 24.9 V counted as not full. The check is now per cell, with a tiny tolerance.
- Nothing is wired into the app yet; steps 2–5 do that.

**Step 2 result (26 Oct):**
- **Database version 25** (`DatabaseManager.cpp`) adds:
  - table `power_types`: frame, prop diameter, prop pitch, cells, motor note; the build columns are UNIQUE;
  - table `vehicle_power_type`: device UID → type;
  - 17 columns on `flight_sessions`: power type, hover/cruise minutes with and without payload, cruise speed, FC mAh, synthetic current per phase, reference throttles, cell volts at arming, full-at-arming flag, telemetry coverage, time at target, charger mAh, learning state.
- **Learning states:**
  - The recorder marks a flight `pending`, or `excluded` when it did not start on a full pack or had under 90 % telemetry.
  - The pilot sets `used` or `dismissed`. `used` needs a charger number, and an excluded flight can never be used.
  - A later recorder write never undoes the pilot's choice.
- **Storage API** (`DatabaseManager`): `savePowerType`, `listPowerTypes`, `powerType`, `deletePowerType`, `linkVehiclePowerType`, `vehiclePowerTypeId`, `saveFlightPowerRecord`, `setFlightChargerMah`, `setFlightLearningState`, `powerTypeFlights`.
  - A duplicate build is refused.
  - A type cannot be deleted while it is linked or has flights; the database's foreign key refuses it too.
- **For the screens** (`VehicleProfileManager`):
  - properties `currentPowerTypeId` and `currentPowerTypeName`, reloaded on connect and cleared on disconnect;
  - `listPowerTypes`, `savePowerType`, `deletePowerType` and `linkCurrentVehiclePowerType`;
  - `powerTypeSummary`: the fitted model from the `used` flights, or a starting model from the FC's synthetic current, marked "synthetic", until there are enough; plus validated, charger factor, used and pending counts;
  - `powerTypeFlights`, with the predicted mAh and the outlier flag;
  - `setFlightChargerMah` and `setFlightLearningState`.
- **Build:** 42 incremental steps. 16 objects include `VehicleProfileManager.h` and 39 include `DatabaseManager.h`, well under the 300 limit.
- **Tests:**
  - DatabaseManagerTest: `testPowerTypeRegistry`, `testPowerTypeFlightRecords`, and `testPowerTypeUpgradeFromV24` (a v24 file gains the tables and columns when opened). 41/41 pass.
  - BatteryEstimatorTest: `testPowerTypeSummaryFromStoredFlights` (the worked example's four flights stored, then fitted back to 32 + 6 A/kg and 21 + 4 A/kg, factor 1.20, validated). 21/21 pass.
- **Full suite:** 34 classes, 440 passed, 0 failed, 7 skipped (the known ones), written to `tests/baseline-step3.txt`.
- **Mutants:** 7 of 8 caught:
  - an edited type refused against itself;
  - an under-filled pack not excluded;
  - the recorder overwriting the pilot's choice;
  - `used` without a charger number;
  - v25 adding no columns;
  - fitting every flight instead of `used` only;
  - no synthetic seed.
- **Survivor:** "a linked type can be deleted". Removing the explicit check changes nothing, because the foreign key on `vehicle_power_type` still refuses the delete.
- **Not pinned by a test:** `currentPowerTypeId` reloading on connect and clearing on disconnect, because that needs a live vehicle. Step 3's live test now covers it.

**Step 3 result (27 Oct):**
- **`PowerTypeModel::FlightTracker`** (header-only) takes one tick per second of the flight:
  - **Airborne:** above 1 m or at cruise speed. Time armed on the ground draws idle current, so it is left out.
  - **Hover or cruise:** below or above 3 m/s.
  - **Loaded or empty:** the payload counts as aboard until the copter first comes within 20 m of the target. With no target it stays aboard.
  - **Averages:** FC current, throttle and cruise speed are time-weighted per phase; a phase with no data averages to -1.
  - **Telemetry coverage:** seconds with telemetry ÷ all seconds.
- **`SessionRecorder`:**
  - A 1 s timer feeds the tracker while a non-bench flight of a linked aircraft is open.
  - The power record (`saveFlightPowerRecord`) is written with every 10 s summary and at close: minutes, FC mAh used, current and throttle per phase, cell volts at arm, full-at-arm, coverage, time at target.
  - A flight joined already armed never counts as started full. It started before the GCS saw it, so its charger number would cover unseen time.
  - The sample has five new fields: ground speed, FC current, throttle, latitude and longitude.
  - The flight start has five new fields: power type, cells, time at target, and the target's latitude and longitude.
- **`PreflightPlugin`:**
  - It fills the new sample fields from TelemetryBridge, with throttle from QGC's `VFR_HUD` throttle %.
  - At arming it passes the aircraft's power type, its cell count and the saved target.
  - Time at target stays -1 until step 4 adds the field to the payload dialog.
- **Moved to step 4:** reading `BATT_MONITOR` / `BATT_AMP_PERVLT`. It belongs with the connect-time warning check, which receives parameter updates.
- **Tests (all pass):**
  - `BatteryEstimatorTest::testPowerTypeTrackerWithoutTarget`. BatteryEstimatorTest 22/22.
  - `DatabaseManagerTest::testRecorderPowerTypeFlight` flies a whole mission through the recorder: ground time, take-off, cruise out, drop at the target, a link loss, cruise back, then a bench arm and a joined flight. DatabaseManagerTest 43/43.
  - `LivePluginBenchTest::testPowerTypeFlightRecordedLive` uses the real plugin against QGC's mock vehicle, and reruns safely on the persistent test database. LivePluginBenchTest 8/8. It checks that:
    - the type is cleared on disconnect;
    - another aircraft gets its own type from the database;
    - an RC-armed flight is recorded with the type and with the 1 s tracker's coverage.
- **Full suite:** 34 classes, 444 passed, 0 failed, 7 skipped (the known ones).
- **Mutants: all 8 caught:**
  - payload never dropped;
  - ground time counted as hover;
  - link-lost seconds counted as seen;
  - bench arms tracked;
  - joined flight counted as started full;
  - record not written at close;
  - plugin never passing the power type;
  - 1 s timer never started.
- **The first run of the live test had a gap, now fixed:** it found an earlier run's flight in the test database, which let "plugin never passing the power type" through. It now counts only the flight it creates.
- **Known limits:**
  - Before the first BATTERY_STATUS, TelemetryBridge reports 0 A rather than "unknown", so a flight with no current data averages 0 A. The synthetic-current starting model ignores averages of 0.
  - Ground idle current is not modelled, so the hover current comes out slightly high, which is the safe direction.

**Step 4 result (27 Oct):**
- **"Power type" item** (`src/core/PowerTypeCheck.h`, Power section; mandatory Action item; copters only, Skipped on other airframes):
  - **Pending** until the copter is linked to a type and the pilot confirms "props fitted match" for this flight.
  - **Warning** (does not block) when the type is waived, or when the flight controller disagrees:
    - its frame (FRAME_CLASS) is not the type's frame;
    - the battery voltage does not fit the type's cell count;
    - `BATT_MONITOR` is 0 or 3, i.e. no current is reported.
  - A generic confirm from a checklist button is undone.
- **"Mission check" item** (`src/core/MissionCheck.h`, Power section; mandatory Action item; copters only):
  - **What it costs:** the trip to the saved target with `VehicleProfileManager::missionCheck()`:
    - the type's fitted model;
    - cruise speed from its used flights, or 10 m/s, stated as assumed;
    - usable charge from the pack config and live charge;
    - take-off and landing minutes from the settings (`PowerTypes/takeoffMin`, `landMin`; default 1).
  - **Failed (blocks arming)** only when the trip is short and the type is validated; a logged waiver turns that into a Warning.
  - **Warning** when the trip is short and the type is not yet validated, or when the model is not usable yet.
  - **Pending** for a validated type until the payload is entered and the GPS position is known, so the check cannot be dodged.
  - **No target:** Passed, with the furthest out-and-back distance.
  - **No power type or no pack:** Skipped.
- **Per-flight state** (`VehicleProfileManager`):
  - props confirmed, power-type waiver and mission waiver; both waivers go to compliance_logs (`power_type_waived` / `mission_check_waived`);
  - time at target (0–600 min, with the payload entry).
  - Cleared by the disarm that ends a non-bench flight and by a vehicle change. Linking another type drops the props confirmation.
  - `currentFrameName()` maps FRAME_CLASS/FRAME_TYPE to "Quad X" etc.
- **Dialogs** (inline components in `ChecklistEngine.qml`, opened from both checklist pages):
  - The payload dialog gains "Time at target (min)".
  - The Power Type dialog: link a type, confirm "Props fitted match", or waive.
  - The Mission Check dialog: the legs as a table, the result, and a waiver when short.
- **Arming:** no new gate code. Both items are mandatory, so the existing rule refuses while either is Pending or the mission check is Failed.
- **Also:**
  - `BATT_AMP_PERVLT` is added to the parameter watchlist, for step 5's suggestion.
  - The 5 s timer and the input signals re-evaluate both items.
- **Found and fixed while testing:** a `Repeater` inside the Mission Check dialog crashed both checklist pages on load (ChecklistPagesSmokeTest, SIGSEGV in Qt's QML object creator). The legs are now one rich-text table.
- **Tests:**
  - ArmingGateTest: `testPowerTypeItemNeedsConfirmedProps`, `testMissionCheckBlocksOnlyWhenValidated` and `testPowerTypeWaiversAndTimeAtTarget`, including the gate's failing list. 21/21 pass.
  - LivePluginBenchTest (real plugin, mock copter): 10/10 pass.
    - `testPowerTypeAndMissionItemsLive`: Pending → linked → confirmed; relinking drops the confirmation; a 60 km target fails and holds the gate; the waiver releases it; an RC flight's disarm clears the confirmation.
    - `testPowerDialogsDriveTheItems`: the real dialogs are instantiated and driven. The payload and time are saved, the button confirms, and the mission dialog shows the costed trip.
- **Full suite:** 34 classes, 449 passed, 0 failed, 7 skipped (the known ones).
- **Mutants: all 10 caught:**
  - props never required;
  - a generic confirm passing the item;
  - a short validated trip only warning;
  - the payload dodge;
  - the mission waiver ignored;
  - the confirmation surviving a flight;
  - waivers logged under the wrong event;
  - relinking keeping the confirmation;
  - the payload dialog dropping the time;
  - the button confirming nothing.
- **Not pinned by a test:** the frame, cell-count and `BATT_MONITOR` warnings. They are built in the plugin's provider from live parameters the mock does not let the test choose. Usable in the app only once step 5 adds the screen to create power types; until then the Power type item can only be waived.

**Step 5a result (27 Oct): the Power types page**
- **Where:** Analyze → Vehicles, as a second tab, "Power types". `VehicleManagement.qml` is never opened in the app (its only entry is `Config.navigatePush`, which goes nowhere), so the page is the one the app shows. It is a tab in the existing file, with no new QML file and so no qrc or CMake change.
- **Left pane:**
  - the types, with linked aircraft and flight counts, and New;
  - the connected copter's type;
  - the fleet's take-off and landing minutes for the mission check (0–10, default 1, stored in settings).
- **Right pane:**
  - **Editor:** frame (a new type is prefilled from the copter's FRAME_CLASS/FRAME_TYPE), prop diameter and pitch, cells, motor note; Save, Link connected copter, and Delete (only while unused).
  - **What the type has learned:** the status, hover and cruise currents with the per-kg extras, Validated, the starting model from the synthetic current, used and waiting counts, the charger ÷ FC factor, and the "fly next" advice.
  - **The `BATT_AMP_PERVLT` suggestion** for the connected copter, from its latest flight with both numbers. The pilot sets it; the GCS never writes it.
  - **The type's flights, newest first.** Each has its numbers, a charger mAh field with Save, Use (only once a charger number is saved), Dismiss, the model's prediction and an outlier warning. Excluded flights say why.
- **Charger entry is on this page, not the post-flight page,** which is still hidden (audit item 16).
- **`VehicleProfileManager` additions:** `takeoffMin` / `landMin` with their setters (`missionCheck()` now uses them), and `ampPerVoltSuggestion()`.
- **Found and fixed by the test:**
  - The fleet Save reloaded the page after saving take-off, which refilled the landing field with the old value before it was read. Both fields are now read first.
  - The page title read an undefined name for a moment after selecting a type, which gave a QML warning.
- **Test:** `LivePluginBenchTest::testPowerTypesPageDrivesStorage` loads the real page from the resources on the mock copter and drives it, checking each step in the database:
  - New → fill → Save creates the type;
  - Link links the copter;
  - a recorded flight shows a charger field;
  - Use is disabled until a charger number is saved, then sets "used";
  - the fleet minutes are saved.
  LivePluginBenchTest 11/11; QmlShadowTest 7/7 (no colour literals).
- **Mutants: 5 of 5 caught:**
  - Save reading the landing field after the reload;
  - Use enabled before a charger number;
  - charger Save storing nothing;
  - Link linking nothing;
  - the minutes not stored.
  Two of them first slipped through because the settings file kept values from an earlier aborted run. The test now picks values that differ from whatever is stored.
- **Not pinned by a test:** the `BATT_AMP_PERVLT` line is only checked when the mock copter reports that parameter.

**Step 5b result (27 Oct): the Fly view**
- **`PowerTypeModel::liveEstimate()`** (pure). Its inputs:
  - the type's model and charger factor;
  - the pack and reserve, and the FC's mAh used (its % as fallback);
  - the smoothed FC current;
  - ground speed, altitude and position;
  - home and the target;
  - whether the payload is still aboard, and the time at the target;
  - the type's cruise speed and the landing minutes.

  It returns:
  - the charge left above the reserve;
  - the expected current for the phase the copter is in (the model × the live ratio, within ±30 %, only while flying);
  - time and distance left;
  - hover/cruise endurance and cruise range for before the arm;
  - the charge the rest of the trip needs. With the payload aboard that is the way to the target, the time there, the way home empty, and landing; after the drop or with no target, it is the way home.
  - **RETURN NOW**, which is only set while airborne.
- **`VehicleProfileManager.powerLive`** (map, refreshed with the existing 2 s live estimate):
  - Active for a copter linked to a type with a usable model, with neither the type nor the pack waived.
  - The model is re-fitted only when the type's flights change.
  - The payload counts as aboard from the arm until the copter first comes within 20 m of the saved target.
  - Cleared on disconnect.
- **Battery chip:**
  - before the arm, the cruise endurance with the payload entered;
  - in flight, the time left;
  - "RETURN NOW" in red;
  - the detail popover adds Power type, Left (min / km) and Rest of the trip.
- **Info box:** a "Left" row (min · km) and a "Trip" row, in both layouts. The Trip row shows:
  - "+x Ah" spare, in green;
  - "short x Ah" in amber, on the ground;
  - "RETURN NOW" in red, airborne.
- **App message:** once when RETURN NOW comes on; again only after it has cleared.
- **Tests:**
  - `BatteryEstimatorTest::testPowerTypeLiveEstimate`, the worked example in flight. 16 Ah, 20 % reserve, FC 4,333 mAh × 1.20 gives 7.6 Ah left. 23.3 A × 1.2 against a modelled 24.2 A is a ratio of 1.155, so 16.3 min and 12.7 km at 13 m/s. Half way to a 5 km target the trip fits; to a 20 km target it is short by 12.5 Ah, so RETURN NOW. Also covered:
    - on the ground: no RETURN NOW and no live ratio;
    - after the drop: home costed empty, the ratio limited to 1.3;
    - before the arm: the endurance;
    - % fallback; no model.
  - `LivePluginBenchTest::testPowerLiveInFlyView`, on the mock copter with the real info box and chip loaded from the source tree:
    - the estimate comes on;
    - a 60 km target shows "short", not RETURN NOW, on the ground and armed alike.
  - The Left and Trip rows are added to the info-box source scan.
  - `_validatedPowerType()` is now a shared helper for the live tests.
  - BatteryEstimatorTest 23/23, LivePluginBenchTest 12/12, FlyQuickActionsSmokeTest 11/11, QmlShadowTest 7/7.
- **Not pinned by a test:**
  - the drop at the target and RETURN NOW in the app (the mock copter cannot fly; the pure test covers both);
  - the app message;
  - the estimate switching off when the pack or the type is waived.

  Step 6's SITL flight covers the in-flight parts.
- **Mutants: 5 of 5 caught:**
  - RETURN NOW on the ground too;
  - the dropped payload still counted in the current;
  - the charger factor ignored in the charge used;
  - the info box showing a spare when short;
  - the chip saying RETURN NOW whenever the trip is short.
- **Full suite (steps 5a + 5b):** 34 classes, 452 passed, 0 failed, 7 skipped (the known ones).

**Step 6 result (27 Oct): SITL check**
- **Scenario:** `tools/sitl/sc_power_type.py`, run log in `tools/sitl/runs/2026-10-27-power-type-arducopter.md`.
- **Setup:**
  - ArduCopter SITL (ArduPilot 4.6.0-beta1-6903, instance 5) with `BATT_MONITOR = 25`.
  - The GCS uses a throw-away data folder; the operator's SITL and database are not touched.
  - The GCS database was seeded the way the Power types page leaves it: a validated "Quad X · 13×4.5 · 3S" type linked to the copter, a target 300 m north, and a 250 mAh pack so the way home cannot fit.
- **The flight:** take off to 20 m, hover 30 s, fly to the target, hover 20 s, fly back, land.
- **Result: PASS.** Recorded on the flight:
  - hover and cruise split at the drop: loaded 0.63 + 0.48 min before the target, empty 0.75 + 0.62 min after;
  - cruise speed 20 m/s; FC mAh 13; throttle 0.36 hover / 0.39 cruise;
  - the pack full at arm (4.2 V per cell); coverage 100 %; waiting for the charger number.
- **RETURN NOW:** on 25 s after take-off ("trip needs 0.37 Ah, 0.20 Ah left"), with the app message; off after the landing. No QML errors from the power-type screens.
- **The GCS now logs** when the power-type estimate comes on or goes off (and why), and when RETURN NOW changes, for after-flight review.
- **Found and fixed: a pack-config bug.** The Fly view's battery estimator panel (`BatteryTimeEstimatorPanel.qml`, always loaded) filled its fields from the stored pack one at a time. Each field's change wrote all four fields back, including the ones not yet loaded, and it also wrote its defaults while being created. So whenever a vehicle connected, the stored pack (cells, mAh) was overwritten with 6S or 5000 mAh. It now writes nothing until it has loaded, and nothing while loading. Its minimum cell size is 250 mAh, as elsewhere.
  - Test: `LivePluginBenchTest::testEstimatorPanelKeepsTheStoredPack` (a stored 3S·2P·2200 mAh survives the panel opening and a later config change).
  - Mutants: writing while loading, and writing while being created, both caught.
- **Full suite after step 6:** 34 classes, 453 passed, 0 failed, 7 skipped (the known ones).
- **Finding: the synthetic current does not work on copters** (section 9).

## 9. The synthetic current on copters (decided 27 Oct)

The SITL flight read **0.30 A in hover and in cruise**, exactly `BATT_AMP_OFFSET`. ArduPilot computes it as:

```
current = (V / BATT_MAX_VOLT) × throttle² × 0.0001 × BATT_AMP_PERVLT + BATT_AMP_OFFSET
```

(`libraries/AP_BattMonitor/AP_BattMonitor_Synthetic_Current.cpp`). `throttle` comes from the `k_throttle` servo output, which a multicopter does not use: its motors are `k_motor1…N`. On ArduCopter that term is always 0, so `BATT_MONITOR = 25` reports a constant idle current. ArduPilot's documentation says only "inaccurate" for multicopters; in practice it does not follow throttle at all.

What still works without it:
- Learning hover and cruise current from the charger numbers (the core of the method).
- The mission check before take-off.
- The recorder's hover/cruise/payload split.
- The throttle per phase.

What it breaks:
- "Charge used" in flight. FC mAh × charger factor becomes elapsed time × the average current of past flights: no response to hover versus cruise, wind or payload.
- The live correction of the model current. It is limited to ±30 %, but it is meaningless.
- The `BATT_AMP_PERVLT` suggestion, which changes nothing on a copter.

Options (the operator decides):
1. **The GCS's own charge count** (the plan's "no current sensor" path). The GCS integrates model current × (throttle ÷ the phase's reference throttle)^1.5 every second, and ignores the FC current on a copter with `BATT_MONITOR = 25`. About half a day.
2. **Current sensors** (`BATT_MONITOR = 4`, a power module) on the copters. Real current; everything built works as designed. Hardware cost and setup.
3. **Both:** option 1 now, sensors later.

**Decision (operator, 27 Oct): option 1, the GCS counts the charge.** Built and checked the same day:
- **When the GCS counts.** The GCS ignores the FC current and counts the charge itself when either:
  - the FC reports no current that follows throttle (`BATT_MONITOR` 0, 3 or 25); or
  - the type's recorded flights show the same FC current in hover and cruise (`PowerTypeModel::currentFollowsThrottle`).

  With a real current sensor the FC's mAh × the charger factor is used as before.
- **How it counts.** Each update adds the model current (phase, payload aboard) × (throttle now ÷ the phase's learned reference throttle)^1.5, limited to ±30 %, × the time (`PowerTypeModel::chargeStepAh`).
  - The reference throttles are the used flights' averages.
  - The count starts at an arm seen on this connection. Ground time armed is not counted, as in the recorder.
  - When the GCS joined a flight already armed, the FC's charge % is used instead, because what was used before is unknown.
  - Before the first arm the pack is taken as full.
- **Constant FC current ignored:** it seeds no starting model. The `BATT_AMP_PERVLT` suggestion is withheld on copters, with the reason. The page says the GCS counts the charge instead of showing a meaningless charger ÷ FC factor.
- **"Power type" item:** warns only for `BATT_MONITOR = 0` (no voltage), no longer for 3 or 25.
- **Log:** "power-type estimate on (…, charge used from gcs | fc | fcPercent)".
- **Tests:**
  - `BatteryEstimatorTest::testPowerTypeGcsChargeCount` covers the constant-current test, the counting step and its limit, and the throttle correction in the live estimate.
  - `testPowerTypeSummaryFromStoredFlights` covers a constant synthetic current seeding no model, and the reference throttles. BatteryEstimatorTest 24/24.
  - SITL (`sc_power_type.py`, `BATT_MONITOR = 25`):
    - "charge used from gcs";
    - RETURN NOW on 25 s after take-off (needs 0.53 Ah, 0.18 Ah left);
    - the counted charge had used the whole small pack by the landing. The FC's constant 0.3 A had shown 0.18 Ah still left.
- **Accuracy:** the count is as good as the learned model and the throttle law. The charger numbers keep correcting the model flight by flight; real current sensors would give about ±3 %.

Build rules (as for all Skywin work):
- No CMake re-run and no `custom.qrc` change; incremental builds only.
- Tests use the `_unittest` database. Every new test is mutation-checked.
- Nothing is written to the aircraft.

**Operator side (not in the days above):**
- Set `BATT_MONITOR = 25` and a starting `BATT_AMP_PERVLT` on each copter.
- Fly the 2–4 learning flights per power type and type the charger numbers.
- Set the suggested `BATT_AMP_PERVLT` when it settles.
- Confirm that the charger shows mAh charged, and that each copter's empty weight is entered in Vehicle Management.

---

## 8. Limits

- **Accuracy:** the estimate is an average. A gusty day reads high, which is the safe direction. Expected accuracy is not measured yet; the validation rule (±10 % on 3 flights) is what proves it for each type.
- **The synthetic current's shape stays a straight line in throttle.** The charger corrects its total, and the GCS model corrects the hover/cruise split for planning. The live ratio in flight still inherits some of that shape error, which is why it is limited to ±30 %.
- **The charger number belongs to one flight:** charge that pack straight after landing, with nothing else in between.
- **The drop point is assumed.** The payload counts as dropped when the copter first comes within 20 m of the target; a drop elsewhere makes that flight's learning slightly off.
- **No telemetry, no live estimate.** The flight controller's own battery failsafe stays the last line of defence.
- **SITL tests the code, not the amps.**

## Sources
- [ArduPilot: Synthetic Current Sensor / Analog Voltage Monitor](https://ardupilot.org/copter/docs/common-synthetic-current-monitor.html)
- [ArduPilot: Onboard Log Messages (Copter)](https://ardupilot.org/copter/docs/logmessages.html)
- [ArduPilot: Power Monitor/Module Configuration in Mission Planner](https://ardupilot.org/copter/docs/common-power-module-configuration-in-mission-planner.html)
- [ArduPilot: Downloading and Analyzing Data Logs in Mission Planner](https://ardupilot.org/copter/docs/common-downloading-and-analyzing-data-logs-in-mission-planner.html)
