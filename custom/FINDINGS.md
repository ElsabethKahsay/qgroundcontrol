# FINDINGS — Consolidated Preflight Subsystem Audit

> **This is the single source of truth for audit findings.** The older reports
> (`analysis.md`, `ANALYSIS.md`, `STATIC_ANALYSIS_REPORT.md`) are superseded for
> status purposes and are known to contain stale and false-positive claims. Do
> not rely on their status; update **this** file instead.
>
> **Scope:** `custom/` (Skywin GCS = QGroundControl + custom preflight suite).
> **Written:** 2026-10-17. Line numbers are as of this date and drift — re-grep
> before acting on any single line ref.
>
> Status legend:
> - **RESOLVED** — fixed and verified in the current tree.
> - **FALSE_POSITIVE** — report was wrong; code is correct.
> - **STALE** — referenced code no longer exists / already superseded.
> - **OPEN** — still present, action pending.
> - **REPORT_ONLY** — by design; documented, no code change planned.
> - **UNVERIFIED** — carried from an older report; current status not confirmed.

---

## 1. Resolved findings (do not re-fix)

| ID | Finding | Fix evidence | Source |
|----|---------|--------------|--------|
| F-01 | RC channel data never populated → RC checks blind | `TelemetryBridge.cpp` RC_CHANNELS handler populates `_rcChannelValues`; `RcCalibrationCheck.cpp` empty-check uses `size() < 4` | my review #1 |
| F-02 | `allMandatoryPassed()` treated Warning/Skipped as blocking / gate couldn't open | `PreflightManager::allMandatoryPassed()` accepts Passed/Warning/Skipped | my review #2 |
| F-03 | `RcCheck` composite status ranking wrong (Skipped ranked above Passed) | `RcCheck.cpp` `statusRank()` Skipped → 0; `evaluate()` rewritten | my review #3 |
| F-04 | `AbstractCheck::confirm()` allowed overriding Auto checks | `AbstractCheck.cpp` `confirm()` guards Auto | my review #4a |
| F-05 | Config cache not invalidated on reset / DB swap | `AbstractCheck::reset()` calls `clearConfigCache()`; `PreflightManager::setDatabaseManager()` invalidates all | my review #4b |
| F-06 | Flight-session lifecycle signals never fired (gate↔session dead) | `PreflightPlugin.cpp` COMMAND_ACK handler emits `gateOpened/gateClosed` + `vehicleArmed/vehicleDisarmed` | my review #5 |
| F-07 | Gimbal detection dead — `_gimbalDetected` never assigned | `TelemetryBridge.cpp` handles `MOUNT_ORIENTATION` / `GIMBAL_DEVICE_ATTITUDE_STATUS` (latched); `GimbalLinkCheck` no-video downgraded Failed→Warning (advisory) | my review #7 |
| F-08 | `AccelConsistencyCheck` NaN → false mandatory Failed | `AccelConsistencyCheck.cpp` guards six values with `qIsNaN` | my review #10 |
| F-09 | RC factory-default detection compared against channel-1 values (masked uncalibrated ch2–4) | `RcCalibrationCheck::_isFactoryDefault` compares fixed 1100/1900 | my review #2(RC) |
| F-10 | `activatePostFlightChecks()` was a no-op (override refuses Auto; no mode) | `PreflightManager` post-flight mode flag gates `tick()`; post-flight checks marked | my review #5(PF) |
| F-11 | Weather checks flapped (timeout measured from post-`setStatus` timestamp) | `MetarTemperatureCheck` / `TafDeteriorationCheck`: 10 s timeout from fetch start | weather fix |
| F-12 | AccelConsistency NaN (see F-08) | — | — |
| F-13 | `R1` PreflightPlugin construction-order (connects before `_armingGate` created) | `PreflightPlugin.cpp:161` creates gate before connects at `:180`; `setDependencies` gets non-null args | analysis.md R1 |
| F-14 | `R2` many check classes never instantiated | All check headers exercised; RC sub-checks composed via `RcCheck`; 35 unique checks registered (smoke log) | analysis.md R2 |
| F-15 | `R3` `UavParameterManager::setWatchlist` never called (param FSM leg dead) | `PreflightManager.cpp:152` calls `setWatchlist(ParameterWatchlist::names())` | analysis.md R3 |
| F-16 | ArmingGate periodic evaluation timer never started | `ArmingGate.cpp:87` starts `m_gateTimer` | STATIC |
| F-17 | ArmingGate `forceArm` hardcoded 10 s timeout | `ArmingGate.cpp:337` uses `kForceArmOverrideSec` | STATIC |
| F-18 | Dead `s_emergencyCommands` branch in ArmingGate | removed | STATIC |
| F-19 | Never-emitted `forceArmIssued` signal | removed | STATIC |
| F-20 | Empty detection stub files (`DetectionBridge`/`DetectionModel`) | removed; only `VehicleRegistry` remains under `src/detection/` | STATIC |
| F-21 | `_connectionQuality` hardcoded to 100 on connect, never updated | `TelemetryBridge.cpp:1064` updates `_connectionQuality` from a computed quality | STATIC |

