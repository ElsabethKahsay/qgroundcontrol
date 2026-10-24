# SITL run: rtl-link-loss-arducopter — PASS

- Vehicle: ArduCopter (SITL instance 3, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-24T09:44:44Z, finished 2026-10-24T09:47:25Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 9156, 'to_sitl': 597, 'dropped': 7388}

## Checks

- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** vehicle armed
- **PASS** climbed to 32 m
- **PASS** link_lost then link_restored logged — ['link_lost', 'link_restored']
- **PASS** flight still open after the link came back
- **PASS** copter landed and disarmed during the blackout
- **PASS** flight closed with end_reason = link_lost — end_reason=link_lost ended_at=2026-10-24T09:46:30.064Z (link cut 2026-10-24T09:46:25Z)
- **PASS** history row written for the flight — duration 61s

## Timeline

```
2026-10-24T09:44:44Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduCopter -N --no-mavproxy -I 3 -w
2026-10-24T09:44:46Z  vehicle heartbeat on control port (sysid 1, type 2)
2026-10-24T09:44:46Z  GCS started (pid 90152), data folder /tmp/skywin-sitl-rtl-link-loss-arducopter-bpibif3f
2026-10-24T09:44:51Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-24T09:44:58Z  vehicle pre-arm healthy: True
2026-10-24T09:45:01Z  arm refused (4): ['Arm: AHRS: waiting for home', 'PreArm: AHRS: waiting for home']
2026-10-24T09:45:08Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:45:15Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:45:23Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:45:28Z  arm command result 0
2026-10-24T09:45:29Z  PASS: vehicle armed 
2026-10-24T09:45:45Z  PASS: climbed to 32 m 
2026-10-24T09:45:55Z  link blackout 20 s (armed)
2026-10-24T09:46:15Z  link restored
2026-10-24T09:46:25Z  PASS: link_lost then link_restored logged ['link_lost', 'link_restored']
2026-10-24T09:46:25Z  PASS: flight still open after the link came back 
2026-10-24T09:46:25Z  second blackout; LAND commanded on the control port during it
2026-10-24T09:47:10Z  PASS: copter landed and disarmed during the blackout 
2026-10-24T09:47:15Z  PASS: flight closed with end_reason = link_lost end_reason=link_lost ended_at=2026-10-24T09:46:30.064Z (link cut 2026-10-24T09:46:25Z)
2026-10-24T09:47:15Z  PASS: history row written for the flight duration 61s
```

## Database rows

### connection_sessions (1 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T09:44:50.215Z | 2026-10-24T09:47:20.063Z | None | None |

### flight_sessions (1 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-24T09:45:29.058Z | 2026-10-24T09:46:30.064Z | 61.006 | 0.0 |  | 0.0 | 0.0 | 9.5 | 0.0 | 0.0 | 0.0 |  | 1 | external | 0 | None | 0 | link_lost | 2026-10-24T09:46:30.064Z | 12.6 | 755.0 | 1 |

### flights (1 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 09:45:29 | 2026-10-24 09:45:29 | 2026-10-24T09:47:14Z | 2026-10-24 09:47:14 | 61 | 131.21391076115484 | 12.6 | 12.6 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |

### flight_batteries (1 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | None | None | None | None | None | None | None | 12.6 | 100 | 12.6 | 77 | 12.6 | 755.0 | 9.5 |

### session_events (4 rows)

| id | connection_session_id | flight_session_id | event | detail | at |
|---|---|---|---|---|---|
| 1 | 1 | 1 | link_lost |  | 2026-10-24T09:46:00.064Z |
| 2 | 1 | 1 | link_restored | armed | 2026-10-24T09:46:18.062Z |
| 3 | 1 | 1 | link_lost |  | 2026-10-24T09:46:30.064Z |
| 4 | 1 | None | link_restored | disarmed | 2026-10-24T09:47:15.562Z |

### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


