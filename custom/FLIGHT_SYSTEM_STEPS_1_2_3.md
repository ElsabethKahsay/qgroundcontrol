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

- **A1 — VehicleKind enum + resolution** (`VehicleProfileManager`)
  Add a `VehicleKind` enum (Multirotor / FixedWing / VtolConventional /
  Unknown), a `kindFromMavType(MAV_TYPE)` mapper, a `kindFromTypeString()`
  mapper, and a `vehicleKind` Q_PROPERTY exposed to QML.
- **A2 — Kind-aware blocking rules** (`PreflightManager`)
  Implement the previously-declared `updateCriticalChecks()`: per-kind
  `setMandatory()`/blocking decisions. Quad blocks on motor count + motor
  spin; FixedWing blocks on airspeed + RTL alt. Never let airspeed block a
  quad.
- **A3 — Wiring** (`PreflightPlugin`)
  Connect `VehicleProfileManager::vehicleTypeResolved` → the kind-aware
  blocking filter and re-evaluate.
- **A4 — Badge + UNKNOWN warning** (QML, small)
  Hardware header shows the resolved kind badge; UNKNOWN shows amber warning
  and falls back to the most comprehensive surface set.

### Phase B — Control surfaces by kind (controller layer)

- **B1 — SERVO_FUNCTION param map** (`ControlSurfaceTestController`)
  Read `SERVO1_FUNCTION`…`SERVO16_FUNCTION` and map function ID → surface
  (ailerons, elevator(s), rudder, flap, steering/nose wheel).
- **B2 — Vehicle-kind filter**
  Multirotor → surface list is empty. Fixed wing → aileron/elevator/rudder,
  plus flap and nose wheel only when params say so.
- **B3 — Params-unavailable fallback**
  Fixed wing no params: Aileron CH1, Elevator CH2, Rudder CH4.

### Phase C — Motor layout by kind (controller layer)

- **C1** — Multirotor: N-motor grid (FRAME_CLASS / CA_ROTOR_CNT). *(already
  built — verify)*
- **C2** — Fixed wing: 1 motor labeled "Motor" not "M1" assigning should be done dynamically no hardcoding .

### Phase D — QML adaptive rendering (UI layer)

- **D1** — Control surface section hidden for MULTIROTOR.
- **D2** — Airspeed card shown only for FIXED_WING / VTOL.
- **D3** — Vehicle badge in Hardware header.

### Phase E — Verification

Manual acceptance checklist per vehicle type (see Verify section below).

---

## Implementation

### Phase A1 — VehicleKind enum + resolution

In `VehicleProfileManager`, add a canonical kind enum:

```cpp
enum class VehicleKind {
    Multirotor,       // Quad, 
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
static const QHash<int, QPair<QString,QString>> kFunctionMap = {
    { 4,  {"aileron_l",   "Left Aileron"}  },
    { 19, {"aileron_r",   "Right Aileron"} },
    { 2,  {"elevator",    "Elevator"}      },
    { 21, {"elevator_r",  "Right Elevator"}},
    { 6,  {"rudder",      "Rudder"}        },
    { 14, {"flap_l",      "Flap"}          },
    { 26, {"steering",    "Nose Wheel"}    },
};
```

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




# Flight History Page — Complete Overhaul

All changes under `custom/` only. Implement every section.
Build must be clean after each part before starting the next.

---

## Part 1 — Data Integrity and Persistence

### 1.1 Crash recovery — orphaned sessions

If the app crashes or loses power mid-flight, `flights.ended_at` is
never written. On next launch, those sessions stay open forever and
corrupt statistics.

In `PreflightPlugin` or `DatabaseManager` constructor, on startup:

