# SITL run: power-type-arducopter — FAIL

- Vehicle: ArduCopter (SITL instance 5, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-27T11:54:45Z, finished 2026-10-27T11:58:18Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 22376, 'to_sitl': 624, 'dropped': 0}

## Checks

- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** vehicle armed
- **PASS** climbed to 16 m
- **PASS** reached the target
- **PASS** came back over home
- **PASS** vehicle disarmed
- **PASS** flight recorded with the power type — flight 5
- **PASS** hover before the target counted loaded — hover_min_loaded=0.6333666666666671, cruise_min_loaded=0.5, hover_min_empty=0.7665500000000006, cruise_min_empty=0.6001, cruise_speed_ms=20.331769863669226, fc_mah=13.0, synth_hover_a=0.30000000000000004, synth_cruise_a=0.30000000000000004, ref_throttle_hover=0.3566687302815645, ref_throttle_cruise=0.39333045480713874, arm_cell_v=4.2, started_full=1, telemetry_coverage=1.0, learning_state=pending
- **PASS** cruise out counted loaded
- **PASS** hover at the target and landing counted empty (dropped)
- **PASS** cruise back counted empty
- **PASS** cruise speed recorded
- **PASS** FC mAh used recorded (synthetic monitor)
- **PASS** synthetic current per phase
- **PASS** throttle per phase
- **PASS** pack full at arm (3S, about 4.2 V per cell)
- **PASS** telemetry coverage near 100 %
- **PASS** waiting for the charger number
- **PASS** RETURN NOW announced while airborne
- **FAIL** charge used counted by the GCS (synthetic current ignored)
- **PASS** no QML errors from the power-type screens

## Timeline

```
2026-10-27T11:54:45Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduCopter -N --no-mavproxy -I 5 -w --add-param-file=/tmp/skywin-sitl-power-type-arducopter-ahvcqy0i/sitl/scenario.parm
2026-10-27T11:54:46Z  vehicle heartbeat on control port (sysid 1, type 2)
2026-10-27T11:54:46Z  GCS started (pid 158704), data folder /tmp/skywin-sitl-power-type-arducopter-ahvcqy0i
2026-10-27T11:54:51Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-27T11:54:59Z  home -35.363262,149.165237
2026-10-27T11:54:59Z  closing the GCS to seed its database
2026-10-27T11:55:09Z  seeded power type 1 for 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762, target -35.360564,149.165237, 250 mAh pack
2026-10-27T11:55:09Z  GCS started (pid 158831), data folder /tmp/skywin-sitl-power-type-arducopter-ahvcqy0i
2026-10-27T11:55:09Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-27T11:55:29Z  vehicle pre-arm healthy: True
2026-10-27T11:55:29Z  arm command result 0
2026-10-27T11:55:30Z  PASS: vehicle armed 
2026-10-27T11:55:40Z  PASS: climbed to 16 m 
2026-10-27T11:56:43Z  PASS: reached the target 
2026-10-27T11:57:36Z  PASS: came back over home 
2026-10-27T11:58:07Z  PASS: vehicle disarmed 
2026-10-27T11:58:08Z  PASS: flight recorded with the power type flight 5
2026-10-27T11:58:08Z  power record: hover_min_loaded=0.6333666666666671, cruise_min_loaded=0.5, hover_min_empty=0.7665500000000006, cruise_min_empty=0.6001, cruise_speed_ms=20.331769863669226, fc_mah=13.0, synth_hover_a=0.30000000000000004, synth_cruise_a=0.30000000000000004, ref_throttle_hover=0.3566687302815645, ref_throttle_cruise=0.39333045480713874, arm_cell_v=4.2, started_full=1, telemetry_coverage=1.0, learning_state=pending
2026-10-27T11:58:08Z  PASS: hover before the target counted loaded hover_min_loaded=0.6333666666666671, cruise_min_loaded=0.5, hover_min_empty=0.7665500000000006, cruise_min_empty=0.6001, cruise_speed_ms=20.331769863669226, fc_mah=13.0, synth_hover_a=0.30000000000000004, synth_cruise_a=0.30000000000000004, ref_throttle_hover=0.3566687302815645, ref_throttle_cruise=0.39333045480713874, arm_cell_v=4.2, started_full=1, telemetry_coverage=1.0, learning_state=pending
2026-10-27T11:58:08Z  PASS: cruise out counted loaded 
2026-10-27T11:58:08Z  PASS: hover at the target and landing counted empty (dropped) 
2026-10-27T11:58:08Z  PASS: cruise back counted empty 
2026-10-27T11:58:08Z  PASS: cruise speed recorded 
2026-10-27T11:58:08Z  PASS: FC mAh used recorded (synthetic monitor) 
2026-10-27T11:58:08Z  PASS: synthetic current per phase 
2026-10-27T11:58:08Z  PASS: throttle per phase 
2026-10-27T11:58:08Z  PASS: pack full at arm (3S, about 4.2 V per cell) 
2026-10-27T11:58:08Z  PASS: telemetry coverage near 100 % 
2026-10-27T11:58:08Z  PASS: waiting for the charger number 
2026-10-27T11:58:08Z  FINDING: the synthetic current stayed at BATT_AMP_OFFSET (0.30 A) in hover and cruise. ArduPilot computes it from the k_throttle servo output, which a multicopter does not use (its motors are k_motor1..N), so on ArduCopter it does not follow throttle.
2026-10-27T11:58:18Z  GCS log: 4.058 - info: VehicleProfileManager: power-type estimate off: not a copter (default:VehicleProfileManager::_updatePowerLive:1059)
2026-10-27T11:58:18Z  GCS log: 3.240 - info: VehicleProfileManager: power-type estimate off: not a copter (default:VehicleProfileManager::_updatePowerLive:1059)
2026-10-27T11:58:18Z  GCS log: 5.341 - info: VehicleProfileManager: power-type estimate on (Quad X · 13×4.5 · 3S, charge used from fc) (default:VehicleProfileManager::_updatePowerLive:1059)
2026-10-27T11:58:18Z  GCS log: 25.363 - info: VehicleProfileManager: RETURN NOW on (trip needs 0.37 Ah, 0.20 Ah left) (default:VehicleProfileManager::_updatePowerLive:1067)
2026-10-27T11:58:18Z  GCS log: 25.375 - warning: RETURN NOW: the rest of the trip needs 0.4 Ah, 0.2 Ah are left above the reserve. (preflight.plugin:PreflightPlugin::init()::<lambda()>:401)
2026-10-27T11:58:18Z  GCS log: 175.362 - info: VehicleProfileManager: RETURN NOW off (trip needs 0.53 Ah, 0.18 Ah left) (default:VehicleProfileManager::_updatePowerLive:1067)
2026-10-27T11:58:18Z  PASS: RETURN NOW announced while airborne 
2026-10-27T11:58:18Z  FAIL: charge used counted by the GCS (synthetic current ignored) 
2026-10-27T11:58:18Z  PASS: no QML errors from the power-type screens 
```

