# SITL run: lossy-link-arducopter — PASS

- Vehicle: ArduCopter (SITL instance 3, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-24T08:59:51Z, finished 2026-10-24T09:05:03Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 23435, 'to_sitl': 927, 'dropped': 9303}

No `parserStalled` signal exists in the code; see the module docstring.

## Checks

- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** vehicle armed
- **PASS** climbed to 24 m
- **PASS** 20 % / 150 ms: 0 link_lost event(s) — expected none
- **PASS** GCS still running
- **PASS** flight still open
- **PASS** 40 % / 400 ms: 0 link_lost event(s) (any count allowed)
- **PASS** GCS still running
- **PASS** flight still open
- **PASS** 80 % / 400 ms: 0 link_lost event(s) (any count allowed)
- **PASS** GCS still running
- **PASS** flight still open
- **PASS** vehicle disarmed
- **PASS** flight closed with end_reason = disarmed — disarmed

## Timeline

```
2026-10-24T08:59:51Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduCopter -N --no-mavproxy -I 3 -w
2026-10-24T08:59:53Z  vehicle heartbeat on control port (sysid 1, type 2)
2026-10-24T08:59:53Z  GCS started (pid 71377), data folder /tmp/skywin-sitl-lossy-link-arducopter-oli8_jft
2026-10-24T08:59:58Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-24T09:00:05Z  vehicle pre-arm healthy: True
2026-10-24T09:00:08Z  arm refused (4): ['Arm: AHRS: waiting for home', 'PreArm: AHRS: waiting for home']
2026-10-24T09:00:15Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:00:23Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:00:30Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:00:35Z  arm command result 0
2026-10-24T09:00:35Z  PASS: vehicle armed 
2026-10-24T09:00:50Z  PASS: climbed to 24 m 
2026-10-24T09:00:50Z  link: 20 % loss, 150 ms delay for 60 s
2026-10-24T09:01:58Z  PASS: 20 % / 150 ms: 0 link_lost event(s) expected none
2026-10-24T09:01:58Z  PASS: GCS still running 
2026-10-24T09:01:58Z  PASS: flight still open 
2026-10-24T09:01:58Z  link: 40 % loss, 400 ms delay for 60 s
2026-10-24T09:03:06Z  PASS: 40 % / 400 ms: 0 link_lost event(s) (any count allowed) 
2026-10-24T09:03:06Z  PASS: GCS still running 
2026-10-24T09:03:06Z  PASS: flight still open 
2026-10-24T09:03:06Z  link: 80 % loss, 400 ms delay for 60 s
2026-10-24T09:04:14Z  PASS: 80 % / 400 ms: 0 link_lost event(s) (any count allowed) 
2026-10-24T09:04:14Z  PASS: GCS still running 
2026-10-24T09:04:14Z  PASS: flight still open 
2026-10-24T09:04:52Z  PASS: vehicle disarmed 
2026-10-24T09:04:53Z  PASS: flight closed with end_reason = disarmed disarmed
```

## Database rows

### connection_sessions (1 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T08:59:57.229Z | 2026-10-24T09:04:57.063Z | None | None |

### flight_sessions (1 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-24T09:00:35.994Z | 2026-10-24T09:04:52.049Z | 256.054 | 0.0 |  | 0.0 | 0.0 | 24.666666666666668 | 0.0 | 0.0 | 0.0 |  | 1 | external | 0 | None | 0 | disarmed | 2026-10-24T09:04:52.049Z | 12.6 | 1959.0 | 1 |

### flights (1 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 09:00:35 | 2026-10-24 09:00:35 | 2026-10-24T09:04:52Z | 2026-10-24 09:04:52 | 256 | 98.42847769028872 | 12.6 | 12.6 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |

### flight_batteries (1 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | None | None | None | None | None | None | None | 12.6 | 100 | 12.6 | 40 | 12.6 | 1959.0 | 24.666666666666668 |

### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


