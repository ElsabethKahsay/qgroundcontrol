"""The mission estimate against ArduCopter's simulator: the 22-inch copter
flies a mission in AUTO, and the GCS's take-off prediction is compared with
what the simulated copter really did.

    python3 custom/tools/sitl/sc_mission_estimate.py                       # mission A
    python3 custom/tools/sitl/sc_mission_estimate.py --mission other.waypoints
    python3 custom/tools/sitl/sc_mission_estimate.py --mission long        # ~6 km rectangle, 50 m, RTL
    python3 custom/tools/sitl/sc_mission_estimate.py --mission no-rtl      # mission A without its RTL
    python3 custom/tools/sitl/sc_mission_estimate.py --ladder              # results by distance, on the ground

Built-in missions (from mission A's home, frames relative to home):
- long: take off to 50 m, 2 km north, 1 km east, 2 km south, then RTL: about
  6 km at WP_SPD with head, tail and cross winds, most of the pack;
- no-rtl: mission A without its last item (RTL); the script switches to RTL
  when the autopilot says the mission is complete, as the pilot would, and
  the GCS's assumed return is compared with ArduPilot's;
- ladder: straight out-and-back missions (take off to 50 m, one waypoint D km
  out, RTL) for D from 0.5 to 5 km, each only assessed: the GCS is restarted
  to load it, the copter armed in GUIDED (it stays on the ground) and
  disarmed, and the mission estimate item's result at arming is read back.
  The results must get no better as D grows, the shortest must be
  Comfortable and the longest Can't be done.

Set-up (the same as sim_22inch.py, which this reuses):
- the airframe from models/skywin_m6215_6s.json (12 kg, four 22-inch props,
  6S Li-ion 20 Ah, the maker's thrust test), home at the mission's home;
- every flying parameter of custom/tools/22Inches.params; the hardware ones
  are left to the simulator (listed in the run log);
- for the simulator only: AUTO_OPTIONS = 3 (arm in AUTO and take off without
  raising the RC throttle: the simulator has no pilot), the wind the same at
  every height (SIM_WIND_T = 1), and flat ground at home's height
  (SIM_TERRAIN = 0). With the real terrain, a mission flown at a fixed height
  above home meets the ground where it rises (north of mission A's home it
  rises 50 m in 500 m), so the time and charge are compared over flat ground.
  The GCS's terrain check still sees the real terrain (QGroundControl's
  terrain data, online): for the long mission it must warn that the mission
  comes too close to the ground.

The GCS runs headless with a throw-away data folder, set up as an operator
would leave it: the copter linked to the M6215 motor table, the 6S Li-ion
20 Ah pack, and an empty weight of 12.0 kg (the simulated copter's mass; a
payload can only be entered in a dialog, so none is carried and nothing is
dropped).

The wind: the GCS fetches the forecast for the site as usual; the simulator
is then given the same wind, the forecast at the mission's height (between
its 80 and 120 m values, in ln(height), as the GCS reads it), so both fly in
the same air. Without a forecast the simulator stays calm and the run log
says so.

The flight: armed in AUTO, the mission flown to its end; a mission that ends
in the air gets RTL, as the pilot would. Each item is logged when the
autopilot reports it reached.

Checked afterwards, from the GCS's own records:
- the mission estimate item's result at arming (the checklist as it stood);
- the flight's record for the charger comparison, with the mission record:
  the prediction made at arming next to what happened (total time, landing
  charge, each item's time and charge). The landing charge is checked against
  the GCS's own count (the same motor table: a difference is the estimate's);
  the simulator's own count is only logged, its motor model being a simpler
  fit than the maker's table;
- the GCS's recorded item times against the autopilot's own.
The differences are logged; the checks have loose limits (they test that the
estimate is in the right range, not exact).
"""

import argparse
import glob
import json
import math
import os
import re
import sqlite3
import sys
import time

from pymavlink import mavutil

import sitl_common as c
import sim_22inch as s22