## Database rows

### connection_sessions (2 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-27T11:54:50.899Z | 2026-10-27T11:55:01.045Z | 2026-10-27T11:55:01.045Z | crash_recovered |
| 2 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-27T11:55:12.925Z | 2026-10-27T11:58:13.046Z | None | None |

### flight_sessions (5 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id | power_type_id | hover_min_empty | hover_min_loaded | cruise_min_empty | cruise_min_loaded | cruise_speed_ms | fc_mah | synth_hover_a | synth_cruise_a | ref_throttle_hover | ref_throttle_cruise | arm_cell_v | started_full | telemetry_coverage | time_at_target_min | charger_mah | learning_state |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T11:55:09Z | 2026-10-27T11:55:09Z | 0.0 | 0.0 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | None |  | None |  | 0 | crash_recovered | None | None | None | None | 1 | 3.0 | 0.0 | 10.0 | 0.0 | 15.0 | 4250.0 | None | None | None | None | None | 1 | 1.0 | None | 5100.0 | used |
| 2 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T11:55:09Z | 2026-10-27T11:55:09Z | 0.0 | 0.0 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | None |  | None |  | 0 | crash_recovered | None | None | None | None | 1 | 8.0 | 0.0 | 4.0 | 0.0 | 15.0 | 4722.25 | None | None | None | None | None | 1 | 1.0 | None | 5666.7 | used |
| 3 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T11:55:09Z | 2026-10-27T11:55:09Z | 0.0 | 1.5 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | None |  | None |  | 0 | crash_recovered | None | None | None | None | 1 | 0.0 | 3.0 | 0.0 | 10.0 | 15.0 | 5458.333333333334 | None | None | None | None | None | 1 | 1.0 | None | 6550.0 | used |
| 4 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T11:55:09Z | 2026-10-27T11:55:09Z | 0.0 | 1.5 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | None |  | None |  | 0 | crash_recovered | None | None | None | None | 1 | 0.0 | 8.0 | 0.0 | 4.0 | 15.0 | 6055.583333333333 | None | None | None | None | None | 1 | 1.0 | None | 7266.7 | used |
| 5 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T11:55:30.676Z | 2026-10-27T11:58:07.702Z | 157.026 | 0.0 |  | 0.0 | 0.0 | 0.16666666666666669 | 598.7307660924845 | -35.36056413526366 | 149.1652374 | typed | 2 | external | 0 | None | 0 | disarmed | 2026-10-27T11:58:07.702Z | 12.6 | 13.0 | 1 | 1 | 0.7665500000000006 | 0.6333666666666671 | 0.6001 | 0.5 | 20.331769863669226 | 13.0 | 0.30000000000000004 | 0.30000000000000004 | 0.3566687302815645 | 0.39333045480713874 | 4.2 | 1 | 1.0 | -1.0 | None | pending |

### flights (1 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-27 11:55:30 | 2026-10-27 11:55:30 | 2026-10-27T11:58:07Z | 2026-10-27 11:58:07 | 157 | 37.312992125984245 | 12.6 | 12.6 | 0 | 0.0 | 0.0 | 598.7307660924845 | 0.0 | 0 | 0 | 0 | 0 | None | -35.36056413526366 | 149.1652374 | typed |

### flight_batteries (1 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 5 | 1 | None | None | None | None | None | None | None | 12.6 | 99 | 12.6 | 99 | 12.6 | 13.0 | 0.16666666666666669 |

### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


