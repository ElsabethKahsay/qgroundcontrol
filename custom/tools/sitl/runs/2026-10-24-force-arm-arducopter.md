# SITL run: force-arm-arducopter — PASS

- Vehicle: ArduCopter (SITL instance 3, ArduPilot ArduPilot-4.6.0-beta1-6903-g7f5070b504)
- Started 2026-10-24T08:44:00Z, finished 2026-10-24T08:44:45Z
- GCS: build/Debug/PreflightQGroundControl, throw-away data folder (deleted afterwards)
- Relay counters: {'to_gcs': 4032, 'to_sitl': 208, 'dropped': 0}

Pre-arm made to fail with BATT_ARM_VOLT = 20 V (the simulated pack is about 12.6 V).

## Checks

- **PASS** GCS opened a connection session — device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
- **PASS** GPS fix (pre-arm still failing on purpose)
- **PASS** normal arm refused by the failing pre-arm
- **PASS** force arm accepted
- **PASS** GCS opened a flight on the external arm — arm_source=external gate_allowed=0
- **PASS** arm source recorded as external — external
- **PASS** vehicle disarmed
- **PASS** flight closed with end_reason = disarmed — end_reason=disarmed duration=6.015s

## Timeline

```
2026-10-24T08:44:00Z  SITL: /home/lisabeth/ardupilot/Tools/autotest/sim_vehicle.py -v ArduCopter -N --no-mavproxy -I 3 -w --add-param-file=/tmp/skywin-sitl-force-arm-arducopter-4llztmuz/sitl/scenario.parm
2026-10-24T08:44:02Z  vehicle heartbeat on control port (sysid 1, type 2)
2026-10-24T08:44:02Z  GCS started (pid 65471), data folder /tmp/skywin-sitl-force-arm-arducopter-4llztmuz
2026-10-24T08:44:07Z  PASS: GCS opened a connection session device 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762
2026-10-24T08:44:11Z  PASS: GPS fix (pre-arm still failing on purpose) 
2026-10-24T08:44:24Z  normal arm: ACK 4, vehicle said ['Arm: Battery 1 below minimum arming voltage']
2026-10-24T08:44:27Z  PASS: normal arm refused by the failing pre-arm 
2026-10-24T08:44:27Z  force arm (2989): ACK 0
2026-10-24T08:44:28Z  PASS: force arm accepted 
2026-10-24T08:44:33Z  PASS: GCS opened a flight on the external arm arm_source=external gate_allowed=0
2026-10-24T08:44:33Z  PASS: arm source recorded as external external
2026-10-24T08:44:34Z  PASS: vehicle disarmed 
2026-10-24T08:44:35Z  PASS: flight closed with end_reason = disarmed end_reason=disarmed duration=6.015s
```

## Database rows

### connection_sessions (1 rows)

| id | device_uid | session_mode | operator | app_version | opened_at | last_seen_at | closed_at | end_reason |
|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None |  | phase1-step0-4-g7aeb4a40d | 2026-10-24T08:44:06.214Z | 2026-10-24T08:44:36.063Z | None | None |

### flight_sessions (1 rows)

| id | device_uid | battery_serial | started_at | ended_at | duration_seconds | payload_weight_kg | location_name | plan_lat | plan_lon | energy_consumed_wh | distance_m | target_lat | target_lon | target_source | connection_session_id | arm_source | gate_allowed | session_mode | is_bench | end_reason | last_summary_at | min_voltage | mah_used | history_flight_id |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 35b13a255f4fc00d2a54562ac1b3a51cb65d31f6c43ea1c96a64654b991f5762 | None | 2026-10-24T08:44:28.962Z | 2026-10-24T08:44:34.977Z | 6.015 | 0.0 |  | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 | 0.0 |  | 1 | external | 0 | None | 0 | disarmed | 2026-10-24T08:44:34.977Z | 12.6 | 0.0 | 1 |

### flights (1 rows)

| id | operator_id | vehicle_id | mode | purpose | location | notes | weather_summary | pre_checklist_complete | post_checklist_complete | started_at | armed_at | disarmed_at | ended_at | duration_sec | max_altitude_m | min_battery_v | max_battery_v | flight_mode_changes | max_ground_speed_ms | max_vertical_speed_ms | distance_flown_m | avg_battery_v | check_pass_count | check_fail_count | check_warn_count | anomaly_count | device_uid | target_lat | target_lon | target_source |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | None | 1 | NONE | None | None | arm source: external | None | 0 | 0 | 2026-10-24 08:44:28 | 2026-10-24 08:44:28 | 2026-10-24T08:44:34Z | 2026-10-24 08:44:34 | 6 | 0.0 | 12.6 | 12.6 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 0 | 0 | 0 | 0 | None | None | None | None |

### flight_batteries (1 rows)

| id | flight_session_id | slot | pack_serial | pack_label | chemistry | series_cells | parallel_cells | cell_mah | rated_mah | start_voltage | start_percent | end_voltage | end_percent | min_voltage | mah_used | wh_used |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | None | None | None | None | None | None | None | 12.6 | 100 | 12.6 | 100 | 12.6 | 0.0 | 0.0 |

### session_events (0 rows)


### check_results (0 rows)


### hardware_test_events (0 rows)


### motor_test_results (0 rows)


### surface_test_results (0 rows)


