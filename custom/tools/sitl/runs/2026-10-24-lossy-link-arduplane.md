# SITL run: lossy-link-arduplane — PASS

- Vehicle: ArduPlane (SITL instance 3, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-24T08:33:11Z, finished 2026-10-24T08:37:14Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 18651, 'to_sitl': 656, 'dropped': 10520}

No `parserStalled` signal exists in the code; see the module docstring.

## Checks

- **PASS** GCS opened a connection session — device 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6
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
2026-10-24T08:33:11Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduPlane -N --no-mavproxy -I 3 -w
2026-10-24T08:33:12Z  vehicle heartbeat on control port (sysid 1, type 1)
2026-10-24T08:33:12Z  GCS started (pid 62686), data folder /tmp/skywin-sitl-lossy-link-arduplane-hlu2htzs
2026-10-24T08:33:17Z  PASS: GCS opened a connection session device 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6
2026-10-24T08:33:32Z  vehicle pre-arm healthy: True
2026-10-24T08:33:32Z  arm command result 0
2026-10-24T08:33:32Z  PASS: vehicle armed 
2026-10-24T08:33:38Z  PASS: climbed to 24 m 
2026-10-24T08:33:38Z  link: 20 % loss, 150 ms delay for 60 s
2026-10-24T08:34:46Z  PASS: 20 % / 150 ms: 0 link_lost event(s) expected none
2026-10-24T08:34:46Z  PASS: GCS still running 
2026-10-24T08:34:46Z  PASS: flight still open 
2026-10-24T08:34:46Z  link: 40 % loss, 400 ms delay for 60 s
2026-10-24T08:35:54Z  PASS: 40 % / 400 ms: 0 link_lost event(s) (any count allowed) 
2026-10-24T08:35:54Z  PASS: GCS still running 
2026-10-24T08:35:54Z  PASS: flight still open 
2026-10-24T08:35:54Z  link: 80 % loss, 400 ms delay for 60 s
2026-10-24T08:37:02Z  PASS: 80 % / 400 ms: 0 link_lost event(s) (any count allowed) 
2026-10-24T08:37:02Z  PASS: GCS still running 
2026-10-24T08:37:02Z  PASS: flight still open 
2026-10-24T08:37:04Z  PASS: vehicle disarmed 
2026-10-24T08:37:04Z  PASS: flight closed with end_reason = disarmed disarmed
```

## Database rows

### connection_sessions (1 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T08:33:17.037Z | 2026-10-24T08:37:07.063Z | None | None |

### flight_sessions (1 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6 | None | 2026-10-24T08:33:32.530Z | 2026-10-24T08:37:03.604Z | 211.075 | 0.0 |  | 0.0 | 0.0 | 9.083333333333334 | 4899.380089344347 | 0.0 | 0.0 |  | 1 | external | 0 | None | 0 | disarmed | 2026-10-24T08:37:03.604Z | 11.9 | 743.0 | 1 |

### flights (1 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 08:33:32 | 2026-10-24 08:33:32 | 2026-10-24T08:37:03Z | 2026-10-24 08:37:03 | 211 | 80.13451443569554 | 11.9 | 12.6 | 0 | 0.0 | 0.0 | 4899.380089344347 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |

### flight_batteries (1 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | None | None | None | None | None | None | None | 12.6 | 99 | 12.554 | 77 | 11.9 | 743.0 | 9.083333333333334 |

### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


