# Flight & Training Session System — Verification Checklist
## Steps 1 through 10 — Complete Acceptance Test

Run every item in order. Do not skip sections.
A single FAIL in Part A or B blocks all further testing.
Record pass/fail and exact observed behavior for every item.

---

## PART A — Build and Static Verification

Must pass before launching the app.

**A.1 Clean build**
```bash
cmake --build build --config Debug -j$(nproc) 2>&1 | grep "error:"
```
- [ ] Output is empty — zero build errors

**A.2 No unresolved Q_PROPERTY references**
```bash
grep -rn "FlightSession\.\|OperatorManager\." custom/qml/ --include="*.qml" \
  | sed 's/.*\.\([a-zA-Z]*\).*/\1/' | sort -u
```
Cross-reference every property name found against `FlightSession.h` and
`OperatorManager.h` Q_PROPERTY declarations.
- [ ] Every QML property access has a matching Q_PROPERTY declaration

**A.3 New files registered in CMake and QRC**
```bash
grep -n "FlightSession\|OperatorManager\|TelemetryEventLogger\|FlightHistoryModel" \
  custom/CMakeLists.txt
grep -n "SessionStartDialog\|FlightFormDialog\|FlightHistoryPage" \
  custom/custom.qrc
```
- [ ] All 4 C++ classes appear in CMakeLists.txt
- [ ] All 3 QML files appear in custom.qrc

**A.4 Nothing outside custom/ was modified**
```bash
git status --porcelain | grep -v "^?? custom/" | grep -v "^?? build/"
```
- [ ] Output is empty

**A.5 Unit tests still pass**
```bash
export QT_QPA_PLATFORM=offscreen
./build/Debug/QGroundControlTests 2>&1 | tail -5
```
- [ ] Final line reads "OK (N tests)" — zero failures

---

## PART B — Database Verification

Run before launching the app.

**B.1 All four new tables exist**
```bash
sqlite3 ~/.config/QGroundControl.org/QGroundControl.db \
  "SELECT name FROM sqlite_master WHERE type='table'
   AND name IN ('operators','flights','flight_check_results',
                'flight_telemetry_events');"
```
- [ ] All 4 table names returned

**B.2 Schema is correct — operators**
```bash
sqlite3 ~/.config/QGroundControl.org/QGroundControl.db \
  "PRAGMA table_info(operators);"
```
- [ ] Columns: id, name, role, created_at, total_flights, total_training

**B.3 Schema is correct — flights**
```bash
sqlite3 ~/.config/QGroundControl.org/QGroundControl.db \
  "PRAGMA table_info(flights);"
```
- [ ] Columns: id, operator_id, vehicle_id, mode, purpose, location,
      notes, weather_summary, pre_checklist_complete,
      post_checklist_complete, started_at, armed_at, disarmed_at,
      ended_at, duration_sec, max_altitude_m, min_battery_v,
      max_battery_v, flight_mode_changes

**B.4 Schema is correct — flight_check_results**
```bash
sqlite3 ~/.config/QGroundControl.org/QGroundControl.db \
  "PRAGMA table_info(flight_check_results);"
```
- [ ] Columns: id, flight_id, check_id, category, is_post_flight,
      status, message, confirmed_by, evaluated_at

**B.5 Schema is correct — flight_telemetry_events**
```bash
sqlite3 ~/.config/QGroundControl.org/QGroundControl.db \
  "PRAGMA table_info(flight_telemetry_events);"
```
- [ ] Columns: id, flight_id, event_type, triggered_by, battery_v,
      altitude_m, gps_sats, flight_mode, timestamp

**B.6 Schema version incremented**
```bash
sqlite3 ~/.config/QGroundControl.org/QGroundControl.db \
  "PRAGMA user_version;"
```
- [ ] Returns a value higher than the version before this implementation

**B.7 DB integrity**
```bash
sqlite3 ~/.config/QGroundControl.org/QGroundControl.db \
  "PRAGMA integrity_check;"
```
- [ ] Returns: ok