### UI changes applied (not findings; recorded for traceability)
- Merged the two top status bars on the Analyze preflight page into one
  (`custom/qml/cpts/PreflightChecklistView.qml`): pass-count text, arm-blocked
  summary, First/Next fix hints, and a SHOW/HIDE toggle for the priority list.
- Moved **ESC Firmware Version** out of the Propulsion grid to the weather info
  bar header via `kHiddenFromChecklist` (`PreflightChecklistFilterModel.cpp`) +
  `WeatherInfoPanel.qml` (`_escFirmware`, label 11 px / value 9 px).

---

## 2. False positives / retracted (old reports were wrong)

| ID | Old claim | Reality |
|----|-----------|---------|
| FP-01 | `RcRssiCheck` epoch arithmetic (`currentMSecsSinceEpoch() * 1000`) always passes | **Correct.** `rcLastUpdateUsec` is populated in the same µs units (`TelemetryBridge.cpp:738`), threshold `5'000'000` µs = 5 s (`RcRssiCheck.cpp:47`). |
| FP-02 | `qFuzzyCompare(param, 0.0)` is always false | Not proven; `qFuzzyIsNull` is clearer but behavior is not a hard bug. See U-09. |
| FP-03 | "ArmingGate timer never started" (see F-16) | Already addressed — treat old report as stale. |
| FP-04 | `analysis.md` construction/roster/watchlist blockers (R1–R3) | Fixed (F-13…F-15). |

---

## 3. Verified OPEN findings

| ID | Sev | Finding | Evidence | Action |
|----|-----|---------|----------|--------|
| O-01 | HIGH | **NaN pass-through:** checks read telemetry that can be `NaN` and compare without guards → false Pass on missing data. Only ~12 of 40 check `.cpp` files guard. | `grep -rl qIsNaN\|isnan custom/src/core/*Check.cpp` = 12; base returns NaN in `AbstractCheck.cpp` | Add guards / centralize a safe accessor; target registered checks first |
| O-02 | HIGH | **Empty-name DB upsert:** vehicle upserted with empty `friendly_name`, risking NOT NULL/FK failures on `startFlightSession`. | `VehicleProfileManager.cpp:313` `upsertVehicleEx(m_currentDeviceUid, {}, …)`; schema `friendly_name TEXT NOT NULL DEFAULT ''` | Pass a real name or make insert robust |
| O-03 | MED | **Unguarded `_activeVehicle` QML derefs** → TypeErrors on disconnect. | `MainStatusIndicator.qml`, `FlyViewWidgetLayer.qml` (≈35 refs total) | Add `_activeVehicle ?` guards / null checks |
| O-04 | MED | **ZoneComplianceCheck does SQLite/DB I/O on the UI thread** during evaluation (no cache/worker). | `custom/src/core/ZoneComplianceCheck.cpp` | Cache zones / move to worker; architectural decision needed |
| O-05 | LOW | **P2/P3 cleanups:** checkId prefix coherence; some checks bypass `getTelemetryDouble()` via raw `property()`; doc/behavior mismatches; counter UI `NOTIFY` mismatch; minor dead code; `setStatus()` does not clear a stale `message`. | spread across `custom/src/core/` + QML | Batch cleanup |

---

## 4. REPORT_ONLY (by design — documented, no change planned)

| ID | Finding | Rationale |
|----|---------|-----------|
| R-ONLY-1 | `#11` BatteryVoltageCheck behavior | Code is correct; the parameter watchlist doc was wrong. |
| R-ONLY-2 | `#12` EscResponsivenessCheck "first 4 channels" | Deliberate. |
| R-ONLY-3 | `#14` VehicleProfileManager type latch (HELI/ROVER/unrecognized keep gate closed) | Fails **closed**; intentional safety posture. |
| R-ONLY-4 | Blanket `mandatory = true` for all Auto checks once airframe kind resolves | Intentional (conservative); means new advisory checks must use Warning, not Failed. |
| R-ONLY-5 | Post-flight checks = `power.battery.temperature`, `propulsion.motor.temperature` | Matches design doc; extend list only with design sign-off. |

---

## 5. UNVERIFIED — carried from older reports (re-verify before acting)

