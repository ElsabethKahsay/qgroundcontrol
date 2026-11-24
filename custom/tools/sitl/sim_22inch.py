"""The 22-inch copter in ArduPilot's simulator, set up from its own parameter
file (custom/tools/22Inches.params), to check the mission estimate against
what ArduCopter really does.

    python3 custom/tools/sitl/sim_22inch.py                 # simulator + GCS window, mission A loaded
    python3 custom/tools/sitl/sim_22inch.py --fly           # ... and fly it: AUTO, then RTL, with times
    python3 custom/tools/sitl/sim_22inch.py --no-gcs        # simulator only (connect your own GCS)
    python3 custom/tools/sitl/sim_22inch.py --mission other.waypoints

What is simulated:
- The airframe from models/skywin_m6215_6s.json (12 kg, four 22-inch props,
  6S Li-ion 20 Ah, the maker's thrust test), with home at the mission's home
  (mission A: 9.0481706, 38.8186616, 2632 m).
- Every parameter of 22Inches.params that the simulator can run with: the
  speeds, accelerations, RTL and landing settings, failsafes, tuning, filters,
  battery voltages, outputs. Values that belong to the real hardware are
  replaced by the simulator's own working ones and listed when it starts:
  sensor calibrations and IDs (compass, accelerometers, gyros, barometers),
  serial ports, CAN (the real GPS and battery monitor are on DroneCAN:
  GPS1_TYPE 9, BATT_MONITOR 8), the board, the ESC protocol (DShot), and
  peripherals the simulator does not have (gimbal, camera, rangefinders,
  OSD...). FRAME_TYPE 3 (H) is flown as X (1): the simulator has no H frame,
  and an H is an X with the propellers turning the other way (same positions,
  mass, power and times).

The GCS is the one built in build/Debug, started with a throw-away data folder
(deleted at the end, never the real flight records), already set up as an
operator would leave it: the copter linked to the M6215 motor table, the 6S
Li-ion 20 Ah pack, 8.09 kg without payload, the RC drop at item 3. Enter the
4 kg payload in the Payload item, then open Mission estimate.

--fly flies the loaded mission in AUTO; when it ends in the air (no RTL in the
file) it switches to RTL, as the pilot would. It prints when each item is
reached and when the copter lands, next to the estimate's times.

Ctrl+C stops everything and removes the throw-away folder.
"""

import argparse
import os
import sqlite3
import sys
import time

from pymavlink import mavutil

import sitl_common as c

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PARAMS_FILE = os.path.join(REPO, "custom", "tools", "22Inches.params")
MISSION_A = os.path.join(REPO, "custom", "tools", "motor_tables", "A.waypoints")
MODEL = os.path.join(HERE, "models", "skywin_m6215_6s.json")
TABLE_ID = "m6215-320kv-msc22x7.8-6s"
EMPTY_KG = 8.09
DROP_ITEM = 3

# Parameters that describe the real hardware, not the flying: the simulator
# keeps its own values for these (prefixes, or exact names).
HARDWARE_PREFIXES = (
    "COMPASS_", "INS_ACC", "INS_GYR", "INS_USE", "INS_ENABLE_MASK", "INS_TCAL", "INS_FAST_SAMPLE",
    "INS_GYRO_RATE", "INS_HNTCH_", "INS_HNTC2_", "INS_LOG_", "INS_RAW_LOG",
    "BARO", "AHRS_ORIENTATION", "AHRS_TRIM_", "AHRS_CUSTOM_",
    "SERIAL", "CAN_", "BRD_", "NTF_", "GPS", "BATT2_", "BATT3_", "BATT4_", "BATT5_", "BATT6_", "BATT7_",
    "BATT8_", "BATT9_", "MNT", "CAM", "RNGFND", "PRX", "FLOW_", "EFI_", "RPM", "ESC_", "SERVO_BLH_",
    "SERVO_DSHOT_", "SERVO_FTW_", "SERVO_32_", "SERVO_SBUS_", "OSD", "VTX_", "FRSKY_", "MSP_", "NET_",
    "RELAY", "BTN_", "ADSB_", "ARSPD", "LOG_", "TERRAIN_", "SCR_", "DEV_OPTIONS", "FORMAT_VERSION",
    "STAT_", "SYSID_", "MAV1_", "MAV2_", "MAV3_", "FFT_", "VISO_", "RC_PROTOCOLS", "RSSI_",
    "FILT", "TCAL", "SID_", "EK3_IMU_MASK", "EK3_PRIMARY", "BATT_MONITOR", "BATT_VOLT_PIN",
    "BATT_CURR_PIN", "BATT_VOLT_MULT", "BATT_AMP_", "BATT_SERIAL_NUM", "BATT_I2C_", "BATT_OPTIONS",
    "BATT_ESC_INDEX", "BATT_SUM_MASK", "BATT_CURR_MULT", "BATT_MAX_AMPS", "BATT_SHUNT",
)
# Tuning values among those prefixes: kept.
KEEP = {"INS_GYRO_FILTER", "INS_ACCEL_FILTER"}
# What the simulator flies instead.
OVERRIDES = {
    "FRAME_TYPE": 1,        # H flown as X: same positions, props turning the other way
    "MOT_PWM_TYPE": 0,      # the simulated motors take PWM, not DShot
    # The simulator's quad has motor N on output N. The real copter's wiring
    # may differ (since Nov 2026: SERVO1/2/3 = motors 2/3/1), which in the
    # simulator would drive each motor from another's position.
    "SERVO1_FUNCTION": 33, "SERVO2_FUNCTION": 34, "SERVO3_FUNCTION": 35, "SERVO4_FUNCTION": 36,
}


