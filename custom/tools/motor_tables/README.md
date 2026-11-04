# Motor table: M6215 HighProt 320KV + MSC 22×7.8 on 6S

A readable copy of the maker's table in `M6215_HighProt_320KV_MSC22x7.8_23V.csv`, and the equations that go with it.
This is the table the GCS uses for the 6S quad's flight-time estimate (`custom/src/core/MotorTable.h`).
All values are for **one motor**. *t* is throttle as a fraction (49.5 % = 0.495), *V* the pack voltage, *P* the power the
motor takes, *T* its thrust in grams.

## The maker's table at 23 V (6S)

Measured by the maker. Temperature was given only for the first row (69.3 °C).

File: `M6215_HighProt_320KV_MSC22x7.8_23V.csv`

| Throttle | Thrust (g) | Current (A) | Power in (W) | Speed (RPM) | Efficiency (g/W) | Torque (N·m) | Power out (W) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 33 % | 1392 | 5.5 | 127.5 | 2403 | 10.9 | 0.37 | 93.6 |
| 35 % | 1564 | 6.4 | 146.9 | 2537 | 10.6 | 0.42 | 110.6 |
| 37 % | 1745 | 7.3 | 169.1 | 2674 | 10.3 | 0.46 | 129.8 |
| 39 % | 1935 | 8.4 | 193.9 | 2813 | 10.0 | 0.51 | 151.2 |
| 42 % | 2230 | 10.2 | 235.5 | 3017 | 9.5 | 0.59 | 186.8 |
| 45 % | 2533 | 12.2 | 281.5 | 3215 | 9.0 | 0.67 | 226.0 |
| 48 % | 2844 | 14.3 | 331.1 | 3405 | 8.6 | 0.75 | 268.4 |
| 51 % | 3160 | 16.6 | 384.0 | 3585 | 8.2 | 0.84 | 313.7 |
| 54 % | 3481 | 19.1 | 440.1 | 3757 | 7.9 | 0.92 | 361.5 |
| 57 % | 3810 | 21.6 | 499.5 | 3921 | 7.6 | 1.00 | 412.1 |
| 60 % | 4147 | 24.4 | 562.6 | 4079 | 7.4 | 1.09 | 465.6 |
| 63 % | 4494 | 27.3 | 630.1 | 4233 | 7.1 | 1.18 | 522.5 |
| 66 % | 4853 | 30.4 | 702.7 | 4385 | 6.9 | 1.27 | 583.2 |
| 69 % | 5224 | 33.8 | 781.3 | 4536 | 6.7 | 1.37 | 648.6 |
| 72 % | 5608 | 37.5 | 866.4 | 4688 | 6.5 | 1.46 | 719.0 |
| 75 % | 6004 | 41.5 | 958.5 | 4841 | 6.3 | 1.57 | 794.9 |
| 78 % | 6408 | 45.8 | 1057.4 | 4995 | 6.1 | 1.68 | 876.3 |
| 81 % | 6817 | 50.4 | 1162.2 | 5150 | 5.9 | 1.78 | 962.3 |
| 84 % | 7223 | 55.1 | 1270.5 | 5302 | 5.7 | 1.89 | 1051.1 |
| 87 % | 7618 | 59.8 | 1378.8 | 5445 | 5.5 | 2.00 | 1139.3 |
| 90 % | 7988 | 64.3 | 1482.0 | 5573 | 5.4 | 2.09 | 1222.3 |
| 100 % | 8801 | 74.0 | 1705.3 | 5814 | 5.2 | 2.29 | 1393.0 |

## The formulas the GCS uses

These are the formulas in `custom/src/core/MotorTable.h`. All values come straight from the table: there is no fitted
curve inside it.

| Symbol | Meaning |
|---|---|
| *t* | a motor's throttle, as a fraction (49.5 % = 0.495) |
| *V* | the pack voltage the flight controller reports |
| *t*ₑ | "t-e", the **equivalent** table throttle: the throttle on the maker's 23 V test that gives the motor the same voltage as throttle *t* on a pack at *V*. The small e is a label (subscript), not a power |
| *t*ₖ, *T*ₖ, *P*ₖ | throttle, thrust and power of table row *k* (at 23 V) |
| *P* | power one motor takes, W |
| *I* | current, A |

### 1. Each motor's throttle

From the motor's output signal in µs and the ESC range set on the copter:

  ***t* = (signal − `MOT_PWM_MIN`) ÷ (`MOT_PWM_MAX` − `MOT_PWM_MIN`)**