TIME_TOLERANCE = 0.20        # predicted total time within 20 % of the real one
CHARGE_TOLERANCE_PTS = 8.0   # predicted likely landing charge within 8 points
EMPTY_KG = 12.0              # the simulated copter's mass (models/skywin_m6215_6s.json)
PACK_AH = 20.0               # the pack the GCS is set up with (seed_gcs)
SIM_ONLY = {"AUTO_OPTIONS": 3, "SIM_WIND_T": 1, "SIM_WIND_SPD": 0, "SIM_WIND_DIR": 0, "SIM_TERRAIN": 0}


def clock(s):
    if s is None or s < 0:
        return "--"
    t = int(round(s))
    return "%d:%02d" % (t // 60, t % 60)


def seed_gcs(r, uid):
    """Motor table, pack and empty weight, written while the GCS is closed."""
    assert r.db_path != c.REAL_DB
    con = sqlite3.connect(r.db_path, timeout=10)
    try:
        con.execute("INSERT OR REPLACE INTO vehicle_motor_table (device_uid, table_id, linked_at) VALUES (?, ?, ?)",
                    (uid, s22.TABLE_ID, c.utc_now()))
        con.execute("UPDATE vehicles SET battery_series_cells = 6, battery_parallel_cells = 1, "
                    "battery_cell_mah = 20000, battery_type = 'LiIon' WHERE device_uid = ?", (uid,))
        con.commit()
    finally:
        con.close()
    ini_dir = os.path.join(r.gcs_env()["XDG_CONFIG_HOME"], "UAVPreflight")
    for name in c.GCS_INI_NAMES:
        with open(os.path.join(ini_dir, name), "a") as f:
            f.write("\n[MissionEstimate]\nemptyKg\\%s=%s\n" % (uid, EMPTY_KG))
    r.note("GCS set up: motor table, 6S Li-ion 20 Ah, %.1f kg empty, no payload" % EMPTY_KG)


def forecast_wind(r, height_m):
    """The wind the GCS's forecast gives at @height_m (speed m/s, direction it
    comes from, degrees), for the forecast hour nearest now; None without one."""
    files = glob.glob(os.path.join(r.work, "data", "**", "site_weather.json"), recursive=True)
    if not files:
        return None
    with open(files[0]) as fh:
        saved = json.load(fh)
    try:
        fc = json.loads(saved.get("forecast") or "{}")
    except ValueError:
        return None
    h = fc.get("hourly") or {}
    times = h.get("time") or []
    if not times:
        return None
    now = time.time()
    i = min(range(len(times)), key=lambda k: abs(times[k] - now))

    def at(name):
        v = (h.get(name) or [None] * len(times))[i]
        return float(v) if v is not None else None

    levels = [(hm, at("wind_speed_%dm" % hm), at("wind_direction_%dm" % hm)) for hm in (10, 80, 120)]
    if any(v is None for _, sp, d in levels for v in (sp, d)):
        return None
    # Speed in ln(height) between the two levels around it (the nearest one
    # outside 10-120 m); direction as a vector (as SiteWeather does).
    hgt = min(max(height_m, 10.0), 120.0)
    lo, hi = (levels[0], levels[1]) if hgt <= 80.0 else (levels[1], levels[2])
    f = (math.log(hgt) - math.log(lo[0])) / (math.log(hi[0]) - math.log(lo[0]))
    speed = lo[1] + f * (hi[1] - lo[1])
    k = math.pi / 180.0
    vx = (1 - f) * lo[1] * math.sin(lo[2] * k) + f * hi[1] * math.sin(hi[2] * k)
    vy = (1 - f) * lo[1] * math.cos(lo[2] * k) + f * hi[1] * math.cos(hi[2] * k)
    direction = (math.atan2(vx, vy) / k) % 360.0
    return speed, direction, times[i]


def item(seq, command, lat=0.0, lon=0.0, alt=0.0, frame=3, p=(0.0, 0.0, 0.0, 0.0)):
    return {"seq": seq, "current": 0, "frame": frame, "command": command, "p": list(p),
            "lat": lat, "lon": lon, "alt": alt, "auto": 1}


def offset(home, north_m, east_m):
    """Latitude and longitude @north_m and @east_m from home."""
    return (home["lat"] + north_m / 111194.93,
            home["lon"] + east_m / (111194.93 * math.cos(home["lat"] * math.pi / 180.0)))


def mission_through(home, legs, alt=50.0, rtl=True):
    """Home, take off to @alt, a waypoint at each (north, east) metres, RTL."""
    items = [dict(home, seq=0)]
    items.append(item(1, 22, alt=alt))
    for n, e in legs:
        lat, lon = offset(home, n, e)
        items.append(item(len(items), 16, lat, lon, alt))
    if rtl:
        items.append(item(len(items), 20))
    return items


def built_in(name, mission_a):
    home = mission_a[0]
    if name == "long":
        return mission_through(home, [(2000, 0), (2000, 1000), (0, 1000)])
    if name == "no-rtl":
        return [it for it in mission_a if it["command"] not in (20, 21)]
    return None


LADDER_KM = (0.5, 1.5, 2.0, 2.5, 3.0, 3.5, 5.0)
RESULT_ORDER = ["Comfortable", "Fits", "Tight", "Uses the reserve", "One way only", "Can't be done", "Too heavy"]


# --gps-poor: (from, to) seconds after the arm while the simulated GPS
# reports 20 m accuracy (the real copter's interference: satellites kept,
# position wandering).
GPS_POOR_WINDOW = None


def fly(r, m, items):
    """Armed in AUTO, the mission to its end (RTL when it ends in the air).
    Returns {seq: seconds from the arm} for the items the autopilot reported
    reached, and the seconds from the arm to the disarm (None when it did
    not land)."""
    c.wait_ekf_ready(m, 180)
    if s22.mission_count(m) != len(items):
        s22.ensure_mission(r, m, items)
    c.set_mode(m, "AUTO")
    deadline = time.time() + 120
    while True:
        res = c.command(m, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 1)
        if res == mavutil.mavlink.MAV_RESULT_ACCEPTED or time.time() > deadline:
            break
        r.note("arm refused: %s" % c.statustexts(m, 2)[-2:])
        time.sleep(5)
    if not r.check("armed in AUTO", c.wait_armed(m, True, 20)):
        return {}, None
    t0 = time.time()
    c.command(m, mavutil.mavlink.MAV_CMD_MISSION_START)
    r.note("0:00  armed, mission started")
    reached = {}
    home = items[0]
    last_log = last_progress = time.time()
    last = max(it["seq"] for it in items)
    ends_with_return = items[-1]["command"] in (20, 21)
    rtl_sent = False
    gps_degraded = False
    while time.time() - t0 < 1800:
        if GPS_POOR_WINDOW:
            el = time.time() - t0
            want = GPS_POOR_WINDOW[0] <= el < GPS_POOR_WINDOW[1]
            if want != gps_degraded:
                c.param_set(m, "SIM_GPS1_ACC", 20.0 if want else 0.3)
                gps_degraded = want
                r.note("%s  simulated GPS accuracy %s" % (clock(el), "20 m" if want else "back to 0.3 m"))
        msg = m.recv_match(type=["MISSION_ITEM_REACHED", "HEARTBEAT", "STATUSTEXT", "MISSION_CURRENT"],
                           blocking=True, timeout=1)
        if time.time() - last_log >= 30:
            # Where the copter is, every 30 s.
            last_log = time.time()
            g, v, b = m.messages.get("GLOBAL_POSITION_INT"), m.messages.get("VFR_HUD"), m.messages.get("SYS_STATUS")
            if g:
                dn = (g.lat / 1e7 - home["lat"]) * 111194.93
                de = (g.lon / 1e7 - home["lon"]) * 111194.93 * math.cos(home["lat"] * math.pi / 180.0)
                r.note("%s  at %.0f m N, %.0f m E, %.0f m up, %.1f m/s, mode %s, item %s, %.1f V"
                       % (clock(time.time() - t0), dn, de, g.relative_alt / 1000.0, v.groundspeed if v else -1,
                          mavutil.mode_string_v10(m.messages["HEARTBEAT"]) if "HEARTBEAT" in m.messages else "?",
                          getattr(m.messages.get("MISSION_CURRENT"), "seq", "?"),
                          b.voltage_battery / 1000.0 if b else -1))
        if time.time() - last_progress > 480:
            r.note("no mission progress for 8 min: giving up")
            return reached, None
        if msg is None:
            continue
        t = time.time() - t0
        kind = msg.get_type()
        if kind == "MISSION_ITEM_REACHED" and msg.seq not in reached:
            reached[msg.seq] = t
            last_progress = time.time()
            r.note("%s  item %d reached" % (clock(t), msg.seq))
            if msg.seq == last and not ends_with_return and not rtl_sent:
                time.sleep(1.0)
                c.set_mode(m, "RTL")
                rtl_sent = True
                r.note("%s  mission ended in the air: RTL" % clock(time.time() - t0))
        elif kind == "STATUSTEXT":
            r.note("%s  autopilot: %s" % (clock(t), msg.text))
        elif kind == "MISSION_CURRENT":
            # ArduCopter says nothing when a mission ends in the air (it holds
            # position); MISSION_CURRENT's state 5 is "complete".
            if getattr(msg, "mission_state", 0) == 5 and not ends_with_return and not rtl_sent:
                c.set_mode(m, "RTL")
                rtl_sent = True
                r.note("%s  mission complete in the air: RTL" % clock(t))
        elif kind == "HEARTBEAT" and msg.get_srcSystem() == m.target_system and t > 5:
            if not (msg.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED):
                r.note("%s  disarmed" % clock(t))
                return reached, t
    r.note("still armed after 30 min")
    return reached, None


def assess_on_ground(r, m):
    """Armed in GUIDED (it stays on the ground) and disarmed; the mission
    estimate item's message at that arming, and whether it armed."""
    before = r.q("SELECT COALESCE(MAX(id), 0) AS n FROM flight_check_results")[0]["n"]
    c.set_mode(m, "GUIDED")
    c.command(m, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 1)
    armed = c.wait_armed(m, True, 20)
    time.sleep(4)
    c.command(m, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0)
    c.wait_armed(m, False, 20)
    rows = r.wait_rows("SELECT message FROM flight_check_results WHERE check_id = 'power.mission_estimate' "
                       "AND id > ? ORDER BY id DESC LIMIT 1", (before,), timeout=20)
    return (rows[0]["message"] if rows else ""), armed


def ladder(r, m, home):
    """Each out-and-back distance assessed on the ground: the GCS restarted to
    load the mission, armed in GUIDED (no take-off) and disarmed; the mission
    estimate item's result at arming read back."""
    results = []
    for km in LADDER_KM:
        items = mission_through(home, [(km * 1000.0, 0.0)])
        r.kill_gcs()   # not talking the mission protocol while the new mission goes up
        if not s22.ensure_mission(r, m, items):
            r.check("%.1f km mission on the copter" % km, False)
            continue
        r.start_gcs(fresh_settings=False)
        r.wait_gcs_connected()
        msg, armed = "", False
        for attempt in range(2):
            time.sleep(45)   # the GCS loads the mission and home, and works out the estimate
            msg, armed = assess_on_ground(r, m)
            if "Home position not known" not in msg:
                break
            r.note("%.1f km: the GCS had no home yet, assessing again" % km)
        result = next((name for name in RESULT_ORDER if msg.startswith(name)),
                      "Can't assess" if msg.startswith("Can't assess") else "?")
        r.note("%5.1f km out and back: %s%s" % (km, msg, "" if armed else " (did not arm)"))
        results.append((km, result))
    ranks = [RESULT_ORDER.index(res) if res in RESULT_ORDER else -1 for _, res in results]
    r.note("ladder: " + ", ".join("%.1f km %s" % kr for kr in results))
    r.check("every distance assessed", len(results) == len(LADDER_KM) and all(k >= 0 for k in ranks))
    r.check("results get no better as the distance grows", all(a <= b for a, b in zip(ranks, ranks[1:])),
            " -> ".join(res for _, res in results))
    r.check("the shortest is Comfortable", bool(results) and results[0][1] == "Comfortable")
    r.check("the longest is Can't be done", bool(results) and results[-1][1] == "Can't be done")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mission", default=s22.MISSION_A)
    ap.add_argument("--params", default=s22.PARAMS_FILE)
    ap.add_argument("--instance", type=int, default=8)
    ap.add_argument("--ladder", action="store_true", help="assess missions of growing length on the ground")
    ap.add_argument("--gps-poor", action="store_true",
                    help="the GPS reports 20 m accuracy from 40 s to 90 s after the arm")
    ap.add_argument("--dead-current", action="store_true",
                    help="the copter's current sensor reads 0 A (BATT_AMP_PERVLT 0), as on the real 22-inch copter")
    args = ap.parse_args()

    mission_a = s22.read_mission(s22.MISSION_A)
    if args.ladder:
        name = "ladder"
        items = mission_through(mission_a[0], [(LADDER_KM[0] * 1000.0, 0.0)])
    elif built_in(args.mission, mission_a):
        name = args.mission
        items = built_in(args.mission, mission_a)
    else:
        name = os.path.splitext(os.path.basename(args.mission))[0]
        items = s22.read_mission(args.mission)
    home = items[0]
    kept, left = s22.flying_params(args.params)
    params = dict(s22.sitl_copter_defaults(), **kept)
    params.update(SIM_ONLY)
    if args.dead_current:
        params.update({"BATT_AMP_PERVLT": 0, "BATT_AMP_OFFSET": 0})
        name += "-dead-current"
    if args.gps_poor:
        global GPS_POOR_WINDOW
        GPS_POOR_WINDOW = (40.0, 90.0)
        name += "-gps-poor"
    sim_args = ["-f", "x", "--model", "x:%s" % s22.MODEL,
                "-l", "%.7f,%.7f,%.1f,0" % (home["lat"], home["lon"], home["alt"])]
    top_m = max((it["alt"] for it in items[1:] if it["frame"] in (3, 10)), default=0.0)

    r = c.Run("mission-estimate-%s" % name, "ArduCopter", args.instance, params=params, sim_args=sim_args)
    r.note("mission %s, %d items, highest %.0f m" % (name, len(items), top_m))
    for it in items:
        r.note("  item %d: command %d, %.7f, %.7f, %.0f m (frame %d)"
               % (it["seq"], it["command"], it["lat"], it["lon"], it["alt"], it["frame"]))
    r.note("parameters from %s: %d flown; left to the simulator: %s"
           % (os.path.relpath(args.params, s22.REPO), len(kept), ", ".join(left)))
    r.note("changed for the simulator: %s" % ", ".join(
        "%s=%s" % kv for kv in list(s22.OVERRIDES.items()) + list(SIM_ONLY.items())))
    verdict = "FAIL"
    try:
        r.start_sitl()
        m = r.mav
        c.wait_ekf_ready(m, 180, prearm=False)
        r.check("mission on the copter", s22.ensure_mission(r, m, items))

        r.start_gcs()
        conns = r.wait_gcs_connected()
        if not conns:
            raise RuntimeError("the GCS did not connect")
        uid = conns[-1]["device_uid"]
        r.wait_rows("SELECT device_uid FROM vehicles WHERE device_uid = ?", (uid,))
        r.kill_gcs()
        seed_gcs(r, uid)
        log_from = os.path.getsize(r.gcs_log.name) if os.path.exists(r.gcs_log.name) else 0
        r.start_gcs(fresh_settings=False)
        r.wait_gcs_connected()
        time.sleep(30)   # parameters, the mission, the motor table and the weather load

        wind = forecast_wind(r, top_m)
        if wind:
            speed, direction, hour = wind
            c.param_set(m, "SIM_WIND_SPD", round(speed, 2))
            c.param_set(m, "SIM_WIND_DIR", round(direction, 1))
            r.note("wind from the GCS's forecast (hour %s UTC) at %.0f m: %.1f m/s from %.0f deg, set in the simulator"
                   % (time.strftime("%H:%M", time.gmtime(hour)), top_m, speed, direction))
        else:
            r.note("no forecast in the GCS (no network?): the simulator stays calm")

        if args.ladder:
            ladder(r, m, mission_a[0])
            return

        # As a pilot would (minutes on the checklist), arm once the GCS has
        # the terrain under every leg, or after 2 minutes.
        t_wait = time.time()
        complete = False
        while time.time() - t_wait < 120 and not complete:
            with open(r.gcs_log.name) as fh:
                fh.seek(log_from)   # this run of the GCS only, not the one that set it up
                for line in fh:
                    mt = re.search(r"terrain along the mission: (\d+) of (\d+) legs", line)
                    if mt and mt.group(1) == mt.group(2) and int(mt.group(2)) > 0:
                        complete = True
            if not complete:
                time.sleep(2)
        r.note("terrain %s after %.0f s waiting" % ("complete" if complete else "NOT complete", time.time() - t_wait))

        reached, total = fly(r, m, items)
        r.check("landed and disarmed", total is not None)
        time.sleep(5)

        # The checklist as it stood at arming: the mission estimate item.
        rows = r.q("SELECT status, message FROM flight_check_results WHERE check_id = 'power.mission_estimate' "
                   "ORDER BY id DESC LIMIT 1")
        item = rows[0] if rows else {}
        r.note("mission estimate item at arming: %s: %s" % (item.get("status"), item.get("message")))
        r.check("mission estimate assessed at arming",
                bool(item) and not (item.get("message") or "").startswith("Can't assess"), item.get("message") or "")
        # The terrain: the long mission flies north at 50 m above home into
        # ground that rises 50 m in 500 m.
        warned = "too close to the ground" in (item.get("message") or "")
        if name.startswith("long"):
            r.check("the GCS warns that the mission comes too close to the ground", warned, item.get("message") or "")
        else:
            r.note("terrain warning at arming: %s" % ("yes" if warned else "no"))

        flights = r.wait_rows("SELECT * FROM motor_table_flights WHERE device_uid = ? ORDER BY id", (uid,), timeout=30)
        f = flights[-1] if flights else {}
        r.check("flight recorded for the charger comparison", bool(f))
        rec = json.loads(f.get("mission_json") or "{}") if f else {}
        if not r.check("mission record kept with the flight", bool(rec)):
            raise RuntimeError("no mission record")

        r.note("result at take-off: %s" % rec.get("resultAtTakeoff"))
        pt, rt = rec.get("predTotalS", -1), rec.get("realTotalS", -1)
        r.note("total time: predicted %s, real %s (GCS, arm to disarm); autopilot arm to disarm %s"
               % (clock(pt), clock(rt), clock(total)))
        pl, pp, rl = rec.get("predLandLikely", -1), rec.get("predLandPess", -1), rec.get("realLandSoc", -1)
        r.note("landing charge: predicted %.0f %% likely, %.0f %% pessimistic; real %.0f %%" % (pl, pp, rl))
        r.note("charge used: GCS count %.2f Ah" % (f.get("gcs_ah") or -1))
        r.note("flight record: hover %s W over %s s, cruise ratio %s over %s s, air density %s, pack %s ohm"
               % tuple(f.get(k) for k in ("hover_w", "hover_s", "cruise_ratio", "cruise_s", "density", "pack_ohm")))
        r.note("item  predicted  GCS real  autopilot  | charge predicted (likely/pess)  real")
        for it in rec.get("items", []):
            seq = it.get("seq")
            r.note("%4d  %9s  %8s  %9s  | %5.0f %% / %5.0f %%  %6s"
                   % (seq, clock(it.get("predTimeS")), clock(it.get("realTimeS")), clock(reached.get(seq)),
                      it.get("predSocLikely", -1), it.get("predSocPess", -1),
                      "%.0f %%" % it["realSoc"] if it.get("realSoc", -1) >= 0 else "--"))

        if pt > 0 and rt > 0:
            r.check("predicted total time within %d %% of the real one" % int(TIME_TOLERANCE * 100),
                    abs(pt - rt) <= TIME_TOLERANCE * rt, "%+.0f s (%+.1f %%)" % (pt - rt, (pt - rt) / rt * 100))
        # The prediction against the GCS's own count (its charge at arming
        # less the Ah it counted in flight): the same motor table, so a
        # difference is the estimate's. The simulator's own count is logged
        # beside it, not checked: its motor model is a simpler fit than the
        # maker's table, and neither is the real copter.
        start_soc, gcs_ah = f.get("start_soc"), f.get("gcs_ah")
        if pl >= 0 and start_soc is not None and start_soc >= 0 and gcs_ah:
            own = start_soc - gcs_ah / PACK_AH * 100.0
            r.check("predicted landing charge within %.0f points of the GCS's own count" % CHARGE_TOLERANCE_PTS,
                    abs(pl - own) <= CHARGE_TOLERANCE_PTS,
                    "predicted %.0f %%, GCS count %.0f %% (%+.1f points)" % (pl, own, pl - own))
            r.check("landed at or above the pessimistic prediction (GCS count)", own >= pp - 1e-6,
                    "%.0f %% vs %.0f %%" % (own, pp))
        if pl >= 0 and rl >= 0:
            r.note("simulator's own count: landed at %.0f %% (predicted %.0f %%, %+.1f points; not checked)"
                   % (rl, pl, pl - rl))
        gaps = [abs(it["realTimeS"] - reached[it["seq"]]) for it in rec.get("items", [])
                if it.get("realTimeS", -1) >= 0 and it.get("seq") in reached]
        if gaps:
            r.check("the GCS's item times match the autopilot's (within 3 s)", max(gaps) <= 3.0,
                    "largest gap %.1f s over %d items" % (max(gaps), len(gaps)))

        r.kill_gcs()
        with open(r.gcs_log.name) as fh:
            log = fh.read()
        for line in log.splitlines():
            if ("battery estimate" in line or "RETURN NOW" in line or "drag constant" in line
                    or "current reads" in line or "current is measured" in line or "terrain" in line
                    or "GPS" in line):
                r.note("GCS log: " + line.strip()[:220])
        if args.gps_poor:
            # The GPS reports 20 m accuracy for 50 s in flight: the GCS marks
            # it unreliable at once, says so once, and trusts it again 10 s
            # after it is good.
            r.check("the GCS found the GPS unreliable in flight", "GPS unreliable: position accuracy too poor" in log)
            r.check("the pilot was told once",
                    log.count("alert: GPS unreliable") == 1, "%d alerts" % log.count("alert: GPS unreliable"))
            r.check("the GPS trusted again after the interference", "GPS trusted again" in log)
        if args.dead_current:
            # The sensor reads 0 A: the GCS must notice in flight and count the
            # charge itself, so the in-flight estimate still drains (RETURN NOW
            # can come on) and the landing charge recorded is its count.
            r.check("the GCS found the current sensor reading about 0 A in flight", "current reads about 0 A" in log)
            r.check("RETURN NOW still comes on from the mission", "mission RETURN NOW on" in log)
            r.check("the real landing charge recorded is the GCS's count, not a full pack",
                    0.0 <= rl < 90.0, "%.0f %%" % rl)
    except Exception as e:
        r.check("scenario ran to the end", False, repr(e))
    finally:
        r.kill_gcs()
        verdict = r.write_log()
        r.stop()
    return verdict


if __name__ == "__main__":
    print(main())