**B.8 Migration runs on existing DB**
Delete the DB file, launch the app once (to create fresh), then verify
all 4 tables exist. Confirm the app does not crash on first run.
- [ ] Fresh DB created successfully with all tables

---

## PART C — OperatorManager (Step 2)

Launch app with Mock Link. Open QML debug console.

**C.1 Singleton accessible from QML**
```js
console.log(typeof OperatorManager)
```
- [ ] Returns "object" — not undefined

**C.2 Empty state on fresh DB**
```js
console.log(OperatorManager.hasCurrentOperator)  // false
console.log(OperatorManager.operators.length)    // 0
console.log(OperatorManager.currentOperatorId)   // -1
```
- [ ] All three match expected values

**C.3 Add operator**
```js
var id = OperatorManager.addOperator("Lisabeth", "Pilot")
console.log(id)                                   // > 0
console.log(OperatorManager.operators.length)     // 1
```
- [ ] id is a positive integer
- [ ] operators list length increased to 1

**C.4 Operator persists in DB**
```bash
sqlite3 ~/.config/QGroundControl.org/QGroundControl.db \
  "SELECT id, name, role FROM operators;"
```
- [ ] Row exists with correct name and role

**C.5 Select operator**
```js
OperatorManager.selectOperator(1)
console.log(OperatorManager.currentOperatorName)  // "Lisabeth"
console.log(OperatorManager.hasCurrentOperator)   // true
console.log(OperatorManager.currentOperatorRole)  // "Pilot"
```
- [ ] All three match expected values

**C.6 Selection persists across restart**
Close and relaunch app. Without selecting anything:
```js
console.log(OperatorManager.currentOperatorName)  // "Lisabeth"
console.log(OperatorManager.currentOperatorId)    // 1
```
- [ ] Last selected operator restored from QSettings

**C.7 Duplicate name rejected**
```js
var id2 = OperatorManager.addOperator("Lisabeth", "Trainee")
console.log(id2)  // -1
console.log(OperatorManager.operators.length)  // still 1
```
- [ ] Duplicate rejected, list unchanged

**C.8 Empty name rejected**
```js
var id3 = OperatorManager.addOperator("", "Pilot")
console.log(id3)  // -1
```
- [ ] Empty name rejected

---

## PART D — FlightSession (Step 3)

**D.1 Singleton accessible**
```js
console.log(typeof FlightSession)  // "object"
console.log(FlightSession.mode)    // 0 (None)
console.log(FlightSession.state)   // 0 (Idle)
```
- [ ] Singleton accessible, correct initial state

**D.2 Vehicle connect triggers AwaitingModeSelect**
Connect Mock Link. Immediately check:
```js
console.log(FlightSession.state)  // 2 (AwaitingModeSelect)
```
- [ ] State changed to AwaitingModeSelect on connect

**D.3 Training session start**
```js
FlightSession.startTrainingSession()
console.log(FlightSession.isTraining)      // true
console.log(FlightSession.isFlight)        // false
console.log(FlightSession.armingPermitted) // false — always in training
console.log(FlightSession.currentFlightId) // -1 — no DB record
```
- [ ] All four match expected values
- [ ] No row created in flights table:
  ```bash
  sqlite3 ... "SELECT COUNT(*) FROM flights;"
  ```
  Result: 0

**D.4 Arming blocked in training mode**
Attempt arm via Mock Link toolbar:
- [ ] Arm command blocked — zero MAV_CMD_COMPONENT_ARM_DISARM in
      MAVLink Inspector
- [ ] ArmingGate log shows "training mode" block message

**D.5 Session close on disconnect**
Disconnect Mock Link:
```js
console.log(FlightSession.state)  // 0 (Idle)
console.log(FlightSession.mode)   // 0 (None)
```
- [ ] State and mode reset on disconnect

