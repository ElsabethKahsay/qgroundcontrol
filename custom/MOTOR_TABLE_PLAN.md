# Flight-time estimate from the motor table: plan

Status: plan approved; decisions below. Building in the order under "Build order".

## Decisions (2 Nov)

| Question | Decision |
|---|---|
| How a vehicle is marked as this drone type | A "Motor table" choice on Analyze → Vehicles, one entry for now (M6215 320KV + MSC 22×7.8, 8S). One click per vehicle. |
| Can the trip check hold arming from the first flight? | No. Amber warning only until the table is proven on this vehicle: at least 3 flights where the charger mAh is within 10 % of the GCS count. After that it holds arming like today, with a waiver. |
| Charger mAh after a flight | Optional. Used to fine-tune the count (median ratio, limited to 0.80–1.20) and to prove the table (above). |
| Typed-in power types | Kept, unchanged, for copters not linked to a table. |

## Goal

For one drone type, the GCS estimates flight time from the motor maker's test table instead of a typed-in power type and
learning flights. The pilot enters nothing for the estimate to work.

The drone type:

| | |
|---|---|
| Frame | Quad (`FRAME_CLASS` = 1) |
| Motor | M6215 HighProt 320KV |
| Prop | MSC 22×7.8 |
| Battery | 8S LiPo, 20 000 mAh |
| Take-off weight | about 12 kg |
| Current sensor | none assumed (no ESC telemetry, no current-measuring power module) |

## The table the GCS uses

`custom/tools/motor_tables/M6215_HighProt_320KV_MSC22x7.8_8S_30.7V_derived.csv`: one motor at 30.7 V (8S at 3.83 V per
cell), 22 rows from 24.7 % to 74.9 % throttle. Derived from the maker's 23 V (6S) table: the same motor speed gives the same
thrust and power; only the throttle needed (× 23 ÷ 30.7) and the battery current (power ÷ 30.7 V) change. The 6S source
table and the 12S table (160KV winding, used only to confirm the prop data) are stored next to it.

The app cannot read a CSV without a resource change (a ~70-minute rebuild), so the same rows go into a header,
`custom/src/core/MotorTable.h`, with a test that the header and the CSV match row for row.

## How the GCS works out battery current

Every second, in flight:

1. **Each motor's throttle.** Read the motor output signals (`SERVO_OUTPUT_RAW`). The outputs that drive motors are the
   ones whose `SERVOn_FUNCTION` is 33–36 (Motor 1–4). Throttle = (signal − `MOT_PWM_MIN`) ÷ (`MOT_PWM_MAX` − `MOT_PWM_MIN`).
   The GCS asks the flight controller to send these messages at 2 Hz (a message-rate request, not a parameter change, and
   it is not saved on the aircraft).
2. **Correct for the pack voltage.** Equivalent table throttle = throttle × (pack voltage ÷ 30.7 V).
3. **Look up the power** at that throttle, reading in a straight line between the two nearest rows.
4. **Current per motor** = that power ÷ pack voltage.
5. **Battery current** = the four motors added together + 1 A for the flight controller, radio and GPS.
6. **Count it**: current × the time since the last update, added to the charge used since the arm.

Outside the table:

- Below 24.7 %: the curve is continued down to 0 A at 0 % throttle (idle on the ground and fast descents).
- Above 74.9 %: the curve is continued from the top rows, and the reading is marked "beyond table" in the log and the
  Fly-view tooltip. These are short full-throttle moments.

If the motor outputs stop arriving, the GCS uses the overall throttle the flight controller reports for all four motors,
and says so ("estimate from overall throttle").

## Before take-off: the trip check

There is no live throttle yet, so the GCS needs the hover current another way, in this order:

1. **The last flight's measured hover current** for this vehicle (recorded by step 7 below). Best: real motors, real
   weight.
2. **`MOT_THST_HOVER`**, the hover throttle the flight controller learns in flight, if `MOT_HOVER_LEARN` is 1 or 2 and the
   value is not the default 0.35. Converted to motor throttle the way ArduPilot does it (`MOT_SPIN_MIN`, `MOT_SPIN_MAX`,
   `MOT_THST_EXPO`), then steps 2–5 at 3.7 V per cell.
