# Vehicle-Adaptive Hardware Tests — Control Surfaces, Motors (Quad + Fixed Wing)

> **Scope note:** VTOL tiltrotor, helicopter, and rover are explicitly OUT OF
> SCOPE for now. This plan covers only **Quad/Hex/Octa** (multirotor) and
> **Fixed Wing**. VTOL conventional is kept only where it shares fixed-wing
> blocking behavior; no dedicated VTOL UI work is done yet.

## Reference table — what applies to which vehicle

| Test | Quad/Hex/Octa | Fixed Wing |
|---|---|---|
| **MOTORS** | | |
| Multi-motor spin (per motor grid) | ✅ | ❌ |
| Single motor spin | ❌ | ✅ |
| Motor hand spin (physical) | ✅ | ✅ |
| Motor temperature | ✅ | ✅ |
| **CONTROL SURFACES** | | |
| Aileron | ❌ | ✅ |
| Elevator | ❌ | ✅ |
| Rudder | ❌ | ✅ |
| Flaps | ❌ | ⚠️ if equipped |
| Nose wheel / steering | ❌ | ⚠️ if equipped |
| **SENSORS** | | |
| Airspeed | ❌ | ✅ BLOCKING |

✅ = always present for this type
⚠️ = present only if that channel is configured on the FC
❌ = not applicable, hide entirely

---

## Logical order & task breakdown

Tasks are ordered by dependency and importance. Each phase must be completed
and verified before the next, because later phases consume the output of
earlier ones.

### Phase A — Vehicle kind foundation (SAFETY-CRITICAL, do first)

Everything below depends on a single canonical vehicle classification.
Without it we cannot decide which checks block, which surfaces exist, or what
the UI shows.

- **A1 — VehicleKind enum + resolution** (`VehicleProfileManager`) ✅ DONE
  Add a `VehicleKind` enum (Multirotor / FixedWing / VtolConventional /
  Unknown), a `kindFromMavType(MAV_TYPE)` mapper, a `kindFromTypeString()`
  mapper, and a `vehicleKind` Q_PROPERTY exposed to QML.
- **A2 — Kind-aware blocking rules** (`PreflightManager`) ✅ DONE
  Implement the previously-declared `updateCriticalChecks()`: per-kind
  `setMandatory()`/blocking decisions. Quad blocks on motor count + motor
  spin; FixedWing blocks on airspeed + RTL alt. Never let airspeed block a
  quad. Public entry point: `applyVehicleKind(kindString)`.
- **A3 — Wiring** (`PreflightPlugin`) ✅ DONE
  Connect `VehicleProfileManager::vehicleTypeResolved` → `applyVehicleKind`.
- **A4 — Badge + UNKNOWN warning** (QML, small) ⏳ PENDING
  Hardware header shows the resolved kind badge; UNKNOWN shows amber warning
  and falls back to the most comprehensive surface set.

### Phase B — Control surfaces by kind (controller layer)

- **B1 — SERVO_FUNCTION param map** (`ControlSurfaceTestController`) ✅ DONE
  Reads `SERVO1_FUNCTION`…`SERVO16_FUNCTION` and maps function ID → surface
  (ailerons, elevator(s), rudder, flap, steering/nose wheel).
- **B2 — Vehicle-kind filter** ✅ DONE
  Multirotor → surface list is empty. Fixed wing / VTOL conv. / Unknown →
  aileron/elevator/rudder, plus flap and nose wheel only when params say so.
- **B3 — Params-unavailable fallback** ✅ DONE
  Fixed wing no params: Aileron CH1, Elevator CH2, Rudder CH4.

### Phase C — Motor layout by kind (controller layer)

- **C1** — Multirotor: N-motor grid (FRAME_CLASS / CA_ROTOR_CNT). ✅ already
  built (`HardwareTestController` resolves counts + HEARTBEAT fallback).
- **C2** — Fixed wing: 1 motor labeled "Motor" not "M1". ⏳ label work in QML.

### Phase D — QML adaptive rendering (UI layer)

- **D1** — Control surface section hidden for MULTIROTOR. ✅ GimbalTest sweep
  card gated on `_isMultirotor`.
- **D2** — Airspeed card shown only for FIXED_WING / VTOL. ⏳ PENDING
- **D3** — Vehicle badge in Hardware header. ⏳ PENDING (same as A4).

### Phase E — Verification

Manual acceptance checklist per vehicle type (see Verify section below).

---

## Implementation