**D.6 Flight session start**
Reconnect Mock Link. Ensure operator is selected. Call:
```js
FlightSession.startFlightSession("Test Flight", "Bole Field", "Test notes")
console.log(FlightSession.isFlight)        // true
console.log(FlightSession.currentFlightId) // > 0
console.log(FlightSession.formComplete)    // true
console.log(FlightSession.state)           // 3 (PreFlight)
```
- [ ] All four match expected values
- [ ] Row exists in flights table:
  ```bash
  sqlite3 ... "SELECT id, mode, purpose, location FROM flights;"
  ```
  Result: 1 row with mode=FLIGHT, correct purpose and location

**D.7 armingPermitted gates correctly**
```js
// In PreFlight state, not yet ReadyToArm:
console.log(FlightSession.armingPermitted)  // false

// Simulate all checks passing:
FlightSession.onPreFlightComplete()
console.log(FlightSession.state)            // 4 (ReadyToArm)
console.log(FlightSession.armingPermitted)  // true
```
- [ ] False before pre-flight complete, true after

**D.8 sessionSummary updates**
```js
console.log(FlightSession.sessionSummary)
// "Flight — Lisabeth — Pre-flight"  or  "Flight — Lisabeth — Ready to Arm"
```
- [ ] Contains operator name and current state description

---

## PART E — Session Start Dialog (Step 4)

**E.1 Dialog appears on vehicle connect**
Disconnect and reconnect Mock Link. Without any QML console intervention:
- [ ] SessionStartDialog opens automatically
- [ ] Dialog is modal — cannot interact with checklist behind it

**E.2 Operator must be selected first**
Before selecting an operator:
- [ ] Training and Flight tiles are visually disabled (opacity reduced
      or not interactive)
- [ ] "Select or add an operator" warning visible

**E.3 Add operator via dialog**
Type a name in the inline TextField, select role, click Add:
- [ ] New operator appears in the ComboBox immediately
- [ ] New operator is auto-selected in the ComboBox
- [ ] Both tiles become interactive

**E.4 Training path from dialog**
Select an operator, click Training tile:
- [ ] Dialog closes
- [ ] Training banner appears in FlyView
- [ ] FlightFormDialog does NOT open

**E.5 Flight path from dialog**
Disconnect, reconnect, select operator, click Record Flight:
- [ ] SessionStartDialog closes
- [ ] FlightFormDialog opens immediately

---

## PART F — Flight Form Dialog (Step 5)

**F.1 Pre-populated fields**
When FlightFormDialog opens:
- [ ] Vehicle name shows current connected vehicle name
- [ ] Date/Time shows current date and time
- [ ] Weather field shows WeatherProvider.currentSummary
      (or "Weather not yet fetched" if unavailable)
- [ ] All three are read-only

**F.2 Validation — submit blocked without required fields**
Without selecting purpose or entering location:
- [ ] "Start Flight Record" button is disabled

Select purpose only:
- [ ] Button still disabled (location missing)

Enter location only (purpose still default):
- [ ] Button still disabled (purpose not selected)

**F.3 Cancel returns to mode selection**
Click Cancel:
- [ ] FlightFormDialog closes
- [ ] SessionStartDialog reopens

**F.4 Submit creates DB record**
Select purpose "Test Flight", enter location "Bole Field", click Submit:
- [ ] Dialog closes
- [ ] Checklist page active, no dialog blocking it
- [ ] DB record created:
  ```bash
  sqlite3 ... "SELECT mode, purpose, location, started_at FROM flights
               ORDER BY id DESC LIMIT 1;"
  ```
  Result: FLIGHT | Test Flight | Bole Field | [timestamp]

**F.5 Weather summary captured**
```bash
sqlite3 ... "SELECT weather_summary FROM flights ORDER BY id DESC LIMIT 1;"
```
- [ ] Non-empty value — weather was captured at session start

---

## PART G — Check Results Wired to Flight (Step 6)

After completing Part F (flight session open):

