"""The motor-table flight-time estimate (MOTOR_TABLE_PLAN.md, step 7) on an
ArduCopter SITL flight set up like the Skywin 6S quad.

The simulated copter is the drone as far as it is known
(models/skywin_m6215_6s.json): 12 kg, quad X, four 22-inch props, a 6S
Li-ion pack of 20 Ah from 25.8 V, its hover and thrust curve from the maker's
thrust test, flown from a home at 2900 m. The flight controller is set up as
the real one is expected to be: no current sensor on the pack the GCS reads
(BATT_MONITOR = 3, voltage only), the ESC and spin ranges, voltage
compensation for 6S Li-ion, hover learning on, 70 km/h waypoint speed, and
filter values for 22-inch props. A second battery monitor (BATT2, current
sensing) is the simulator's own count of the charge, kept for comparison only:
the GCS does not read it.

Before the flight the GCS's throw-away database is seeded as an operator
would leave it after the Vehicles page: the copter linked to the M6215 motor
table and a 6S Li-ion 20 Ah pack. Nothing about the copter's weight or hover is
seeded: the GCS has to measure them.

The flight, from the control port in GUIDED: take off to 20 m, hover 40 s, fly
600 m north and back at 70 km/h, land.

Expected: the GCS's estimate on, from the motor table, the charge counted by
the GCS; the motor signals arriving the whole flight (no time on the overall
throttle); the flight recorded for the charger comparison (airborne, full pack,
hover and cruise time, the air density at 2900 m), waiting for its charger
number; the hover remembered for the next take-off; no QML errors from the
motor-table screens. The GCS's count against the simulator's own is logged,
not checked: the simulator's power model is simpler than the maker's table.
"""

import math
import os
import sqlite3
import sys
import time

from pymavlink import mavutil

import sitl_common as c

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.join(HERE, "models", "skywin_m6215_6s.json")
TABLE_ID = "m6215-320kv-msc22x7.8-6s"
HOME_ALT_M = 2900
CRUISE_MS = 70.0 / 3.6
LEG_M = 600.0

PARAMS = {
    # Frame: quad X.
    "FRAME_CLASS": 1, "FRAME_TYPE": 1,
    # The pack the GCS reads: voltage only, no current sensor; 20 Ah.
    "BATT_MONITOR": 3, "BATT_CAPACITY": 20000,
    # The simulator's own current count, for comparison only.
    "BATT2_MONITOR": 4, "BATT2_VOLT_PIN": 13, "BATT2_CURR_PIN": 12, "BATT2_CAPACITY": 20000,
    # ESC and spin ranges, thrust curve, voltage compensation (6S Li-ion
    # 4.3 -> 2.9 V per cell), hover learning.
    "MOT_PWM_MIN": 1000, "MOT_PWM_MAX": 2000, "MOT_SPIN_MIN": 0.15, "MOT_SPIN_MAX": 0.95,
    "MOT_THST_EXPO": 0.27, "MOT_THST_HOVER": 0.27, "MOT_HOVER_LEARN": 2,
    "MOT_BAT_VOLT_MAX": 25.8, "MOT_BAT_VOLT_MIN": 17.4,
    # Filters for 22-inch props (the usual starting values for this size).
    "INS_GYRO_FILTER": 22, "ATC_RAT_RLL_FLTD": 11, "ATC_RAT_PIT_FLTD": 11,
    "ATC_RAT_RLL_FLTT": 11, "ATC_RAT_PIT_FLTT": 11, "ATC_RAT_YAW_FLTE": 2,
    # Cruise at 70 km/h (m/s), with the lean angle to reach it (degrees).
    "WP_SPD": round(CRUISE_MS, 2), "ATC_ANGLE_MAX": 35,
}
DRONE_PARAMS = dict(PARAMS)   # the drone's own values, read back after start-up


def _sitl_copter_defaults():
    """ArduPilot's own SITL copter defaults (default_params/copter.parm). The
    simulator loads them by itself only for its built-in frames, not for a
    frame model file, and without them it refuses to arm (no accelerometer
    calibration). The drone's own values above go on top of them."""
    path = os.path.join(os.path.expanduser("~/ardupilot"), "Tools", "autotest", "default_params", "copter.parm")
    values = {}
    with open(path) as fh:
        for line in fh:
            parts = line.split("#", 1)[0].split()
            if len(parts) >= 2:
                values[parts[0]] = float(parts[1]) if "." in parts[1] else int(parts[1])
    return values


