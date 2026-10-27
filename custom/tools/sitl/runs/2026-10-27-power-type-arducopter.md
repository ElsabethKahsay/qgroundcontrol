# SITL run: power-type-arducopter — PASS

- Vehicle: ArduCopter (SITL instance 5, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-27T10:54:56Z, finished 2026-10-27T10:58:30Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 22355, 'to_sitl': 954, 'dropped': 0}

## Checks

- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** vehicle armed
- **PASS** climbed to 16 m
- **PASS** reached the target
- **PASS** came back over home
- **PASS** vehicle disarmed
- **PASS** flight recorded with the power type — flight 5
- **PASS** hover before the target counted loaded — hover_min_loaded=0.63315, cruise_min_loaded=0.5003, hover_min_empty=0.7337166666666668, cruise_min_empty=0.6161666666666668, cruise_speed_ms=20.069316836602237, fc_mah=13.0, synth_hover_a=0.3000000000000001, synth_cruise_a=0.29999999999999993, ref_throttle_hover=0.35658696288348046, ref_throttle_cruise=0.39301113632292356, arm_cell_v=4.2, started_full=1, telemetry_coverage=1.0, learning_state=pending
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
- **PASS** no QML errors from the power-type screens

## Timeline

```
2026-10-27T10:54:56Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduCopter -N --no-mavproxy -I 5 -w --add-param-file=/tmp/skywin-sitl-power-type-arducopter-m_7eygte/sitl/scenario.parm
2026-10-27T10:54:58Z  vehicle heartbeat on control port (sysid 1, type 2)
2026-10-27T10:54:58Z  GCS started (pid 128235), data folder /tmp/skywin-sitl-power-type-arducopter-m_7eygte
2026-10-27T10:55:03Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-27T10:55:11Z  home -35.363262,149.165237
2026-10-27T10:55:11Z  closing the GCS to seed its database
2026-10-27T10:55:21Z  seeded power type 1 for 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762, target -35.360564,149.165237, 250 mAh pack
2026-10-27T10:55:21Z  GCS started (pid 128400), data folder /tmp/skywin-sitl-power-type-arducopter-m_7eygte
2026-10-27T10:55:21Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-27T10:55:41Z  vehicle pre-arm healthy: True
2026-10-27T10:55:41Z  arm command result 0
2026-10-27T10:55:42Z  PASS: vehicle armed 
2026-10-27T10:55:52Z  PASS: climbed to 16 m 
2026-10-27T10:56:55Z  PASS: reached the target 
2026-10-27T10:57:47Z  PASS: came back over home 
2026-10-27T10:58:19Z  PASS: vehicle disarmed 
2026-10-27T10:58:20Z  PASS: flight recorded with the power type flight 5
2026-10-27T10:58:20Z  power record: hover_min_loaded=0.63315, cruise_min_loaded=0.5003, hover_min_empty=0.7337166666666668, cruise_min_empty=0.6161666666666668, cruise_speed_ms=20.069316836602237, fc_mah=13.0, synth_hover_a=0.3000000000000001, synth_cruise_a=0.29999999999999993, ref_throttle_hover=0.35658696288348046, ref_throttle_cruise=0.39301113632292356, arm_cell_v=4.2, started_full=1, telemetry_coverage=1.0, learning_state=pending
2026-10-27T10:58:20Z  PASS: hover before the target counted loaded hover_min_loaded=0.63315, cruise_min_loaded=0.5003, hover_min_empty=0.7337166666666668, cruise_min_empty=0.6161666666666668, cruise_speed_ms=20.069316836602237, fc_mah=13.0, synth_hover_a=0.3000000000000001, synth_cruise_a=0.29999999999999993, ref_throttle_hover=0.35658696288348046, ref_throttle_cruise=0.39301113632292356, arm_cell_v=4.2, started_full=1, telemetry_coverage=1.0, learning_state=pending
2026-10-27T10:58:20Z  PASS: cruise out counted loaded 
2026-10-27T10:58:20Z  PASS: hover at the target and landing counted empty (dropped) 
2026-10-27T10:58:20Z  PASS: cruise back counted empty 
2026-10-27T10:58:20Z  PASS: cruise speed recorded 
2026-10-27T10:58:20Z  PASS: FC mAh used recorded (synthetic monitor) 
2026-10-27T10:58:20Z  PASS: synthetic current per phase 
2026-10-27T10:58:20Z  PASS: throttle per phase 
2026-10-27T10:58:20Z  PASS: pack full at arm (3S, about 4.2 V per cell) 
2026-10-27T10:58:20Z  PASS: telemetry coverage near 100 % 
2026-10-27T10:58:20Z  PASS: waiting for the charger number 
2026-10-27T10:58:20Z  FINDING: the synthetic current stayed at BATT_AMP_OFFSET (0.30 A) in hover and cruise. ArduPilot computes it from the k_throttle servo output, which a multicopter does not use (its motors are k_motor1..N), so on ArduCopter it does not follow throttle.
2026-10-27T10:58:30Z  GCS log: 4.096 - info: VehicleProfileManager: power-type estimate off: not a copter (default:VehicleProfileManager::_updatePowerLive:998)
2026-10-27T10:58:30Z  GCS log: 3.194 - info: VehicleProfileManager: power-type estimate off: not a copter (default:VehicleProfileManager::_updatePowerLive:998)
2026-10-27T10:58:30Z  GCS log: 5.296 - info: VehicleProfileManager: power-type estimate on (Quad X · 13×4.5 · 3S) (default:VehicleProfileManager::_updatePowerLive:998)
2026-10-27T10:58:30Z  GCS log: 25.417 - info: VehicleProfileManager: RETURN NOW on (trip needs 0.37 Ah, 0.20 Ah left) (default:VehicleProfileManager::_updatePowerLive:1004)
2026-10-27T10:58:30Z  GCS log: 25.444 - warning: RETURN NOW: the rest of the trip needs 0.4 Ah, 0.2 Ah are left above the reserve. (preflight.plugin:PreflightPlugin::init()::<lambda()>:400)
2026-10-27T10:58:30Z  GCS log: 175.417 - info: VehicleProfileManager: RETURN NOW off (trip needs 0.53 Ah, 0.18 Ah left) (default:VehicleProfileManager::_updatePowerLive:1004)
2026-10-27T10:58:30Z  PASS: RETURN NOW announced while airborne 
2026-10-27T10:58:30Z  PASS: no QML errors from the power-type screens 
```

