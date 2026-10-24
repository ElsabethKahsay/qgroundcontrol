"""Force-arm against a failing pre-arm check.

BATT_ARM_VOLT is set above the simulated pack voltage, so the vehicle's own
pre-arm check fails. A normal arm must be refused; an arm with param2 = 2989
(ArduPilot's force-arm magic number) must be accepted. The GCS did not send
either command, so the flight it records must say arm_source = external and
close with end_reason = disarmed.
"""

import sys
import time

from pymavlink import mavutil

import sitl_common as c


def run(vehicle="ArduPlane", instance=3):
    r = c.Run("force-arm-%s" % vehicle.lower(), vehicle, instance, params={"BATT_ARM_VOLT": 20})
    try:
        r.start_sitl()
        r.start_gcs()
        r.wait_gcs_connected()
        m = r.mav
        r.check("GPS fix (pre-arm still failing on purpose)", c.wait_ekf_ready(m, 180, prearm=False))
        time.sleep(10)
        res = c.command(m, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 1)
        texts = c.statustexts(m, 3)
        r.note("normal arm: ACK %s, vehicle said %s" % (res, [t for t in texts if "Arm" in t or "PreArm" in t][:3]))
        r.check("normal arm refused by the failing pre-arm",
                res != mavutil.mavlink.MAV_RESULT_ACCEPTED and not c.wait_armed(m, True, 3))
        res = c.command(m, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 1, 2989)
        r.note("force arm (2989): ACK %s" % res)
        r.check("force arm accepted", c.wait_armed(m, True, 10))
        # A copter on the ground disarms itself after DISARM_DELAY (10 s), so
        # look for the flight whether or not it is still open.
        time.sleep(5)
        rows = r.wait_rows("SELECT * FROM flight_sessions", timeout=20)
        r.check("GCS opened a flight on the external arm", rows,
                rows and "arm_source=%s gate_allowed=%s" % (rows[-1]["arm_source"], rows[-1]["gate_allowed"]))
        if rows:
            r.check("arm source recorded as external", rows[-1]["arm_source"] == "external", rows[-1]["arm_source"])
        c.command(m, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0, 21196)
        r.check("vehicle disarmed", c.wait_armed(m, False, 15))
        rows = r.wait_rows("SELECT * FROM flight_sessions WHERE ended_at IS NOT NULL", timeout=20)
        r.check("flight closed with end_reason = disarmed", rows and rows[-1]["end_reason"] == "disarmed",
                rows and "end_reason=%s duration=%ss" % (rows[-1]["end_reason"], rows[-1]["duration_seconds"]))
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log("Pre-arm made to fail with BATT_ARM_VOLT = 20 V (the simulated pack is about 12.6 V).")
        r.stop()
    return verdict


if __name__ == "__main__":
    print(run(*(sys.argv[1:2] or ["ArduPlane"])))