PARAMS = dict(_sitl_copter_defaults(), **PARAMS)
# The frame model, and the default SITL home moved up to 2900 m.
SIM_ARGS = ["-f", "x", "--model", "x:%s" % MODEL, "-l", "-35.363261,149.165230,%d,353" % HOME_ALT_M]
QML_FILES = ("VehiclesPage.qml", "BatteryStatusChip.qml", "FlyViewTelemetryStrip.qml", "ChecklistEngine.qml")


def position(m, timeout=5):
    """The latest position; drains the queued messages first."""
    while m.recv_match(blocking=False) is not None:
        pass
    g = m.recv_match(type="GLOBAL_POSITION_INT", blocking=True, timeout=timeout)
    return (g.lat / 1e7, g.lon / 1e7, g.relative_alt / 1000.0) if g else None


def distance_m(a, b):
    k = math.pi / 180.0
    dlat, dlon = (b[0] - a[0]) * k, (b[1] - a[1]) * k
    h = math.sin(dlat / 2) ** 2 + math.cos(a[0] * k) * math.cos(b[0] * k) * math.sin(dlon / 2) ** 2
    return 2 * 6371000.0 * math.asin(min(1.0, math.sqrt(h)))


def fly_to(r, lat, lon, alt, within=8.0, timeout=150):
    """GUIDED position target; True once within @within metres. Logs the top
    ground speed reached."""
    m = r.mav
    deadline = time.time() + timeout
    last, top = None, 0.0
    while time.time() < deadline:
        m.mav.set_position_target_global_int_send(
            0, m.target_system, m.target_component, mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT_INT,
            0b110111111000, int(lat * 1e7), int(lon * 1e7), alt, 0, 0, 0, 0, 0, 0, 0, 0)
        v = m.recv_match(type="VFR_HUD", blocking=True, timeout=2)
        if v:
            top = max(top, v.groundspeed)
        p = position(m, 2)
        if p:
            last = distance_m(p, (lat, lon))
            if last <= within:
                r.note("fly_to: arrived, top ground speed %.1f m/s" % top)
                return True
        time.sleep(0.5)
    r.note("fly_to: still %.0f m away, top ground speed %.1f m/s" % (last if last is not None else -1, top))
    return False


def sim_mah(m):
    """The simulator's own charge used (BATT2, current sensing), mAh; -1 when not reported."""
    deadline = time.time() + 5
    while time.time() < deadline:
        b = m.recv_match(type="BATTERY_STATUS", blocking=True, timeout=1)
        if b and b.id == 1 and b.current_consumed >= 0:
            return b.current_consumed
    return -1


def check_params(m, wanted):
    """Each wanted parameter read back from the vehicle; a list of the ones
    missing (a name this ArduPilot does not know) or not as set."""
    wrong = []
    for name, value in sorted(wanted.items()):
        got = None
        for _ in range(3):
            m.mav.param_request_read_send(m.target_system, m.target_component, name.encode(), -1)
            deadline = time.time() + 2
            while time.time() < deadline:
                pv = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
                if pv and pv.param_id.rstrip("\x00") == name:
                    got = pv.param_value
                    break
            if got is not None:
                break
        if got is None:
            wrong.append("%s missing" % name)
        elif abs(got - value) > 1e-3 * max(1.0, abs(value)):
            wrong.append("%s=%g, not %g" % (name, got, value))
    return wrong


def seed(r, uid):
    """The operator's state after the Vehicles page, written while the GCS is closed."""
    assert r.db_path != c.REAL_DB
    con = sqlite3.connect(r.db_path, timeout=10)
    try:
        now = c.utc_now()
        con.execute("INSERT OR REPLACE INTO vehicle_motor_table (device_uid, table_id, linked_at) VALUES (?, ?, ?)",
                    (uid, TABLE_ID, now))
        con.execute("UPDATE vehicles SET battery_series_cells = 6, battery_parallel_cells = 1, "
                    "battery_cell_mah = 20000, battery_type = 'LiIon' WHERE device_uid = ?", (uid,))
        con.commit()
    finally:
        con.close()
    r.note("seeded: %s on the motor table, 6S Li-ion 20 Ah pack (no hover, no weight)" % uid)


