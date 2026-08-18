# Vehicles Feature — Known Limitations

Status: accepted as-is (Aug 2026), re-evaluate when the Vehicles feature is extended.

## 1. QGC identifies vehicles by sysid, not by fingerprint

QGC's `MultiVehicleManager` keys vehicles by `(sysid, compid)`. When a vehicle
broadcasts on a sysid that is already known, QGC reuses the existing vehicle
object and never emits a *new vehicle added* event.

Consequence for the Vehicles registry: if the *same* sysid is later used by a
different airframe/type (e.g. a fixed-wing at sysid 2, then a quadcopter also at
sysid 2), the existing database row is updated (not replaced with a new row) and
no second fingerprint row is created. The DB layer *does* support creating two
rows for same-sysid/different-type — proven by the offline DB harness (test
3.1) — it's just that QGC will not trigger it in this scenario.

- Visibility: low. Different physical vehicles on one link normally use
  different sysids; a single user swapping airframes one at a time is the only
  realistic trigger.
- Offline reference: `db_harness` section 3 (registration + uniqueness) passes.

## 2. AUTOPILOT_VERSION is only requested for the *active* vehicle

The registry's `_requestAutopilotCapabilities()` runs for QGC's single *active*
vehicle (the one currently selected in the UI). When two vehicles are live on the
same link at the same time, only the active one receives the
`MAV_CMD_REQUEST_MESSAGE(148)` request, so only it gets a populated
`firmware_version` in the DB.

- Both vehicles are still registered as separate rows (works correctly).
- Only the `firmware_version` field of the non-active vehicle can remain empty.
- If multiple active-vehicle support (or per-vehicle link handling) is ever
  added in QGC, this resolves itself; otherwise iterate over all connected
  vehicles when sending the request.