def read_params(path):
    """QGroundControl's .params ("sysid compid NAME VALUE TYPE") or
    Mission Planner's ("NAME,VALUE")."""
    values = {}
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace(",", " ").split()
            if len(parts) >= 5 and parts[0].isdigit():
                name, value = parts[2], parts[3]
            elif len(parts) >= 2:
                name, value = parts[0], parts[1]
            else:
                continue
            values[name] = float(value)
    return values


def flying_params(path):
    """The file's values the simulator can fly with, and the ones left out."""
    kept, left = {}, []
    for name, value in read_params(path).items():
        if name.startswith(HARDWARE_PREFIXES) and name not in KEEP:
            left.append(name)
            continue
        kept[name] = int(value) if float(value).is_integer() else value
    # Two RC channels with the same switch option make ArduCopter refuse to
    # arm ("Duplicate Aux Switch Options"). The simulator keeps the first and
    # clears the others so it can fly; the real file still needs fixing.
    seen = {}
    for name in sorted((n for n in kept if n.startswith("RC") and n.endswith("_OPTION")),
                       key=lambda n: int(n[2:-7]) if n[2:-7].isdigit() else 99):
        option = kept[name]
        if option and option in seen:
            print("note: %s=%s duplicates %s: cleared for the simulator" % (name, option, seen[option]))
            kept[name] = 0
        elif option:
            seen[option] = name
    kept.update(OVERRIDES)
    return kept, sorted(left)


def sitl_copter_defaults():
    """ArduPilot's SITL copter defaults: sensor and port values the simulator
    needs (the drone's own values go on top)."""
    path = os.path.join(os.path.expanduser("~/ardupilot"), "Tools", "autotest", "default_params", "copter.parm")
    values = {}
    with open(path) as fh:
        for line in fh:
            parts = line.split("#", 1)[0].split()
            if len(parts) >= 2:
                values[parts[0]] = float(parts[1]) if "." in parts[1] else int(parts[1])
    return values


def read_mission(path):
    """Mission Planner .waypoints: list of dicts (item 0 is home)."""
    items = []
    with open(path) as fh:
        lines = [l.rstrip("\r\n") for l in fh]
    if not lines or not lines[0].startswith("QGC WPL 110"):
        raise SystemExit("%s is not a QGC WPL 110 .waypoints file" % path)
    for line in lines[1:]:
        f = line.split("\t")
        if len(f) < 12:
            continue
        items.append({"seq": int(f[0]), "current": int(f[1]), "frame": int(f[2]), "command": int(f[3]),
                      "p": [float(x) for x in f[4:8]], "lat": float(f[8]), "lon": float(f[9]),
                      "alt": float(f[10]), "auto": int(float(f[11]))})
    return items