```cpp
void DatabaseManager::recoverOrphanedSessions() {
    // Any session with armed_at set but ended_at null is interrupted
    QSqlQuery q(m_db);
    q.prepare(R"(
        UPDATE flights
        SET ended_at = datetime('now'),
            duration_sec = CAST(
                (julianday('now') - julianday(armed_at)) * 86400 AS INTEGER),
            notes = COALESCE(notes || ' ', '') || '[Session interrupted — app closed mid-flight]'
        WHERE ended_at IS NULL
          AND armed_at IS NOT NULL
    )");
    q.exec();

    // Sessions never armed (pre-flight only, app closed) — just close them
    q.prepare(R"(
        UPDATE flights
        SET ended_at = started_at,
            duration_sec = 0,
            notes = COALESCE(notes || ' ', '') || '[Session interrupted — closed before arm]'
        WHERE ended_at IS NULL
          AND armed_at IS NULL
    )");
    q.exec();

    int count = q.numRowsAffected();
    if (count > 0)
        qCWarning(dbLog) << "Recovered" << count << "orphaned flight sessions";
}
```

Call `recoverOrphanedSessions()` in `DatabaseManager` constructor after
schema migration completes, before any other DB operation.

### 1.2 Wrap multi-table writes in transactions

Anywhere multiple tables are written for one logical event, use a
transaction. Find all places in `FlightSession` and `TelemetryEventLogger`
where multiple inserts happen and wrap them:

```cpp
// Example: on arm — write ARM event + update flights.armed_at
m_db.transaction();
bool ok = true;
ok &= setFlightArmedAt(flightId, now);
ok &= insertTelemetryEvent(flightId, "ARM", "", battV, alt, gps, mode);
if (ok) m_db.commit();
else    m_db.rollback();
```

Specifically wrap:
- Arm event (flights update + telemetry event)
- Disarm event (flights update + telemetry event + close session)
- Post-flight check completion (post_checklist_complete flag + check results)
- Zone compliance acknowledgment (compliance log + check result update)

### 1.3 Foreign key enforcement

SQLite foreign keys are OFF by default. Enable them:

```cpp
// In DatabaseManager::_openDatabase(), after opening:
QSqlQuery pragma(m_db);
pragma.exec("PRAGMA foreign_keys = ON");
pragma.exec("PRAGMA journal_mode = WAL");  // write-ahead logging — safer on Jetson
pragma.exec("PRAGMA synchronous = NORMAL"); // balance safety vs speed
```

