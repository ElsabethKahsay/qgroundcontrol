# SITL run: gimbal-arducopter — PASS

- Vehicle: ArduCopter (SITL instance 3, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-24T08:31:10Z, finished 2026-10-24T08:32:08Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 5501, 'to_sitl': 286, 'dropped': 0}

Servo gimbal: MNT1_TYPE = 1, SERVO9/10/11_FUNCTION = 7/8/6.

## Checks

- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** vehicle reports gimbal attitude — roll -0.0 pitch -35.0 yaw -0.0
- **PASS** pitch to -45 deg (ACK 0) — reported -45.0 deg; servo 9 1500 -> 1427 us
- **PASS** yaw to 60 deg (ACK 0) — reported 60.0 deg; servo 11 1500 -> 1633 us
- **PASS** pitch back to 0 deg (ACK 0) — reported -0.1 deg; servo 9 1754 -> 1754 us
- **PASS** roll to 20 deg (DO_MOUNT_CONTROL, ACK 0) — reported 19.9 deg; servo 10 1765 us

## Timeline

```
2026-10-24T08:31:10Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduCopter -N --no-mavproxy -I 3 -w --add-param-file=/tmp/skywin-sitl-gimbal-arducopter-zj6fgnqf/sitl/scenario.parm
2026-10-24T08:31:12Z  vehicle heartbeat on control port (sysid 1, type 2)
2026-10-24T08:31:12Z  GCS started (pid 61447), data folder /tmp/skywin-sitl-gimbal-arducopter-zj6fgnqf
2026-10-24T08:31:17Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-24T08:31:31Z  PASS: vehicle reports gimbal attitude roll -0.0 pitch -35.0 yaw -0.0
2026-10-24T08:31:35Z  PASS: pitch to -45 deg (ACK 0) reported -45.0 deg; servo 9 1500 -> 1427 us
2026-10-24T08:31:40Z  PASS: yaw to 60 deg (ACK 0) reported 60.0 deg; servo 11 1500 -> 1633 us
2026-10-24T08:31:44Z  PASS: pitch back to 0 deg (ACK 0) reported -0.1 deg; servo 9 1754 -> 1754 us
2026-10-24T08:31:48Z  PASS: roll to 20 deg (DO_MOUNT_CONTROL, ACK 0) reported 19.9 deg; servo 10 1765 us
```

## Database rows

### connection_sessions (1 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T08:31:16.500Z | 2026-10-24T08:32:06.062Z | None | None |

### flight_sessions (0 rows)


### flights (0 rows)


### flight_batteries (0 rows)


### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


