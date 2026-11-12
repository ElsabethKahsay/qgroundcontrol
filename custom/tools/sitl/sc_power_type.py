"""Power types (POWER_TYPES_PLAN.md, step 6) on an ArduCopter SITL flight.

The copter runs the synthetic current monitor (BATT_MONITOR = 25), the way
the fleet is set up. Before the flight the GCS's throw-away database is
seeded the way an operator would have left it after the Power types page:

* a power type "Quad X · 13×4.5 · 3S" (SITL's pack is 3S at 12.6 V, so it
  is full at arming), linked to this copter and validated by six used
  flights (the worked example's four and two more) (hover 32 + 6 A/kg, cruise 21 + 4 A/kg);
* a target about 300 m north of home, as saved on the gimbal page;
* a 250 mAh pack (the smallest the GCS accepts), so that once airborne the
  way home (at least the landing) can never fit above the reserve and the
  Fly view must say RETURN NOW.

The flight, driven from the control port in GUIDED: take off to 20 m, hover
30 s, fly to the target, hover 20 s there, fly back, land.

Expected on the flight's row (the recorder's 1 s tracker): the power type;
hover and cruise minutes with the payload "aboard" before the target and
"empty" after it (the drop is the first pass within 20 m of the target);
the FC's mAh used and its synthetic current per phase; the pack full at arm
(4.2 V per cell); telemetry coverage near 100 %; learning state "pending"
(no charger number yet). Expected in the GCS log: the RETURN NOW line, and
no QML errors from the power-type screens.

Not exercised: the payload weight and the time at target, which are
entered in a dialog per connection (the recorder splits loaded/empty time
by the target alone, so the drop is still checked).
"""

import math
import sqlite3
import sys
import time

from pymavlink import mavutil

import sitl_common as c

PARAMS = {"BATT_MONITOR": 25, "BATT_AMP_PERVLT": 40, "BATT_AMP_OFFSET": 0.3}
TARGET_NORTH_M = 300.0
QML_FILES = ("VehiclesPage.qml", "BatteryStatusChip.qml", "FlyViewTelemetryStrip.qml", "ChecklistEngine.qml")


def position(m, timeout=5):
    """The latest position. Messages queue up on the control link between
    reads, so drain the queue first: the next message in it can be tens of
    seconds old."""
    while m.recv_match(blocking=False) is not None:
        pass
    g = m.recv_match(type="GLOBAL_POSITION_INT", blocking=True, timeout=timeout)
    return (g.lat / 1e7, g.lon / 1e7, g.relative_alt / 1000.0) if g else None


def distance_m(a, b):
    k = math.pi / 180.0
    dlat, dlon = (b[0] - a[0]) * k, (b[1] - a[1]) * k
    h = math.sin(dlat / 2) ** 2 + math.cos(a[0] * k) * math.cos(b[0] * k) * math.sin(dlon / 2) ** 2
    return 2 * 6371000.0 * math.asin(min(1.0, math.sqrt(h)))


def fly_to(r, lat, lon, alt, within=5.0, timeout=120):
    """GUIDED position target; True once within @within metres."""
    m = r.mav
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        m.mav.set_position_target_global_int_send(
            0, m.target_system, m.target_component, mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT_INT,
            0b110111111000, int(lat * 1e7), int(lon * 1e7), alt, 0, 0, 0, 0, 0, 0, 0, 0)
        p = position(m, 2)
        if p:
            last = distance_m(p, (lat, lon))
            if last <= within:
                return True
        time.sleep(1)
    r.note("fly_to: still %.0f m away" % (last if last is not None else -1))
    return False


def seed(r, uid, home):
    """The operator's state after the Power types page, written while the GCS is closed."""
    assert r.db_path != c.REAL_DB
    con = sqlite3.connect(r.db_path, timeout=10)
    try:
        now = c.utc_now()
        cur = con.execute("INSERT INTO power_types (frame, prop_diameter_in, prop_pitch_in, cells, motor_note, created_at) "
                          "VALUES ('Quad X', 13, 4.5, 3, 'SITL', ?)", (now,))
        type_id = cur.lastrowid
        con.execute("INSERT INTO vehicle_power_type (device_uid, power_type_id, linked_at) VALUES (?, ?, ?)",
                    (uid, type_id, now))
        # Six learning flights: a type needs at least 5 to be validated.
        for hover, cruise, kg, mah in ((3, 10, 0.0, 5100.0), (8, 4, 0.0, 5666.7), (3, 10, 1.5, 6550.0), (8, 4, 1.5, 7266.7),
                                       (5, 6, 0.0, 4766.7), (5, 6, 1.5, 6116.7)):
            loaded = kg > 0
            con.execute("INSERT INTO flight_sessions (device_uid, started_at, payload_weight_kg, power_type_id, "
                        "hover_min_empty, hover_min_loaded, cruise_min_empty, cruise_min_loaded, cruise_speed_ms, "
                        "fc_mah, started_full, telemetry_coverage, charger_mah, learning_state) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 15, ?, 1, 1.0, ?, 'used')",
                        (uid, now, kg, type_id, 0 if loaded else hover, hover if loaded else 0,
                         0 if loaded else cruise, cruise if loaded else 0, mah / 1.2, mah))
        target = (home[0] + TARGET_NORTH_M / 111194.93, home[1])
        con.execute("UPDATE vehicles SET target_lat = ?, target_lon = ?, target_source = 'typed', target_set_at = ?, "
                    "battery_series_cells = 3, battery_parallel_cells = 1, battery_cell_mah = 250 WHERE device_uid = ?",
                    (target[0], target[1], now, uid))
        con.commit()
    finally:
        con.close()
    r.note("seeded power type %d for %s, target %.6f,%.6f, 250 mAh pack" % (type_id, uid, target[0], target[1]))
    return type_id, target


