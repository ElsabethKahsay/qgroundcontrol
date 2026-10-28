# Power Types: Flight-Time and Mission Check for Copters

Skywin GCS · copters only · designed with the operator 26 Oct 2026 · built and tested 26–28 Oct 2026

**Status:** built and tested on the bench, in a simulator and on QGC's mock copter. **Not yet flown on a real copter.** Four known issues are listed in section 9; fix the first two before letting the mission check block arming in the field.

---

## 1. The problem in one paragraph

Before a delivery flight the pilot needs to know: *"Can this copter carry this payload to that target, wait there, drop it, and come back with enough battery left?"* During the flight they need to know: *"How long can I still fly, and must I turn back now?"* Our copters have **no current sensor**, so the flight controller cannot measure how much battery a flight uses. The GCS has to work it out another way.

## 2. The idea in one picture

```
   LEARN (a few flights per copter build)                     USE (every flight)
   ───────────────────────────────────────                    ─────────────────────────────────
   Fly ──► GCS records: minutes hovering,                     Before take-off:
           minutes cruising, payload, throttle                  "Can I do this trip?"  ──► OK / short
     │                                                                 (can block arming)
     ▼
   Land ──► charge the pack ──► type the charger's mAh        In flight:
                                                                "Time & distance left"
     ▼                                                          "RETURN NOW" when the way
   GCS learns:  hover current, cruise current,                  home no longer fits
                and how much each kg of payload adds
```

- **The charger is the measuring instrument.** After a flight the charger puts back exactly what the flight used. Typing that one number is the pilot's only extra job.
- **The GCS knows how the flight was spent:** how many minutes it hovered and cruised, with and without the payload. From several flights it works out how many amps hovering and cruising cost.
- **Those numbers belong to the copter's build (its "power type"), not to one airframe.** Every copter built the same way shares them.

## 3. Key words

| Word | Plain meaning |
|---|---|
| **Power type** | A copter build: **frame + propeller size + battery cells**, e.g. "Quad X · 15×5.5 · 6S". Different props mean a different type. |
| **Learning flight** | A flight whose charger number has been typed in and accepted ("Use"). The GCS learns from these. |
| **Hover / cruise** | Below 3 m/s counts as hover (take-off, landing, waiting at the target); faster counts as cruise. |
| **Payload** | What the copter carries this flight (not its own weight). It is **dropped at the target**, so the way back is costed empty. |
| **Reserve** | The share of the battery that is never planned to be used. Default 20 %. |
| **Validated** | The type has enough learning flights, and its predictions match reality within 10 %. Only then can the mission check **block** arming. |
| **Waiver** | The pilot flies anyway, giving their name; the GCS logs it. |

## 4. How it works, step by step

### 4.1 Once per copter build (setup)
| You do | The GCS does |
|---|---|
| **Analyze Tools → Vehicles → "Power types" tab → New.** Enter prop diameter and pitch (inches) and battery cells. The frame is filled in from the copter. Save. | Stores the type. Two types can't describe the same build. |
| With the copter connected: **Link connected copter**. | Remembers which type this copter is (by its hardware ID). |
| Optional: set the **take-off and landing minutes** on the same page (default 1 min each). | Uses them in every trip calculation. |

### 4.2 Learning flights (2–4 per type)
1. Fly with a **full pack**. The GCS checks the voltage at arming.
2. Fly **one flight that is mostly cruising** and **one that is mostly hovering**, both **without payload**. Then fly **one or two with a known payload**, kept aboard.
3. After landing, **charge that pack straight away** and read the mAh on the charger.
4. **Power types tab → the flight → type the charger mAh → Save → Use.**

**What the GCS records by itself** during every flight: minutes of hover and cruise (with and without payload), throttle, cruise speed, whether the pack was full at arming, and whether telemetry was there the whole time. Flights that didn't start on a full pack, or that lost too much telemetry, are marked "not used for learning".

**What the GCS learns** after these flights: four numbers. They are hover amps, cruise amps, and the extra amps per kg in each. If the flights are too alike to tell hover from cruise, the page says which flight to fly next.