### Phase A1 — VehicleKind enum + resolution

In `VehicleProfileManager`, add a canonical kind enum:

```cpp
enum class VehicleKind {
    Multirotor,       // Quad, Hex, Octo, Tri
    FixedWing,        // Fixed wing, flying wing
    VtolConventional, // QuadPlane — kept so fixed-wing blocking rules extend to it
    Unknown
};
```

```cpp
VehicleKind VehicleProfileManager::kindFromMavType(MAV_TYPE t) {
    switch (t) {
    case MAV_TYPE_QUADROTOR:
    case MAV_TYPE_HEXAROTOR:
    case MAV_TYPE_OCTOROTOR:
    case MAV_TYPE_TRICOPTER:        return VehicleKind::Multirotor;
    case MAV_TYPE_FIXED_WING:       return VehicleKind::FixedWing;
    case MAV_TYPE_VTOL_FIXEDROTOR:
    case MAV_TYPE_VTOL_RESERVED5:   return VehicleKind::VtolConventional;
    default:                        return VehicleKind::Unknown;
    }
}
```

Add `Q_PROPERTY(QString vehicleKind READ vehicleKindString NOTIFY vehicleTypeResolved)`
returning: "MULTIROTOR", "FIXED_WING", "VTOL_CONVENTIONAL", "UNKNOWN".

The kind must be set on every resolution path in
`resolveVehicleTypeAndMotorCount()` (FRAME_CLASS, CA_AIRFRAME, and the
HEARTBEAT fallback) so it is never stale.

### Phase A2 — Kind-aware blocking rules

Apply when vehicleKind == MULTIROTOR (Quad/Hex/Octa/Tri):

```
airframe.motor_count         → BLOCKING  (wrong count = wrong mixing = instant crash)
propulsion.motors.spin       → BLOCKING  (non-responding motor = asymmetric thrust)
sensors.airspeed             → advisory  (WARN if present but failing, skip if absent)
safety.rtl_alt               → advisory
```

Apply when vehicleKind == FIXED_WING (and VTOL_CONVENTIONAL):

```
sensors.airspeed             → BLOCKING  (without it autopilot uses GPS groundspeed,
                                          fails in wind, stall is fatal)
safety.rtl_alt               → BLOCKING  (must clear terrain on return)
airframe.motor_count         → advisory
propulsion.motors.spin       → advisory
```

Apply when vehicleKind == UNKNOWN: leave defaults untouched.

For airspeed on a multirotor it must NEVER block arming. When no airspeed
sensor is present the check should report Skipped/Warning, never Failed.

**Advisory (never block):**
```
weather.wind                    — operator judgment
weather.metar_visibility        — environmental awareness
weather.metar_ceiling           — environmental awareness
weather.metar_precipitation     — environmental awareness
weather.metar_temperature       — environmental awareness
navigation.gps_speed_accuracy   — marginal GPS still flyable
navigation.home_position        — Stabilize flyable without home
hardware.imu_temperature        — cold warning, pilot decides
power.battery_temperature       — warn, not immediate risk
hardware.gimbal_link            — not safety critical
communication.video_feed        — not safety critical
hardware.vibration              — warn unless extreme
hardware.motor_temperature      — post-flight relevance
hardware.propeller_direction    — physical inspection, advisory
hardware.antenna_placement      — advisory
hardware.visual_inspection      — advisory
hardware.weight_balance         — advisory
hardware.battery_physical       — advisory
hardware.battery_temp_visual    — advisory
hardware.gps_antenna_condition  — advisory
```

### Phase B1 — ControlSurfaceTestController — SERVO_FUNCTION surface loading

