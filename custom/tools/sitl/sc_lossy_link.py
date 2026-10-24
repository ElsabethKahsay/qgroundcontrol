"""Lossy and slow link while armed (stands in for tc netem, which needs root).

The relay drops a share of MAVLink frames and delays the rest, in both
directions, in three steps of 60 s each while the vehicle flies:

    20 % loss + 150 ms   a poor but usable radio: no link_lost expected
    40 % loss + 400 ms   bad radio: link_lost is allowed, the flight must stay open
    80 % loss + 400 ms   nearly gone: link_lost allowed, the flight must stay open

QGC declares comm lost only after 3.5 s with no message at all from the
vehicle, and the vehicle sends dozens of messages a second, so even 80 % loss
rarely produces a link_lost; only the full blackout in sc_rtl_link_loss.py
must. What matters here is that the GCS stays up and the flight row survives.

Item 12 asked to watch a "parserStalled" signal; no such signal exists in the
GCS or in upstream QGC, so this run watches what does exist: the session
events, whether the GCS process stays up, and that the flight row survives.
"""

import sys
import time

import sitl_common as c

STEPS = [(0.20, 0.150, False), (0.40, 0.400, None), (0.80, 0.400, None)]  # loss, delay, link_lost expected?


def run(vehicle="ArduCopter", instance=3):
    r = c.Run("lossy-link-%s" % vehicle.lower(), vehicle, instance)
    try:
        r.start_sitl()
        r.start_gcs()
        r.wait_gcs_connected()
        m = r.mav
        if not r.take_off(30):
            raise RuntimeError("take-off failed")
        if vehicle == "ArduPlane":
            c.set_mode(m, "LOITER")
        for loss, delay, expect_lost in STEPS:
            before = len(r.q("SELECT id FROM session_events WHERE event = 'link_lost'"))
            r.relay.loss, r.relay.delay_s = loss, delay
            r.note("link: %d %% loss, %d ms delay for 60 s" % (loss * 100, delay * 1000))
            time.sleep(60)
            r.relay.loss, r.relay.delay_s = 0.0, 0.0
            time.sleep(8)
            lost = len(r.q("SELECT id FROM session_events WHERE event = 'link_lost'")) - before
            label = "%d %% / %d ms: %d link_lost event(s)" % (loss * 100, delay * 1000, lost)
            if expect_lost is None:
                r.check(label + " (any count allowed)", True)
            else:
                r.check(label, (lost > 0) == expect_lost, "expected %s" % ("some" if expect_lost else "none"))
            r.check("GCS still running", r.gcs.poll() is None)
            r.check("flight still open", len(r.q("SELECT id FROM flight_sessions WHERE ended_at IS NULL")) == 1)
        r.end_flight()
        rows = r.wait_rows("SELECT * FROM flight_sessions WHERE ended_at IS NOT NULL", timeout=30)
        r.check("flight closed with end_reason = disarmed", rows and rows[-1]["end_reason"] == "disarmed",
                rows and rows[-1]["end_reason"])
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log("No `parserStalled` signal exists in the code; see the module docstring.")
        r.stop()
    return verdict


if __name__ == "__main__":
    print(run(*(sys.argv[1:2] or ["ArduCopter"])))
