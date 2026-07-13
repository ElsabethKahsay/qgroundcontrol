# GCS Preflight Plugin — P0–P3 Completion Checklist

> Generated: July 2026

---

## P0 — Core

- [x] 0.1 ChecklistItemModel — unit tests created, compiles
- [x] 0.2 ChecklistEngine — unit tests created, selective eval, stale detection, all-hits
- [x] 0.3 ChecklistEngine wired into PreflightPlugin — QML context properties registered
- [x] 0.4 Battery cycle DB writes — save/get/count methods, FlightSession round-trip
- [x] 0.5 Parameter poll replaced with subscription — TelemetryBridge::parameterUpdated signal, PreflightManager connected

## P1 — Correctness

- [x] 1.1 RcCalibrationCheck — unit test (factory default fails)
- [x] 1.2 MissionEnergyCheck — Haversine, margin, subtitle
- [x] 1.3 AmbientTemperatureCheck — auto ENV-003, manual version removed
- [x] 1.4 HardwareTestController — unit test (initial state, abort, profile)
- [x] 1.5 CompassOrientationCheck — auto-PASS on ROTATION_NONE, manual confirm on non-default

## P2 — Safety / Tests

- [x] 2.1 Test Infrastructure — mocks, QGC_BUILD_TESTING=ON
- [x] 2.2 ArmingGateTest — passive/active/hybrid/override/disconnect
- [x] 2.3 ChecklistEngineTest — selective eval, evaluateAll, stale detection, disconnected state
- [x] 2.4 MissionEnergyCheckTest — Haversine, energy margin, no-mission
- [x] 2.5 StateMachineTest — all states reachable, transitions, reset
- [x] 2.6 DatabaseManagerTest — schema, vehicle CRUD, battery CRUD, cycles, sessions
- [x] 2.7 RcCalibrationCheckTest — factory default fails
- [x] 2.8 WeatherProviderTest — singleton, initial state
- [x] 2.9 CompassOrientationCheckTest — orientation checks
- [x] 2.10 ChecklistItemModelTest — rowCount, data, signals, filter
- [x] 2.11 HardwareTestControllerTest — initial state, abort, profile

## P3 — UAV Auto-Detection

- [x] 3.1 VehicleRegistry — singleton, fingerprint, QML context property
- [x] 3.2 DB lookup & auto-register — lookupVehicleByFingerprint, registerNewVehicle
- [x] 3.3 Auto-registration flow — known/new vehicle signals
- [x] 3.4 Vehicle Management QML — VehicleManagement.qml
- [x] 3.5 Vehicle-specific check profile — vehicle_config table, applyVehicleConfig
- [x] 3.6 Session tracking — flight_sessions, ArmingGate integration

## Build Integration

- [ ] Full build succeeds — `cmake --build build -j$(nproc)` zero errors
- [ ] Test suite passes — `./build/Debug/QGroundControl --unittest:*` all pass
- [ ] DB has at least one complete session record
- [ ] App launches clean — no crash on startup
- [ ] Nothing outside custom/ modified
