Redesign the preflight checklist into a multi-step wizard with weather sidebar, hardware verification stages, training mode, and force arm audit trail.

CURRENT STATE:
- Single scrollable page with all categories stacked vertically
- Environment/weather checks mixed into main checklist
- No staged workflow — everything visible at once
- No training mode distinction
- Force arm button exists but no structured override flow

REQUIRED STATE:

STEP 1 — VEHICLE & OPERATOR SELECTION (New initial screen)
- Two mode buttons: "Training Flight" | "Operational Flight"
- If Training selected:
  * Instructor name input (text field)
  * Trainee name input (text field)
  * RC Link ID dropdown for instructor (auto-detect from bound transmitters)
  * RC Link ID dropdown for trainee
  * Both fields required before proceeding
  * Log both names + RC IDs to audit trail
- If Operational selected:
  * Pilot name input (text field, required)
  * Optional: Observer name input
  * RC Link ID dropdown for pilot
- Vehicle auto-detected from connection, editable if multiple stored
- "Start Preflight" button disabled until all required fields filled
- If Testing selected:
  *it continues as is with out storing anything 
  * no need for pilot name input or anything 

STEP 2 — SYSTEM PREFLIGHT (Left panel: 70% width)
- Categories displayed vertically: Power, GPS/Navigation, Communication, Airframe, Safety
- Each category expandable/collapsible
- use 2 columns for the catagories
- Checks render as current cards with pass/warn/fail status
- Real-time evaluation as vehicle sends telemetry
- Progress bar at top: "X/Y system checks passed"
- "Next" button at bottom, disabled until all blocking checks pass OR user acknowledges warnings

STEP 3 — WEATHER SIDEBAR (Right panel: 30% width, sticky)
- Fixed position, does not scroll with left panel
- Title: "Weather Conditions"
- Auto-fetches METAR/TAF for nearest airport from HOME_POSITION
- Displays:
  * Wind speed + direction (arrow icon)
  * Gusts
  * Visibility
  * Cloud ceiling
  * Temperature
  * Precipitation status
  * TAF 6-hour forecast summary
- Data age indicator: "Updated 2 min ago"
- If fetch fails: "Weather unavailable — check manually" with checkbox confirm
- Color coding: green/yellow/red per threshold
- This is INFORMATIONAL ONLY — not part of pass/fail count
- Operator can reference but weather does not block "Next" button

STEP 4 — HARDWARE VERIFICATION (Camera & Gimbal)
- Triggered by clicking "Next" from Step 2
- New page/section replaces left panel content
- Camera Feed Check:
  * Live video widget (hooks QGC VideoManager)
  * "Video visible and clear" checkbox — manual confirm
  * Recording test: "Start 5s recording" button → playback verify
  * If no video source: "No camera detected — skip" checkbox
- Gimbal Movement Check:
  * Pan left/right buttons (send MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW)
  * Tilt up/down buttons
  * "Gimbal responds correctly" checkbox — manual confirm
  * If no gimbal: "No gimbal detected — skip" checkbox
- "Back to System Checks" button (top left)
- "Next to Final Review" button (bottom right)

STEP 5 — FINAL REVIEW & ARMING DECISION
- Summary screen showing:
  * System checks: X passed, Y warnings, Z critical failures
  * Weather status: current conditions summary
  * Hardware checks: camera pass/fail, gimbal pass/fail
  * Operator: [Pilot/Instructor name]
- If NO critical issues:
  * Green banner: "Preflight Complete — Ready to Arm"
  * "Arm Vehicle" button (primary action)
- If CRITICAL issues exist:
  * Red banner: "X critical issues must be fixed before arming"
  * List critical items with "Go Fix" links (jump back to relevant section)
  * "Force Arm" button (secondary, orange, requires explicit action)
- If WEATHER outside limits (but system checks pass):
  * Yellow banner: "Weather conditions marginal"
  * Show which thresholds exceeded
  * Options: "Proceed with Caution" | "Abort and Reschedule"
- Force Arm flow:
  * Click "Force Arm" → dialog opens
  * Required fields: Override reason (textarea, min 10 chars)
  * Checkbox: "I accept responsibility for bypassing safety checks"
  * "Confirm Force Arm" button disabled until reason entered + checkbox checked
  * On confirm: log to audit trail with timestamp, pilot name, critical issues list, weather status, override reason
  * Send MAV_CMD_COMPONENT_ARM_DISARM with force flag (param2=21196)
  * Show confirmation: "Force arm recorded — flight authorized under override"

NAVIGATION & STATE MANAGEMENT:
- Step indicator at top: Operator → System → Hardware → Review
- Current step highlighted, completed steps checked, future steps grayed
- State persists if user goes back (weather data cached, check states retained)
- On vehicle disconnect: reset to Step 1, clear all states
- On page close/reopen: restore to current step if same vehicle still connected

UI LAYOUT SPECIFICATIONS:
- Overall: full-width panel inside Analyze Tools, no external window
- Step 2 layout: flex row, left 70% (scrollable checklist), right 30% (sticky weather box)
- Weather box: border 3px rgba(255,255,255,0.1), border-radius 8px, padding 16px, background slightly darker than main panel
- Step transitions: slide left/right animation, 200ms, ease-out
- All buttons: match pink/purple theme, primary action filled, secondary action outlined
- Mobile responsive: stack vertically on narrow screens, weather box moves above checklist

AUDIT TRAIL ENHANCEMENTS:
- Training mode: log instructor + trainee + both RC link IDs + flight duration
- Operational mode: log pilot + observer + RC link ID
- Force arm: log as separate event type with full context
- All logs immutable, exportable as CSV/PDF

REMOVE:
- Environment category from main checklist (moved to weather sidebar)
- Old single-page stacked layout
- Inline force arm button (moved to structured Step 5 flow)
- Test Page and Module Test from Analyze Tools sidebar (already planned)

IMPLEMENTATION PRIORITY:
- MUST: Step 2 + 3 layout, weather sidebar, Step 5 review with force arm logging
- SHOULD: Step 1 operator modes, Step 4 camera/gimbal checks
- COULD: Training mode RC link ID auto-detect, slide animations 