> These came from `analysis.md` (R-register), `ANALYSIS.md` (R-0x), or
> `STATIC_ANALYSIS_REPORT.md`. Their status was **not** confirmed in the
> 2026-10-17 pass. Several are known stale; grep before opening work.

### 5.1 `analysis.md` risk register
| ID | Claim (abbreviated) |
|----|---------------------|
| U-01 | R4 force-arm bypass paths (partially refuted already) |
| U-02 | R7 unresettable singletons (FlightSession, OperatorManager, VehicleProfileManager, WeatherProvider, PreflightSettingsManager) |
| U-03 | R8 dead `Armed` FSM terminal — `transitionTo(Armed)` never called |
| U-04 | R9 `qFuzzyCompare(param, 0.0)` "disabled" detection in EkfFailsafe/GeofenceParam/BatteryFailsafe |
| U-05 | R10 duplicated logic: flight-mode QHash ×2, haversine ×2, cell-count ×3, capacity Wh ×2 |
| U-06 | R11 magic numbers (fallback 14.8 V, avgSpeed 30, +5 %/kg, 2 min hover, …) |
| U-07 | R12 debug/log noise (`console.log`, 5 s preflight dump, per-tick `armingBlocked`) |
| U-08 | R13 QML timer lifecycle (~18 timers, 1 `Component.onDestruction`) |
| U-09 | R14 unguarded `window.mainStackView.push/pop` (≈24 sites) |
| U-10 | R16 safety window: `createPhase1Checks` mandatory=false + `updateCriticalChecks` early-return pre-kind-resolution |
| U-11 | R17 hardcoded export/cache paths; Addis Ababa default map center |

### 5.2 `ANALYSIS.md` risk register
| ID | Claim (abbreviated) |
|----|---------------------|
| U-20 | R-05 MotorSpinCheck has no timeout/auto-fail fallback |
| U-21 | R-06 ChecklistEngine has no evaluation-cycle timeout |
| U-22 | R-07 PreflightStateMachine forward-only (no revert on degradation) |
| U-23 | R-08 TelemetryBridge fixed 200 ms poll (no adaptive interval) |
| U-24 | R-09 `hasTelemetry()` uses hardcoded `connectionQuality >= 10` |
| U-25 | R-10 DatabaseManager singleton thread safety undocumented |
| U-26 | R-11 PowerModel per-airframe defaults hardcoded / no calibration persistence |
| U-27 | R-12 synthetic battery serial `batt:<sysId>` |
| U-28 | R-15 MotorCheckPanel safety dialog is first-test-only |

### 5.3 `STATIC_ANALYSIS_REPORT.md`
| ID | Claim (abbreviated) |
|----|---------------------|
| U-30 | 23 "missing Q_PROPERTY" on TelemetryBridge (now 137 props — recheck which, if any, remain) |
| U-31 | Blocking 3 s network wait in `WeatherProvider` |
| U-32 | `getCheckConfig` per evaluate() tick (cache now exists — recheck) |
| U-33 | Motor-test result timer hardcoded margins (`HardwareTestController`) |
| U-34 | `TestPage.qml` dev artifact in `res/` + qrc |
| U-35 | ~42 hardcoded hex colors in `PreflightChecklistView.qml` (not `Colors.*`) |
| U-36 | Duplicate constants in `Config.h` and `Config.qml` |
| U-37 | `VirtualJoystick.qml` qrc path not in `custom.qrc` |
| U-38 | Network calls in `Component.onCompleted` (FinalChecks, PreFlightChecklist) |
| U-39 | Dialog opened in `Component.onCompleted` (PreFlightChecklist `motorConfigDialog`) |
| U-40 | ~80 un-`qsTr`'d user-facing strings |
| U-41 | God classes: `DatabaseManager` (≈1.9k lines), `TelemetryBridge` |
| U-42 | Test coverage gaps: PreflightManager, TelemetryBridge, UavParameterManager, most check subclasses |

---

## 6. Verification method / how to maintain

1. Before acting on any item, re-grep the cited symbol — line numbers drift.
2. When an item is fixed, move it to §1 with a current `file:line` and the fix;
   do **not** edit the old reports.
3. When an UNVERIFIED item is confirmed/denied, move it to §3, §4, or §2.
4. Keep the date at the top current on each pass.

### Source documents (superseded for status)
- `custom/analysis.md` — Part C/D risk register + checklists (2026 run; partially stale).
- `custom/ANALYSIS.md` — Phase 1 analysis + R-01…R-15 (2026-09-03).
- `custom/STATIC_ANALYSIS_REPORT.md` — static scan (oldest; most stale, ≥1 false positive).
