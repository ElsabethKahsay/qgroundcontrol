"""Shared plumbing for the SITL campaign scripts.

What one run looks like:

    ArduPilot SITL  --TCP 57x0-->  LinkRelay  --UDP-->  Skywin GCS (throw-away HOME)
                                     ^ drop / delay / blackout
    scenario script --TCP 57x2-->  SITL (second serial port, used to command the vehicle)

* SITL is started with sim_vehicle.py --no-mavproxy, so nothing is sent to
  UDP 14550 and an operator's own QGC on this machine is never touched.
* The relay reads the SITL TCP stream, cuts it into whole MAVLink frames and
  sends each frame to the GCS as one UDP datagram (and the reverse). Because it
  works on whole frames it can drop or delay messages the way a lossy radio
  does, without root (tc netem would need sudo).
* The GCS runs with HOME, XDG_DATA_HOME and XDG_CONFIG_HOME pointed at a fresh
  temporary folder. Its settings file is written before launch with every
  auto-connect switched off (so it cannot grab a real flight controller on USB)
  and one UDP link to the relay. Its database therefore lives in that folder.
"""

import heapq
import os
import random
import shutil
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
from datetime import datetime, timezone

from pymavlink import mavutil

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
GCS_BIN = os.path.join(REPO, "build", "Debug", "PreflightQGroundControl")
SIM_VEHICLE = os.path.expanduser("~/ardupilot/Tools/autotest/sim_vehicle.py")
RUNS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "runs")

# QSettings file names the app may use (the custom build renames itself).
GCS_INI_NAMES = ("Skywin GCS.ini", "PreflightQGroundControl.ini")
GCS_DB_REL = os.path.join("UAVPreflight", "Skywin GCS", "uav_preflight_data.db")
REAL_DB = os.path.expanduser("~/.local/share/UAVPreflight/Skywin GCS/uav_preflight_data.db")


def utc_now():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def ports(instance):
    """SITL serial0/serial1 TCP ports and the GCS/relay UDP pair for an instance."""
    base = 5760 + 10 * instance
    return {"sitl": base, "ctrl": base + 2, "gcs_udp": 14600 + 10 * instance, "relay_udp": 14601 + 10 * instance}


# ---------------------------------------------------------------- relay

def _split_frames(buf):
    """Cut complete MAVLink v1/v2 frames off the front of buf.

    Returns (frames, rest). Bytes before a start marker are discarded, the way
    a receiver resynchronises.
    """
    frames = []
    i = 0
    n = len(buf)
    while i < n:
        b = buf[i]
        if b == 0xFD:
            if n - i < 10:
                break
            length = buf[i + 1]
            signed = buf[i + 2] & 0x01
            total = 12 + length + (13 if signed else 0)
        elif b == 0xFE:
            if n - i < 6:
                break
            total = 8 + buf[i + 1]
        else:
            i += 1
            continue
        if n - i < total:
            break
        frames.append(bytes(buf[i:i + total]))
        i += total
    return frames, buf[i:]