3. **Neither:** the trip check says "hover current not known yet, fly once to learn it" and does not block.

Worked example for this drone at 12 kg: 3000 g per motor needs 37.1 % throttle; 357 W ÷ 30.7 V = 11.6 A per motor;
4 × 11.6 + 1 = 47.6 A. 16 Ah usable (20 % reserve) gives 20 minutes of hover.

The trip check itself stays as it is today (distance to the target, time at the target, take-off and landing minutes,
reserve). The cruise current is taken as the hover current, which errs on the safe side.

## Checks the GCS adds for this drone type

| Check | From | When it fails |
|---|---|---|
| Four motors | `FRAME_CLASS`, motor outputs | "The table is for a quad" — estimate off |
| 8 cells | `MOT_BAT_VOLT_MAX` ÷ 4.2, else the pack voltage at connection | "The table is for 8S" — estimate off |
| Not overloaded | hover current found above → hover throttle | Hover above 60 % throttle: warning "copter heavy for its motors" |
| Motor outputs arriving | `SERVO_OUTPUT_RAW` | Warning; falls back to overall throttle |
| Pack size set | `BATT_CAPACITY` (or the pack entered at preflight) | No time-left figure |

## Optional fine-tuning from the charger

The charger mAh entry stays available after a flight but is no longer required. The GCS compares it with its own count;
the median ratio of the last flights (limited to 0.80–1.20) multiplies the count from then on. This covers worn motors and
differences between the test stand and the aircraft.

## Recorded for each flight

GCS-counted charge, average hover current and hover throttle, average pack voltage, time beyond the table, and whether the
motor outputs or the overall throttle were used. This feeds the pre-flight hover current (step 1 above), the charger
comparison, and the makers' validation flights.

## What happens to typed-in power types

Vehicles linked to the motor table skip power types entirely: no type to define, no learning flights, no 5-flight
validation. Copters without a table keep today's behaviour unchanged, so nothing that works now breaks.

## The older battery-time panel

The Fly view already has a battery-time panel (`BatteryTimeEstimatorPanel`) that works from weight: thrust per motor →
current, from a thrust→current datasheet stored per vehicle. It is separate from this plan and stays as it is. It cannot
count charge in flight from each motor's throttle, which is what this plan adds. Pointing that panel at the same motor
table can come later.

## Build order

1. **Done.** `MotorTable.h`: the rows and the pure maths (lookup, voltage correction, outside-the-table rules, PWM to throttle,
   `MOT_THST_HOVER` to motor throttle). Tests with the worked numbers above. Mutation-checked.
2. **Done.** Inputs: the motor outputs (`SERVO_OUTPUT_RAW` rate request and parsing), the motor and battery parameters added to the
   watchlist, the motor-output mapping from `SERVOn_FUNCTION`.
3. Live count: the table replaces the learned model in the in-flight charge count and time left for linked vehicles.
4. Before take-off: hover current and the trip check; the new checks.
5. Linking a vehicle to the table, and what the pilot sees (preflight item, Fly-view info box, battery chip tooltip,
   Vehicles page showing the table).
6. Recording per flight; optional charger fine-tuning.
7. SITL run (motor outputs and counting work end to end; SITL's motors are not this motor, so it cannot prove accuracy),
   full test suite, report and docs.

## What only real flights can prove

The table describes a test stand. Before the trip check is allowed to hold arming for this drone type, 3–5 real flights
should compare the GCS count with the charger, aiming for agreement within 10 %.

## Assumptions to confirm with the makers

- The test stand's throttle % matches the aircraft's ESC calibration (`MOT_PWM_MIN`/`MAX`).
- 1 A for everything that is not a motor (no gimbal or video link drawing more).
- The aircraft really has no current sensor (`BATT_MONITOR` is 0, 3 or 25). If it has one, the measured current wins and
  the table becomes a cross-check.
