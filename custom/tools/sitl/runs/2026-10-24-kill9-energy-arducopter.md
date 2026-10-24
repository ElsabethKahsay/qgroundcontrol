# SITL run: kill9-energy-arducopter — PASS

- Vehicle: ArduCopter (SITL instance 3, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-24T09:47:25Z, finished 2026-10-24T09:53:54Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 40963, 'to_sitl': 1159, 'dropped': 0}

## Checks

- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** vehicle armed
- **PASS** climbed to 16 m
- **PASS** vehicle disarmed
- **PASS** flight Wh within 5 % of BATTERY_STATUS.energy_consumed — GCS 12.472 Wh, vehicle 12.472 Wh (0.0 %)
- **PASS** vehicle armed
- **PASS** climbed to 16 m
- **PASS** flight row left open by the crash
- **PASS** orphaned flight closed as crash_recovered — duration 69.289s, last summary 2026-10-24T09:51:31.063Z
- **PASS** new flight opened for the still-armed vehicle, arm_source = already_armed — arm_source=already_armed
- **PASS** vehicle disarmed
- **PASS** joined flight closed with end_reason = disarmed — disarmed
- **PASS** joined flight has energy and min voltage recorded — GCS 10.667 Wh from its first battery sample; vehicle 18.306 Wh from the join; min 12.60 V

## Timeline

```
2026-10-24T09:47:25Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduCopter -N --no-mavproxy -I 3 -w
2026-10-24T09:47:26Z  vehicle heartbeat on control port (sysid 1, type 2)
2026-10-24T09:47:26Z  GCS started (pid 90852), data folder /tmp/skywin-sitl-kill9-energy-arducopter-kr200d1f
2026-10-24T09:47:31Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-24T09:47:39Z  vehicle pre-arm healthy: True
2026-10-24T09:47:41Z  arm refused (4): ['Arm: AHRS: waiting for home', 'PreArm: AHRS: waiting for home']
2026-10-24T09:47:49Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:47:56Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:48:03Z  arm refused (4): ['Arm: Need Position Estimate']
2026-10-24T09:48:08Z  arm command result 0
2026-10-24T09:48:09Z  PASS: vehicle armed 
2026-10-24T09:48:20Z  PASS: climbed to 16 m 
2026-10-24T09:50:20Z  PASS: vehicle disarmed 
2026-10-24T09:50:20Z  PASS: flight Wh within 5 % of BATTERY_STATUS.energy_consumed GCS 12.472 Wh, vehicle 12.472 Wh (0.0 %)
2026-10-24T09:50:21Z  vehicle pre-arm healthy: True
2026-10-24T09:50:21Z  arm command result 0
2026-10-24T09:50:21Z  PASS: vehicle armed 
2026-10-24T09:50:32Z  PASS: climbed to 16 m 
2026-10-24T09:51:32Z  kill -9 the GCS (pid 90852) mid-flight
2026-10-24T09:51:32Z  PASS: flight row left open by the crash 
2026-10-24T09:51:37Z  GCS started (pid 91838), data folder /tmp/skywin-sitl-kill9-energy-arducopter-kr200d1f
2026-10-24T09:51:38Z  PASS: orphaned flight closed as crash_recovered duration 69.289s, last summary 2026-10-24T09:51:31.063Z
2026-10-24T09:51:40Z  PASS: new flight opened for the still-armed vehicle, arm_source = already_armed arm_source=already_armed
2026-10-24T09:53:41Z  PASS: vehicle disarmed 
2026-10-24T09:53:44Z  PASS: joined flight closed with end_reason = disarmed disarmed
2026-10-24T09:53:44Z  PASS: joined flight has energy and min voltage recorded GCS 10.667 Wh from its first battery sample; vehicle 18.306 Wh from the join; min 12.60 V
```

## Database rows

### connection_sessions (2 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T09:47:30.915Z | 2026-10-24T09:51:31.063Z | 2026-10-24T09:51:31.063Z | crash_recovered |
| 2 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T09:51:39.960Z | 2026-10-24T09:53:50.062Z | None | None |

### flight_sessions (3 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-24T09:48:09.751Z | 2026-10-24T09:50:20.784Z | 131.033 | 0.0 |  | 0.0 | 0.0 | 12.472222222222221 | 0.0 | 0.0 | 0.0 |  | 1 | external | 0 | None | 0 | disarmed | 2026-10-24T09:50:20.784Z | 12.6 | 991.0 | 1 |
| 2 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-24T09:50:21.775Z | 2026-10-24T09:51:31.063Z | 69.289 | 0.0 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | 1 | external | 0 | None | 0 | crash_recovered | 2026-10-24T09:51:31.063Z | 12.6 | 528.0 | 2 |
| 3 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-24T09:51:39.960Z | 2026-10-24T09:53:41.811Z | 121.851 | 0.0 |  | 0.0 | 0.0 | 10.666666666666668 | 0.0 | 0.0 | 0.0 |  | 2 | already_armed | 0 | None | 0 | disarmed | 2026-10-24T09:53:41.811Z | 12.6 | 846.0 | 3 |

### flights (3 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 09:48:09 | 2026-10-24 09:48:09 | 2026-10-24T09:50:20Z | 2026-10-24 09:50:20 | 131 | 65.60695538057742 | 12.6 | 12.6 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |
| 2 | None | 1 | NONE | None | None | arm source: external [crash recovered] | None | 0 | 0 | 2026-10-24 09:50:21 | 2026-10-24 09:50:21 | None | 2026-10-24T09:51:31.063Z | 69 | None | None | None | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |
| 3 | None | 1 | NONE | None | None | arm source: already_armed | None | 0 | 0 | 2026-10-24 09:51:39 | 2026-10-24 09:51:39 | 2026-10-24T09:53:41Z | 2026-10-24 09:53:41 | 122 | 65.61679790026247 | 12.6 | 0.0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |

### flight_batteries (3 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | None | None | None | None | None | None | None | 12.6 | 100 | 12.6 | 69 | 12.6 | 991.0 | 12.472222222222221 |
| 2 | 2 | 1 | None | None | None | None | None | None | None | 12.6 | 69 | None | None | None | None | None |
| 3 | 3 | 1 | None | None | None | None | None | None | None | None | None | 12.6 | 23 | 12.6 | 846.0 | 10.666666666666668 |

### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