**G.1 Auto checks write results**
Wait for checklist to evaluate (10 seconds after Mock Link connects):
```bash
sqlite3 ... "SELECT check_id, status, evaluated_at
             FROM flight_check_results
             WHERE flight_id = (SELECT MAX(id) FROM flights)
             LIMIT 10;"
```
- [ ] Multiple rows exist — checks are being written
- [ ] Status values are PASS, FAIL, or WARN (not empty)
- [ ] evaluated_at timestamps are recent

**G.2 Manual check records confirmedBy**
Open the checklist, tap "Mark as Checked" on any manual confirm check:
```bash
sqlite3 ... "SELECT check_id, status, confirmed_by
             FROM flight_check_results
             WHERE confirmed_by IS NOT NULL
             LIMIT 5;"
```
- [ ] Row exists with `confirmed_by` = current operator ID
- [ ] `confirmed_by` is NOT -1 or NULL

**G.3 Training session writes NO check results**
Close session, reconnect, choose Training, run checklist:
```bash
sqlite3 ... "SELECT COUNT(*) FROM flight_check_results;"
```
- [ ] Count is unchanged from before training session — nothing new written

---

## PART H — Telemetry Event Logger (Step 7)

Start a new Flight session. Complete pre-flight. Ensure Mock Link is
connected and checklist passes.

**H.1 ARM event on arm**
Arm via Mock Link:
```bash
sqlite3 ... "SELECT event_type, battery_v, timestamp
             FROM flight_telemetry_events
             WHERE flight_id = (SELECT MAX(id) FROM flights);"
```
- [ ] At least one row with event_type = "ARM"
- [ ] ARM row has non-null battery_v, timestamp

**H.2 MODE_CHANGE event on flight mode change**
In Mock Link, change the simulated flight mode (if available):
```bash
sqlite3 ... "SELECT event_type, triggered_by FROM flight_telemetry_events
             WHERE event_type = 'MODE_CHANGE';"
```
- [ ] Row exists with triggered_by = new mode name

**H.3 DISARM event on disarm**
Disarm via Mock Link:
```bash
sqlite3 ... "SELECT event_type, timestamp FROM flight_telemetry_events
             WHERE flight_id = (SELECT MAX(id) FROM flights)
             ORDER BY id;"
```
- [ ] ARM event appears before DISARM event
- [ ] DISARM event exists with timestamp after ARM timestamp

**H.4 No events written in training mode**
Start training session, run checklist, arm attempt fails (training blocks it).
```bash
sqlite3 ... "SELECT COUNT(*) FROM flight_telemetry_events
             WHERE flight_id = -1 OR flight_id IS NULL;"
```
- [ ] Count is 0 — no orphaned event rows

