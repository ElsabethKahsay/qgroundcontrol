# SITL run: run-of-show-arduplane — PASS

- Vehicle: ArduPlane (SITL instance 3, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-24T08:48:45Z, finished 2026-10-24T08:57:26Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 62786, 'to_sitl': 1326, 'dropped': 0}

## Checks

- **PASS** GCS opened a connection session — device 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6
- **PASS** vehicle armed
- **PASS** climbed to 24 m
- **PASS** vehicle disarmed
- **PASS** run 1: GCS exited — exit code -9
- **PASS** GCS opened a connection session — device 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6
- **PASS** vehicle armed
- **PASS** climbed to 24 m
- **PASS** vehicle disarmed
- **PASS** run 2: GCS exited — exit code -9
- **PASS** connections of both runs closed — [(1, 'crash_recovered'), (2, 'crash_recovered'), (3, None)]
- **PASS** two flights, both end_reason = disarmed — [(1, 'disarmed', 137.044), (2, 'disarmed', 136.021)]
- **PASS** flight durations non-zero
- **PASS** two history rows
- **PASS** no link_lost events — 0

## Timeline

```
2026-10-24T08:48:45Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduPlane -N --no-mavproxy -I 3 -w
2026-10-24T08:48:47Z  vehicle heartbeat on control port (sysid 1, type 1)
2026-10-24T08:48:47Z  GCS started (pid 67421), data folder /tmp/skywin-sitl-run-of-show-arduplane-fn6aqwri
2026-10-24T08:48:52Z  PASS: GCS opened a connection session device 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6
2026-10-24T08:49:12Z  vehicle pre-arm healthy: True
2026-10-24T08:49:12Z  arm command result 0
2026-10-24T08:49:13Z  PASS: vehicle armed 
2026-10-24T08:49:18Z  PASS: climbed to 24 m 
2026-10-24T08:51:30Z  PASS: vehicle disarmed 
2026-10-24T08:51:42Z  run 1: closing the GCS normally (SIGTERM)
2026-10-24T08:51:52Z  PASS: run 1: GCS exited exit code -9
2026-10-24T08:51:57Z  GCS started (pid 68345), data folder /tmp/skywin-sitl-run-of-show-arduplane-fn6aqwri
2026-10-24T08:51:57Z  PASS: GCS opened a connection session device 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6
2026-10-24T08:54:17Z  vehicle pre-arm healthy: False
2026-10-24T08:54:17Z  arm command result 0
2026-10-24T08:54:18Z  PASS: vehicle armed 
2026-10-24T08:54:22Z  PASS: climbed to 24 m 
2026-10-24T08:56:34Z  PASS: vehicle disarmed 
2026-10-24T08:56:46Z  run 2: closing the GCS normally (SIGTERM)
2026-10-24T08:56:56Z  PASS: run 2: GCS exited exit code -9
2026-10-24T08:56:56Z  GCS started (pid 69541), data folder /tmp/skywin-sitl-run-of-show-arduplane-fn6aqwri
2026-10-24T08:57:26Z  PASS: connections of both runs closed [(1, 'crash_recovered'), (2, 'crash_recovered'), (3, None)]
2026-10-24T08:57:26Z  PASS: two flights, both end_reason = disarmed [(1, 'disarmed', 137.044), (2, 'disarmed', 136.021)]
2026-10-24T08:57:26Z  PASS: flight durations non-zero 
2026-10-24T08:57:26Z  PASS: two history rows 
2026-10-24T08:57:26Z  PASS: no link_lost events 0
```

## Database rows

### connection_sessions (3 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T08:48:51.711Z | 2026-10-24T08:51:52.062Z | 2026-10-24T08:51:52.062Z | crash_recovered |
| 2 | 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T08:52:01.424Z | 2026-10-24T08:56:51.063Z | 2026-10-24T08:56:51.063Z | crash_recovered |
| 3 | 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T08:56:59.428Z | 2026-10-24T08:57:19.063Z | None | None |

### flight_sessions (2 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6 | None | 2026-10-24T08:49:13.181Z | 2026-10-24T08:51:30.226Z | 137.044 | 0.0 |  | 0.0 | 0.0 | 6.611111111111111 | 3194.6013012020985 | 0.0 | 0.0 |  | 1 | external | 0 | None | 0 | disarmed | 2026-10-24T08:51:30.226Z | 12.146 | 542.0 | 1 |
| 2 | 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6 | None | 2026-10-24T08:54:18.245Z | 2026-10-24T08:56:34.267Z | 136.021 | 0.0 |  | 0.0 | 0.0 | 6.472222222222223 | 3173.9847821001745 | 0.0 | 0.0 |  | 2 | external | 0 | None | 0 | disarmed | 2026-10-24T08:56:34.267Z | 11.9 | 532.0 | 2 |

### flights (2 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 08:49:13 | 2026-10-24 08:49:13 | 2026-10-24T08:51:30Z | 2026-10-24 08:51:30 | 137 | 121.88976377952756 | 12.146 | 12.582 | 0 | 0.0 | 0.0 | 3194.6013012020985 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |
| 2 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 08:54:18 | 2026-10-24 08:54:18 | 2026-10-24T08:56:34Z | 2026-10-24 08:56:34 | 136 | 329.5505249343832 | 11.9 | 12.6 | 0 | 0.0 | 0.0 | 3173.9847821001745 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |

### flight_batteries (2 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | None | None | None | None | None | None | None | 12.582 | 99 | 12.51 | 83 | 12.146 | 542.0 | 6.611111111111111 |
| 2 | 2 | 1 | None | None | None | None | None | None | None | 12.6 | 83 | 12.258 | 67 | 11.9 | 532.0 | 6.472222222222223 |

### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


