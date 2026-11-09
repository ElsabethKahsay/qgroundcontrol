# Waypoint time and mission assessment: what we need before building

**Status:** proposal for discussion. Nothing here is built yet.

**The idea:** as well as battery % and current, the GCS gives a time estimate for each waypoint in the mission, and an
assessment of whether the mission can be flown. The mission is read from the autopilot, where it was originally loaded
by Mission Planner. The autopilot for now is ArduPilot.

**Arming:** the drone is RC-primary. The assessment is advice only and never holds arming. Stock QGroundControl arming
logic is not changed; only code in our `custom/` folder is.

## Proposed assessment

Each mission gets a result, the reasons for it, and how sure the estimate is. The limits are proposals to agree.

### Whole mission (the worst one that applies)

| Result | Meaning |
|---|---|
| Comfortable | Lands at least 10 points above the reserve, even in the pessimistic case |
| Fits | Lands above the reserve in the pessimistic case, with less margin |
| Tight | Lands above the reserve in the likely case only |
| Uses the reserve | Likely case lands between 10 % and 20 % |
| One way only | Reaches the far point but cannot get home |
| Can't be done | Runs out before landing, even in the best case |
| Too heavy to fly safely | Spare thrust is below the minimum, even at take-off |
| Can't assess | Says what is missing, such as the table, the weight or an endless loiter |

### Each waypoint

| State | Meaning |
|---|---|
| On track | Reached, with enough charge to continue as planned |
| Last safe turn-back point | The furthest waypoint from which it can still get home above the reserve |
| Past the turn-back point | Reached, but it could no longer get home above the reserve from here |
| Reached only on the reserve | Arrives with less than the reserve left |
| Autopilot will turn back before here | The autopilot's own battery failsafe is expected to trigger first |
| Not reachable | The battery runs out before this waypoint |

### Reasons shown alongside

- Over the battery's current limit on a leg
- Low spare thrust on a leg
- The autopilot's battery failsafe will trigger at a named waypoint
- Headwind beyond the copter's tilt limit, or gusts over the limit
- Above the tested altitude
- Too close to terrain
- Landing after dark
- Old weather or an old copy of the mission

### How sure

| Level | Meaning |
|---|---|
| High | Cruise power measured and agreeing with the charger |
| Medium | Some of that is still missing |
| Low | The table is unproven and cruise power is estimated |

## What we need

### Aircraft and battery

| # | Item | From | Status |
|---|---|---|---|
| 1 | Empty weight, and what makes up 9 or 12 kg | Operator | Missing |
| 2 | Real capacity of the packs, from the charger | Tests | Missing (20 Ah assumed) |
| 3 | Pack resistance | Measure | Assumed 25.2 mΩ |
| 4 | Battery continuous and peak current limits | Battery maker | 4C (80 A) only |
| 5 | Motor resistance and the thrust test's air conditions | HobbyWing | Assumed 0.065 Ω |
| 6 | ESC current and heat limits at altitude | Maker | Missing |
| 7 | Battery capacity when cold at altitude | Tests or maker | Missing |
| 8 | Accuracy of the autopilot's current sensor | Charger comparison | Missing |

### Time estimate

| # | Item | From | Status |
|---|---|---|---|
| 9 | ArduPilot version on the aircraft (parameter names change between versions) | Operator | Missing |
| 10 | `.params` export: cruise, climb, descent, acceleration, return and landing speeds, return height | Operator | Missing |
| 11 | Real `.waypoints` files from Mission Planner | Operator | Missing |
| 12 | Commands used: holds, speed changes, loops, splines, loiters | The files | Missing |
| 13 | Height type used: relative, sea level or terrain | The files | Missing |
| 14 | Do missions end with a return or a landing? | Operator | Missing |
| 15 | Can the mission change after the GCS has connected? | Operator | Missing |
| 16 | How the flight starts. If the pilot takes off on RC and then switches to Auto, when does the clock start? | Operator | Missing |
| 17 | How to show time: from take-off, countdown or clock time | Group | Missing |

### Battery use along the mission

The payload does not change waypoint times, apart from any hold at the drop point. It changes only the battery figures
after the drop.

| # | Item | From | Status |
|---|---|---|---|
| 18 | Payload weight, and the waypoint where it is dropped | Operator | 4 kg example only |
| 19 | How the drop appears in the mission; can there be several? | Operator | Missing |
| 20 | Must the trip also work if the payload fails to release? | Group | Missing |
| 21 | Measured cruise power at 70 km/h, loaded and empty | Test flights | Missing (estimated × 1.1 to × 1.4 of hover) |
| 22 | Power at other speeds, if missions change speed | Test flights | Missing |
| 23 | Autopilot battery failsafe levels and actions | `.params` export | Missing |

### Environment

| # | Item | From | Status |
|---|---|---|---|
| 24 | Internet at the site, or data saved beforehand | Operator | Missing |
| 25 | Terrain data and minimum clearance | Group | Missing |
| 26 | Wind at flight height; the forecast is at ground level | Group | Missing |
| 27 | Temperature at the site: height-corrected or pilot-entered | Group | Pending |
| 28 | Wind and gust limits for this copter | Group | Missing |

### Limits to agree

| # | Item | Status |
|---|---|---|
| 29 | Landing reserve | 20 % default |
| 30 | Lowest acceptable landing charge | Proposed 10 % |
| 31 | Pessimistic cruise case before flights measure it | Proposed × 1.4 |
| 32 | Whether going over 80 A is only a warning | Missing |
| 33 | Minimum spare thrust | 2.0 set |
| 34 | Acceptable error for time and charge | Missing |

### Screen and scope

| # | Item | Status |
|---|---|---|
| 35 | Where it appears: preflight, Fly view, Plan view | Missing |
| 36 | Live updating in flight, and any alert sounds | Missing |
| 37 | Copters with a motor table only, or others too | Proposed: these only |
| 38 | Saving each prediction to compare with the real flight | Proposed: yes |

### Before coding

| # | Item | Status |
|---|---|---|
| 39 | Fix the altitude error in the motor table code and correct the plan and report (physics in `custom/tools/motor_tables/README.md`) | Done in code; to be confirmed by the maker's R and flights |
| 40 | Stop our `custom/` code holding arming: the Mission check and our arming gate. Stock QGroundControl arming logic stays as it is | Deferred: to fix later, not part of the current work |
| 41 | Simulator missions covering each result | To build |
| 42 | Test flights at the operating altitude, with charger readings | To schedule |
