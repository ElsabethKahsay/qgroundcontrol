"""Energy figure, then crash recovery, on copter flights.

1. A normal flight: take off, hover 90 s, land. Its energy_consumed_wh must
   agree within 5 % with the vehicle's own counter over the same flight
   (BATTERY_STATUS.energy_consumed, hectojoules, read before arm and after
   disarm on the control port).
2. A second flight: hover, then kill -9 the GCS mid-flight. Its row is left
   open with only its last 10 s summary.
3. Start the GCS again on the same data folder. Start-up recovery must close
   that row with end_reason = crash_recovered, and, the vehicle being still
   armed, the GCS must open a new flight with arm_source = already_armed for
   the rest of it (counted from its first battery sample, so up to one 10 s
   summary interval short: its energy is reported, not judged).
"""

import sys
import time

import sitl_common as c


def battery_wh(m):
    b = m.recv_match(type="BATTERY_STATUS", blocking=True, timeout=5)
    if b is None or b.energy_consumed < 0:
        return None
    return b.energy_consumed * 100.0 / 3600.0   # hJ -> Wh


def run(vehicle="ArduCopter", instance=3):
    r = c.Run("kill9-energy-%s" % vehicle.lower(), vehicle, instance)
    try:
        r.start_sitl()
        r.start_gcs()
        r.wait_gcs_connected()
        m = r.mav

        # 1. normal flight, energy check
        c.wait_ekf_ready(m, 120)
        wh0 = battery_wh(m)
        if not r.take_off(20):
            raise RuntimeError("take-off failed")
        time.sleep(90)
        r.end_flight()
        wh1 = battery_wh(m)
        rows = r.wait_rows("SELECT * FROM flight_sessions WHERE end_reason = 'disarmed'", timeout=30)
        if rows and wh0 is not None and wh1 is not None:
            gcs_wh, fc_wh = rows[-1]["energy_consumed_wh"] or 0, wh1 - wh0
            diff = abs(gcs_wh - fc_wh) / fc_wh if fc_wh > 0 else 1
            r.check("flight Wh within 5 % of BATTERY_STATUS.energy_consumed", diff <= 0.05,
                    "GCS %.3f Wh, vehicle %.3f Wh (%.1f %%)" % (gcs_wh, fc_wh, diff * 100))
        else:
            r.check("normal flight recorded with energy", False, "rows %d, counter %s -> %s" % (len(rows), wh0, wh1))

        # 2. kill -9 mid-flight
        if not r.take_off(20):
            raise RuntimeError("second take-off failed")
        time.sleep(60)
        r.note("kill -9 the GCS (pid %d) mid-flight" % r.gcs.pid)
        r.kill_gcs(sig=9)
        orphan = r.q("SELECT * FROM flight_sessions WHERE ended_at IS NULL")
        r.check("flight row left open by the crash", len(orphan) == 1)
        time.sleep(5)

        # 3. restart: recovery, then the joined flight
        r.start_gcs(fresh_settings=False)
        rows = r.wait_rows("SELECT * FROM flight_sessions WHERE end_reason = 'crash_recovered'", timeout=60)
        r.check("orphaned flight closed as crash_recovered", rows,
                rows and "duration %ss, last summary %s" % (rows[0]["duration_seconds"], rows[0]["last_summary_at"]))
        new = r.wait_rows("SELECT * FROM flight_sessions WHERE ended_at IS NULL", timeout=60)
        r.check("new flight opened for the still-armed vehicle, arm_source = already_armed",
                new and new[-1]["arm_source"] == "already_armed", new and "arm_source=%s" % new[-1]["arm_source"])
        wh_join = battery_wh(m)
        time.sleep(90)
        r.end_flight()
        wh_end = battery_wh(m)
        time.sleep(3)
        rows = r.wait_rows("SELECT * FROM flight_sessions WHERE arm_source = 'already_armed' AND ended_at IS NOT NULL",
                           timeout=30)
        r.check("joined flight closed with end_reason = disarmed", rows and rows[-1]["end_reason"] == "disarmed",
                rows and rows[-1]["end_reason"])
        if rows and wh_join is not None and wh_end is not None:
            r.check("joined flight has energy and min voltage recorded",
                    (rows[-1]["energy_consumed_wh"] or 0) > 0 and (rows[-1]["min_voltage"] or 0) > 0,
                    "GCS %.3f Wh from its first battery sample; vehicle %.3f Wh from the join; min %.2f V"
                    % (rows[-1]["energy_consumed_wh"] or 0, wh_end - wh_join, rows[-1]["min_voltage"] or 0))
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log()
        r.stop()
    return verdict


if __name__ == "__main__":
    print(run(*(sys.argv[1:2] or ["ArduCopter"])))