def upload_mission(m, items):
    """MISSION_COUNT / MISSION_ITEM_INT handshake. The clear before it is
    acknowledged too: that acknowledgement is read first, and the upload's own
    one counts only once every item has been sent (taking the clear's for it
    ended the upload before any item went, leaving the autopilot waiting)."""
    m.mav.mission_clear_all_send(m.target_system, m.target_component)
    m.recv_match(type="MISSION_ACK", blocking=True, timeout=3)
    m.mav.mission_count_send(m.target_system, m.target_component, len(items))
    sent = set()
    deadline = time.time() + 30
    while time.time() < deadline:
        req = m.recv_match(type=["MISSION_REQUEST", "MISSION_REQUEST_INT", "MISSION_ACK"], blocking=True, timeout=5)
        if req is None:
            continue
        if req.get_type() == "MISSION_ACK":
            if len(sent) < len(items):
                continue   # a late acknowledgement of the clear
            return req.type == mavutil.mavlink.MAV_MISSION_ACCEPTED
        sent.add(req.seq)
        it = items[req.seq]
        m.mav.mission_item_int_send(m.target_system, m.target_component, it["seq"], it["frame"], it["command"],
                                    0, it["auto"], it["p"][0], it["p"][1], it["p"][2], it["p"][3],
                                    int(it["lat"] * 1e7), int(it["lon"] * 1e7), it["alt"])
    return False


def mission_count(m):
    """How many items the copter has (MISSION_REQUEST_LIST); -1 when no answer."""
    m.mav.mission_request_list_send(m.target_system, m.target_component)
    msg = m.recv_match(type="MISSION_COUNT", blocking=True, timeout=5)
    return msg.count if msg else -1


def ensure_mission(r, m, items, tries=6):
    """Uploads until the copter reports the mission back: an upload in the
    first seconds after start-up can be accepted and then lost."""
    for _ in range(tries):
        upload_mission(m, items)
        time.sleep(1)
        n = mission_count(m)
        if n == len(items):
            return True
        r.note("the copter has %d mission items, %d sent: uploading again" % (n, len(items)))
        time.sleep(4)
    return False


def seed_gcs(r, uid):
    """The GCS as an operator would leave it, written while it is closed."""
    assert r.db_path != c.REAL_DB
    con = sqlite3.connect(r.db_path, timeout=10)
    try:
        con.execute("INSERT OR REPLACE INTO vehicle_motor_table (device_uid, table_id, linked_at) VALUES (?, ?, ?)",
                    (uid, TABLE_ID, c.utc_now()))
        con.execute("UPDATE vehicles SET battery_series_cells = 6, battery_parallel_cells = 1, "
                    "battery_cell_mah = 20000, battery_type = 'LiIon' WHERE device_uid = ?", (uid,))
        con.commit()
    finally:
        con.close()
    ini_dir = os.path.join(r.gcs_env()["XDG_CONFIG_HOME"], "UAVPreflight")
    for name in c.GCS_INI_NAMES:
        with open(os.path.join(ini_dir, name), "a") as f:
            f.write("\n[MissionEstimate]\nemptyKg\\%s=%s\n" % (uid, EMPTY_KG))
    with open(os.path.join(ini_dir, "UAVPreflight.ini"), "a") as f:
        f.write("\n[General]\npayloadDropItem=%d\npayloadCount=1\n" % DROP_ITEM)
    r.note("GCS set up: motor table, 6S Li-ion 20 Ah, %.2f kg without payload, RC drop at item %d"
           % (EMPTY_KG, DROP_ITEM))