With ArduPilot's default range 1000…2000 µs (also used when those parameters are not set): *t* = (signal − 1000) ÷ 1000.
Example: 1495 µs → 0.495.

This assumes the maker's "throttle %" on the test stand means the same thing: the same fraction of the ESC's signal
range. To confirm with the makers.

### 2. Why the pack voltage matters

The ESC switches the pack voltage on and off very fast, so the motor sees on average *V* × *t*. Its speed is set by that
voltage, and the prop's thrust and power by the speed. The same *V* × *t* therefore gives very nearly the same speed,
thrust and power on any pack (the ESC's own losses differ by about 1–2 %). This assumes the ESC turns throttle into
on-time in a straight line, which is how ESCs normally work. The table was measured at 23 V, so the row matching a copter
at throttle *t* on a pack at *V* is at:

  23 × *t*ₑ = *V* × *t*  →  ***t*ₑ = *t* × *V* ÷ 23**

### 3. Power from the table

*t*ₑ falls between two rows *t*ₖ and *t*ₖ₊₁ (or on one). The power is read in a straight line between them:

  ***P* = *P*ₖ + (*t*ₑ − *t*ₖ) ÷ (*t*ₖ₊₁ − *t*ₖ) × (*P*ₖ₊₁ − *P*ₖ)**

Example: *t*ₑ = 0.495 lies between the 48 % row (331.1 W) and the 51 % row (384.0 W):
331.1 + (49.5 − 48) ÷ 3 × (384.0 − 331.1) = 357.6 W.

### 4. Current of one motor

Power from the pack is voltage × current, so with the pack's real voltage:

  ***I* = *P* ÷ *V***

At 23 V this is the maker's current column to within 0.2 A (its rounding). At any other voltage only *P* ÷ *V* is right,
so the GCS never reads the current column.

### 5. Battery current

The battery feeds all four motors at once, plus the flight controller, radio and GPS (assumed 1 A; a gimbal, camera or
video link adds more, to confirm with the makers):

  ***I*batt = *I*₁ + *I*₂ + *I*₃ + *I*₄ + 1**

Formulas 2–5 together, for the four motors' throttles *t*₁…*t*₄:

  ***I*batt = [ *P*(*t*₁·*V*/23) + *P*(*t*₂·*V*/23) + *P*(*t*₃·*V*/23) + *P*(*t*₄·*V*/23) ] ÷ *V* + 1**

### 6. Charge used and time left

**Charge used.** On every motor-output reading (the flight controller sends 5 a second), while armed:

  **charge used (Ah) += *I*batt × Δ*t* ÷ 3600**  (Δ*t* = seconds since the previous reading, at most 1 s)

Every reading counts because throttle jumps around in gusts and current grows faster than throttle: one reading every
2 s would miss most of that. If the readings stop, the overall throttle the flight controller reports is used instead,
every 2 s.

**Time left.** The motors need about the same power for the rest of the flight, but the pack's voltage keeps falling as
it empties, so the same power needs more current later. Time left is therefore not "charge left ÷ current now". The GCS
takes the motors' power now and steps through the charge still to come, down to the reserve, in 1 % steps:

1. motors' power now: ***W* = *P*₁ + *P*₂ + *P*₃ + *P*₄** (formula 3 for each motor), averaged over the readings since
   the last update and then smoothed over about 20 s
2. for each 1 % step, the pack's resting voltage at that charge: ***V*rest = cells × *v*(charge)**, from the pack's
   curve (below)
3. the current for that power, with the pack's voltage drop under load *R* (its internal resistance):
   (*I* − 1) × (*V*rest − *I* × *R*) = *W*, which gives
   ***I* = [*b* − √(*b*² − 4 *R* (*V*rest + *W*))] ÷ (2 *R*)**, with *b* = *V*rest + *R*
4. that step's time: **1 % × pack Ah ÷ *I***
5. **time left = the sum of the steps' times**

The current shown and used for the trip is charge left ÷ time left: the average current over the rest of the flight.

**The pack's curve** *v*(charge), resting voltage per cell (Li-ion, used from 4.3 V to 2.9 V; a typical Li-ion shape, to
be replaced by the cell maker's curve or the pack's own once learned):

