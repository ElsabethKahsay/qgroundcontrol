# SITL run: rtl-link-loss-arduplane — PASS

- Vehicle: ArduPlane (SITL instance 3, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-24T07:48:19Z, finished 2026-10-24T07:50:20Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 11696, 'to_sitl': 325, 'dropped': 2450}

## Checks

- **PASS** GCS opened a connection session — device 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6
- **PASS** vehicle ready to arm
- **PASS** vehicle armed
- **PASS** climbed to 32 m
- **PASS** RTL accepted
- **PASS** still in RTL and airborne after 30 s — mode RTL, alt 38 m
- **PASS** link_lost then link_restored logged — ['link_lost', 'link_restored']
- **PASS** flight still open after the link came back
- **PASS** vehicle disarmed
- **PASS** flight closed with end_reason = disarmed — disarmed
- **PASS** history row written for the flight — duration 78s

## Timeline

```
2026-10-24T07:48:19Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduPlane -N --no-mavproxy -I 3 -w
2026-10-24T07:48:20Z  vehicle heartbeat on control port (sysid 1, type 1)
2026-10-24T07:48:20Z  GCS started (pid 50654), data folder /tmp/skywin-sitl-rtl-link-loss-arduplane-m975npaq
2026-10-24T07:48:25Z  PASS: GCS opened a connection session device 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6
2026-10-24T07:48:40Z  PASS: vehicle ready to arm 
2026-10-24T07:48:40Z  arm command result 0
2026-10-24T07:48:41Z  PASS: vehicle armed 
2026-10-24T07:48:48Z  PASS: climbed to 32 m 
2026-10-24T07:48:58Z  PASS: RTL accepted 
2026-10-24T07:49:28Z  PASS: still in RTL and airborne after 30 s mode RTL, alt 38 m
2026-10-24T07:49:28Z  link blackout 20 s (armed)
2026-10-24T07:49:48Z  link restored
2026-10-24T07:49:58Z  PASS: link_lost then link_restored logged ['link_lost', 'link_restored']
2026-10-24T07:49:58Z  PASS: flight still open after the link came back 
2026-10-24T07:49:59Z  PASS: vehicle disarmed 
2026-10-24T07:50:00Z  PASS: flight closed with end_reason = disarmed disarmed
2026-10-24T07:50:00Z  PASS: history row written for the flight duration 78s
```

## Database rows

### connection_sessions (1 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T07:48:25.257Z | 2026-10-24T07:50:15.062Z | None | None |

### flight_sessions (1 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 772d51c6c5aba6fbc66470b0fd327b808b866baa41842902f8f5c8232dd3cfd6 | None | 2026-10-24T07:48:41.840Z | 2026-10-24T07:49:59.856Z | 78.015 | 0.0 |  | 0.0 | 0.0 | 4.305555555555555 | 1467.9473227623719 | 0.0 | 0.0 |  | 1 | external | 0 | None | 0 | disarmed | 2026-10-24T07:49:59.856Z | 11.9 | 356.0 | 1 |

### flights (1 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 07:48:41 | 2026-10-24 07:48:41 | 2026-10-24T07:49:59Z | 2026-10-24 07:49:59 | 78 | 328.03477690288713 | 11.9 | 12.6 | 0 | 0.0 | 0.0 | 1467.9473227623719 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |

### flight_batteries (1 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | None | None | None | None | None | None | None | 12.6 | 99 | 12.259 | 89 | 11.9 | 356.0 | 4.305555555555555 |

### session_events (2 rows)

| id | connection_session_id | flight_session_id | event | detail | at |
|---|---|---|---|---|---|
| 1 | 1 | 1 | link_lost |  | 2026-10-24T07:49:33.064Z |
| 2 | 1 | 1 | link_restored | armed | 2026-10-24T07:49:48.902Z |

### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


