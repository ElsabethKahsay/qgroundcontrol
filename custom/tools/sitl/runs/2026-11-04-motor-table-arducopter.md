# SITL run: motor-table-arducopter — PASS

- Vehicle: ArduCopter (SITL instance 6, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-11-04T09:40:05Z, finished 2026-11-04T09:43:25Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 20564, 'to_sitl': 771, 'dropped': 0}

## Checks

- **PASS** the drone's parameters took in the simulator
- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** vehicle armed
- **PASS** climbed to 16 m
- **PASS** flew out 600 m
- **PASS** came back over home
- **PASS** vehicle disarmed
- **PASS** flight recorded for the charger comparison — flight 1
- **PASS** charge counted by the GCS — gcs_ah=3.4011394522404412, started_full=1, start_soc=100.0, hover_w=1932.4651330274137, hover_s=42.0, cruise_ratio=2.0979084806005903, cruise_s=50.0, avg_volts=23.864028169014087, min_volts=23.454, beyond_s=0.0, overall_throttle_s=0.0, density=0.6928154516615539, pack_ohm=0.008436958572136605, charger_mah=None, state=pending
- **PASS** started on a full pack (25.8 V, 4.3 V per cell)
- **PASS** at least 30 s of steady hover
- **PASS** motor signals the whole flight (no time on the overall throttle)
- **PASS** pack voltage recorded (6S)
- **PASS** air density from the 2900 m pressure (0.66-0.78)
- **PASS** waiting for the charger number
- **PASS** hover remembered for the next take-off — {'hover_thrust_g': 3254.9221014498535, 'hover_samples': 21}
- **PASS** estimate on from the motor table, counted by the GCS
- **PASS** no QML errors from the motor-table screens

## Timeline