| Charge left | 0 % | 5 % | 10 % | 20 % | 30 % | 40 % | 50 % | 60 % | 70 % | 80 % | 90 % | 100 % |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Volts per cell | 2.90 | 3.20 | 3.35 | 3.48 | 3.57 | 3.65 | 3.73 | 3.81 | 3.90 | 4.00 | 4.12 | 4.30 |

**The pack's drop under load** *R* is measured in every flight: ***R* = (*V*rest at the charge now − the voltage the
flight controller reports) ÷ *I*batt**, smoothed, and remembered for the next flight once a flight has 30 s of it.
Before the first flight it is 4.2 mΩ per series cell (25.2 mΩ for 6S), a typical value.

**The charge left now** = the pack's charge at the arm (full, or from its resting voltage on the curve) − charge used.
The reserve is 20 % by default.

### 7. Before take-off: from thrust instead of throttle

Thrust and power both depend only on the motor's speed, so the power for a given thrust is the same at any voltage.
It is read from the thrust and power columns in a straight line, like formula 3:

  ***P*(*T*) = *P*ₖ + (*T* − *T*ₖ) ÷ (*T*ₖ₊₁ − *T*ₖ) × (*P*ₖ₊₁ − *P*ₖ)**

Then the same steps as formula 6, with *W* = 4 × *P*(*T*), from the pack's charge now (from its resting voltage on the
ground) down to the reserve. The hover current is the average over that: charge ÷ time.

**Where *T* comes from.** The GCS does not ask for the copter's weight. *T* is the hover thrust per motor it remembers:

1. the last flight's steady hover (at least 30 s airborne, not moving, not climbing, no payload), read from the motors'
   throttle with formulas 1–2 and the thrust column; or, if there is none yet,
2. the hover throttle the copter has learned (`MOT_THST_HOVER`), turned into motor output with ArduPilot's own formula.
   This source is about 2 % low on thrust (ArduPilot's voltage correction is simpler than the table's), so about 2.5 %
   low on current. The first source has no such error, which is why it comes first.

The copter's weight is then *T* × 4 (shown, not entered).

**A payload entered at preflight** adds payload ÷ 4 to *T*. The trip is costed at two hovers, with the payload aboard (to
the target) and without it (after the drop); both are read from the table at their real thrust, at the site's air
density, through the rest of the pack. Example, 12 kg + 2 kg at 2900 m: *T* = 3000 + 500 = 3500 g per motor, which
needs 9.5 min of hover to the reserve at 101.2 A on average, against 11.9 min at 80.5 A without it.

**Cruise at 70 km/h** (19.4 m/s) is costed as hover power × the copter's **cruise ratio**: cruise power ÷ the power the
same copter needs to hover in the same air. The GCS measures it in flight whenever the copter flies level at
70 km/h ± 15 % without payload, and remembers it after 30 s of such flying. Until then the ratio is 1 (cruise costed as
hover) and the Mission check says "cruise power not measured yet". At 70 km/h drag is large, so cruise may need more
than hover.

### 8. Air density (altitude, temperature)

The maker measured the table in sea-level air (taken as 15 °C; to confirm). Thinner air changes what the prop does at a
given speed: **thrust and torque are both × σ**, where

  **σ = air density ÷ 1.225 kg/m³**

so the prop's shaft power is × σ too. The motor's own losses depend on its current and speed, not on the air.

**Where σ comes from:**

  **ρ = (*p* − *e*) × 100 ÷ (287.058 × *T*) + *e* × 100 ÷ (461.495 × *T*)**,  **σ = ρ ÷ 1.225**

