"""RTL on a plane, then link loss while armed.

1. Plane: take off, switch to RTL and check the vehicle flies it (mode stays RTL and
   it stays airborne). The GCS should log the mode change.
2. Black out the link for 20 s while armed. QGC declares comm lost after
   3.5 s without a heartbeat, so the GCS must log link_lost and, when the link
   returns with the vehicle still armed, link_restored; the flight stays open.
3. Copter only: black out again and land during the blackout. The link comes
   back with the vehicle disarmed, so the flight must close with
   end_reason = link_lost, ended at the moment the link was lost.
   Plane: force-disarm with the link up; end_reason = disarmed.
"""

import sys
import time

from pymavlink import mavutil

import sitl_common as c


def run(vehicle="ArduPlane", instance=3):
    r = c.Run("rtl-link-loss-%s" % vehicle.lower(), vehicle, instance)
    try:
        r.start_sitl()
        r.start_gcs()
        r.wait_gcs_connected()
        m = r.mav
        if not r.take_off(40):
            raise RuntimeError("take-off failed")
        time.sleep(10)
        if vehicle == "ArduPlane":
            r.check("RTL accepted", c.set_mode(m, "RTL"))
            time.sleep(30)
            mode, alt = c.current_mode(m), c.rel_alt(m)
            r.check("still in RTL and airborne after 30 s", mode == "RTL" and alt and alt > 10,
                    "mode %s, alt %.0f m" % (mode, alt or -1))
        # A copter's RTL from above home is a landing, which would end the
        # flight before the blackout; it holds position in GUIDED instead.

        r.note("link blackout 20 s (armed)")
        r.relay.blackout = True
        time.sleep(20)
        r.relay.blackout = False
        r.note("link restored")
        time.sleep(10)
        ev = [e["event"] for e in r.q("SELECT event FROM session_events ORDER BY id")]
        r.check("link_lost then link_restored logged", "link_lost" in ev and "link_restored" in ev
                and ev.index("link_lost") < ev.index("link_restored"), str(ev))
        open_rows = r.q("SELECT * FROM flight_sessions WHERE ended_at IS NULL")
        r.check("flight still open after the link came back", len(open_rows) == 1)

        if vehicle == "ArduCopter":
            r.note("second blackout; LAND commanded on the control port during it")
            r.relay.blackout = True
            lost_at = c.utc_now()
            c.set_mode(m, "LAND")
            landed = c.wait_armed(m, False, 120)
            r.check("copter landed and disarmed during the blackout", landed)
            time.sleep(3)
            r.relay.blackout = False
            rows = r.wait_rows("SELECT * FROM flight_sessions WHERE ended_at IS NOT NULL", timeout=30)
            r.check("flight closed with end_reason = link_lost", rows and rows[-1]["end_reason"] == "link_lost",
                    rows and "end_reason=%s ended_at=%s (link cut %s)" % (rows[-1]["end_reason"], rows[-1]["ended_at"], lost_at))
        else:
            r.end_flight()
            rows = r.wait_rows("SELECT * FROM flight_sessions WHERE ended_at IS NOT NULL", timeout=30)
            r.check("flight closed with end_reason = disarmed", rows and rows[-1]["end_reason"] == "disarmed",
                    rows and rows[-1]["end_reason"])
        rows = r.q("SELECT * FROM flights")
        r.check("history row written for the flight", rows, rows and "duration %ss" % rows[-1]["duration_sec"])
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log()
        r.stop()
    return verdict


if __name__ == "__main__":
    print(run(*(sys.argv[1:2] or ["ArduPlane"])))
