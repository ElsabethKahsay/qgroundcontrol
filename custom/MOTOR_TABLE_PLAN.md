# Flight-time estimate from the motor table

What this feature does, how it works, what has been decided, and what still needs confirming. Written so it can be
checked with the makers and the pilots before the remaining parts are built.

## In one paragraph

For our 6S quad (M6215 320KV motors, MSC 22×7.8 props), the ground station works out the battery current from the motor
maker's test table and the throttle each motor is getting, adds it up through the flight, and shows how long the copter
can still fly and whether the planned trip fits. Nobody has to type in a "power type" or fly learning flights first.
The pilot links the copter to the table once, with one click.

## Why

The earlier method asked the pilot to define a power type (frame, prop size, cell count) and then enter the charger's
mAh after at least five flights before the estimate could be trusted. Users found that too much work. The motor table
already says how much current a motor draws at each throttle, so the GCS can calculate it instead of learning it.

## The drone type

| | |
|---|---|
| Frame | Quad (`FRAME_CLASS` = 1) |
| Motor | M6215 HighProt 320KV |
| Prop | MSC 22×7.8 |
| Battery | 6S **Li-ion**, 20 000 mAh, used from **4.3 V to 2.9 V per cell** (25.8 V to 17.4 V); C rating to come |
| Take-off weight | not entered: the GCS measures it from the copter's hover (about 12 kg according to the makers, used for the worked examples in this document) |
| Where it flies | **2000–4000 m** above sea level (now at 2900 m; planned 3500–4000 m) |
| Cruise speed | **70 km/h** (19.4 m/s) |
| Current sensor | none assumed: no ESC telemetry, no current-measuring power module |

## The data

The GCS uses the maker's thrust test for this motor and prop at **23 V (6S)**, as published:
`custom/tools/motor_tables/M6215_HighProt_320KV_MSC22x7.8_23V.csv`. A readable copy with the equations is in
`custom/tools/motor_tables/README.md`. The table has 22 rows from **33 % to 100 % throttle**, for **one motor**: thrust,
current, power taken, speed, torque and power out at each step.

The table checks itself: its current column is power ÷ 23 V to within 0.2 A, and power out is torque × speed to within
1 %. The app carries the same rows in `custom/src/core/MotorTable.h`, and a test fails if the two ever differ.

## How it works in flight

Every 2 seconds while the copter is armed:

1. **Each motor's throttle.** The flight controller reports each output's signal (`SERVO_OUTPUT_RAW`). The outputs whose
   `SERVOn_FUNCTION` is 33–36 drive motors 1–4. Throttle = (signal − `MOT_PWM_MIN`) ÷ (`MOT_PWM_MAX` − `MOT_PWM_MIN`),
   with 1000–2000 when those are not set (ArduPilot's own default). Example: 1495 µs → 49.5 %.
2. **Correct for the pack voltage.** The motor's speed depends on the voltage it gets, pack voltage × throttle. So the
   GCS looks up the table row at throttle × (pack voltage ÷ 23 V).
3. **Look up the power** at that row, reading in a straight line between the two nearest rows.
4. **Current per motor** = that power ÷ the pack voltage.
5. **Battery current** = all four motors added together + 1 A for the flight controller, radio and GPS. The battery feeds
   every motor at once, so its current is the sum.
6. **Count it.** Current × the time since the last update is added to the charge used since the arm.
7. **Time left** = charge left above the reserve ÷ the current now (smoothed over about 20 s so it does not jump).

### Worked example: hover at 12 kg

| Step | Calculation | Result |
|---|---|---|
| Thrust per motor | 12 000 g ÷ 4 | 3000 g |
| Throttle for 3000 g | between the 48 % (2844 g) and 51 % (3160 g) rows | 49.5 % |
| Power per motor | between 331.1 W and 384.0 W | 357 W |
| Current per motor | 357 W ÷ 23 V | 15.5 A |
| Battery current | 4 × 15.5 + 1 | **63.1 A** |
| Usable charge | 20 Ah − 20 % reserve | 16 Ah |
| Hover time to the reserve | 16 ÷ 63.1 h | **about 15 minutes** |

### As the pack drains

The motors need the same 1429 W to hold the hover. Lower voltage means more amps:

| Pack voltage | Battery current |
|---|---|
| 24 V (fresh, under load) | 60.5 A |
| 23 V | 63.1 A |
| 21 V (near the end) | 69.0 A |

On a drained pack the copter needs more throttle for the same hover (49.5 % × 23 ÷ 21 = 54.2 %). The GCS sees that higher
throttle and the lower voltage, and the two together give the higher current.

### Why each motor is worked out on its own

In wind or turns the flight controller speeds some motors up and slows others down. The current curve bends upwards, so
a motor sped up gains more current than a slowed motor saves: uneven motors draw more than their average throttle
suggests. Working per motor catches that.

### Outside the table

- **Below 33 %** (idle on the ground, fast descents): the curve of the first rows is continued down to 0 A at 0 %.
- **Above the table** (only on a pack above 23 V at full throttle, for example a fresh 25.2 V pack at 100 %): the power is
  continued as throttle³ from the last row. That is more than the maker's flattening top rows suggest, so the estimate
  errs towards more current. The reading is marked "beyond table".

### When information is missing

| Missing | What the GCS does |
|---|---|
| Motor output signals stop for more than 3 s | Uses the overall throttle the flight controller reports, turned into motor output the way ArduPilot does, for all four motors, and says "overall throttle" |
| `SERVOn_FUNCTION` not known | Assumes motors 1–4 on outputs 1–4 (ArduPilot's default) |
| Pack voltage not known | Reads the table at its own 23 V |
| The GCS joined a flight already armed | Cannot know what was used before: falls back to the flight controller's charge % |

### If the copter has a current sensor after all

If `BATT_MONITOR` is anything other than 0, 3 or 25 (for example 4, a current-measuring power module), the flight
controller measures the real current. The GCS then uses that measured count, and the table is only a cross-check.

## Accuracy work (agreed 3 Nov, before step 5)

A recheck for how close the estimate gets to the real aircraft, with the site (2000–4000 m), the Li-ion pack
(4.3 → 2.9 V per cell) and 70 km/h cruise, found these. Numbers are for the 12 kg quad; the altitude figures come from a
motor model fitted to the maker's table (speed, torque, power columns) and standard-atmosphere air density (0.82 at
2000 m, 0.75 at 2900 m, 0.67 at 4000 m).

| # | Issue | Size now | Fix | After the fix |
|---|---|---|---|---|
| A1 (**done**) | **Time left uses the current now**, but the Li-ion pack falls from about 25 V to 18.9 V at the reserve and the same hover then needs about 30 % more amps (71.6 A → 94.3 A at 4000 m) | **14 % optimistic** at the start of a flight (13.4 min shown, 11.8 min real at 4000 m) | Work in power: time left = charge left ÷ (motor power ÷ the average voltage still to come + 1 A), with the pack's voltage curve from now to the reserve. The curve starts as a generic Li-ion curve between 4.3 and 2.9 V per cell and is then learned per pack from each flight (voltage against charge used, and the sag under load). | about 2–3 % until learned, about 1 % after |
| A2 (**done**) | **Air density** at 2000–4000 m. In flight the throttle-and-voltage reading stays within about 1 % (the motor spins faster in thin air at the same throttle, which nearly cancels). But before take-off the remembered hover is read as thrust at sea-level density | weight shown 13.9 kg instead of 12; payload and overload checks off by the same; a hover remembered at 2900 m used at 4000 m plans **5.6 % low** | Density from the flight controller's pressure and the outdoor temperature (the weather service; standard atmosphere if missing). Remember the hover as power and density; turn it into thrust and back with the table's speed, thrust, torque and power columns at each density. | within about 1–2 % (temperature error of 5 °C is about 1.7 %) |
| A3 (**done**) | **Planning voltage** 3.7 V per cell is LiPo; the pack is Li-ion 4.3–2.9 V | about 1 % with a straight-line curve; depends on the real curve | Plan with the same voltage curve as A1 | as A1 |
| A4 (**done**) | **Only one motor reading per 2 s** is counted | noise in gusts; current curves upwards with throttle | Count every motor reading as it arrives; ask for 5 per second | under 1 % |
| A5 (**done**) | **Payload as a straight line** in amps per kg | up to 2.9 % low at 4 kg (through the rest of the pack, after A1) | Read the table at the real thrust, at the density | exact |
| A6 (**done**; learns from flights) | **Cruise at 70 km/h costed as hover** | unknown, could be ± 20 %: at 70 km/h drag is large, so cruise may need **more** than hover, not less | Trip check at 19.4 m/s; until flights show the cruise power, plan cruise at hover power and say it is unconfirmed; learn the cruise power from recorded flights (step 6) | known after a few flights |
| A7 (**rating check done, 4C**; capacity from the charger in step 6) | **Real pack capacity** at this current (about 3–5 C) is below the label, more so for Li-ion | 5–15 %, depends on the C rating | Use the C rating when given; the charger comparison (step 6) measures it | about 2–3 % |
| A8 (**done**, limit 2.0) | **Headroom at altitude and at the end of the pack.** At the reserve voltage the hover needs about 63–65 % throttle; at 2.9 V per cell and 4000 m the maximum thrust is only 1.78 × the weight | the 60 % overload rule would trip at every reserve; the real risk (little spare thrust late in a flight at 4000 m) is not shown | Replace the rule with a thrust-to-weight check at the reserve voltage and the site's density (warn below a limit to agree with the makers, for example 2.0) | — |
| A9 (**done** with A1) | **Start charge of a Li-ion pack** is read with the LiPo resting-voltage curve | a part-charged pack's start charge can be off by 10 % or more | A Li-ion curve for 4.3–2.9 V per cell | a few % |

### How precise the estimate is now and after these fixes

| Part | Now | After the fixes |
|---|---|---|
| Reading the maker's table | exact (straight line between rows) | exact |
| Motor current from throttle and voltage, in flight | about 1 % at 2000–4000 m | about 1 % |
| Time left during a flight | up to 14 % optimistic early on | 2–3 %, then about 1 % once the pack's curve is learned |
| Hover before take-off | 5–6 % low across sites; wrong weight | 1–2 % |
| Trip with cruise at 70 km/h | unknown (± 20 %) | known after a few recorded flights |
| Pack capacity | label value, 5–15 % high | measured by the charger, 2–3 % |
| Assumptions only the makers can confirm | 1 A electronics (± 1.5 %), ESC range and efficiency (a few %), test-stand air density (if not near sea level, up to its density ratio) | the same until confirmed |

The motor model used for A2 and A8 matches the maker's rows to within 2–3 throttle points (0.8 on average). It is only
needed where the table cannot be read directly: thin air and the headroom check.

## Before take-off

**The copter's weight is never entered.** On the ground there is no throttle to read, so the GCS uses the hover it
measured, as **thrust per motor** (the weight is that × 4). Thrust does not depend on the pack voltage, and the table
turns it into current at any voltage and air density. In this order:

1. **The last flight's steady hover** for this copter: at least 30 s airborne, below 3 m/s, climbing or sinking slower
   than 0.5 m/s, no payload aboard, read from the motor outputs and corrected for the air density. Remembered at the
   disarm. Best: the real copter as it flies.
2. **`MOT_THST_HOVER`**, the hover throttle the flight controller learns in flight (when `MOT_HOVER_LEARN` is 1 or 2 and the
   value is not the default 0.35), turned into motor output with ArduPilot's own formula (`MOT_SPIN_MIN`, `MOT_SPIN_MAX`,
   `MOT_THST_EXPO`, `MOT_BAT_VOLT_MAX`). About 2.5 % low on current, which is why it comes second.
