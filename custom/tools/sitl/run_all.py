"""Run the SITL campaign, plane first, then copter.

    python3 run_all.py            # every scenario except the long idle
    python3 run_all.py idle       # add the 3-hour idle (runs on SITL instance 4)

Each scenario writes its own run log under runs/; this prints a summary.
Every run uses a throw-away GCS data folder and its own ports (see
sitl_common.py), so the operator's QGC and database are never touched.
"""

import sys

import sc_force_arm
import sc_gimbal
import sc_idle
import sc_kill9_energy
import sc_lossy_link
import sc_rtl_link_loss
import sc_run_of_show

PLAN = [
    ("force arm", sc_force_arm.run, "ArduPlane"),
    ("RTL + link loss", sc_rtl_link_loss.run, "ArduPlane"),
    ("lossy link", sc_lossy_link.run, "ArduPlane"),
    ("run of show", sc_run_of_show.run, "ArduPlane"),
    ("force arm", sc_force_arm.run, "ArduCopter"),
    ("link loss + land in blackout", sc_rtl_link_loss.run, "ArduCopter"),
    ("lossy link", sc_lossy_link.run, "ArduCopter"),
    ("kill -9 + energy", sc_kill9_energy.run, "ArduCopter"),
    ("run of show", sc_run_of_show.run, "ArduCopter"),
    ("gimbal axes", sc_gimbal.run, "ArduCopter"),
]


def main():
    results = []
    plan = list(PLAN)
    only = [a for a in sys.argv[1:] if a != "idle"]
    if only:
        plan = [p for p in plan if any(o.lower() in (p[0] + " " + p[2]).lower() for o in only)]
    if "idle" in sys.argv[1:]:
        plan.append(("3 h idle", sc_idle.run, "ArduPlane"))
    for name, fn, vehicle in plan:
        print("=== %s (%s)" % (name, vehicle), flush=True)
        results.append((name, vehicle, fn(vehicle)))
    print("\nSummary")
    for name, vehicle, verdict in results:
        print("  %-5s %s (%s)" % (verdict, name, vehicle))
    return 0 if all(v == "PASS" for _, _, v in results) else 1


if __name__ == "__main__":
    sys.exit(main())
