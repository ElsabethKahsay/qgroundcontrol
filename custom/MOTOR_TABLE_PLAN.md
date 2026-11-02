# Flight-time estimate from the motor table

What this feature does, how it works, what has been decided, and what still needs confirming. Written so it can be
checked with the makers and the pilots before the remaining parts are built.

## In one paragraph

For our 8S quad (M6215 320KV motors, MSC 22×7.8 props), the ground station works out the battery current from the motor
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
| Battery | 8S LiPo, 20 000 mAh |
| Take-off weight | about 12 kg |
| Current sensor | none assumed: no ESC telemetry, no current-measuring power module |

## The data

### What the maker published

Two tables for this motor size and prop, each for **one motor**:

| Table | Winding | Voltage | Used for |
|---|---|---|---|
| `M6215_HighProt_320KV_MSC22x7.8_23V.csv` | 320KV (ours) | 23 V (6S) | The source of our 8S table |
| `M6215_HighProt_160KV_MSC22x7.8_46V.csv` | 160KV (a different winding) | 46 V (12S) | Only to check the prop data |

There is no 8S table, so we derived one.

### How the 8S table was made

What happens at the motor depends on how fast it spins. At the same speed the prop gives the same thrust and takes the
same power, whatever the battery. So each 23 V row becomes an 8S row at 30.7 V (3.83 V per cell, the same per cell as the
maker's test):

- **throttle needed** = the 23 V throttle × 23 ÷ 30.7 (it takes less throttle to reach the same speed on a higher voltage);
- **thrust, speed, torque and power** stay the same;
- **battery current** = power ÷ 30.7 V.

Example: the 23 V row at 60 % throttle (4147 g, 4079 RPM, 562.6 W) becomes 45.0 % throttle on 8S, with the same 4147 g and
562.6 W, at 562.6 ÷ 30.7 = **18.3 A** instead of 24.4 A.

The result is `M6215_HighProt_320KV_MSC22x7.8_8S_30.7V_derived.csv`: 22 rows from **24.7 % to 74.9 % throttle**, up to
8.8 kg of thrust per motor. Above 74.9 % on 8S the motor would spin faster than the maker measured, so there is no data.

### Why we trust it

- Every number comes straight from the maker's measurements; only the throttle and the current are rescaled.
- The 12S test (other winding, same prop) agrees with the 6S test about the prop to within 2–5 % at the same speed (for
  example 3978 g vs 3863 g at 4000 RPM). That supports "same speed, same thrust, same power".
- The table checks itself: current = power ÷ voltage to within 0.2 A, and power out = torque × speed to within 1 %.
- What is left as an assumption is small: the ESC is about as efficient at 30.7 V as at 23 V (about 1–2 %).

### Where it lives in the app

The CSV files are in `custom/tools/motor_tables/`. The app carries the same 22 rows in `custom/src/core/MotorTable.h`,
and a test fails if the two ever differ.

## How it works in flight

Every 2 seconds while the copter is armed:

1. **Each motor's throttle.** The flight controller reports each output's signal (`SERVO_OUTPUT_RAW`). The outputs whose
   `SERVOn_FUNCTION` is 33–36 drive motors 1–4. Throttle = (signal − `MOT_PWM_MIN`) ÷ (`MOT_PWM_MAX` − `MOT_PWM_MIN`),
   with 1000–2000 when those are not set (ArduPilot's own default). Example: 1371 µs → 37.1 %.
2. **Correct for the pack voltage.** The motor's speed depends on the voltage it gets, pack voltage × throttle. So the
   GCS looks up the table row at throttle × (pack voltage ÷ 30.7 V).
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
| Throttle for 3000 g (8S table) | between the 36.0 % and 38.2 % rows | 37.1 % |
| Power per motor | between 331.1 W and 384.0 W | 357 W |
| Current per motor | 357 W ÷ 30.7 V | 11.6 A |
| Battery current | 4 × 11.6 + 1 | **47.6 A** |
| Usable charge | 20 Ah − 20 % reserve | 16 Ah |
| Hover time to the reserve | 16 ÷ 47.6 h | **20 minutes** |

### As the pack drains

The motors need the same 1429 W to hold the hover. Lower voltage means more amps:

| Pack voltage | Battery current |
|---|---|
| 33 V (fresh, under load) | 44.3 A |
| 30.7 V | 47.5 A |
| 28 V (near the end) | 52.0 A |

On a drained pack the copter needs more throttle for the same hover (37.1 % × 30.7 ÷ 28 = 40.7 %). The GCS sees that
higher throttle and the lower voltage, and the two together give the higher current.

### Why each motor is worked out on its own

In wind or turns the flight controller speeds some motors up and slows others down. The current curve bends upwards, so
a motor sped up gains more current than a slowed motor saves: uneven motors draw more than their average throttle
suggests. Working per motor catches that.

### Outside the table

- **Below 24.7 %** (idle on the ground, fast descents): the curve of the first rows is continued down to 0 A at 0 %.
- **Above 74.9 %** (short full-throttle moments): the power is continued as throttle³ from the last row. That is more than
  the maker's flattening top rows suggest, so the estimate errs towards more current. The reading is marked "beyond
  table".

### When information is missing

| Missing | What the GCS does |
|---|---|
| Motor output signals stop for more than 3 s | Uses the overall throttle the flight controller reports, turned into motor output the way ArduPilot does, for all four motors, and says "overall throttle" |
| `SERVOn_FUNCTION` not known | Assumes motors 1–4 on outputs 1–4 (ArduPilot's default) |
| Pack voltage not known | Reads the table at its own 30.7 V |
| The GCS joined a flight already armed | Cannot know what was used before: falls back to the flight controller's charge % |

### If the copter has a current sensor after all

If `BATT_MONITOR` is anything other than 0, 3 or 25 (for example 4, a current-measuring power module), the flight
controller measures the real current. The GCS then uses that measured count, and the table is only a cross-check.

## Before take-off (step 4, not built yet)

On the ground there is no live throttle, so the GCS needs the hover current another way, in this order:

1. **The last flight's measured hover current** for this copter. Best: real motors, real weight, real payload.
2. **`MOT_THST_HOVER`**, the hover throttle the flight controller learns in flight (when `MOT_HOVER_LEARN` is 1 or 2 and the
   value is not the default 0.35), turned into motor output with ArduPilot's own formula (`MOT_SPIN_MIN`, `MOT_SPIN_MAX`,
   `MOT_THST_EXPO`, `MOT_BAT_VOLT_MAX`), at 3.7 V per cell.
3. **Neither:** the trip check says "hover current not known yet, fly once to learn it" and does not block.

The trip check works as today: distance to the target and back, time at the target, take-off and landing minutes
(default 1 each), and the reserve (default 20 %). Cruise is taken at the hover current, which errs on the safe side.

Trip example at 12 kg: a target 3 km away at 10 m/s is 10 min out and back, + 2 min take-off and landing, + 3 min at the
target = 15 min × 47.6 A = **11.9 Ah needed** against 16 Ah usable. It fits, with 4.1 Ah spare. The furthest target with
the same 2 + 3 minutes is about 4.5 km out.

Until step 4 is built, a linked copter's estimate is off on the ground ("motor table: no current before take-off yet")
and comes on at the arm.

## Checks the GCS adds (step 4, not built yet)

| Check | From | When it fails |
|---|---|---|
| Four motors | `FRAME_CLASS`, the motor outputs | "The table is for a quad": estimate off |
| 8 cells | `MOT_BAT_VOLT_MAX` ÷ 4.2, else the pack voltage at connection | "The table is for 8S": estimate off |
| Not overloaded | the hover current above, as a throttle | Hover above 60 % throttle: "copter heavy for its motors" (warning) |
| Motor outputs arriving | `SERVO_OUTPUT_RAW` | Warning; falls back to the overall throttle |
| Pack size known | the pack entered at preflight | No time-left figure |

## What the pilot sees and does (step 5, not built yet)

- **Once per copter:** Analyze → Vehicles → "Motor table" → pick "M6215 HighProt 320KV · MSC 22×7.8 · 8S quad".
- **Preflight:** the "Power type" item passes for a linked copter. The trip check shows the hover current and whether the
  trip fits; it is an amber warning, not a block, until the table is proven (see decisions).
- **Fly view:** time and distance left as today, with "motor table" as the source, and "beyond table" or "overall
  throttle" when those apply.
- **After the flight (optional):** enter the charger's mAh. It fine-tunes the count and counts towards proving the
  table.

## Proving it on real flights (step 6, not built yet)

The table comes from a test stand, so it has to be checked on the aircraft:

- each flight records the GCS's count, the average hover current and hover throttle, the average pack voltage, the time
  beyond the table, and whether motor outputs or the overall throttle were used;
- the pilot enters the charger mAh after a flight;
- once **3 flights** agree with the charger **within 10 %**, the trip check may hold arming (with a waiver), like the
  old power types;
- from then on the GCS multiplies its count by the median charger ÷ count ratio of the recent flights, limited to
  0.80–1.20. This covers worn motors and differences from the test stand.

## What the GCS never does

- It never changes a parameter on the aircraft. It only reads them.
- To get the motor output signals it sends a message-rate request (2 per second). That is not a parameter; it changes
  nothing that is saved and is gone after a reboot.
- After a motor test, the motor output messages go back to 2 per second on a copter instead of being switched off, so the
  estimate keeps working.

## Decisions (2 Nov)

| Question | Decision |
|---|---|
| How a copter is marked as this drone type | A "Motor table" choice on Analyze → Vehicles; one entry for now. One click per copter. |
| Can the trip check hold arming from the first flight? | No. Amber warning only until 3 flights agree with the charger within 10 %. Then it holds arming, with a waiver. |
| Charger mAh after a flight | Optional. Fine-tunes the count and proves the table. |
| Typed-in power types | Kept, unchanged, for copters not linked to a table. |
| Which table | The 8S table derived from the maker's 6S table, up to 74.9 % throttle. |
| Where the props are entered | Not needed for this drone type: the table is for its exact motor and prop. |

## To confirm with the makers

1. **The motor really is the 320KV winding** on every copter of this type, with MSC 22×7.8 props.
2. **No current sensor:** `BATT_MONITOR` is 0, 3 or 25, and the ESCs send no telemetry. If they do have one, the measured
   current is better and the GCS will use it.
3. **ESC calibration matches the test stand:** throttle % on the stand corresponds to `MOT_PWM_MIN`…`MOT_PWM_MAX` on the
   aircraft (often 1000–2000 µs).
4. **Everything that is not a motor draws about 1 A.** A gimbal, camera or video link can add more; tell us how much.
5. **`MOT_HOVER_LEARN` = 2**, so the copter keeps `MOT_THST_HOVER` up to date.
6. **The hover throttle is close to ArduPilot's lower limit.** Working backwards from the table, this build should hover
   with `MOT_THST_HOVER` around 0.13, and ArduPilot does not let it go below 0.125. Do they see hover-throttle warnings
   or a `MOT_THST_HOVER` stuck at 0.125? If so, the GCS will rely on the last flight's measured hover current instead.
7. **`BATT_CAPACITY` and the pack entered at preflight** match the pack actually fitted (20 000 mAh).
8. **Take-off weight** with the usual payload, and whether it changes from flight to flight (the throttle already
   reflects it, but it helps the overload check).
9. **A measured 8S table**, if the motor maker has one or can run one. It would replace the derived table and cover
   0–100 % throttle.

## Limits

- **Test stand vs flight:** the table has no forward airflow. In forward flight a motor draws a little less at the same
  throttle, so cruise estimates err on the safe side.
- **Above 74.9 % throttle** there is no data; those moments are estimated high on purpose and marked.
- **Wear, damaged props and heat** make the real current drift from the table. The charger comparison catches this.
- **The simulator cannot prove accuracy:** SITL's motors are not this motor. SITL only shows that the signals arrive and
  the count runs. Accuracy needs real flights against the charger.
- **One drone type** for now. Another type needs its own table.

## Status

| Step | What | State |
|---|---|---|
| 1 | The 8S table in the app, and its maths (lookup, voltage correction, outside the table, ArduPilot's hover formula) | Done, commit 600099f48 |
| 2 | Reading the motors: motor parameters watched, motor output signals requested at 2 Hz, each motor's throttle in motor order, stale after 3 s | Done, commit 600099f48 |
| 3 | In flight: a linked copter's charge count and time left from the table; current sensor wins; overall throttle when outputs stop | Done, being committed |
| 4 | Before take-off: hover current, trip check, the new checks | Next |
| 5 | Linking on the Vehicles page; preflight item; Fly-view display | To do |
| 6 | Recording per flight; charger fine-tuning; proving the table | To do |
| 7 | SITL run, full test suite, docs | To do |

How each step is checked: tests reproduce the worked numbers above (for example 47.6 A and 20 minutes at 12 kg); each
rule is then broken on purpose to confirm a test fails (22 such breaks so far, all caught); and the full test suite runs
before each commit. Nothing here has been flown on the real aircraft yet.

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
| `custom/tools/motor_tables/*.csv` | The maker's tables and the derived 8S table |
| `custom/src/core/MotorTable.h` | The table in the app and the maths |
| `custom/src/core/VehicleProfileManager.cpp` | Reading the motors, the link, and the live estimate |
| `custom/tests/BatteryEstimatorTest.cpp` | Tests of the maths and the table |
| `custom/tests/QmlSmokeTest.cpp` (`LivePluginBenchTest`) | Tests on the simulated copter |