`WAL` mode prevents DB corruption if the Jetson loses power mid-write.
`PRAGMA integrity_check` on every launch (log result, don't crash):

```cpp
QSqlQuery ic(m_db);
ic.exec("PRAGMA integrity_check");
ic.next();
QString result = ic.value(0).toString();
if (result != "ok")
    qCCritical(dbLog) << "DB integrity check FAILED:" << result;
else
    qCDebug(dbLog) << "DB integrity check: ok";
```

### 1.4 Detailed telemetry snapshot columns

Add to `flight_telemetry_events` if not present:

```sql
ALTER TABLE flight_telemetry_events ADD COLUMN flight_mode  TEXT DEFAULT '';
ALTER TABLE flight_telemetry_events ADD COLUMN latitude     REAL DEFAULT 0;
ALTER TABLE flight_telemetry_events ADD COLUMN longitude    REAL DEFAULT 0;
ALTER TABLE flight_telemetry_events ADD COLUMN hdop         REAL DEFAULT 0;
ALTER TABLE flight_telemetry_events ADD COLUMN vertical_speed REAL DEFAULT 0;
ALTER TABLE flight_telemetry_events ADD COLUMN heading_deg  REAL DEFAULT 0;
```

Add to `flights` if not present:

```sql
ALTER TABLE flights ADD COLUMN max_ground_speed_ms   REAL DEFAULT 0;
ALTER TABLE flights ADD COLUMN max_vertical_speed_ms REAL DEFAULT 0;
ALTER TABLE flights ADD COLUMN distance_flown_m      REAL DEFAULT 0;
ALTER TABLE flights ADD COLUMN avg_battery_v         REAL DEFAULT 0;
ALTER TABLE flights ADD COLUMN check_pass_count      INTEGER DEFAULT 0;
ALTER TABLE flights ADD COLUMN check_fail_count      INTEGER DEFAULT 0;
ALTER TABLE flights ADD COLUMN check_warn_count      INTEGER DEFAULT 0;
ALTER TABLE flights ADD COLUMN anomaly_count         INTEGER DEFAULT 0;
-- anomaly = any CHECK_DEGRADED or BATTERY_WARN event during flight
```

### 1.5 Populate flight summary on close

In `FlightSession::closeSession()`, compute and write all summary fields:

```cpp
void FlightSession::closeSession() {
    // ... existing close logic ...

    // Compute check counts from flight_check_results
    auto counts = DatabaseManager::instance().getCheckCountsForFlight(m_flightId);
    // Compute anomaly count from flight_telemetry_events
    int anomalies = DatabaseManager::instance().getAnomalyCountForFlight(m_flightId);

    DatabaseManager::instance().closeFlight(
        m_flightId, durationSec,
        m_maxAltitude, m_minBatteryV, m_maxBatteryV,
        m_modeChanges, m_maxGroundSpeed, m_maxVerticalSpeed,
        m_distanceFlown, m_avgBatteryV,
        counts.pass, counts.fail, counts.warn, anomalies
    );
}
```

`TelemetryEventLogger` accumulates `m_maxGroundSpeed`, `m_distanceFlown`
(sum of position deltas using Haversine), `m_avgBatteryV` (running mean)
during the flight. Pass these to `FlightSession::onDisarmedWithStats()`.

---

## Part 2 — FlightHistoryModel (C++)

**File:** `custom/src/models/FlightHistoryModel.h/.cpp`

```cpp
class FlightHistoryModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    // Filter state — applied on reload()
    Q_PROPERTY(QString filterFromDate WRITE setFilterFromDate)
    Q_PROPERTY(QString filterToDate   WRITE setFilterToDate)
    Q_PROPERTY(int     filterVehicleId WRITE setFilterVehicleId)
    Q_PROPERTY(int     filterOperatorId WRITE setFilterOperatorId)
    Q_PROPERTY(QString filterMode     WRITE setFilterMode)
    // mode filter: "" = all, "FLIGHT", "TRAINING", "TESTING"
    Q_PROPERTY(QString filterSearch   WRITE setFilterSearch)
    // search: matches purpose, location, operator name, vehicle name
    Q_PROPERTY(QString sortBy         WRITE setSortBy)
    // sort: "date_desc"(default), "date_asc", "duration_desc",
    //       "operator", "vehicle", "pass_rate_desc"

    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    // total matching records (ignoring pagination)
    Q_PROPERTY(int pageSize   READ pageSize   CONSTANT)
    // fixed at 50 rows per page
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)

public:
    // Roles
    enum Role {
        FlightIdRole = Qt::UserRole + 1,
        DateRole, OperatorNameRole, VehicleNameRole,
        ModeRole, PurposeRole, LocationRole,
        DurationSecRole, DurationStrRole,
        PrePassRateRole, AnomalyCountRole,
        ArmedAtRole, IsCompleteRole,
        MaxAltRole, MinBattRole
    };

    Q_INVOKABLE void reload();
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void prevPage();
    Q_INVOKABLE void goToPage(int page);
    Q_INVOKABLE QVariantMap getFlightDetail(int flightId);
    // Returns full flight record + all sub-records as nested lists
};
```

`getFlightDetail(flightId)` returns a QVariantMap with keys:
```
"flight"         → the flights row
"checks_pre"     → list of pre-flight check results grouped by category
"checks_post"    → list of post-flight check results
"events"         → list of telemetry events in chronological order
"handovers"      → list of trainer handover events (training sessions)
"motor_tests"    → list of motor test results
"surface_tests"  → list of surface test results
"compliance"     → list of zone compliance records
```

---

## Part 3 — Flight History Page UI

**File:** `custom/qml/pages/FlightHistoryPage.qml` — rewrite the filter
and list sections.

### 3.1 Filter bar (sticky, above the list)

```qml
Rectangle {
    // Filter bar — does not scroll
    id: filterBar
    width: parent.width
    height: 56
    color: Colors.surface2

    RowLayout {
        anchors { fill: parent; margins: 12 }
        spacing: 8

        // Date range
        Label { text: "From" }
        TextField {
            id: fromField; width: 110
            placeholderText: "YYYY-MM-DD"
            onTextChanged: historyModel.filterFromDate = text
        }
        Label { text: "To" }
        TextField {
            id: toField; width: 110
            placeholderText: "YYYY-MM-DD"
            onTextChanged: historyModel.filterToDate = text
        }

        // Vehicle filter
        ComboBox {
            id: vehiclePicker
            width: 160
            model: vehicleFilterModel  // [{id:-1,name:"All Vehicles"}, ...vehicles]
            textRole: "name"
            onCurrentIndexChanged:
                historyModel.filterVehicleId = model[currentIndex].id
        }

        // Mode filter
        ComboBox {
            width: 120
            model: ["All Modes", "FLIGHT", "TRAINING", "TESTING"]
            onCurrentIndexChanged:
                historyModel.filterMode = currentIndex === 0 ? "" : currentText
        }

        // Search
        TextField {
            Layout.fillWidth: true
            placeholderText: "Search purpose, location, operator..."
            onTextChanged: historyModel.filterSearch = text
        }

        // Sort
        ComboBox {
            width: 160
            model: ["Newest First", "Oldest First", "Longest First",
                    "By Operator", "By Vehicle", "By Pass Rate"]
            property var sorts: ["date_desc","date_asc","duration_desc",
                                  "operator","vehicle","pass_rate_desc"]
            onCurrentIndexChanged: historyModel.sortBy = sorts[currentIndex]
        }

        // Refresh
        Button {
            text: "↻"
            ToolTip.text: "Refresh"
            ToolTip.visible: hovered
            onClicked: historyModel.reload()
        }
    }
}
```

### 3.2 Result count and pagination bar

```qml
Row {
    spacing: 16
    Label {
        text: historyModel.totalCount + " flights"
              + (historyModel.totalCount !== historyModel.rowCount()
                 ? " (showing " + historyModel.rowCount() + ")"
                 : "")
        color: Colors.textSecondary
    }

    // Pagination
    Row {
        visible: historyModel.totalCount > historyModel.pageSize
        spacing: 4
        Button { text: "‹"; onClicked: historyModel.prevPage()
                 enabled: historyModel.currentPage > 0 }
        Label {
            text: "Page " + (historyModel.currentPage + 1)
                  + " of " + Math.ceil(historyModel.totalCount / historyModel.pageSize)
        }
        Button { text: "›"; onClicked: historyModel.nextPage()
                 enabled: (historyModel.currentPage + 1) * historyModel.pageSize
                           < historyModel.totalCount }
    }
}
```

### 3.3 Flight list row

```qml
delegate: Rectangle {
    width: parent.width
    height: 72
    color: index % 2 === 0 ? Colors.surface1 : Colors.surface2

    RowLayout {
        anchors { fill: parent; margins: 12 }

        // Index number
        Label {
            text: (historyModel.currentPage * historyModel.pageSize + index + 1) + "."
            color: Colors.textSecondary
            font.pixelSize: Config.fontSizeSmall
            width: 32
        }

        // Mode badge
        Rectangle {
            width: 70; height: 22; radius: 4
            color: {
                if (model.mode === "FLIGHT")   return "#1A3A2A"
                if (model.mode === "TRAINING") return "#1A2A4A"
                return "#2A2A1A"  // TESTING
            }
            Label {
                anchors.centerIn: parent
                text: model.mode
                color: {
                    if (model.mode === "FLIGHT")   return Colors.statePass
                    if (model.mode === "TRAINING") return Colors.accent
                    return Colors.stateWarn
                }
                font.pixelSize: Config.fontSizeSmall
                font.bold: true
            }
        }

        // Core info
        Column {
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: model.vehicleName + "  ·  " + model.operatorName
                font.bold: true
            }
            Label {
                text: model.purpose + (model.location.length > 0
                      ? "  ·  " + model.location : "")
                color: Colors.textSecondary
                font.pixelSize: Config.fontSizeSmall
                elide: Text.ElideRight
                width: parent.width
            }
        }

        // Stats
        Column {
            spacing: 2
            horizontalItemAlignment: Qt.AlignRight
            Label { text: model.durationStr }
            Label {
                text: model.prePassRate + "% pre"
                color: model.prePassRate >= 90 ? Colors.statePass : Colors.stateWarn
                font.pixelSize: Config.fontSizeSmall
            }
        }

        // Anomaly indicator
        Rectangle {
            visible: model.anomalyCount > 0
            width: 24; height: 24; radius: 12
            color: Colors.stateFail
            Label {
                anchors.centerIn: parent
                text: model.anomalyCount
                color: "white"
                font.pixelSize: Config.fontSizeSmall
                font.bold: true
            }
            ToolTip.text: model.anomalyCount + " in-flight anomaly(s)"
            ToolTip.visible: hovered
        }

        // Date
        Label {
            text: model.date
            color: Colors.textSecondary
            font.pixelSize: Config.fontSizeSmall
            width: 90
            horizontalAlignment: Text.AlignRight
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: detailPanel.load(model.flightId)
    }
}
```

### 3.4 Detail panel — full flight log

When a row is tapped, the right 60% of the page shows:

```
┌─────────────────────────────────────────────────────────┐
│  Flight #12  ·  Fixed Wing-1-a3f91b  ·  08 Aug 2026    │
│  Lisabeth  ·  Test Flight  ·  Bole Field                │
│  Duration: 4m 23s  ·  Max Alt: 45m  ·  Min Batt: 11.2V │
├─────────────────────────────────────────────────────────┤
│  PRE-FLIGHT CHECKS    [expand/collapse per category]    │
│  Power       ✓ 3/3   Navigation  ✓ 6/6  Comm  ⚠ 2/3   │
│  [expand Communication → shows RC Calibration: WARN]   │
├─────────────────────────────────────────────────────────┤
│  FLIGHT EVENTS        [chronological]                   │
│  10:23:15  ARM         Batt: 12.6V  Alt: 0m  GPS: 9    │
│  10:23:42  MODE_CHANGE → FBWA                           │
│  10:25:11  BATTERY_WARN  Batt: 11.4V                    │
│  10:27:38  DISARM      Batt: 11.2V  Duration: 4m 23s   │
├─────────────────────────────────────────────────────────┤
│  POST-FLIGHT CHECKS                                     │
│  Motor Temp ✓  Battery Temp ✓  Frame Condition: pending │
├─────────────────────────────────────────────────────────┤
│  MOTOR TESTS                                            │
│  Motor 1: PASS 1150µs  Motor 2: PASS  [etc]            │
├─────────────────────────────────────────────────────────┤
│  ZONE COMPLIANCE                                        │
│  3 zones checked  ·  0 intersections  ·  All Clear ✓   │
└─────────────────────────────────────────────────────────┘
```

Each section is a `CollapsibleSection` component. All collapsed by default,
user expands what they need.

---

## Part 4 — Export with dual filter (date + vehicle)

### 4.1 Export button placement

Add to filter bar, after Refresh button:
```qml
Button {
    text: "⬇ Export CSV"
    onClicked: exportMenu.open()
}

Menu {
    id: exportMenu
    MenuItem {
        text: "Export current view (with filters)"
        onTriggered: {
            var path = DatabaseManager.exportFlightsCsv(
                fromField.text, toField.text,
                vehiclePicker.model[vehiclePicker.currentIndex].id,
                historyModel.filterMode)
            exportFeedback(path)
        }
    }
    MenuItem {
        text: "Export detailed log for selected flight"
        enabled: detailPanel.currentFlightId > 0
        onTriggered: {
            var path = DatabaseManager.exportFlightDetailCsv(
                detailPanel.currentFlightId)
            exportFeedback(path)
        }
    }
    MenuItem {
        text: "Export all flights (no filter)"
        onTriggered: {
            var path = DatabaseManager.exportFlightsCsv("", "", -1, "")
            exportFeedback(path)
        }
    }
}
```

### 4.2 `DatabaseManager::exportFlightsCsv`

```cpp
QString DatabaseManager::exportFlightsCsv(
    const QString& fromDate, const QString& toDate,
    int vehicleId, const QString& mode)
{
    QString filename = "skywin_flights_"
                     + QDateTime::currentDateTime()
                           .toString("yyyyMMdd_HHmmss")
                     + ".csv";
    QString path = QStandardPaths::writableLocation(
                       QStandardPaths::DocumentsLocation) + "/" + filename;

    // Build SQL with applied filters (same WHERE logic as FlightHistoryModel)
    QString sql = R"(
        SELECT f.id, o.name as operator, v.display_name as vehicle,
               f.session_mode as mode, f.purpose, f.location,
               f.started_at, f.armed_at, f.disarmed_at, f.ended_at,
               f.duration_sec, f.max_altitude_m, f.min_battery_v,
               f.max_battery_v, f.flight_mode_changes, f.check_pass_count,
               f.check_fail_count, f.check_warn_count, f.anomaly_count,
               f.weather_summary, f.notes
        FROM flights f
        LEFT JOIN operators o ON f.operator_id = o.id
        LEFT JOIN vehicles  v ON f.vehicle_id  = v.id
        WHERE 1=1
    )";
    if (!fromDate.isEmpty()) sql += " AND f.started_at >= '" + fromDate + "'";
    if (!toDate.isEmpty())   sql += " AND f.started_at <= '" + toDate + " 23:59:59'";
    if (vehicleId > 0)       sql += " AND f.vehicle_id = " + QString::number(vehicleId);
    if (!mode.isEmpty())     sql += " AND f.session_mode = '" + mode + "'";
    sql += " ORDER BY f.started_at DESC";

    _writeCsvFile(path, sql, {
        "ID","Operator","Vehicle","Mode","Purpose","Location",
        "Started","Armed","Disarmed","Ended","Duration(sec)",
        "MaxAlt(m)","MinBatt(V)","MaxBatt(V)","ModeChanges",
        "CheckPass","CheckFail","CheckWarn","Anomalies",
        "Weather","Notes"
    });

    return path;
}
```

### 4.3 `DatabaseManager::exportFlightDetailCsv`

Exports every event, check result, and test for ONE flight:

```cpp
QString DatabaseManager::exportFlightDetailCsv(int flightId)
{
    // Creates a multi-section CSV:
    // Section 1: Flight metadata (single row)
    // Section 2: Pre-flight checks (one row per check)
    // Section 3: Telemetry events (one row per event)
    // Section 4: Post-flight checks
    // Section 5: Motor test results
    // Section 6: Zone compliance

    // Separate sections with blank lines and section headers
    // e.g. "=== PRE-FLIGHT CHECKS ===" as a row
}
```

---

## Part 5 — Flight Statistics Dashboard

Add a stats banner above the filter bar:

```qml
Rectangle {
    width: parent.width; height: 60
    color: Colors.surface2

    RowLayout {
        anchors { fill: parent; margins: 16 }
        spacing: 32

        StatBox { label: "Total Flights";  value: stats.totalFlights }
        StatBox { label: "Total Hours";    value: stats.totalHoursStr }
        StatBox { label: "Avg Pass Rate";  value: stats.avgPassRate + "%" }
        StatBox { label: "Anomaly Rate";
                  value: stats.anomalyRate + "%"
                  valueColor: stats.anomalyRate > 10 ? Colors.stateWarn
                                                     : Colors.statePass }
        StatBox { label: "Vehicles";       value: stats.vehicleCount }
        StatBox { label: "Operators";      value: stats.operatorCount }

        Item { Layout.fillWidth: true }

        // Stats update when filters change
        Label { text: "stats reflect current filter"
                color: Colors.textSecondary
                font.pixelSize: Config.fontSizeSmall }
    }
}
```

`StatBox` is a small column: label in small grey text, value in large white text.

Add `DatabaseManager::getFlightStats(fromDate, toDate, vehicleId, mode)`
returning a QVariantMap with all the above fields. Call on filter change.

---

## Part 6 — Additional reliability improvements

### 6.1 DB backup on launch

Before any migration:
```cpp
void DatabaseManager::_backupBeforeMigration() {
    QString src  = m_dbPath;
    QString dest = m_dbPath + ".bak";
    if (QFile::exists(dest)) QFile::remove(dest);
    QFile::copy(src, dest);
    qCInfo(dbLog) << "DB backed up to" << dest;
}
```

### 6.2 Periodic WAL checkpoint

On a 5-minute timer while the app is running:
```cpp
QTimer::singleShot(300000, this, [this]() {
    QSqlQuery q(m_db);
    q.exec("PRAGMA wal_checkpoint(PASSIVE)");
});
```

### 6.3 Flight list empty state

When no flights match the filter — not blank white space:
```qml
Item {
    visible: historyModel.rowCount() === 0
    anchors.centerIn: parent
    Column {
        anchors.centerIn: parent
        spacing: 12
        Label { text: "📋"; font.pixelSize: 48
                anchors.horizontalCenter: parent.horizontalCenter }
        Label { text: historyModel.filterFromDate.length > 0
                      || historyModel.filterVehicleId > 0
                      ? "No flights match the current filter"
                      : "No flights recorded yet"
                color: Colors.textSecondary
                anchors.horizontalCenter: parent.horizontalCenter }
        Button {
            visible: historyModel.filterFromDate.length > 0
                  || historyModel.filterVehicleId > 0
            text: "Clear Filters"
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: { fromField.text = ""; toField.text = "";
                          vehiclePicker.currentIndex = 0 }
        }
    }
}
```

---

## Verify

**Data integrity:**
```bash
# After connecting, arming, disarming, disconnecting:
sqlite3 ... "PRAGMA integrity_check;"  # → ok

sqlite3 ... "SELECT id, duration_sec, check_pass_count,
                    anomaly_count, ended_at
             FROM flights ORDER BY id DESC LIMIT 3;"
# Expected: ended_at non-null, duration_sec > 0, counts populated

# Crash recovery test: kill -9 the app while armed
# Relaunch — orphaned session should be closed with [interrupted] note
sqlite3 ... "SELECT notes FROM flights ORDER BY id DESC LIMIT 1;"
# Expected: contains '[Session interrupted'
```

**Filter and export:**
- [ ] Date filter: set From=today → only today's flights shown
- [ ] Vehicle filter: select one vehicle → only that vehicle's flights
- [ ] Mode filter: select TRAINING → only training sessions
- [ ] Search: type an operator name → matches correctly
- [ ] "Export current view" → CSV file with only filtered rows
- [ ] "Export detailed log" → multi-section CSV for selected flight
- [ ] Pagination: with 60+ flights → page 1 shows 50, page 2 shows rest
- [ ] Stats update when filter changes

**Detail panel:**
- [ ] Tap a flight → detail loads without crash
- [ ] Pre-flight checks grouped by category, collapse/expand works
- [ ] Telemetry events show in chronological order
- [ ] Anomaly count in list row matches events in detail panel

**Refresh:**
- [ ] Complete a flight, tap Refresh → new row appears without restarting

```bash
git status --porcelain | grep -v "^?? custom/" | grep -v "^?? build/"
# Expected: empty
```