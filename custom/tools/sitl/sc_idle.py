"""Long idle: the GCS connected to a disarmed vehicle for hours.

Records the GCS's resident memory every 5 minutes. If the GCS quits by itself
the exit code (or killing signal) and the tail of its own log are written to
the run log, which identifies who asked it to quit. Passes when the GCS is
still running at the end and memory grew by less than 30 %.

    python3 sc_idle.py ArduPlane 3      # 3 hours (default)
"""

import os
import sys
import time

import sitl_common as c


def run(vehicle="ArduPlane", hours=3.0, instance=4, interval_s=300):
    r = c.Run("idle-%gh-%s" % (hours, vehicle.lower()), vehicle, instance)
    samples = []
    try:
        r.start_sitl()
        r.start_gcs()
        r.wait_gcs_connected()
        time.sleep(60)   # let parameter download and the checks finish
        end = time.time() + hours * 3600
        while time.time() < end:
            if r.gcs.poll() is not None:
                break
            kb = c.rss_kb(r.gcs.pid)
            samples.append((c.utc_now(), kb))
            r.note("GCS RSS %s MB" % (kb and round(kb / 1024)))
            time.sleep(min(interval_s, max(1, end - time.time())))
        code = r.gcs.poll()
        if code is not None:
            r.check("GCS still running after %g h" % hours, False, "quit early, exit code %s" % code)
            with open(os.path.join(r.work, "gcs.log"), errors="replace") as f:
                r.note("GCS log tail:\n" + "".join(f.readlines()[-40:]))
        else:
            r.check("GCS still running after %g h" % hours, True)
        kbs = [k for _, k in samples if k]
        if len(kbs) >= 2:
            growth = (kbs[-1] - kbs[0]) / kbs[0]
            r.check("memory growth under 30 %", growth < 0.30,
                    "%d MB -> %d MB (%+.1f %%)" % (kbs[0] / 1024, kbs[-1] / 1024, growth * 100))
        conns = r.q("SELECT * FROM connection_sessions")
        r.check("one connection session for the whole idle", len(conns) == 1, "%d" % len(conns))
        lost = r.q("SELECT id FROM session_events WHERE event = 'link_lost'")
        r.check("no link_lost events", not lost, "%d" % len(lost))
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log("RSS samples (UTC, kB):\n\n" + "\n".join("- %s %s" % s for s in samples))
        r.stop()
    return verdict


if __name__ == "__main__":
    args = sys.argv[1:]
    print(run(args[0] if args else "ArduPlane", float(args[1]) if len(args) > 1 else 3.0))
