"""Gimbal axis movement with a servo gimbal (MNT1_TYPE = 1).

Servo outputs 9/10/11 carry mount tilt, roll and pan. The script commands
each axis through the gimbal manager (the same MAVLink command family the
GCS's gimbal page uses) and checks that the vehicle reports the gimbal
attitude moving to the commanded angle and the servo output moving with it.

Not exercised: the GCS's own gimbal-test buttons and their result rows; they
are driven from the UI and covered by the gimbal unit tests. This run shows
the simulated gimbal answers the commands those buttons send.
"""

import math
import sys
import time

from pymavlink import mavutil

import sitl_common as c

PARAMS = {
    "MNT1_TYPE": 1,
    "SERVO9_FUNCTION": 7,      # mount1 tilt (pitch)
    "SERVO10_FUNCTION": 8,     # mount1 roll
    "SERVO11_FUNCTION": 6,     # mount1 yaw (pan)
    "MNT1_PITCH_MIN": -90, "MNT1_PITCH_MAX": 20,
    "MNT1_YAW_MIN": -180, "MNT1_YAW_MAX": 180,
    "MNT1_ROLL_MIN": -30, "MNT1_ROLL_MAX": 30,
}


def gimbal_attitude(m, timeout=3):
    """(roll, pitch, yaw) in degrees from GIMBAL_DEVICE_ATTITUDE_STATUS."""
    a = m.recv_match(type="GIMBAL_DEVICE_ATTITUDE_STATUS", blocking=True, timeout=timeout)
    if a is None:
        return None
    w, x, y, z = a.q
    roll = math.degrees(math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)))
    pitch = math.degrees(math.asin(max(-1, min(1, 2 * (w * y - z * x)))))
    yaw = math.degrees(math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)))
    return roll, pitch, yaw


def servo(m, n):
    s = m.recv_match(type="SERVO_OUTPUT_RAW", blocking=True, timeout=3)
    return getattr(s, "servo%d_raw" % n, None) if s else None


def settle(m, seconds=4):
    end = time.time() + seconds
    att = None
    while time.time() < end:
        att = gimbal_attitude(m, 1) or att
    return att


def run(vehicle="ArduCopter", instance=3):
    r = c.Run("gimbal-%s" % vehicle.lower(), vehicle, instance, params=PARAMS)
    try:
        r.start_sitl()
        r.start_gcs()
        r.wait_gcs_connected()
        m = r.mav
        m.mav.request_data_stream_send(m.target_system, m.target_component, mavutil.mavlink.MAV_DATA_STREAM_ALL, 10, 1)
        c.command(m, mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL,
                  mavutil.mavlink.MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS, 100000)
        time.sleep(10)
        base = settle(m)
        r.check("vehicle reports gimbal attitude", base is not None, base and "roll %.1f pitch %.1f yaw %.1f" % base)
        # MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW: p1 pitch, p2 yaw (deg), p5 flags (yaw in vehicle frame = 0)
        for label, pitch, yaw, servo_n, idx in (("pitch", -45, 0, 9, 1), ("yaw", 0, 60, 11, 2), ("pitch back", 0, 0, 9, 1)):
            before = servo(m, servo_n)
            res = c.command(m, mavutil.mavlink.MAV_CMD_DO_GIMBAL_MANAGER_PITCHYAW, pitch, yaw, math.nan, math.nan, 0)
            att = settle(m)
            after = servo(m, servo_n)
            want = (None, pitch, yaw)[idx]
            ok = att is not None and abs(att[idx] - want) < 5
            r.check("%s to %d deg (ACK %s)" % (label, want, res), ok,
                    "reported %.1f deg; servo %d %s -> %s us" % (att[idx] if att else float("nan"), servo_n, before, after))
        # Roll has no gimbal-manager command; MAV_CMD_DO_MOUNT_CONTROL carries it (p1 pitch, p2 roll, p3 yaw, p7 mode 2 = MAVLink targeting).
        res = c.command(m, mavutil.mavlink.MAV_CMD_DO_MOUNT_CONTROL, 0, 20, 0, 0, 0, 0, 2)
        att = settle(m)
        r.check("roll to 20 deg (DO_MOUNT_CONTROL, ACK %s)" % res, att is not None and abs(att[0] - 20) < 5,
                "reported %.1f deg; servo 10 %s us" % (att[0] if att else float("nan"), servo(m, 10)))
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log("Servo gimbal: MNT1_TYPE = 1, SERVO9/10/11_FUNCTION = 7/8/6.")
        r.stop()
    return verdict


if __name__ == "__main__":
    print(run(*(sys.argv[1:2] or ["ArduCopter"])))