def clock(s):
    t = int(round(s))
    return "%d:%02d" % (t // 60, t % 60)


def fly(r, m, items):
    """AUTO from the ground; RTL when the mission ends in the air; times."""
    c.wait_ekf_ready(m, 180)
    if mission_count(m) != len(items):
        ensure_mission(r, m, items)
    # Armed in AUTO from the ground; MISSION_START begins it (no RC throttle).
    c.set_mode(m, "AUTO")
    deadline = time.time() + 120
    while True:
        res = c.command(m, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 1)
        if res == mavutil.mavlink.MAV_RESULT_ACCEPTED or time.time() > deadline:
            break
        r.note("arm refused: %s" % c.statustexts(m, 2)[-2:])
        time.sleep(5)
    if not c.wait_armed(m, True, 20):
        r.note("could not arm: %s" % c.statustexts(m, 3))
        return
    c.command(m, mavutil.mavlink.MAV_CMD_MISSION_START)
    t0 = time.time()
    r.note("AUTO: mission started")
    last = max(it["seq"] for it in items)
    ends_with_return = items[-1]["command"] in (20, 21)
    rtl_at = None
    while time.time() - t0 < 1800:
        msg = m.recv_match(type=["MISSION_ITEM_REACHED", "HEARTBEAT", "STATUSTEXT"], blocking=True, timeout=1)
        if msg is None:
            continue
        t = time.time() - t0
        if msg.get_type() == "MISSION_ITEM_REACHED":
            r.note("%s  item %d reached" % (clock(t), msg.seq))
            if msg.seq == last and not ends_with_return and rtl_at is None:
                time.sleep(1.0)
                c.set_mode(m, "RTL")
                rtl_at = time.time() - t0
                r.note("%s  mission ended in the air: RTL (as the pilot would)" % clock(rtl_at))
        elif msg.get_type() == "STATUSTEXT":
            r.note("%s  autopilot: %s" % (clock(t), msg.text))
        elif msg.get_type() == "HEARTBEAT" and msg.get_srcSystem() == m.target_system:
            if not (msg.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED) and t > 5:
                if rtl_at is None and not ends_with_return:
                    r.note("%s  disarmed before the mission ended" % clock(t))
                    return
                r.note("%s  landed and disarmed: total %s" % (clock(t), clock(t)))
                return


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mission", default=MISSION_A)
    ap.add_argument("--params", default=PARAMS_FILE)
    ap.add_argument("--fly", action="store_true", help="fly the mission (AUTO, then RTL) and print the times")
    ap.add_argument("--no-gcs", action="store_true", help="simulator only; connect your own GCS to UDP 14550")
    ap.add_argument("--instance", type=int, default=7)
    args = ap.parse_args()

    items = read_mission(args.mission)
    home = items[0]
    kept, left = flying_params(args.params)
    params = dict(sitl_copter_defaults(), **kept)
    sim_args = ["-f", "x", "--model", "x:%s" % MODEL,
                "-l", "%.7f,%.7f,%.1f,0" % (home["lat"], home["lon"], home["alt"])]

    r = c.Run("sim-22inch", "ArduCopter", args.instance, params=params, sim_args=sim_args)
    r.note("parameters from %s: %d flown, %d hardware ones left to the simulator"
           % (os.path.relpath(args.params, REPO), len(kept), len(left)))
    r.note("left to the simulator: %s" % ", ".join(left))
    r.note("changed for the simulator: %s" % ", ".join("%s=%s" % kv for kv in OVERRIDES.items()))
    try:
        r.start_sitl()
        m = r.mav
        for name in ("WP_SPD", "WP_SPD_UP", "WP_SPD_DN", "RTL_ALT_M", "RTL_SPEED_MS", "RTL_LOIT_TIME",
                     "LAND_SPD_MS", "LAND_SPD_HIGH_MS", "LAND_ALT_LOW_M", "BATT_LOW_VOLT", "BATT_FS_LOW_ACT"):
            m.mav.param_request_read_send(m.target_system, m.target_component, name.encode(), -1)
            pv = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=3)
            r.note("  %-18s %s (file: %s)" % (name, "%g" % pv.param_value if pv else "not read",
                                               "%g" % kept[name] if name in kept else "-"))
        c.wait_ekf_ready(m, 180, prearm=False)
        r.note("mission %s: %s" % (os.path.basename(args.mission),
                                   "on the copter (%d items)" % len(items) if ensure_mission(r, m, items)
                                   else "UPLOAD FAILED"))
        if not args.no_gcs:
            r.write_gcs_settings()
            env = r.gcs_env()
            env.pop("QT_QPA_PLATFORM", None)            # a real window
            r.gcs_env = lambda: env
            r.start_gcs(fresh_settings=False)
            conns = r.wait_gcs_connected()
            if conns:
                uid = conns[-1]["device_uid"]
                r.wait_rows("SELECT device_uid FROM vehicles WHERE device_uid = ?", (uid,))
                r.kill_gcs()
                seed_gcs(r, uid)
                r.start_gcs(fresh_settings=False)
                r.note("GCS window open (throw-away data in %s)" % r.work)
        else:
            r.note("connect a GCS to UDP %d" % r.ports["gcs_udp"])
        if args.fly:
            fly(r, m, items)
        r.note("running: Ctrl+C to stop the simulator%s" % ("" if args.no_gcs else " and the GCS"))
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        r.stop()


if __name__ == "__main__":
    sys.exit(main())