In `loadSurfacesForVehicle()`, build the surface list from two sources
combined: the vehicle kind (structural) + actual SERVO_FUNCTION params
(what's actually wired on this specific aircraft).

**Source A — SERVO_FUNCTION params (ArduPilot)**
Read `SERVO1_FUNCTION` through `SERVO16_FUNCTION` from ParameterManager.
Map function ID → surface:

```cpp
// ArduPilot Plane 4.5 function IDs (verified against SRV_Channel.h):
//   2 = Flap, 4 = Aileron, 19 = Elevator, 21 = Rudder, 26 = Steering,
//   70 = Throttle (motor, not a surface), 77 = Elevon left, 78 = Elevon right.
static const QHash<int, QPair<QString,QString>> kFunctionMap = {
    { 4,  {"aileron",   "Aileron"}    },
    { 19, {"elevator",  "Elevator"}   },
    { 21, {"rudder",    "Rudder"}     },
    { 2,  {"flap",      "Flap"}       },
    { 26, {"steering",  "Nose Wheel"} },
    { 77, {"elevon_l",  "Left Elevon"}  },
    { 78, {"elevon_r",  "Right Elevon"} },
};
```

> **Elevons (77/78)** cannot be driven one-at-a-time through a single RC
> input: ArduPilot's `channel_function_mixer()` computes
> `elevon_left = (elevator − aileron)·MIXING_GAIN` and
> `elevon_right = (elevator + aileron)·MIXING_GAIN`. The controller instead
> overrides roll AND pitch together — opposite phase isolates the left elevon,
> equal phase isolates the right (the non-target elevon gets E∓A → centered).
> `MAV_CMD_DO_SET_SERVO` is not usable on ArduPilot: it rejects any output
> whose `SERVOn_FUNCTION` is assigned ("channel already in use").

For each SERVO channel: if its FUNCTION value is in `kFunctionMap`,
add that surface to the list with channel number recorded.

### Phase B2 — Vehicle-kind filter

After building the param-derived list, filter by vehicle kind:

```cpp
switch (kind) {
case Multirotor:
    // surface list stays empty — motor tests only
    break;

case FixedWing:
    // keep: aileron, elevator, rudder, flap, steering (nose wheel if present)
    // (tilt/swashplate/tail rotor are out of scope)
    break;

case VtolConventional:
    // same as fixed wing for now
    break;

case Unknown:
    // fall through to fixed wing defaults (most comprehensive set)
    break;
}
```

### Phase B3 — Fallbacks when params unavailable

- Fixed wing no params: add Aileron CH1, Elevator CH2, Rudder CH4.

---

### Phase C — Motor test layout by vehicle kind

`HardwareTestController` already resolves motor counts from FRAME_CLASS /
CA_ROTOR_CNT with a HEARTBEAT fallback (see `resolveMotorCount` /
`_resolveMotorCount`). Verify:

```
Multirotor:   N motors in Quad-X / Hex / Octo grid layout  (already built)
Fixed Wing:   1 motor, label "Motor" not "M1"              (label work in QML)
```

---

### Phase D — QML adaptive panel rendering

**D1 — Control surface section hidden for multirotor:**

```qml
// PreflightChecklistView / GimbalTest — surface card
visible: root._vehicleKind !== "MULTIROTOR"
```

**D2 — Airspeed card only for flight vehicles:**

```qml
Loader {
    active: ["FIXED_WING", "VTOL_CONVENTIONAL"].includes(_vehicleKind)
    sourceComponent: AirspeedCheckCard {}
}
```

**D3 — Vehicle badge in Hardware header (Phase A4):**

```qml
Row {
    Label { text: "Hardware" }
    Rectangle {
        radius: 4
        color: Colors.surface2
        Label {
            text: VehicleProfileManager.vehicleKind
            color: Colors.accent
            font.pixelSize: Config.fontSizeSmall
        }
    }
}
```

If vehicle kind is "UNKNOWN": show an amber warning label
"Vehicle type unrecognised — showing default tests".

---

## Verify

**Quadrotor:**
- [ ] Hardware section shows vehicle badge "MULTIROTOR"
- [ ] Motor grid shows 4 buttons (M1–M4) in Quad-X layout
- [ ] No aileron/elevator/rudder/flap/steering rows anywhere
- [ ] No airspeed check card
- [ ] motor_count + motors.spin block arming; airspeed/rtl_alt do NOT

**Fixed Wing:**
- [ ] Hardware badge shows "FIXED_WING"
- [ ] Motor section shows 1 button labeled "Motor"
- [ ] Surface section shows Aileron, Elevator, Rudder
- [ ] Flap row present if SERVO5_FUNCTION=14 in params, absent otherwise
- [ ] Nose Wheel row present if SERVO7_FUNCTION=26, absent otherwise
- [ ] Airspeed check present and marked BLOCKING
- [ ] motor_count + motors.spin do NOT block arming

**Params drive optional surfaces:**
- [ ] Set SERVO5_FUNCTION=14 (Flap) in FC → Flap row appears
- [ ] Set SERVO5_FUNCTION=0 (disabled) → Flap row gone
- [ ] No restart needed — resolves at connect time during PARAM_LOADING

```bash
git status --porcelain | grep -v "^?? custom/" | grep -v "^?? build/"
# Expected: empty
```
