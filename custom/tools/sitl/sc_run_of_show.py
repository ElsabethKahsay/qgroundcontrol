"""Two clean run-of-show dry runs on the same data folder.

Each run: start the GCS, connect, wait for the checks to settle, take off,
fly a few minutes (RTL at the end), land or disarm, close the GCS normally.
Between the two runs the GCS is closed and started again, as on a field day.

Closing: the script sends SIGTERM, which QGC turns into a main-window close.
With a vehicle connected, QGC then asks "close anyway?", which nobody answers
in an offscreen run, so the harness kills the app after 10 s and the next
start closes the connection as crash_recovered. That is the expected path for
a scripted close (and for a laptop shut down with the GCS open); a final
start at the end lets recovery close the second connection too.

Expected per run: one connection session, closed,
one flight closed with end_reason = disarmed and a non-zero duration, one
history row in flights, and no link_lost events. After both runs: two
connections and two flights, nothing left open.

Not exercised: the GCS's own arm button (the arm comes from the script, so
arm_source is external), the operator's checklist confirmations and the
battery pack selection, which need the UI.
"""

import sys
import time

import sitl_common as c


def one_flight(r, label):
    m = r.mav
    r.start_gcs(fresh_settings=(label == "run 1"))
    r.wait_gcs_connected()
    time.sleep(20)   # checks settle
    if not r.take_off(30):
        raise RuntimeError("%s: take-off failed" % label)
    if r.vehicle == "ArduPlane":
        c.set_mode(m, "LOITER")
    time.sleep(90)
    c.set_mode(m, "RTL")
    time.sleep(40 if r.vehicle == "ArduPlane" else 60)
    r.end_flight()
    time.sleep(12)
    r.note("%s: closing the GCS normally (SIGTERM)" % label)
    r.kill_gcs()
    r.check("%s: GCS exited" % label, r.gcs.poll() is not None, "exit code %s" % r.gcs.returncode)


def run(vehicle="ArduCopter", instance=3):
    r = c.Run("run-of-show-%s" % vehicle.lower(), vehicle, instance)
    try:
        r.start_sitl()
        one_flight(r, "run 1")
        time.sleep(5)
        one_flight(r, "run 2")
        r.start_gcs(fresh_settings=False)
        time.sleep(20)
        r.kill_gcs()
        conns = r.q("SELECT * FROM connection_sessions ORDER BY id")
        flights = r.q("SELECT * FROM flight_sessions ORDER BY id")
        # The last start's own connection stays open: its scripted close hits
        # the same unanswered dialog. Every earlier one must be closed.
        r.check("connections of both runs closed", len(conns) >= 3 and all(x["closed_at"] for x in conns[:-1]),
                str([(x["id"], x["end_reason"]) for x in conns]))
        r.check("two flights, both end_reason = disarmed",
                len(flights) == 2 and all(f["end_reason"] == "disarmed" for f in flights),
                str([(f["id"], f["end_reason"], f["duration_seconds"]) for f in flights]))
        r.check("flight durations non-zero", flights and all((f["duration_seconds"] or 0) > 60 for f in flights))
        r.check("two history rows", len(r.q("SELECT id FROM flights")) == 2)
        lost = r.q("SELECT id FROM session_events WHERE event = 'link_lost'")
        r.check("no link_lost events", not lost, "%d" % len(lost))
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log()
        r.stop()
    return verdict


if __name__ == "__main__":
    print(run(*(sys.argv[1:2] or ["ArduCopter"])))