class LinkRelay:
    """SITL TCP <-> GCS UDP, one MAVLink frame per datagram.

    loss (0..1), delay_s and blackout can be changed at any time from the
    scenario; they apply to both directions.
    """

    def __init__(self, sitl_port, gcs_udp, relay_udp):
        self.sitl_port = sitl_port
        self.gcs_addr = ("127.0.0.1", gcs_udp)
        self.loss = 0.0
        self.delay_s = 0.0
        self.blackout = False
        self.stats = {"to_gcs": 0, "to_sitl": 0, "dropped": 0}
        self._stop = threading.Event()
        self._queue = []          # (due_time, seq, direction, frame)
        self._seq = 0
        self._qlock = threading.Lock()
        self._udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._udp.bind(("127.0.0.1", relay_udp))
        self._udp.settimeout(0.2)
        self._tcp = None

    def start(self):
        deadline = time.time() + 60
        while True:
            try:
                self._tcp = socket.create_connection(("127.0.0.1", self.sitl_port), timeout=2)
                break
            except OSError:
                if time.time() > deadline:
                    raise RuntimeError("SITL did not open TCP %d" % self.sitl_port)
                time.sleep(0.5)
        self._tcp.settimeout(0.2)
        self._threads = [threading.Thread(target=t, daemon=True) for t in (self._from_sitl, self._from_gcs, self._sender)]
        for t in self._threads:
            t.start()

    def stop(self):
        # The reader threads wake every 0.2 s to look at the stop flag; wait
        # for them so the sockets are really released before the next run
        # binds the same ports.
        self._stop.set()
        for t in getattr(self, "_threads", []):
            t.join(2)
        for s in (self._tcp, self._udp):
            try:
                s.close()
            except Exception:
                pass

    def _enqueue(self, direction, frame):
        if self.blackout or (self.loss and random.random() < self.loss):
            self.stats["dropped"] += 1
            return
        with self._qlock:
            self._seq += 1
            heapq.heappush(self._queue, (time.time() + self.delay_s, self._seq, direction, frame))

    def _sender(self):
        while not self._stop.is_set():
            now = time.time()
            ready = []
            with self._qlock:
                while self._queue and self._queue[0][0] <= now:
                    ready.append(heapq.heappop(self._queue))
            for _, _, direction, frame in ready:
                try:
                    if direction == "to_gcs":
                        self._udp.sendto(frame, self.gcs_addr)
                    else:
                        self._tcp.sendall(frame)
                    self.stats[direction] += 1
                except OSError:
                    pass
            time.sleep(0.002)

    def _from_sitl(self):
        buf = bytearray()
        while not self._stop.is_set():
            try:
                data = self._tcp.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                return
            if not data:
                return
            buf += data
            frames, rest = _split_frames(buf)
            buf = bytearray(rest)
            for f in frames:
                self._enqueue("to_gcs", f)

    def _from_gcs(self):
        while not self._stop.is_set():
            try:
                data, _ = self._udp.recvfrom(65536)
            except socket.timeout:
                continue
            except OSError:
                return
            self._enqueue("to_sitl", data)


# ---------------------------------------------------------------- run