def run(vehicle="ArduCopter", instance=6):
    r = c.Run("motor-table-%s" % vehicle.lower(), vehicle, instance, params=PARAMS, sim_args=SIM_ARGS)
    try:
        r.start_sitl()
        m = r.mav
        wrong = check_params(m, DRONE_PARAMS)
        r.check("the drone's parameters took in the simulator", not wrong, "; ".join(wrong))
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
        g = m.recv_match(type="GLOBAL_POSITION_INT", blocking=True, timeout=5)
        r.note("home %.6f,%.6f at %.0f m AMSL" % (home[0], home[1], g.alt / 1000.0 if g else -1))
        r.note("closing the GCS to seed its database")
        r.kill_gcs()
        seed(r, uid)

        r.start_gcs(fresh_settings=False)
        r.wait_gcs_connected()
        time.sleep(20)   # parameters, checks and the motor table load
        mah0 = sim_mah(m)

        if not r.take_off(20):
            raise RuntimeError("take-off failed")
        time.sleep(40)                                         # steady hover
        north = (home[0] + LEG_M / 111194.93, home[1])
        r.check("flew out 600 m", fly_to(r, north[0], north[1], 20))
        r.check("came back over home", fly_to(r, home[0], home[1], 20))
        r.end_flight()
        mah1 = sim_mah(m)

        rows = r.wait_rows("SELECT * FROM motor_table_flights WHERE device_uid = ? ORDER BY id", (uid,), timeout=30)
        f = rows[-1] if rows else {}
        r.check("flight recorded for the charger comparison", bool(f), "flight %s" % f.get("id"))
        detail = ", ".join("%s=%s" % (k, f.get(k)) for k in (
            "gcs_ah", "started_full", "start_soc", "hover_w", "hover_s", "cruise_ratio", "cruise_s", "avg_volts",
            "min_volts", "beyond_s", "overall_throttle_s", "density", "pack_ohm", "charger_mah", "state"))
        r.note("flight record: " + detail)
        num = lambda k: f.get(k) if f.get(k) is not None else -1
        r.check("charge counted by the GCS", num("gcs_ah") > 0.3, detail)
        r.check("started on a full pack (25.8 V, 4.3 V per cell)", f.get("started_full") == 1)
        r.check("at least 30 s of steady hover", num("hover_s") >= 30)
        r.check("motor signals the whole flight (no time on the overall throttle)", 0 <= num("overall_throttle_s") <= 2)
        r.check("pack voltage recorded (6S)", 17.0 < num("avg_volts") <= 26.0)
        # The pressure is the simulated barometer's at 2900 m; the temperature
        # is the live weather at the simulated home's real place (Canberra,
        # about 580 m), so only a range for -10 to 30 C is checked.
        r.check("air density from the 2900 m pressure (0.66-0.78)", 0.66 < num("density") < 0.78)
        r.check("waiting for the charger number", f.get("state") == "pending" and f.get("charger_mah") is None)
        r.note("cruise at 70 km/h: %.0f s recorded, ratio %s" % (num("cruise_s"), f.get("cruise_ratio")))

        hover = r.q("SELECT hover_thrust_g, hover_samples FROM vehicle_motor_table WHERE device_uid = ?", (uid,))
        h = hover[0] if hover else {}
        r.check("hover remembered for the next take-off", (h.get("hover_thrust_g") or 0) > 0, str(h))
        if h.get("hover_thrust_g"):
            r.note("weight the GCS measured: %.1f kg (the model is 12.0 kg)" % (4 * h["hover_thrust_g"] / 1000.0))

        if mah0 >= 0 and mah1 >= 0 and f.get("gcs_ah"):
            sim = (mah1 - mah0) / 1000.0
            r.note("charge used: GCS %.2f Ah, simulator %.2f Ah (GCS / simulator %.3f)"
                   % (f["gcs_ah"], sim, f["gcs_ah"] / sim if sim > 0 else -1))
        else:
            r.note("simulator's own charge count not reported (BATT2)")

        r.kill_gcs()
        with open(r.gcs_log.name) as fh:
            log = fh.read()
        for line in log.splitlines():
            if ("battery estimate" in line or "power-type estimate" in line) or "RETURN NOW" in line:
                r.note("GCS log: " + line.strip()[:220])
        r.check("estimate on from the motor table, counted by the GCS",
                "battery estimate on (M6215" in log and "charge used from gcs" in log)
        qml_errors = [line for line in log.splitlines()
                      if any(q in line for q in QML_FILES)
                      and any(w in line for w in ("Error", "error", "Unable to assign", "undefined", "Cannot"))]
        r.check("no QML errors from the motor-table screens", not qml_errors, "; ".join(qml_errors[:3]))
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log()
        r.stop()
    return verdict


if __name__ == "__main__":
    print(run(*(sys.argv[1:2] or ["ArduCopter"])))