3. **Neither:** the trip check says "Hover not known yet: fly once (30 s of steady hover), or let the copter learn
   MOT_THST_HOVER" and does not block. The GCS does not guess a weight.

A payload entered at preflight adds payload ÷ 4 to the thrust per motor, read from the table at that thrust.

The hover is then costed through the rest of the pack (fix A1), at the site's air density (A2), fine-tuned by the
charger comparison once there is one (step 6). Cruise at 70 km/h is costed as hover × the copter's measured cruise ratio
(A6), or as hover until a flight has measured it.

The trip check works as before: distance to the target and back at 70 km/h, time at the target, take-off and landing
minutes (default 1 each), and the reserve (default 20 %). Until the table is proven on this copter, a trip that does
not fit is an amber warning, not a block.

Example, for a 12 kg copter (the makers' figure) on the 20 Ah Li-ion pack: see "Accuracy work" above and the site table
in the team report: 12.6 min of hover at 2000 m, 11.2 min at 4000 m.

## Checks the GCS adds

| Check | From | When it fails |
|---|---|---|
| Four motors | `FRAME_CLASS`, else the outputs set to motors | "The motor table is for 4 motors; this copter has N": estimate off |
| 6 cells | `MOT_BAT_VOLT_MAX` ÷ 4.2, else the pack entered at preflight | "The motor table is for 6S; this pack is NS": estimate off |
| Pack size known | the pack entered at preflight | "No pack size: enter the battery pack": estimate off |
| Spare thrust | full-throttle thrust at the reserve voltage, in the site's air ÷ the measured hover thrust (with payload) | below 2.0: "Little spare thrust" (warning, item amber) |
| Pack rating | 4C × the pack's Ah; the hover current on average and by the reserve | above it: "Hover draws … above the pack's rating" (warning) |
| Motor outputs arriving | `SERVO_OUTPUT_RAW` | "Motor output signals not arriving: estimating from the overall throttle" (warning) |

## What the pilot sees and does (step 5)

- **Once per copter:** Analyze → Vehicles → Power types → "Motor table" → pick "M6215 HighProt 320KV · MSC 22×7.8 · 6S
  quad" → Link. A line under it shows where the hover comes from (the last flight, or `MOT_THST_HOVER`), the copter's
  weight and hover current, the air density, and any warning.
- **Preflight:** the "Power type" item passes for a linked copter. The "Mission check" item shows the hover, the weight
  and whether the trip fits; it is an amber warning, not a block, until the table is proven.
- **Fly view:** time and distance left as today, with "motor table" as the source, and "beyond table" or "overall
  throttle" when those apply.
- **After the flight (optional):** enter the charger's mAh. It fine-tunes the count and counts towards proving the
  table.

## Proving it on real flights (step 6)

The table comes from a test stand, so it is checked on the aircraft with the charger:

- **Each flight that gets airborne is recorded** at the disarm (a bench arm never gets airborne, so it is not): the
  GCS's charge count, whether the pack started full, the hover power and time, the cruise ratio and time, the average
  and lowest pack voltage, the time beyond the table and on the overall throttle, the air density and the pack's
  resistance.
- **After landing, the pilot charges the pack and enters the charger's mAh** next to that flight (Analyze → Vehicles →
  Power types, with no power type selected: the copter's motor-table flights). A flight can be dismissed (wrong pack
  charged, an odd flight).