### 4.3 Every flight: the preflight checklist (Power section)
| Item | What you do | When it blocks arming |
|---|---|---|
| **Payload** | Enter the payload weight (kg) and the **time at the target** (min). | Never. |
| **Power type** | Confirm **"Props fitted match"** (the GCS can't see the props), or waive. | Until confirmed or waived. |
| **Mission check** | Nothing: the GCS costs the trip to your saved target. Tap **Details** to see each leg. | Only when the trip **doesn't fit** *and* the type is **validated**. A waiver releases it. |

The props confirmation, the waivers and the mission waiver last **one flight**.

### 4.4 In flight: what you see
| Where | Before take-off | In flight |
|---|---|---|
| **Battery chip** (Fly view) | Cruise endurance with your payload, e.g. "87 % · ~32 min" | Time left, e.g. "64 % · ~16 min"; **RETURN NOW** in red when the way home no longer fits |
| **Info box → NAVIGATION** | **Left**: e.g. "32 min · 28.5 km" | **Left**: time and distance left |
| **Info box → NAVIGATION** | **Tgt**: distance to your target (only shown when a target is saved) | Same, live |
| **Pop-up message** | – | Once, when RETURN NOW first comes on |

### 4.5 After landing
Charge the pack and type the charger mAh for that flight (Power types tab). Accept it with **Use**, or **Dismiss** it if something was odd. A flight more than 20 % off the prediction is flagged: was the wrong pack charged, or was the payload heavier than entered?

## 5. Worked example (copter "SKY-03")

**The build:** Quad X, 15×5.5 props, 6S, 16 Ah pack. The copter weighs 9 kg empty.

**Four learning flights** (minutes recorded by the GCS; charger mAh typed by the pilot):

| Flight | Payload | Hover | Cruise | Charger |
|---|---|---|---|---|
| 1 | none | 3 min | 10 min | 5,100 mAh |
| 2 | none | 8 min | 4 min | 5,667 mAh |
| 3 | 1.5 kg | 3 min | 10 min | 6,550 mAh |
| 4 | 1.5 kg | 8 min | 4 min | 7,267 mAh |

**What the GCS learns:**
- Hover costs **32 A**, plus **6 A for each kg** of payload.
- Cruise costs **21 A**, plus **4 A for each kg** of payload.
- Check against flight 1: 32 A × 3 min + 21 A × 10 min = 5.10 Ah ✔.

**Before take-off: a delivery.** The payload is 0.8 kg, the target is 5 km away, the copter waits 2 min there, and cruises at 15 m/s. Usable battery is 16 Ah minus the 20 % reserve, which is **12.8 Ah**.

| Leg | Payload | Current | Time | Uses |
|---|---|---|---|---|
| Take-off | 0.8 kg | 36.8 A | 1 min | 0.61 Ah |
| Fly out 5 km | 0.8 kg | 24.2 A | 5.6 min | 2.24 Ah |
| Wait at target | 0.8 kg | 36.8 A | 2 min | 1.23 Ah |
| Fly back 5 km | **dropped** | 21.0 A | 5.6 min | 1.94 Ah |
| Land | – | 32.0 A | 1 min | 0.53 Ah |
| **Total** | | | **15 min** | **6.56 Ah of 12.8** |

The mission check says: **"OK, 6.2 Ah spare. Furthest target with this payload: 12.4 km."**

**In flight, 10 minutes in:**
- The GCS has counted **5.2 Ah used**, which leaves **7.6 Ah** above the reserve.
- The throttle is 10 % higher than usual for cruising (a headwind), so the GCS raises the expected current by 15 %, to 27.9 A.
- **Time left:** 7.6 Ah ÷ 27.9 A = **16 minutes**. **Distance left:** 12.7 km at the current 13 m/s.
- The rest of the trip (to the target, wait, home, land) still fits, so there is no RETURN NOW.

## 6. Safety rules

| Situation | What happens |
|---|---|
| No power type confirmed (copter) | The GCS won't arm until confirmed or waived. **RC arming is never blocked.** |
| Trip doesn't fit, type **validated** | The GCS won't arm until the pilot waives it (name logged). |
| Trip doesn't fit, type **not yet validated** | Amber warning only; arming allowed. |
| No target saved | No block; shows the furthest out-and-back distance. |
| In flight, the way home no longer fits | **RETURN NOW** on the chip (red) and as a message. The GCS never takes control; the flight controller's own battery failsafe is untouched. |
| Planes | Not affected; they keep the old rough estimate. |

## 7. What we found while building it

1. **ArduPilot's "synthetic current" doesn't work on copters.** We had planned to use `BATT_MONITOR = 25`, which estimates current from throttle. In the simulator it read a constant 0.3 A, whatever the copter did. ArduPilot reads a throttle output that multicopters don't use (confirmed in its source code). **Decision (27 Oct):** the GCS counts the battery used itself. It multiplies the learned amps by how hard the motors work (throttle) and by the time.
2. **A bug that reset every copter's battery settings, now fixed.** A Fly-view panel overwrote the stored pack size (e.g. back to 6S / 5000 mAh) every time a vehicle connected. That made all flight-time estimates wrong.
3. **The Fly-view info box was trimmed** (operator, 27 Oct). GPS, satellites, heading, bearing, the plan distance, the battery-percent line and the trip row were removed: they were shown elsewhere or always empty.

## 8. What the operator still needs to do
1. Create the power types and link each copter (section 4.1).
2. Fly 2–4 learning flights per type and enter the charger numbers (section 4.2).
3. Make sure the charger shows "mAh charged", and charge each pack right after its flight.
4. Fly with real copters and compare the estimate with what the charger says. Nothing has been measured on real aircraft yet.

## 9. Known issues and limits (review of 28 Oct)

**To fix before relying on the arming block:**

| # | Issue | Why it matters | Planned fix |
|---|---|---|---|
| 1 | "Validated" can come too early. The model is checked against the same flights it was learned from, so 3–4 flights always "match". | The mission check could block arming on an untested model. | Check each flight against a model learned *without* it, and require extra flights. About ¼ day. |
| 2 | The pack is assumed full before take-off. | On a half-used pack the trip check is too optimistic and RETURN NOW comes late. | Use the resting cell voltage at arming. Either estimate the charge from it, or refuse the trip check until the pack is full or waived (operator to choose). About ¼ day. |

**Also to fix:**

| # | Issue | Effect |
|---|---|---|
| 3 | The "usual throttle" mixes flights with and without payload. | The in-flight count can be ~10 % off: too high with payload, too low without. |
| 4 | The payload and time-at-target entry carry over to the next flight. | After a delivery, the next flight still shows the old payload unless the pilot changes it. |

**Limits to know:**
- The first estimate needs 2 calibration flights **without** payload. A fleet that only flies deliveries must fly these on purpose.
- Wind isn't in the preflight trip check yet; in flight it shows up through the throttle.
- If the GCS is restarted mid-flight, it can't know what was used before it started counting.
- The payload counts as dropped when the copter first comes within 20 m of the target.

---

## Appendix: technical summary (for engineers)

### A. Where the numbers come from
| Data | Source | Used for |
|---|---|---|
| Ground speed, altitude, position, throttle, voltage | Live MAVLink telemetry | Hover/cruise split, distance, payload drop, throttle correction, full-pack test |
| Charge used (copters, no current sensor) | **Counted by the GCS**: learned amps × (throttle ÷ usual throttle)^1.5 (±30 %) × time, from the arm | Time left, RETURN NOW |
| Charge used (current sensor fitted, `BATT_MONITOR = 4`) | Flight controller's mAh × charger correction | Same; used automatically when present |
| Charger mAh | Typed by the pilot | Learning (the only ground truth) |

### B. The maths
- **Learning:** each accepted flight gives one equation: *charger Ah = Σ (phase amps at the payload aboard × minutes)*. Solved by least squares in two stages: base amps from flights without payload, then the extra per kg.
- **Trip check:** the five legs of section 5, each *amps × time*, compared with usable Ah (capacity × (1 − reserve) − used).
- **In flight:** the expected amps for the current phase × the throttle correction, against the charge left; RETURN NOW only while airborne.

### C. Decisions log
| Date | Decision |
|---|---|
| 26 Oct | Power type = frame + props + cells; the charger is the measuring instrument; learning is suggest-only (Use/Dismiss) |
| 26 Oct | Payload is dropped at the target; time at target is entered per flight; take-off/landing 1 min each (adjustable); reserve 20 % (adjustable) |
| 26 Oct | A short trip blocks arming (with waiver) only once the type is validated; no target → no block |
| 27 Oct | `BATT_MONITOR = 25` doesn't follow throttle on copters → the GCS counts the charge itself |
| 27 Oct | Info box trimmed (section 7.3); Skywin logo slightly bigger |

### D. What was built (all committed on `custom-dev-next`)
| Step | Content | Main files |
|---|---|---|
| 1 | Maths: learning, trip check, live estimate, charge count | `src/core/PowerTypeModel.h` |
| 2 | Storage (database v25: power types, links, per-flight learning data) | `src/utils/DatabaseManager.*` |
| 3 | Flight recorder: per-second hover/cruise/payload/throttle | `src/core/SessionRecorder.h` |
| 4 | Checklist items, arming rule, waivers, payload time | `src/core/PowerTypeCheck.h`, `src/core/MissionCheck.h`, `qml/cpts/ChecklistEngine.qml` |
| 5 | Power types page; Fly-view chip, info box, RETURN NOW | `qml/analyze/VehiclesPage.qml`, `qml/cpts/BatteryStatusChip.qml`, `qml/cpts/FlyViewTelemetryStrip.qml`, `src/core/VehicleProfileManager.*` |
| 6 | Simulator check | `tools/sitl/sc_power_type.py`, run log `tools/sitl/runs/2026-10-27-power-type-arducopter.md` |

### E. Testing
- **Unit tests** reproduce every number in section 5, plus the database, the recorder, the checklist items and the arming rule.
- **On QGC's mock copter**, the real screens are driven: the page, the dialogs, the chip and the info box.
- **ArduCopter simulator flight:** take off, fly 300 m to a target, wait, return and land. Recording, the drop, GCS counting and RETURN NOW all passed.
- **Every test was mutation-checked:** the rule was broken on purpose and the test had to fail.
- **Full suite:** 34 test classes, 454 passed, 0 failed (7 known skips).
- The detailed build log is in the git history of this file and the commit messages.

### F. Sources
- [ArduPilot: Synthetic Current Sensor / Analog Voltage Monitor](https://ardupilot.org/copter/docs/common-synthetic-current-monitor.html)
- ArduPilot source: `libraries/AP_BattMonitor/AP_BattMonitor_Synthetic_Current.cpp` (the throttle term reads `k_throttle`)
- [ArduPilot: Onboard Log Messages (Copter)](https://ardupilot.org/copter/docs/logmessages.html)
- [ArduPilot: Power Monitor/Module Configuration](https://ardupilot.org/copter/docs/common-power-module-configuration-in-mission-planner.html)