def run(vehicle="ArduCopter", instance=5):
    r = c.Run("power-type-%s" % vehicle.lower(), vehicle, instance, params=PARAMS)
    try:
        r.start_sitl()
        m = r.mav
        r.start_gcs()
        conns = r.wait_gcs_connected()
        if not conns:
            raise RuntimeError("the GCS did not connect")
        uid = conns[-1]["device_uid"]
        r.wait_rows("SELECT device_uid FROM vehicles WHERE device_uid = ?", (uid,))
        c.wait_ekf_ready(m, 120)
        home = position(m, 10)
        if not home:
            raise RuntimeError("no position from the vehicle")
        r.note("home %.6f,%.6f" % home[:2])
        r.note("closing the GCS to seed its database")
        r.kill_gcs()
        type_id, target = seed(r, uid, home)

        r.start_gcs(fresh_settings=False)
        r.wait_gcs_connected()
        time.sleep(20)   # parameters, checks and the power type load

        if not r.take_off(20):
            raise RuntimeError("take-off failed")
        time.sleep(30)                                   # hover with the payload "aboard"
        r.check("reached the target", fly_to(r, target[0], target[1], 20))
        time.sleep(20)                                   # hover at the target (payload dropped)
        r.check("came back over home", fly_to(r, home[0], home[1], 20))
        r.end_flight()

        rows = r.wait_rows("SELECT * FROM flight_sessions WHERE power_type_id = ? AND ended_at IS NOT NULL "
                           "AND charger_mah IS NULL ORDER BY id", (type_id,), timeout=30)
        f = rows[-1] if rows else {}
        r.check("flight recorded with the power type", bool(f), "flight %s" % f.get("id"))
        detail = ", ".join("%s=%s" % (k, f.get(k)) for k in (
            "hover_min_loaded", "cruise_min_loaded", "hover_min_empty", "cruise_min_empty", "cruise_speed_ms",
            "fc_mah", "synth_hover_a", "synth_cruise_a", "ref_throttle_hover", "ref_throttle_cruise",
            "arm_cell_v", "started_full", "telemetry_coverage", "learning_state"))
        r.note("power record: " + detail)
        num = lambda k: f.get(k) if f.get(k) is not None else -1
        r.check("hover before the target counted loaded", num("hover_min_loaded") >= 0.4, detail)
        r.check("cruise out counted loaded", num("cruise_min_loaded") > 0.1)
        r.check("hover at the target and landing counted empty (dropped)", num("hover_min_empty") >= 0.25)
        r.check("cruise back counted empty", num("cruise_min_empty") > 0.1)
        r.check("cruise speed recorded", 3.0 < num("cruise_speed_ms") < 30.0)
        r.check("FC mAh used recorded (synthetic monitor)", num("fc_mah") > 0)
        r.check("synthetic current per phase", num("synth_hover_a") > 0 and num("synth_cruise_a") > 0)
        r.check("throttle per phase", 0 < num("ref_throttle_hover") <= 1 and 0 < num("ref_throttle_cruise") <= 1)
        r.check("pack full at arm (3S, about 4.2 V per cell)", f.get("started_full") == 1 and num("arm_cell_v") > 4.1)
        r.check("telemetry coverage near 100 %", num("telemetry_coverage") > 0.95)
        r.check("waiting for the charger number", f.get("learning_state") == "pending")

        if abs(num("synth_hover_a") - num("synth_cruise_a")) < 0.05 and abs(num("synth_hover_a") - PARAMS["BATT_AMP_OFFSET"]) < 0.05:
            r.note("FINDING: the synthetic current stayed at BATT_AMP_OFFSET (%.2f A) in hover and cruise. "
                   "ArduPilot computes it from the k_throttle servo output, which a multicopter does not use "
                   "(its motors are k_motor1..N), so on ArduCopter it does not follow throttle."
                   % PARAMS["BATT_AMP_OFFSET"])

        r.kill_gcs()
        with open(r.gcs_log.name) as fh:
            log = fh.read()
        for line in log.splitlines():
            if ("battery estimate" in line or "power-type estimate" in line) or "RETURN NOW" in line:
                r.note("GCS log: " + line.strip()[:220])
        r.check("RETURN NOW announced while airborne", "RETURN NOW on" in log)
        # BATT_MONITOR = 25 on a copter: the GCS counts the charge itself.
        r.check("charge used counted by the GCS (synthetic current ignored)", "charge used from gcs" in log)
        qml_errors = [line for line in log.splitlines()
                      if any(q in line for q in QML_FILES)
                      and any(w in line for w in ("Error", "error", "Unable to assign", "undefined", "Cannot"))]
        r.check("no QML errors from the power-type screens", not qml_errors, "; ".join(qml_errors[:3]))
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log()
        r.stop()
    return verdict


if __name__ == "__main__":
    print(run(*(sys.argv[1:2] or ["ArduCopter"])))