| Input | From | If missing |
|---|---|---|
| *p*, air pressure (hPa) | the flight controller's barometer | the standard atmosphere at the GPS altitude: 1013.25 × (1 − 2.25577 × 10⁻⁵ × *h*)^5.25588 |
| *T*, temperature (K = °C + 273.15) | the weather service (outdoor, not the flight controller's warm board) | the standard atmosphere: 15 − 0.0065 × *h* °C |
| *e*, water vapour (hPa) | humidity × 6.1078 × 10^(7.5 °C ÷ (°C + 237.3)) (Magnus) | dry air (humid air is lighter, by under 1 % in cool air) |

| Site | Pressure | Temperature (standard) | σ |
|---|---|---|---|
| Sea level | 1013.2 hPa | 15.0 °C | 1.000 |
| 2000 m | 795.0 hPa | 2.0 °C | 0.822 |
| 2900 m | 710.0 hPa | −3.8 °C | 0.750 |
| 3500 m | 657.6 hPa | −7.8 °C | 0.705 |
| 4000 m | 616.4 hPa | −11.0 °C | 0.669 |

A day 10 °C warmer than standard at 4000 m gives σ 0.644 instead of 0.669: about 2 % more power to hover.

**How σ is applied.** In thin air the prop must spin faster for the same thrust: the table's thrust at that speed must be
thrust ÷ σ. The table stays exact at its own density: each result is the table's own value × a correction, and the
correction comes from a model of the motor fitted to the table's rows. The model has two parts, both fitted to all 22
rows:

- losses (power in − shaft power) = *a* × torque² + *b* × torque + *c* + *d* × speed (within 4 W of every row)
- motor voltage, 23 V × throttle = *e* × speed + *f* × torque + *g* (within 3 throttle points of every row)

| Used for | Formula |
|---|---|
| Power for a real thrust *T* (before take-off) | *P* = *P*(*T*) from formula 7 × [σ × shaft power + losses at the speed where table thrust = *T* ÷ σ] ÷ [shaft power + losses at the speed where table thrust = *T*] |
| Power in flight from throttle and voltage | *P* = formula 3 × [model power at the speed the motor voltage gives in air σ] ÷ [model power at the speed it gives in table air] |
| Thrust in flight (the hover the GCS remembers) | table thrust at *t*ₑ × σ × [table thrust at the faster speed] ÷ [table thrust at the table speed] |

In flight the correction is small (+0.3 % at 4000 m): in thin air the motor spins faster at the same throttle, which nearly
makes up for the lighter load, so throttle and voltage already show the extra power. Before take-off it is large,
because there is no throttle to read: the hover power for 12 kg rises by 24 % at 4000 m.

### 9. Checks before take-off

| Check | Formula | Warns when |
|---|---|---|
| Spare thrust | thrust-to-weight = full-throttle thrust per motor at the reserve voltage, in the site's air ÷ hover thrust per motor (with payload). Full-throttle thrust is the table at *t*ₑ = reserve voltage ÷ 23, × the density correction | below **2.0** |
| Pack rating | rating = 4C × 20 Ah = **80 A**; hover current on average through the pack, and by the reserve (the lowest voltage, so the highest current) | either is above 80 A |
| Wrong frame or pack | motors from `FRAME_CLASS` or the motor outputs; cells from `MOT_BAT_VOLT_MAX` ÷ 4.2 or the pack entered | not 4 motors or not 6S: the estimate is off |
| Motor outputs | `SERVO_OUTPUT_RAW` arriving | missing for 3 s: the overall throttle is used |

Warnings make the Mission check amber; they do not block arming.

### 10. The charger comparison

After a flight the pack is charged and the charger's mAh entered. What the charger put back is the charge the flight
used, so for each flight that started on a full pack and counted at least 0.5 Ah:

  ***r* = charger Ah ÷ the GCS's count**

| What | Formula |
|---|---|
| Fine-tuning (from 2 flights) | factor = the median of the last 5 *r*, limited to 0.80–1.20; the table's current and the count × factor |
| Proven | each of the last 3 flights: \|charger Ah ÷ (count × the factor from the flights before it) − 1\| ≤ 10 % |

Example: three flights of 12, 14 and 16 Ah where the charger put back 12.6, 14.7 and 16.8 Ah: *r* = 1.05 each. The
first two are shown at factor 1 (5 % off, inside 10 %); the third at the median of the first two, 1.05 (0 % off). The
table is proven, and the factor is 1.05: a hover of 80.5 A is then counted as 84.5 A.

### 11. Outside the table

| Where | Formula | When |
|---|---|---|
| Below 33 % (*t*ₑ < 0.33) | *P* = 127.5 × (*t*ₑ ÷ 0.33)^2.41, where 2.41 = ln(146.9 ÷ 127.5) ÷ ln(35 ÷ 33), the curve of the first two rows | Idle on the ground, fast descents; small currents |
| Above 100 % (*t*ₑ > 1) | *P* = 1705.3 × *t*ₑ³, marked "beyond table" | Only a pack above 23 V at full throttle (a fresh 25.2 V pack gives *t*ₑ up to 1.10). Prop power grows with speed³, which is more than the table's flattening top rows, so it errs towards more current |

## Worked example: hover at 12 kg

An example: a quad weighing 12 kg (the makers' figure) on a 6S pack of 20 Ah, with a 20 % reserve, so 16 Ah usable.
The 12 kg is not a setting anywhere in the GCS: in use, the weight is what the GCS measured from the copter's hover on
its last flight (formula 7, "Where *T* comes from"). These are the numbers the GCS's code
gives for this case (checked by running `MotorTable.h` on it; the tests check the same values).

The pack is Li-ion, 4.3 → 2.9 V per cell, with the typical 25.2 mΩ until its own is measured. The steps below are worked
in sea-level air; the same quad at the 2000–4000 m sites follows at the end (formula 8).

The two table rows around the hover, used throughout:

| Row | Throttle *t*ₖ | Thrust *T*ₖ | Power *P*ₖ |
|---|---|---|---|
| *k* | 0.48 | 2844 g | 331.1 W |
| *k*+1 | 0.51 | 3160 g | 384.0 W |

### Before take-off (formula 7)

**Step 1. Thrust per motor**

  *T* = weight ÷ motors = 12 000 g ÷ 4 = **3000 g**

(In the GCS this 3000 g comes from the last flight's hover, as described under formula 7; here it is worked from the
weight to show the numbers.)

**Step 2. Power for that thrust** (straight line between the two rows, thrust and power columns)

  *P*(*T*) = *P*ₖ + (*T* − *T*ₖ) ÷ (*T*ₖ₊₁ − *T*ₖ) × (*P*ₖ₊₁ − *P*ₖ)
  = 331.1 + (3000 − 2844) ÷ (3160 − 2844) × (384.0 − 331.1)
  = 331.1 + 156 ÷ 316 × 52.9
  = 331.1 + 0.4937 × 52.9
  = 331.1 + 26.1
  = **357.2 W**, so all four motors: *W* = 4 × 357.2 = **1428.9 W**

**Step 3. The current through the rest of the pack** (formula 6, full pack down to the 20 % reserve). Four of the 80
steps, worked out:

| Charge left | *V*rest = 6 × *v* | Current, from (*I* − 1)(*V*rest − 0.0252 *I*) = 1428.9 | Voltage under load |
|---|---|---|---|
| 100 % | 6 × 4.30 = 25.80 V | 59.8 A | 25.80 − 59.8 × 0.0252 = 24.29 V |
| 80 % | 6 × 4.00 = 24.00 V | 64.9 A | 22.37 V |
| 50 % | 6 × 3.73 = 22.38 V | 70.3 A | 20.61 V |
| 20 % (reserve) | 6 × 3.48 = 20.88 V | 76.4 A | 18.96 V |

Worked for 50 %: *b* = 22.38 + 0.0252 = 22.405; *I* = [22.405 − √(22.405² − 4 × 0.0252 × (22.38 + 1428.9))] ÷
(2 × 0.0252) = [22.405 − √355.7] ÷ 0.0504 = [22.405 − 18.860] ÷ 0.0504 = **70.3 A**.

**Step 4. Hover time to the reserve:** the 80 steps of 0.2 Ah (1 % of 20 Ah), each ÷ its current, add up to
0.2347 h = **14.1 min**. The average current is 16 Ah ÷ 0.2347 h = **68.2 A**.

For comparison:

| Method | Hover time | Error |
|---|---|---|
| Through the rest of the pack (formula 6, what the GCS does) | **14.1 min** | — |
| Charge left ÷ the current at the start (59.8 A) | 16.0 min | 14 % optimistic |
| At a fixed 22.2 V (the first plan) | 14.7 min | 4 % optimistic |

**Step 5. Spare thrust and the pack's rating** (formula 9; sea-level air here)

  reserve voltage under load = 18.96 V (step 3); table throttle at full throttle *t*ₑ = 18.96 ÷ 23 = 0.8243
  full-throttle thrust = between the 81 % row (6817 g) and the 84 % row (7223 g) = 6817 + (82.43 − 81) ÷ 3 × 406 = 7010 g
  spare thrust = 7010 ÷ 3000 = **2.34**, above 2.0
  hover by the reserve = 76.4 A (step 3), under the pack's 80 A (4C): no warning in sea-level air

### In flight (formulas 1–6), at three pack voltages

To hold 3000 g the flight controller sends each motor a signal; a drained pack needs a higher one. The GCS reads it:

| Step | Formula | 24 V (fresh) | 23 V | 21 V (drained) |
|---|---|---|---|---|
| Motor signal | from the flight controller | 1474 µs | 1495 µs | 1542 µs |
| 1. Throttle | *t* = (signal − 1000) ÷ 1000 | (1474 − 1000) ÷ 1000 = 0.474 | 0.495 | 0.542 |
| 2. Table throttle | *t*ₑ = *t* × *V* ÷ 23 | 0.474 × 24 ÷ 23 = 0.4946 | 0.495 × 23 ÷ 23 = 0.4950 | 0.542 × 21 ÷ 23 = 0.4949 |
| 3. Power | *P* = 331.1 + (*t*ₑ − 0.48) ÷ 0.03 × 52.9 | 331.1 + 0.0146 ÷ 0.03 × 52.9 = 356.9 W | 331.1 + 0.015 ÷ 0.03 × 52.9 = 357.6 W | 331.1 + 0.0149 ÷ 0.03 × 52.9 = 357.3 W |
| 4. Current per motor | *I* = *P* ÷ *V* | 356.9 ÷ 24 = 14.87 A | 357.6 ÷ 23 = 15.55 A | 357.3 ÷ 21 = 17.02 A |
| 5. Battery current | *I*batt = 4 × *I* + 1 | 4 × 14.87 + 1 = **60.5 A** | 4 × 15.55 + 1 = **63.2 A** | 4 × 17.02 + 1 = **69.1 A** |
| 6. Charge per 2 s update | *I*batt × 2 ÷ 3600 | 60.5 × 2 ÷ 3600 = 0.0336 Ah | 0.0351 Ah | 0.0384 Ah |

The table throttle in step 2 comes out at 0.495 at every voltage: the motor spins at the same speed and takes the same
power (about 357 W) on any pack. A lower voltage only means more amps for it.

**Time left** (formula 6), after 5 minutes of hover from a full pack: about 5.3 Ah has been used, so the charge left is
73.6 %. The steps from 73.6 % down to 20 % add up to **9.1 min**, at an average 70.9 A. (Charge left ÷ the current at that
moment, 66.1 A, would have said 9.7 min.)

### The same quad at the sites (formula 8)

12 kg, 6S Li-ion 20 Ah from full to the 20 % reserve, standard-atmosphere temperature. Worked by the GCS's code.

| Site | σ | Power per motor | Average current | Hover time | Hover throttle at 23 V | At the reserve voltage | Weight the GCS reads back (without density) |
|---|---|---|---|---|---|---|---|
| Sea level | 1.000 | 357.2 W | 68.2 A | 14.1 min | 49.5 % | 60.0 % at 18.96 V | 12.0 kg |
| 2000 m | 0.822 | 397.4 W | 76.5 A | 12.6 min | 51.5 % | 63.3 % at 18.71 V | 12.0 kg (12.9) |
| 2900 m | 0.750 | 416.6 W | 80.5 A | 11.9 min | 52.5 % | 65.0 % at 18.60 V | 12.0 kg (13.3) |
| 3500 m | 0.705 | 429.9 W | 83.3 A | 11.5 min | 53.3 % | 66.2 % at 18.51 V | 12.0 kg (13.6) |
| 4000 m | 0.669 | 441.2 W | 85.7 A | 11.2 min | 53.9 % | 67.2 % at 18.44 V | 12.0 kg (13.9) |

Worked for 4000 m, before take-off: table thrust needed = 3000 ÷ 0.669 = 4484 g, which the table reaches at a higher
speed; the model's power there in 4000 m air ÷ its power at the sea-level hover speed = 1.235; 357.2 W × 1.235 =
441.2 W per motor. Then formula 6 through the pack with *W* = 4 × 441.2 W.

### All checks for 12 kg at the sites

| Site | Hover time | Average current | By the reserve | vs 80 A (4C) | Spare thrust at the reserve | vs 2.0 |
|---|---|---|---|---|---|---|
| 2000 m | 12.6 min | 76.5 A | 85.9 A at 18.71 V | over by the reserve | 2.15 | OK |
| 2900 m | 11.9 min | 80.5 A | 90.6 A at 18.60 V | over | 2.08 | OK |
| 3500 m | 11.5 min | 83.3 A | 93.9 A at 18.51 V | over | 2.03 | OK |
| 4000 m | 11.2 min | 85.7 A | 96.7 A at 18.44 V | over | 1.98 | **under** |

At 12 kg the pack is at or above its 4C rating just hovering at every site, and climbs and 70 km/h cruise draw more.
At 4000 m the spare thrust falls just under 2.0 by the reserve.