```
2026-11-04T09:40:05Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduCopter -N --no-mavproxy -I 6 -w --add-param-file=/tmp/skywin-sitl-motor-table-arducopter-r5_6i1j2/sitl/scenario.parm -f x --model x:skywin_m6215_6s.json -l -35.363261,149.165230,2900,353
2026-11-04T09:40:06Z  vehicle heartbeat on control port (sysid 1, type 2)
2026-11-04T09:40:08Z  PASS: the drone's parameters took in the simulator 
2026-11-04T09:40:08Z  GCS started (pid 335038), data folder /tmp/skywin-sitl-motor-table-arducopter-r5_6i1j2
2026-11-04T09:40:14Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-11-04T09:40:19Z  home -35.363261,149.165230 at 2900 m AMSL
2026-11-04T09:40:19Z  closing the GCS to seed its database
2026-11-04T09:40:30Z  seeded: 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 on the motor table, 6S Li-ion 20 Ah pack (no hover, no weight)
2026-11-04T09:40:30Z  GCS started (pid 335217), data folder /tmp/skywin-sitl-motor-table-arducopter-r5_6i1j2
2026-11-04T09:40:30Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-11-04T09:40:50Z  vehicle pre-arm healthy: True
2026-11-04T09:40:50Z  arm command result 0
2026-11-04T09:40:50Z  PASS: vehicle armed 
2026-11-04T09:41:01Z  PASS: climbed to 16 m 
2026-11-04T09:42:18Z  fly_to: arrived, top ground speed 19.6 m/s
2026-11-04T09:42:18Z  PASS: flew out 600 m 
2026-11-04T09:42:56Z  fly_to: arrived, top ground speed 19.5 m/s
2026-11-04T09:42:56Z  PASS: came back over home 
2026-11-04T09:43:12Z  PASS: vehicle disarmed 
2026-11-04T09:43:14Z  PASS: flight recorded for the charger comparison flight 1
2026-11-04T09:43:14Z  flight record: gcs_ah=3.4011394522404412, started_full=1, start_soc=100.0, hover_w=1932.4651330274137, hover_s=42.0, cruise_ratio=2.0979084806005903, cruise_s=50.0, avg_volts=23.864028169014087, min_volts=23.454, beyond_s=0.0, overall_throttle_s=0.0, density=0.6928154516615539, pack_ohm=0.008436958572136605, charger_mah=None, state=pending
2026-11-04T09:43:14Z  PASS: charge counted by the GCS gcs_ah=3.4011394522404412, started_full=1, start_soc=100.0, hover_w=1932.4651330274137, hover_s=42.0, cruise_ratio=2.0979084806005903, cruise_s=50.0, avg_volts=23.864028169014087, min_volts=23.454, beyond_s=0.0, overall_throttle_s=0.0, density=0.6928154516615539, pack_ohm=0.008436958572136605, charger_mah=None, state=pending
2026-11-04T09:43:14Z  PASS: started on a full pack (25.8 V, 4.3 V per cell) 
2026-11-04T09:43:14Z  PASS: at least 30 s of steady hover 
2026-11-04T09:43:14Z  PASS: motor signals the whole flight (no time on the overall throttle) 
2026-11-04T09:43:14Z  PASS: pack voltage recorded (6S) 
2026-11-04T09:43:14Z  PASS: air density from the 2900 m pressure (0.66-0.78) 
2026-11-04T09:43:14Z  PASS: waiting for the charger number 
2026-11-04T09:43:14Z  cruise at 70 km/h: 50 s recorded, ratio 2.0979084806005903
2026-11-04T09:43:14Z  PASS: hover remembered for the next take-off {'hover_thrust_g': 3254.9221014498535, 'hover_samples': 21}
2026-11-04T09:43:14Z  weight the GCS measured: 13.0 kg (the model is 12.0 kg)
2026-11-04T09:43:14Z  charge used: GCS 3.40 Ah, simulator 2.38 Ah (GCS / simulator 1.432)
2026-11-04T09:43:25Z  GCS log: 5.177 - info: VehicleProfileManager: power-type estimate off: vehicle type not known yet (default:VehicleProfileManager::_updatePowerLive:1704)
2026-11-04T09:43:25Z  GCS log: 7.122 - info: VehicleProfileManager: power-type estimate off: no power type linked (default:VehicleProfileManager::_updatePowerLive:1704)
2026-11-04T09:43:25Z  GCS log: 4.011 - info: VehicleProfileManager: power-type estimate off: vehicle type not known yet (default:VehicleProfileManager::_updatePowerLive:1704)
2026-11-04T09:43:25Z  GCS log: 5.925 - info: VehicleProfileManager: power-type estimate on (M6215 HighProt 320KV · MSC 22×7.8 · 6S quad, charge used from gcs) (default:VehicleProfileManager::_updatePowerLive:1704)
2026-11-04T09:43:25Z  PASS: estimate on from the motor table, counted by the GCS 
2026-11-04T09:43:25Z  PASS: no QML errors from the motor-table screens 
```

## Database rows

### connection_sessions (2 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-11-04T09:40:14.163Z | 2026-11-04T09:40:24.527Z | 2026-11-04T09:40:24.527Z | crash_recovered |
| 2 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-11-04T09:40:34.113Z | 2026-11-04T09:43:24.527Z | None | None |

### flight_sessions (1 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id | power_type_id | hover_min_empty | hover_min_loaded | cruise_min_empty | cruise_min_loaded | cruise_speed_ms | fc_mah | synth_hover_a | synth_cruise_a | ref_throttle_hover | ref_throttle_cruise | arm_cell_v | started_full | telemetry_coverage | time_at_target_min | charger_mah | learning_state |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-11-04T09:40:50.897Z | 2026-11-04T09:43:12.945Z | 142.047 | 0.0 |  | 0.0 | 0.0 | 0.0 | 1196.748724248177 | 0.0 | 0.0 |  | 2 | external | 0 | None | 0 | disarmed | 2026-11-04T09:43:12.945Z | 23.615 | None | 1 | None | None | None | None | None | None | None | None | None | None | None | None | None | None | None | None | None |

### flights (1 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | Wind 8 kt, Vis 11.9 km, Overcast, 15.9°C (Forecast (Open-Meteo), fetched 09:40Z) | 0 | 0 | 2026-11-04 09:40:50 | 2026-11-04 09:40:50 | 2026-11-04T09:43:12Z | 2026-11-04 09:43:12 | 142 | 20.004 | 23.615 | 25.8 | 0 | 0.0 | 0.0 | 1196.748724248177 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |

### flight_batteries (1 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | None | None | None | None | None | None | None | 25.8 | None | 25.148 | None | 23.615 | None | None |

### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


