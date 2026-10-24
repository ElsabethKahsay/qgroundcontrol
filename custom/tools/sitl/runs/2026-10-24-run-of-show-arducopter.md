# SITL run: run-of-show-arducopter — PASS

- Vehicle: ArduCopter (SITL instance 3, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-24T09:12:18Z, finished 2026-10-24T09:20:17Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 50853, 'to_sitl': 1364, 'dropped': 0}

## Checks

- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** vehicle armed
- **PASS** climbed to 24 m
- **PASS** vehicle disarmed
- **PASS** run 1: GCS exited — exit code -9
- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** vehicle armed
- **PASS** climbed to 24 m
- **PASS** vehicle disarmed
- **PASS** run 2: GCS exited — exit code -9
- **PASS** connections of both runs closed — [(1, 'crash_recovered'), (2, 'crash_recovered'), (3, None)]
- **PASS** two flights, both end_reason = disarmed — [(1, 'disarmed', 148.024), (2, 'disarmed', 148.046)]
- **PASS** flight durations non-zero
- **PASS** two history rows
- **PASS** no link_lost events — 0

## Timeline

```
2026-10-24T09:12:18Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduCopter -N --no-mavproxy -I 3 -w
2026-10-24T09:12:19Z  vehicle heartbeat on control port (sysid 1, type 2)
2026-10-24T09:12:19Z  GCS started (pid 74779), data folder /tmp/skywin-sitl-run-of-show-arducopter-m79o3kb0
2026-10-24T09:12:24Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-24T09:12:44Z  vehicle pre-arm healthy: True
2026-10-24T09:12:47Z  arm refused (4): ['Arm: Need Position Estimate', 'PreArm: Need Position Estimate']
2026-10-24T09:12:54Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:13:02Z  arm refused (4): ['EKF3 IMU0 is using GPS', 'EKF3 IMU1 is using GPS']
2026-10-24T09:13:07Z  arm command result 0
2026-10-24T09:13:07Z  PASS: vehicle armed 
2026-10-24T09:13:21Z  PASS: climbed to 24 m 
2026-10-24T09:15:52Z  PASS: vehicle disarmed 
2026-10-24T09:16:04Z  run 1: closing the GCS normally (SIGTERM)
2026-10-24T09:16:14Z  PASS: run 1: GCS exited exit code -9
2026-10-24T09:16:19Z  GCS started (pid 75777), data folder /tmp/skywin-sitl-run-of-show-arducopter-m79o3kb0
2026-10-24T09:16:19Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-24T09:16:39Z  vehicle pre-arm healthy: True
2026-10-24T09:16:39Z  arm command result 0
2026-10-24T09:16:40Z  PASS: vehicle armed 
2026-10-24T09:16:54Z  PASS: climbed to 24 m 
2026-10-24T09:19:25Z  PASS: vehicle disarmed 
2026-10-24T09:19:37Z  run 2: closing the GCS normally (SIGTERM)
2026-10-24T09:19:47Z  PASS: run 2: GCS exited exit code -9
2026-10-24T09:19:47Z  GCS started (pid 76603), data folder /tmp/skywin-sitl-run-of-show-arducopter-m79o3kb0
2026-10-24T09:20:17Z  PASS: connections of both runs closed [(1, 'crash_recovered'), (2, 'crash_recovered'), (3, None)]
2026-10-24T09:20:17Z  PASS: two flights, both end_reason = disarmed [(1, 'disarmed', 148.024), (2, 'disarmed', 148.046)]
2026-10-24T09:20:17Z  PASS: flight durations non-zero 
2026-10-24T09:20:17Z  PASS: two history rows 
2026-10-24T09:20:17Z  PASS: no link_lost events 0
```

## Database rows

### connection_sessions (3 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T09:12:23.768Z | 2026-10-24T09:16:14.062Z | 2026-10-24T09:16:14.062Z | crash_recovered |
| 2 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T09:16:22.813Z | 2026-10-24T09:19:43.062Z | 2026-10-24T09:19:43.062Z | crash_recovered |
| 3 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T09:19:50.850Z | 2026-10-24T09:20:11.062Z | None | None |

### flight_sessions (2 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-24T09:13:07.641Z | 2026-10-24T09:15:35.665Z | 148.024 | 0.0 |  | 0.0 | 0.0 | 14.083333333333334 | 0.0 | 0.0 | 0.0 |  | 1 | external | 0 | None | 0 | disarmed | 2026-10-24T09:15:35.665Z | 12.6 | 1118.0 | 1 |
| 2 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-24T09:16:40.648Z | 2026-10-24T09:19:08.694Z | 148.046 | 0.0 |  | 0.0 | 0.0 | 14.111111111111109 | 0.0 | 0.0 | 0.0 |  | 2 | external | 0 | None | 0 | disarmed | 2026-10-24T09:19:08.694Z | 12.6 | 1121.0 | 2 |

### flights (2 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 09:13:07 | 2026-10-24 09:13:07 | 2026-10-24T09:15:35Z | 2026-10-24 09:15:35 | 148 | 98.42847769028872 | 12.6 | 12.6 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |
| 2 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 09:16:40 | 2026-10-24 09:16:40 | 2026-10-24T09:19:08Z | 2026-10-24 09:19:08 | 148 | 98.40879265091863 | 12.6 | 12.6 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |

### flight_batteries (2 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | None | None | None | None | None | None | None | 12.6 | 100 | 12.6 | 66 | 12.6 | 1118.0 | 14.083333333333334 |
| 2 | 2 | 1 | None | None | None | None | None | None | None | 12.6 | 66 | 12.6 | 32 | 12.6 | 1121.0 | 14.111111111111109 |

### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