## Database rows

### connection_sessions (2 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-27T10:55:02.398Z | 2026-10-27T10:55:12.046Z | 2026-10-27T10:55:12.046Z | crash_recovered |
| 2 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-27T10:55:24.332Z | 2026-10-27T10:58:24.045Z | None | None |

### flight_sessions (5 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id | power_type_id | hover_min_empty | hover_min_loaded | cruise_min_empty | cruise_min_loaded | cruise_speed_ms | fc_mah | synth_hover_a | synth_cruise_a | ref_throttle_hover | ref_throttle_cruise | arm_cell_v | started_full | telemetry_coverage | time_at_target_min | charger_mah | learning_state |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T10:55:21Z | 2026-10-27T10:55:21Z | 0.0 | 0.0 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | None |  | None |  | 0 | crash_recovered | None | None | None | None | 1 | 3.0 | 0.0 | 10.0 | 0.0 | 15.0 | 4250.0 | None | None | None | None | None | 1 | 1.0 | None | 5100.0 | used |
| 2 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T10:55:21Z | 2026-10-27T10:55:21Z | 0.0 | 0.0 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | None |  | None |  | 0 | crash_recovered | None | None | None | None | 1 | 8.0 | 0.0 | 4.0 | 0.0 | 15.0 | 4722.25 | None | None | None | None | None | 1 | 1.0 | None | 5666.7 | used |
| 3 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T10:55:21Z | 2026-10-27T10:55:21Z | 0.0 | 1.5 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | None |  | None |  | 0 | crash_recovered | None | None | None | None | 1 | 0.0 | 3.0 | 0.0 | 10.0 | 15.0 | 5458.333333333334 | None | None | None | None | None | 1 | 1.0 | None | 6550.0 | used |
| 4 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T10:55:21Z | 2026-10-27T10:55:21Z | 0.0 | 1.5 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | None |  | None |  | 0 | crash_recovered | None | None | None | None | 1 | 0.0 | 8.0 | 0.0 | 4.0 | 15.0 | 6055.583333333333 | None | None | None | None | None | 1 | 1.0 | None | 7266.7 | used |
| 5 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-27T10:55:42.163Z | 2026-10-27T10:58:19.206Z | 157.043 | 0.0 |  | 0.0 | 0.0 | 0.16666666666666669 | 598.9531533398318 | -35.36056413526366 | 149.1652374 | typed | 2 | external | 0 | None | 0 | disarmed | 2026-10-27T10:58:19.206Z | 12.6 | 13.0 | 1 | 1 | 0.7337166666666668 | 0.63315 | 0.6161666666666668 | 0.5003 | 20.069316836602237 | 13.0 | 0.3000000000000001 | 0.29999999999999993 | 0.35658696288348046 | 0.39301113632292356 | 4.2 | 1 | 1.0 | -1.0 | None | pending |

### flights (1 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | Wind 3 kt, Vis 52.8 km, Clear sky, 9.5°C (Forecast (Open-Meteo), fetched 10:55Z) | 0 | 0 | 2026-10-27 10:55:42 | 2026-10-27 10:55:42 | 2026-10-27T10:58:19Z | 2026-10-27 10:58:19 | 157 | 65.63976377952756 | 12.6 | 12.6 | 0 | 0.0 | 0.0 | 598.9531533398318 | 0.0 | 0 | 0 | 0 | 0 | None | -35.36056413526366 | 149.1652374 | typed |

### flight_batteries (1 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 5 | 1 | None | None | None | None | None | None | None | 12.6 | 99 | 12.6 | 99 | 12.6 | 13.0 | 0.16666666666666669 |

### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