class Run:
    """One scenario: SITL + relay + GCS + a control connection to the vehicle."""

    def __init__(self, scenario, vehicle="ArduPlane", instance=3, params=None):
        self.scenario = scenario
        self.vehicle = vehicle
        self.instance = instance
        self.params = params or {}
        self.ports = ports(instance)
        self.work = tempfile.mkdtemp(prefix="skywin-sitl-%s-" % scenario)
        self.log_lines = []
        self.results = []         # (name, passed, detail)
        self.sitl = None
        self.gcs = None
        self.relay = None
        self.mav = None
        self.started_utc = utc_now()

    # -- logging

    def note(self, text):
        line = "%s  %s" % (utc_now(), text)
        print(line, flush=True)
        self.log_lines.append(line)

    def check(self, name, passed, detail=""):
        self.results.append((name, bool(passed), detail))
        self.note("%s: %s %s" % ("PASS" if passed else "FAIL", name, detail))
        return passed

    # -- processes

    def start_sitl(self):
        sitl_dir = os.path.join(self.work, "sitl")
        os.makedirs(sitl_dir)
        cmd = [sys.executable, SIM_VEHICLE, "-v", self.vehicle, "-N", "--no-mavproxy",
               "-I", str(self.instance), "-w"]
        if self.params:
            pfile = os.path.join(sitl_dir, "scenario.parm")
            with open(pfile, "w") as f:
                for k, v in self.params.items():
                    f.write("%s %s\n" % (k, v))
            cmd.append("--add-param-file=%s" % pfile)
        self.note("SITL: %s" % " ".join(cmd[1:]))
        self.sitl = subprocess.Popen(cmd, cwd=sitl_dir, stdout=open(os.path.join(sitl_dir, "sim.log"), "w"),
                                     stderr=subprocess.STDOUT, start_new_session=True)
        self.relay = LinkRelay(self.ports["sitl"], self.ports["gcs_udp"], self.ports["relay_udp"])
        self.relay.start()
        self.mav = connect_ctrl(self.ports["ctrl"])
        self.note("vehicle heartbeat on control port (sysid %d, type %d)"
                  % (self.mav.target_system, self.mav.messages["HEARTBEAT"].type))

    def gcs_env(self):
        env = dict(os.environ)
        env.update({
            "HOME": os.path.join(self.work, "home"),
            "XDG_DATA_HOME": os.path.join(self.work, "data"),
            "XDG_CONFIG_HOME": os.path.join(self.work, "config"),
            "XDG_CACHE_HOME": os.path.join(self.work, "cache"),
            "QT_QPA_PLATFORM": "offscreen",
        })
        return env

    def write_gcs_settings(self):
        env = self.gcs_env()
        for key in ("HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
            os.makedirs(env[key], exist_ok=True)
        ini_dir = os.path.join(env["XDG_CONFIG_HOME"], "UAVPreflight")
        os.makedirs(ini_dir, exist_ok=True)
        text = (
            "[AutoConnect]\n"
            "autoConnectUDP=false\nautoConnectPixhawk=false\nautoConnectSiKRadio=false\n"
            "autoConnectRTKGPS=false\nautoConnectLibrePilot=false\nautoConnectZeroConf=false\n"
            "autoConnectNmeaPort=Disabled\n\n"
            "[LinkConfigurations]\n"
            "Link0\\name=sitl-relay\nLink0\\type=1\nLink0\\auto=true\nLink0\\high_latency=false\n"
            "Link0\\port=%d\nLink0\\hostCount=1\nLink0\\host0=127.0.0.1\nLink0\\port0=%d\n"
            "count=1\n" % (self.ports["gcs_udp"], self.ports["relay_udp"]))
        for name in GCS_INI_NAMES:
            with open(os.path.join(ini_dir, name), "w") as f:
                f.write(text)

    def start_gcs(self, fresh_settings=True):
        if fresh_settings:
            self.write_gcs_settings()
        self.gcs_log = open(os.path.join(self.work, "gcs.log"), "a")
        self.gcs = subprocess.Popen([GCS_BIN, "--allow-multiple"], env=self.gcs_env(), cwd=self.work,
                                    stdout=self.gcs_log, stderr=subprocess.STDOUT, start_new_session=True)
        self.note("GCS started (pid %d), data folder %s" % (self.gcs.pid, self.work))

    def kill_gcs(self, sig=signal.SIGTERM, wait=10):
        if not self.gcs or self.gcs.poll() is not None:
            return
        self.gcs.send_signal(sig)
        try:
            self.gcs.wait(wait)
        except subprocess.TimeoutExpired:
            self.gcs.kill()
            self.gcs.wait()

    def stop(self, keep=False):
        self.kill_gcs()
        if self.relay:
            self.relay.stop()
        if self.sitl and self.sitl.poll() is None:
            try:
                os.killpg(self.sitl.pid, signal.SIGTERM)
                self.sitl.wait(10)
            except Exception:
                try:
                    os.killpg(self.sitl.pid, signal.SIGKILL)
                except Exception:
                    pass
        if not keep and not os.environ.get("SITL_KEEP"):
            shutil.rmtree(self.work, ignore_errors=True)

    # -- flying

    def take_off(self, alt=40):
        """Arm and climb: copter GUIDED + NAV_TAKEOFF, plane TAKEOFF mode."""
        m = self.mav
        copter = self.vehicle == "ArduCopter"
        # Informational only: after a simulated plane has been force-disarmed
        # in the air it can keep a pre-arm complaint, yet still arms; the arm
        # itself is the check.
        self.note("vehicle pre-arm healthy: %s" % wait_ekf_ready(m, 120))
        if copter:
            set_mode(m, "GUIDED")
        else:
            param_set(m, "TKOFF_ALT", alt)
            set_mode(m, "TAKEOFF")
        # A copter refuses to arm in GUIDED until its EKF has a position
        # estimate, which comes some tens of seconds after boot; retry and
        # log what the vehicle says each time.
        deadline = time.time() + 120
        while True:
            res = command(m, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 1)
            if res == mavutil.mavlink.MAV_RESULT_ACCEPTED or time.time() > deadline:
                break
            self.note("arm refused (%s): %s" % (res, statustexts(m, 2)[-2:]))
            time.sleep(5)
        self.note("arm command result %s" % res)
        if not self.check("vehicle armed", wait_armed(m, True, 20)):
            return False
        if copter:
            command(m, mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, p7=alt)
        return self.check("climbed to %d m" % int(alt * 0.8), wait_alt(m, alt * 0.8, 120))

    def end_flight(self):
        """Copter: LAND and wait for the auto-disarm. Plane: force-disarm in
        the air (the simulated plane falls; there is no runway landing here)."""
        m = self.mav
        if self.vehicle == "ArduCopter":
            set_mode(m, "LAND")
            ok = wait_armed(m, False, 120)
        else:
            command(m, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0, 21196)
            ok = wait_armed(m, False, 15)
        self.check("vehicle disarmed", ok)
        return ok

    # -- database

    @property
    def db_path(self):
        return os.path.join(self.work, "data", GCS_DB_REL)

    def q(self, sql, args=()):
        """Read-only query against the throw-away database; [] if not there yet."""
        assert os.path.abspath(self.db_path) != os.path.abspath(REAL_DB)
        if not os.path.exists(self.db_path):
            return []
        con = sqlite3.connect("file:%s?mode=ro" % self.db_path, uri=True, timeout=5)
        con.row_factory = sqlite3.Row
        try:
            return [dict(r) for r in con.execute(sql, args)]
        except sqlite3.OperationalError as e:
            self.note("query failed: %s" % e)
            return []
        finally:
            con.close()

    def wait_rows(self, sql, args=(), timeout=60, at_least=1):
        deadline = time.time() + timeout
        rows = []
        while time.time() < deadline:
            rows = self.q(sql, args)
            if len(rows) >= at_least:
                return rows
            time.sleep(1)
        return rows

    def wait_gcs_connected(self, timeout=90):
        rows = self.wait_rows("SELECT * FROM connection_sessions WHERE closed_at IS NULL", timeout=timeout)
        self.check("GCS opened a connection session", rows, rows and "device %s" % rows[-1]["device_uid"])
        return rows

    # -- run log

    def dump_tables(self):
        out = []
        for table in ("connection_sessions", "flight_sessions", "flights", "flight_batteries",
                      "session_events", "check_results", "hardware_test_events",
                      "motor_test_results", "surface_test_results"):
            sql = "SELECT * FROM %s ORDER BY id" % table
            if table == "check_results":
                # Only the latest result per check: the full history is long.
                sql = ("SELECT check_id, status, message, evaluated_at FROM check_results "
                       "WHERE id IN (SELECT MAX(id) FROM check_results GROUP BY check_id) ORDER BY check_id")
            rows = self.q(sql)
            out.append("### %s (%d rows)\n" % (table, len(rows)))
            if rows:
                cols = list(rows[0].keys())
                out.append("| " + " | ".join(cols) + " |")
                out.append("|" + "---|" * len(cols))
                for r in rows:
                    out.append("| " + " | ".join(str(r[c]).replace("|", "/").replace("\n", " ")[:120]
                                                 for c in cols) + " |")
            out.append("")
        return "\n".join(out)

    def write_log(self, summary=""):
        os.makedirs(RUNS_DIR, exist_ok=True)
        day = datetime.now().strftime("%Y-%m-%d")
        path = os.path.join(RUNS_DIR, "%s-%s.md" % (day, self.scenario))
        verdict = "PASS" if self.results and all(p for _, p, _ in self.results) else "FAIL"
        with open(path, "w") as f:
            f.write("# SITL run: %s — %s\n\n" % (self.scenario, verdict))
            f.write("- Vehicle: %s (SITL instance %d, ArduPilot %s)\n" % (self.vehicle, self.instance, ardupilot_version()))
            f.write("- Started %s, finished %s\n" % (self.started_utc, utc_now()))
            f.write("- GCS: %s, throw-away data folder (deleted afterwards)\n" % os.path.relpath(GCS_BIN, REPO))
            if self.relay:
                f.write("- Relay counters: %s\n" % self.relay.stats)
            if summary:
                f.write("\n%s\n" % summary)
            f.write("\n## Checks\n\n")
            for name, passed, detail in self.results:
                f.write("- **%s** %s%s\n" % ("PASS" if passed else "FAIL", name, (" — " + detail) if detail else ""))
            f.write("\n## Timeline\n\n```\n%s\n```\n\n## Database rows\n\n%s\n" % ("\n".join(self.log_lines), self.dump_tables()))
        print("run log: %s" % path)
        return verdict


_AP_VERSION = None


def ardupilot_version():
    global _AP_VERSION
    if _AP_VERSION is None:
        try:
            _AP_VERSION = subprocess.check_output(["git", "-C", os.path.expanduser("~/ardupilot"), "describe", "--tags"],
                                                  text=True, stderr=subprocess.DEVNULL).strip()
        except Exception:
            _AP_VERSION = "unknown"
    return _AP_VERSION


# ---------------------------------------------------------------- vehicle control

def connect_ctrl(port, timeout=90):
    deadline = time.time() + timeout
    while True:
        try:
            m = mavutil.mavlink_connection("tcp:127.0.0.1:%d" % port, source_system=250, autoreconnect=True)
            if m.wait_heartbeat(timeout=10):
                # ArduCopter streams nothing on its second serial port by
                # default (SR1_* = 0), so ask for everything at 4 Hz.
                m.mav.request_data_stream_send(m.target_system, m.target_component,
                                               mavutil.mavlink.MAV_DATA_STREAM_ALL, 4, 1)
                return m
        except Exception:
            pass
        if time.time() > deadline:
            raise RuntimeError("no heartbeat on control port %d" % port)
        time.sleep(1)


def command(m, cmd, p1=0, p2=0, p3=0, p4=0, p5=0, p6=0, p7=0, timeout=5):
    """COMMAND_LONG and its ACK result (None when no ACK came)."""
    m.mav.command_long_send(m.target_system, m.target_component, cmd, 0, p1, p2, p3, p4, p5, p6, p7)
    deadline = time.time() + timeout
    while time.time() < deadline:
        ack = m.recv_match(type="COMMAND_ACK", blocking=True, timeout=0.5)
        if ack and ack.command == cmd:
            return ack.result
    return None


def statustexts(m, seconds):
    out = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        msg = m.recv_match(type="STATUSTEXT", blocking=True, timeout=0.5)
        if msg:
            out.append(msg.text)
    return out


def is_armed(m):
    hb = vehicle_heartbeat(m)
    return bool(hb and hb.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED)


def wait_armed(m, want=True, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        hb = m.recv_match(type="HEARTBEAT", blocking=True, timeout=1)
        if hb and hb.get_srcSystem() == m.target_system and hb.type != mavutil.mavlink.MAV_TYPE_GCS:
            if bool(hb.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED) == want:
                return True
    return False


def set_mode(m, name, timeout=10):
    mapping = m.mode_mapping()
    mode_id = mapping[name]
    deadline = time.time() + timeout
    while time.time() < deadline:
        m.mav.set_mode_send(m.target_system, mavutil.mavlink.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, mode_id)
        hb = vehicle_heartbeat(m, 1.5)
        if hb and hb.custom_mode == mode_id:
            return True
    return False


def vehicle_heartbeat(m, timeout=3):
    """Next heartbeat from the vehicle itself. ArduPilot routes other
    components' heartbeats (the GCS's included) to this port too."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        hb = m.recv_match(type="HEARTBEAT", blocking=True, timeout=0.5)
        if hb and hb.get_srcSystem() == m.target_system and hb.type != mavutil.mavlink.MAV_TYPE_GCS:
            return hb
    return None


def current_mode(m):
    hb = vehicle_heartbeat(m)
    return mavutil.mode_string_v10(hb) if hb else None


def param_set(m, name, value, timeout=5):
    m.mav.param_set_send(m.target_system, m.target_component, name.encode(), float(value),
                         mavutil.mavlink.MAV_PARAM_TYPE_REAL32)
    deadline = time.time() + timeout
    while time.time() < deadline:
        p = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=0.5)
        if p and p.param_id == name:
            return abs(p.param_value - float(value)) < 1e-3
    return False


def wait_ekf_ready(m, timeout=120, prearm=True):
    """Wait until the vehicle reports a 3D GPS fix and (with prearm) passes
    its own pre-arm checks, which SYS_STATUS reports as the PREARM_CHECK bit."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        s = m.recv_match(type="SYS_STATUS", blocking=True, timeout=1)
        if s and (not prearm or s.onboard_control_sensors_health & mavutil.mavlink.MAV_SYS_STATUS_PREARM_CHECK):
            g = m.messages.get("GPS_RAW_INT")
            if g and g.fix_type >= 3:
                return True
    return False


def wait_alt(m, metres, timeout=90):
    deadline = time.time() + timeout
    while time.time() < deadline:
        g = m.recv_match(type="GLOBAL_POSITION_INT", blocking=True, timeout=1)
        if g and g.relative_alt / 1000.0 >= metres:
            return True
    return False


def rel_alt(m):
    g = m.recv_match(type="GLOBAL_POSITION_INT", blocking=True, timeout=3)
    return g.relative_alt / 1000.0 if g else None


def rss_kb(pid):
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        return None
    return None