**H.5 Flight stats captured on disarm**
```bash
sqlite3 ... "SELECT duration_sec, max_altitude_m, min_battery_v
             FROM flights ORDER BY id DESC LIMIT 1;"
```
- [ ] duration_sec is non-null and > 0
- [ ] min_battery_v is non-null (may be 0 if Mock Link doesn't simulate battery)

---

## PART I — Post-Flight Checklist (Step 8)

Continuing from a completed arm → disarm sequence.

**I.1 Post-flight banner appears on disarm**
After disarming in a Flight session:
- [ ] "Flight complete — complete the post-flight checklist" banner
      is visible in the UI
- [ ] Banner color matches Colors.statePass family (green/teal)

**I.2 Pre-flight checks show as Skipped**
In the checklist after disarm:
- [ ] Pre-flight checks (is_post_flight=false) show SKIPPED status
- [ ] They are not PASS/FAIL/WARN — they are clearly visually distinct

**I.3 Post-flight checks are active**
- [ ] MotorTemperature check is visible and evaluating
- [ ] BatteryTemperature check is visible and evaluating
- [ ] "Propeller Physical Condition (post-flight)" manual check visible
- [ ] "Frame Arm Condition (post-flight)" manual check visible

**I.4 Post-flight results written to DB**
Complete all post-flight checks (tap manual confirms):
```bash
sqlite3 ... "SELECT check_id, is_post_flight, status
             FROM flight_check_results
             WHERE flight_id = (SELECT MAX(id) FROM flights)
             AND is_post_flight = 1;"
```
- [ ] Rows exist with is_post_flight = 1
- [ ] Manual confirms have confirmed_by = operator ID

**I.5 post_checklist_complete set in flights table**
```bash
sqlite3 ... "SELECT post_checklist_complete
             FROM flights ORDER BY id DESC LIMIT 1;"
```
- [ ] Value is 1 after post-flight checks completed

---

## PART J — Flight History Page (Step 9)

**J.1 Page accessible from Analyze Tools**
Open Analyze Tools sidebar:
- [ ] "Flight History" appears as a sidebar item with an icon
- [ ] Clicking it loads FlightHistoryPage without crash

**J.2 Flight list populated**
After completing Parts F-I (at least one full flight recorded):
- [ ] At least one row visible in the left flight list
- [ ] Row shows: mode badge (FLIGHT/TRAINING), date, operator name,
      duration, pre-flight pass rate

**J.3 Mode badge colors correct**
- [ ] FLIGHT rows show green badge
- [ ] TRAINING rows show accent color badge (if any training runs)

**J.4 Flight detail loads on tap**
Tap a flight row:
- [ ] Right panel populates with flight header (vehicle, operator,
      purpose, location, weather, date)
- [ ] Pre-flight check results section shows all check rows with status
- [ ] Telemetry events section shows ARM and DISARM events
- [ ] Post-flight results shown if present

**J.5 Check confirmed_by shown**
In flight detail, for a manual confirm check:
- [ ] "Confirmed by [operator name]" is visible next to that check row

**J.6 No flight selected placeholder**
On page load before any row is tapped:
- [ ] Right panel shows "Select a flight from the list" or equivalent
      placeholder — not blank white space

**J.7 Multiple flights listed correctly**
If more than one flight has been recorded:
- [ ] All flights appear in list, most recent first
- [ ] Tapping different rows loads the correct flight detail each time

---

## PART K — Training Mode Banner and Footer (Step 10)

**K.1 Training banner visible throughout training session**
Connect Mock Link, choose Training:
- [ ] Banner visible at top of FlyView content area
- [ ] Banner shows "TRAINING MODE — Arming disabled"
- [ ] Banner shows current operator name
- [ ] Banner is above all other UI elements (z-order correct)

**K.2 Banner not visible in Flight mode**
Connect Mock Link, choose Record Flight:
- [ ] Training banner is NOT visible

**K.3 Footer in training mode**
During training session, look at the arming gate footer:
- [ ] Mode badge shows "TRAINING" not "ACTIVE/PASSIVE/HYBRID"
- [ ] Override button is NOT visible
- [ ] Force Arm button is NOT visible
- [ ] Blocker area shows "Arming disabled in training mode"

**K.4 Footer in flight mode (pre-arm)**
During flight session in ACTIVE gate mode with failing checks:
- [ ] Mode badge shows "ACTIVE" (not TRAINING)
- [ ] Override button NOT visible (ACTIVE mode, not HYBRID)
- [ ] Force Arm button IS visible
- [ ] Blocker count shows correct number of failing checks

**K.5 Footer in flight mode HYBRID with failures**
Switch gate to HYBRID mode with failing checks:
- [ ] Override button IS visible
- [ ] Override requires confirmation dialog
- [ ] Force Arm button IS visible
- [ ] Force Arm requires confirmation listing failing check names

**K.6 Session summary in toolbar**
During training session:
- [ ] Toolbar shows session summary string
- [ ] Contains operator name and "Training" or "Pre-flight" state

During flight session:
- [ ] Toolbar summary shows "Flight — [operator] — [state]"
- [ ] Color differs from training (green for flight, accent for training)

**K.7 Banner dismisses on session close**
Disconnect Mock Link during training session:
- [ ] Training banner disappears immediately
- [ ] Footer returns to normal arming gate display

---

## PART L — Full End-to-End Run

Run this complete sequence without any QML console intervention.
Everything should work from the UI alone.

1. [ ] Launch app, connect Mock Link Quadrotor
2. [ ] SessionStartDialog appears automatically
3. [ ] Add a new operator "Test Pilot" / role "Pilot" via the dialog
4. [ ] Select "Test Pilot" from the list
5. [ ] Click "Record Flight"
6. [ ] FlightFormDialog opens pre-populated with vehicle and weather
7. [ ] Select purpose "Test Flight", enter location "Test Site"
8. [ ] Click "Start Flight Record"
9. [ ] Checklist evaluates — results appear in flight_check_results
10. [ ] Complete all manual pre-flight checks (tap "Mark as Checked")
11. [ ] Arm gate reaches ReadyToArm, armingPermitted becomes true
12. [ ] Arm via Mock Link — ARM event written to flight_telemetry_events
13. [ ] Disarm via Mock Link — DISARM event written, post-flight banner appears
14. [ ] Complete post-flight checks
15. [ ] Open Analyze Tools → Flight History
16. [ ] Flight appears in list with correct mode, operator, duration
17. [ ] Tap flight — detail shows all pre and post check results plus events
18. [ ] Disconnect Mock Link — session closes, banner gone, footer resets

**DB final state:**
```bash
sqlite3 ~/.config/QGroundControl.org/QGroundControl.db "
SELECT
  f.id,
  o.name as operator,
  f.mode,
  f.purpose,
  f.duration_sec,
  f.pre_checklist_complete,
  f.post_checklist_complete,
  (SELECT COUNT(*) FROM flight_check_results r WHERE r.flight_id = f.id) as check_count,
  (SELECT COUNT(*) FROM flight_telemetry_events e WHERE e.flight_id = f.id) as event_count
FROM flights f
JOIN operators o ON f.operator_id = o.id
ORDER BY f.id DESC LIMIT 5;
"
```
- [ ] Row exists with duration_sec > 0
- [ ] pre_checklist_complete = 1
- [ ] post_checklist_complete = 1
- [ ] check_count > 0
- [ ] event_count ≥ 2 (at minimum ARM + DISARM)

---

## Test Record

| Part | Item | Pass/Fail | Observed Behavior |
|---|---|---|---|
| A | Build clean | | |
| A | Q_PROPERTY coverage | | |
| A | Files in CMake/QRC | | |
| A | No upstream edits | | |
| A | Unit tests pass | | |
| B | All 4 tables exist | | |
| B | Schema correct | | |
| B | DB integrity | | |
| C | OperatorManager accessible | | |
| C | Add/select/persist operator | | |
| C | Duplicate rejected | | |
| D | FlightSession initial state | | |
| D | Training blocks arm | | |
| D | Flight creates DB record | | |
| D | armingPermitted gates | | |
| E | Dialog auto-opens | | |
| E | Operator required | | |
| E | Training/Flight paths | | |
| F | Pre-populated fields | | |
| F | Validation enforced | | |
| F | DB record created | | |
| G | Auto check results written | | |
| G | Manual confirm has confirmedBy | | |
| G | Training writes nothing | | |
| H | ARM event on arm | | |
| H | DISARM event on disarm | | |
| H | No orphaned events | | |
| I | Post-flight banner appears | | |
| I | Pre-flight shows SKIPPED | | |
| I | Post-flight checks active | | |
| I | Post results in DB | | |
| J | History page accessible | | |
| J | Flight list populated | | |
| J | Detail loads on tap | | |
| K | Training banner visible | | |
| K | Footer hides Override/ForceArm | | |
| K | Session summary in toolbar | | |
| L | Full end-to-end run | | |

All items must show PASS before the flight/training session system
is considered complete.