- **Only full flights count:** started on a full pack (otherwise the charger also refills what was missing before
  the flight), and at least 0.5 Ah counted (on a shorter hop the charger's resolution swamps the difference).
- **Fine-tuning:** from 2 such flights, the table's current and the count are multiplied by the median of the last 5
  charger ÷ count ratios, limited to 0.80–1.20.
- **Proven:** each of the last 3 such flights agrees with the charger within 10 % of the count as the GCS would have
  shown it then (fine-tuned by the flights before it only). From then on a trip that does not fit holds the arm in
  the Mission check, with a waiver.

The charger comparison calibrates the current. The pack's real capacity at 4C still needs a flight flown down close to
the reserve, to see what is left.

## What the GCS never does

- It never changes a parameter on the aircraft. It only reads them.
- To get the motor output signals it sends a message-rate request (2 per second). That is not a parameter; it changes
  nothing that is saved and is gone after a reboot.
- After a motor test, the motor output messages go back to 2 per second on a copter instead of being switched off, so the
  estimate keeps working.

## Decisions

| Date | Question | Decision |
|---|---|---|
| 2 Nov | How a copter is marked as this drone type | A "Motor table" choice on Analyze → Vehicles; one entry for now. One click per copter. |
| 2 Nov | Can the trip check hold arming from the first flight? | No. Amber warning only until 3 flights agree with the charger within 10 %. Then it holds arming, with a waiver. |
| 2 Nov | Charger mAh after a flight | Optional. Fine-tunes the count and proves the table. |
| 2 Nov | Typed-in power types | Kept, unchanged, for copters not linked to a table. |
| 2 Nov | Where the props are entered | Not needed for this drone type: the table is for its exact motor and prop. |
| 2 Nov | Which battery and table | The drone flies **6S**: the maker's 23 V table, used as published. The earlier 8S table (derived from it) and the 12S/160KV table are removed. |

## To confirm with the makers

1. **The motor really is the 320KV winding** on every copter of this type, with MSC 22×7.8 props, on 6S.
2. **No current sensor:** `BATT_MONITOR` is 0, 3 or 25, and the ESCs send no telemetry. If they do have one, the measured
   current is better and the GCS will use it.
3. **ESC calibration matches the test stand:** throttle % on the stand corresponds to `MOT_PWM_MIN`…`MOT_PWM_MAX` on the
   aircraft (often 1000–2000 µs).
4. **Everything that is not a motor draws about 1 A.** A gimbal, camera or video link can add more; tell us how much.
5. **`MOT_HOVER_LEARN` = 2**, so the copter keeps `MOT_THST_HOVER` up to date (about 0.24 expected at 12 kg).
6. **`BATT_CAPACITY` and the pack entered at preflight** match the pack actually fitted (20 000 mAh, 6S).
7. **Take-off weight** with the usual payload. At 12 kg on this Li-ion pack, hovering to the 20 % reserve takes about
   13.1 min at 2000 m, 12.5 min at 2900 m and 11.8 min at 4000 m (motor model, straight-line voltage curve).
8. **The Li-ion cells:** model and C rating, cells in parallel, and the voltage curve if the cell maker publishes one.
   At hover the pack gives about 64–94 A (3–5 C), more than many Li-ion cells are made for.
9. **The thrust test's conditions:** the elevation and temperature it was run at. The model assumes sea level at 15 °C.
10. **Spare thrust at altitude:** what thrust-to-weight they want kept at the end of the pack (for example 2.0). At 4000 m
    and 2.9 V per cell this drone has about 1.78.

## Limits

- **Test stand vs flight:** the table has no forward airflow. In forward flight a motor draws a little less at the same
  throttle, so cruise estimates err on the safe side.
- **Above the table** (a pack above 23 V at full throttle) there is no data; those moments are estimated high on purpose
  and marked.
- **Wear, damaged props and heat** make the real current drift from the table. The charger comparison catches this.
- **The simulator cannot prove accuracy:** SITL's motors are not this motor. SITL only shows that the signals arrive and
  the count runs. Accuracy needs real flights against the charger.
- **One drone type** for now. Another type needs its own table.

## Status

| Step | What | State |
|---|---|---|
| 1 | The table in the app, and its maths (lookup, voltage correction, outside the table, ArduPilot's hover formula) | Done (commit 600099f48, then on an 8S table) |
| 2 | Reading the motors: motor parameters watched, motor output signals requested at 2 Hz, each motor's throttle in motor order, stale after 3 s | Done, commit 600099f48 |
| 3 | In flight: a linked copter's charge count and time left from the table; current sensor wins; overall throttle when outputs stop | Done, commit 6f09aa887 |
| 4 | Before take-off: the hover remembered from the last flight or from `MOT_THST_HOVER`; the trip check on the table; the setup checks | Done |
| — | Switch to 6S: the maker's 23 V table as published replaces the 8S table; the 8S and 12S/160KV files removed | Done |
| 5 | Linking on the Vehicles page; the Power type item passes on the table (amber when its estimate is off); the battery chip names the motor table | Done |
| 6 | Recording each flight; the charger comparison; fine-tuning and proving the table; the flights list on the Vehicles page | Done |
| 7 | SITL run, full test suite, docs | Done in the simulator (below); the real flights and charger numbers are still to come |

**Step 7, the simulated flight** (custom/tools/sitl/sc_motor_table.py, run log
custom/tools/sitl/runs/2026-11-04-motor-table-arducopter.md). ArduCopter SITL
set up as the drone as far as it is known (models/skywin_m6215_6s.json: 12 kg
quad X, 22-inch props, 6S Li-ion 20 Ah from 25.8 V, hover and thrust curve from
the maker's test; the diagonal and drag are assumed until a real flight log
exists), home at 2900 m, no current sensor on the pack the GCS reads. Flight:
take-off to 20 m, 40 s hover, 600 m north and back at 70 km/h (19.5 m/s
reached), land. All 19 checks passed: the estimate on from the motor table, the
charge counted by the GCS from the motor signals the whole flight, the flight
recorded with 42 s of hover and 50 s of cruise and waiting for its charger
number, the hover remembered, no QML errors.

What it measured, and why these are not accuracy results:

| | GCS | Simulator | Why they differ |
|---|---|---|---|
| Weight | 13.0 kg | 12.0 kg | The simulator's thrust curve is a one-number fit to the maker's rows (within 7.5 %), not the rows themselves |
| Charge used | 3.40 Ah | 2.38 Ah | The simulator's current is a fixed watts-per-newton from the sea-level hover; it does not rise in thin air. The table does (about +20 % at 2900 m) |
| Cruise ÷ hover power | 2.10 | | The simulator's prop slows its thrust at speed more than real props; a real flight measures its own ratio |
| Air density | 0.69 | | Pressure from the simulated barometer at 2900 m; temperature from the live weather at the simulated home's real place (Canberra, about 580 m, near 18 °C), so lower than the standard 0.75 |

The simulated flight proves the GCS's side of the chain: it reads the motor
signals and the pack, counts the charge, records the flight and remembers the
hover. Only real flights with charger numbers can say how close the count is.

Found by this run and fixed: the GCS read the vehicle's ground speed, height,
airspeed, wind and battery temperature in the units shown on screen instead of
metres and m/s. On a computer set to British or US units (this one), the app
shows mph and feet by default, so 10 m/s read as 22 (mph) and passed for
70 km/h cruise. All telemetry is now read in its own units
(LivePluginBenchTest::testTelemetryReadInMetricUnits).

**Screens after the decision that power types are not user-defined.** On
Analyze → Vehicles the second tab is now **Motor table** (was Power types). It
holds the connected copter's motor-table link and its status, the Mission
check's take-off and landing minutes, and the copter's motor-table flights,
where the charger mAh is entered. The user-defined power types (their list,
New and editor) are hidden, not deleted: a copter already linked to a stored
type keeps using it. The preflight checklist item is now **Motor table**: on a
linked copter it passes with the table's name and its dialog says where to
change the link; a copter with nothing linked is told to link it on the Motor
table tab, or the pilot waives it. Only a copter still on a stored power type
is asked to confirm its props.

How each step is checked: tests reproduce the worked numbers above (for example 63.1 A and about 15 minutes at 12 kg);
each rule is then broken on purpose to confirm a test fails; and the full test suite runs before each commit. Nothing
here has been flown on the real aircraft yet.

## Related, unchanged

- **Typed-in power types** keep working for copters without a table.
- **The Fly view's older battery-time panel** (`BatteryTimeEstimatorPanel`) works from weight and a thrust→current
  datasheet. It is separate and stays as it is; pointing it at the same motor table can come later.

## Parameters the feature reads

| Parameter | What it tells the GCS |
|---|---|
| `FRAME_CLASS` | Number of motors (1 = quad) |
| `SERVO1_FUNCTION` … `SERVO16_FUNCTION` | Which output drives which motor (33–40 = motor 1–8) |
| `MOT_PWM_MIN`, `MOT_PWM_MAX` | The ESC range, to turn an output signal into throttle |
| `MOT_SPIN_MIN`, `MOT_SPIN_MAX`, `MOT_THST_EXPO` | How ArduPilot turns its throttle into motor output |
| `MOT_THST_HOVER`, `MOT_HOVER_LEARN` | The learned hover throttle, and whether it is learned |
| `MOT_BAT_VOLT_MAX`, `MOT_BAT_VOLT_MIN` | ArduPilot's voltage compensation; `MOT_BAT_VOLT_MAX` ÷ 4.2 gives the cell count |
| `BATT_MONITOR` | Whether there is a current sensor (0, 3, 25 = none) |

## Files

| File | What |
|---|---|
| `custom/tools/motor_tables/M6215_HighProt_320KV_MSC22x7.8_23V.csv` | The maker's table |
| `custom/tools/motor_tables/README.md` | The table and its equations, readable |
| `custom/src/core/MotorTable.h` | The table in the app and the maths |
| `custom/src/core/VehicleProfileManager.cpp` | Reading the motors, the link, the hover memory and the estimate |
| `custom/src/core/MissionCheck.h` | The preflight "Mission check" item |
| `custom/tests/BatteryEstimatorTest.cpp` | Tests of the maths and the table |
| `custom/tests/QmlSmokeTest.cpp` (`LivePluginBenchTest`) | Tests on the simulated copter |
