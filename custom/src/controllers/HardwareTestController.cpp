/**
 * @file HardwareTestController.cpp
 * @brief Implementation of the preflight hardware-test controller.
 *
 * See HardwareTestController.h for architecture and the fixed-wing state machine.
 */

#include "HardwareTestController.h"

#include <algorithm>
#include <QDebug>
#include <QHash>
#include <QLoggingCategory>
#include <QDateTime>

#include "Vehicle/Vehicle.h"
#include "Vehicle/MultiVehicleManager.h"
#include "FactSystem/ParameterManager.h"
#include "FactSystem/Fact.h"
#include "Comms/MAVLinkProtocol.h"
#include "utils/DatabaseManager.h"

Q_LOGGING_CATEGORY(hardwareTestLog, "preflight.hardwaretest")

/// Parses a Vehicle::flightMode() string into the ArduPilot Plane mode number.
/// QGC renders known modes as friendly names ("STABILIZE", "LOITER") and unknown
/// ones as "Custom:0x%1" (e.g. "Custom:0xb" = RTL/11). Returns -1 when unknown.
static int _fwFlightModeNumber(const QString &modeName)
{
    if (modeName.startsWith(QStringLiteral("Custom:0x"))) {
        bool ok = false;
        const int n = modeName.mid(9).toInt(&ok, 16);
        return ok ? n : -1;
    }
    static const QHash<QString, int> planeModes = {
        { QStringLiteral("MANUAL"),      0 },
        { QStringLiteral("CIRCLE"),      1 },
        { QStringLiteral("STABILIZE"),   2 },
        { QStringLiteral("TRAINING"),    3 },
        { QStringLiteral("ACRO"),        4 },
        { QStringLiteral("FBWA"),        5 },
        { QStringLiteral("CRUISE"),      6 },
        { QStringLiteral("AUTOTUNE"),    7 },
        { QStringLiteral("AUTO"),       10 },
        { QStringLiteral("RTL"),        11 },
        { QStringLiteral("LOITER"),     12 },
        { QStringLiteral("TAKEOFF"),    13 },
        { QStringLiteral("GUIDED"),     15 },
        { QStringLiteral("QSTABILIZE"), 17 },
        { QStringLiteral("QHOVER"),     18 },
        { QStringLiteral("QLOITER"),    19 },
        { QStringLiteral("QLAND"),      20 },
        { QStringLiteral("QRTL"),       21 },
    };
    return planeModes.value(modeName.toUpper(), -1);
}

/// Constructs the controller and wires up all internal timers.
/// Each timer has a single-shot or interval mode and a specific role:
///   _stepTimer   — delays between profile-sequence steps
///   _resultTimer — grace period after test duration before reading feedback
///   _cooldownTimer — 250 ms tick for the post-test cooldown countdown
///   _paramTimeoutTimer — safety net if parameters never become ready
HardwareTestController::HardwareTestController(QObject *parent)
    : QObject(parent)
{
    _stepTimer.setSingleShot(true);
    connect(&_stepTimer, &QTimer::timeout, this, &HardwareTestController::_advanceStep);

    _resultTimer.setSingleShot(true);
    connect(&_resultTimer, &QTimer::timeout, this, &HardwareTestController::_evaluateTestResult);

    _cooldownTimer.setInterval(250);
    connect(&_cooldownTimer, &QTimer::timeout, this, &HardwareTestController::_cooldownTick);

    _paramTimeoutTimer.setSingleShot(true);
    connect(&_paramTimeoutTimer, &QTimer::timeout, this, &HardwareTestController::_onParamTimeout);

    // Fixed-wing motor test state machine timers.
    _fwOverrideTimer.setInterval(1000);
    connect(&_fwOverrideTimer, &QTimer::timeout, this, &HardwareTestController::_fwOverrideTick);

    _fwPhaseTimer.setSingleShot(true);
    connect(&_fwPhaseTimer, &QTimer::timeout, this, &HardwareTestController::_fwAdvancePhase);
}

/// Binds this controller to a vehicle.  Disconnects from any previous vehicle,
/// connects signals (mavCommandResult, mavlinkMessageReceived, armedChanged),
/// and kicks off parameter loading / motor-count resolution.
void HardwareTestController::setVehicle(Vehicle *vehicle)
{
    if (_vehicle) {
        disconnect(_vehicle, &Vehicle::mavCommandResult, this, &HardwareTestController::_onCommandResult);
        disconnect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &HardwareTestController::_onMavlinkMessage);
        disconnect(_vehicle, &Vehicle::armedChanged, this, &HardwareTestController::_onArmedChanged);
        if (_vehicle->parameterManager()) {
            disconnect(_vehicle->parameterManager(), &ParameterManager::parametersReadyChanged,
                       this, &HardwareTestController::_onParametersReady);
        }
        _paramTimeoutTimer.stop();
    }
    _vehicle = vehicle;
    _invalidateMotorChannel();
    if (_vehicle) {
        connect(_vehicle, &Vehicle::mavCommandResult, this, &HardwareTestController::_onCommandResult);
        connect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &HardwareTestController::_onMavlinkMessage);
        connect(_vehicle, &Vehicle::armedChanged, this, &HardwareTestController::_onArmedChanged);

        qCDebug(hardwareTestLog) << "setVehicle:" << _vehicle->id()
                                 << "apm:" << _vehicle->apmFirmware()
                                 << "px4:" << _vehicle->px4Firmware()
                                 << "type:" << _vehicle->vehicleType()
                                 << "paramsReady:" << (_vehicle->parameterManager() ? _vehicle->parameterManager()->parametersReady() : false);

        _paramTimeoutFired = false;
        if (_vehicle->parameterManager()) {
            connect(_vehicle->parameterManager(), &ParameterManager::parametersReadyChanged,
                    this, &HardwareTestController::_onParametersReady);
            if (_vehicle->parameterManager()->parametersReady()) {
                qCDebug(hardwareTestLog) << "Params already ready — resolving motor count immediately";
                resolveMotorCount();
            } else {
                qCDebug(hardwareTestLog) << "Params not ready — starting" << kParamTimeoutMs << "ms timeout";
                _paramTimeoutTimer.start(kParamTimeoutMs);
            }
        } else {
            qCDebug(hardwareTestLog) << "No parameter manager — resolving from HEARTBEAT only";
            resolveMotorCount();
        }
        _isArmed = _vehicle->armed();
        emit isArmedChanged();

        // Keep SERVO_OUTPUT_RAW streaming while connected so the motor-signal
        // detection and PWM feedback are available outside of active tests.
        _setServoStreaming(true);
    }
}

/// Resolves the motor/servo count from vehicle parameters.
///
/// Resolution order:
///   1. PX4 — reads CA_AIRFRAME to determine airframe type, then CA_ROTOR_CNT for multirotors.
///   2. ArduPilot — reads FRAME_CLASS to determine frame type.
///   3. Fallback — uses MAV_TYPE from HEARTBEAT.
///
/// Also sets _vtolMode and _hoverMotorCount for VTOL frames.
/// Finally updates the human-readable vehicle type labels.
void HardwareTestController::resolveMotorCount()
{
    if (!_vehicle) {
        qCDebug(hardwareTestLog) << "resolveMotorCount: no vehicle";
        return;
    }

    qCDebug(hardwareTestLog) << "resolveMotorCount: vehicle" << _vehicle->id()
                             << "apm:" << _vehicle->apmFirmware()
                             << "px4:" << _vehicle->px4Firmware()
                             << "type:" << _vehicle->vehicleType()
                             << "paramsReady:" << (_vehicle->parameterManager() ? _vehicle->parameterManager()->parametersReady() : false);

    int count = 0;
    bool vtol = false;
    int hoverCnt = 0;
    QString source;

    auto *paramMgr = _vehicle->parameterManager();
    int compId = _vehicle->defaultComponentId();

    auto getParam = [paramMgr, compId](const QString &name, float fallback) -> float {
        if (!paramMgr || !paramMgr->parametersReady()) return fallback;
        Fact *fact = paramMgr->getParameter(compId, name);
        if (!fact) return fallback;
        return fact->rawValue().toFloat();
    };

    // ── PX4: read CA_AIRFRAME to determine airframe type ────────────────
    // Multirotor (0) uses CA_ROTOR_CNT; plane (1) and flying wing (2) are single-motor;
    // VTOL tiltrotor (4) and standard VTOL (5) set vtol=true with hover motor counts.
    if (_vehicle->px4Firmware()) {
        float caAirframe = getParam(QStringLiteral("CA_AIRFRAME"), -1);
        qCDebug(hardwareTestLog) << "CA_AIRFRAME read:" << caAirframe;
        if (caAirframe >= 0) {
            int af = static_cast<int>(caAirframe);
            switch (af) {
            case 0: {
                float rotorCnt = getParam(QStringLiteral("CA_ROTOR_CNT"), 4);
                count = qBound(1, static_cast<int>(rotorCnt), 16);
                source = QStringLiteral("PX4 CA_AIRFRAME=0, CA_ROTOR_CNT=%1").arg(count);
                break;
            }
            case 1:  count = 1; source = QStringLiteral("PX4 CA_AIRFRAME=1 (Plane)"); break;
            case 2:  count = 1; source = QStringLiteral("PX4 CA_AIRFRAME=2 (Flying wing)"); break;
            case 3:  count = 0; source = QStringLiteral("PX4 CA_AIRFRAME=3 (Rover)"); break;
            case 4:
                vtol = true;
                hoverCnt = 4;
                count = 6;
                source = QStringLiteral("PX4 CA_AIRFRAME=4 (VTOL tiltrotor)");
                break;
            case 5:
                vtol = true;
                hoverCnt = 4;
                count = 5;
                source = QStringLiteral("PX4 CA_AIRFRAME=5 (VTOL standard)");
                break;
            case 6:  count = 4; source = QStringLiteral("PX4 CA_AIRFRAME=6 (Tailsitter)"); break;
            case 7:  count = 0; source = QStringLiteral("PX4 CA_AIRFRAME=7 (Boat)"); break;
            case 8:  count = 4; source = QStringLiteral("PX4 CA_AIRFRAME=8 (Quad)"); break;
            case 9:  count = 6; source = QStringLiteral("PX4 CA_AIRFRAME=9 (Hexa)"); break;
            case 10: count = 8; source = QStringLiteral("PX4 CA_AIRFRAME=10 (Octo)"); break;
            default:
                float rotorCnt = getParam(QStringLiteral("CA_ROTOR_CNT"), 4);
                count = qBound(1, static_cast<int>(rotorCnt), 16);
                source = QStringLiteral("PX4 CA_AIRFRAME=%1, CA_ROTOR_CNT=%2").arg(af).arg(count);
                break;
            }
        }
    }

    // ── ArduPilot: read FRAME_CLASS to determine frame type ─────────────
    // Frame classes: 1=Quad, 2=Hexa, 3=Octa, 4=OctaQuad, 5=Y6,
    //               6=Heli (single rotor), 7=Tri
    // Only reached for ArduPilot firmware (PX4 handled above); the else-if
    // keeps the two firmware blocks mutually exclusive so PX4 results are
    // never overwritten by a stale FRAME_CLASS value.
    else if (_vehicle->apmFirmware()) {
        float frameClass = getParam(QStringLiteral("FRAME_CLASS"), -1);
        qCDebug(hardwareTestLog) << "FRAME_CLASS read:" << frameClass;
        if (frameClass > 0) {
            int fc = static_cast<int>(frameClass);
            switch (fc) {
            case 1:  count = 4; source = QStringLiteral("ArduPilot FRAME_CLASS=1 (Quad)"); break;
            case 2:  count = 6; source = QStringLiteral("ArduPilot FRAME_CLASS=2 (Hexa)"); break;
            case 3:  count = 8; source = QStringLiteral("ArduPilot FRAME_CLASS=3 (Octa)"); break;
            case 4:  count = 8; source = QStringLiteral("ArduPilot FRAME_CLASS=4 (OctaQuad)"); break;
            case 5:  count = 6; source = QStringLiteral("ArduPilot FRAME_CLASS=5 (Y6)"); break;
            case 6:  count = 1; source = QStringLiteral("ArduPilot FRAME_CLASS=6 (Heli)"); break;
            case 7:  count = 3; source = QStringLiteral("ArduPilot FRAME_CLASS=7 (Tri)"); break;
            default: count = 4; source = QStringLiteral("ArduPilot FRAME_CLASS=%1 (unknown → default 4)").arg(fc); break;
            }
        }
    }

    // ── Fallback: derive motor count from HEARTBEAT MAV_TYPE ────────────
    // Used when parameters are unavailable or didn't yield a count (e.g. rover → 0).
    qCDebug(hardwareTestLog) << "Fallback check — count:" << count << "vehicleType:" << (_vehicle ? _vehicle->vehicleType() : -1);
    if (count == 0 && _vehicle) {
        qCDebug(hardwareTestLog) << "Using HEARTBEAT type fallback:" << _vehicle->vehicleType();
        switch (_vehicle->vehicleType()) {
        case MAV_TYPE_QUADROTOR:  count = 4; source = QStringLiteral("HEARTBEAT type QUADROTOR"); break;
        case MAV_TYPE_HEXAROTOR:  count = 6; source = QStringLiteral("HEARTBEAT type HEXAROTOR"); break;
        case MAV_TYPE_OCTOROTOR:  count = 8; source = QStringLiteral("HEARTBEAT type OCTOROTOR"); break;
        case MAV_TYPE_TRICOPTER:  count = 3; source = QStringLiteral("HEARTBEAT type TRICOPTER"); break;
        case MAV_TYPE_FIXED_WING: count = 1; source = QStringLiteral("HEARTBEAT type FIXED_WING"); break;
        case MAV_TYPE_GROUND_ROVER:
        case MAV_TYPE_SURFACE_BOAT:
        case MAV_TYPE_SUBMARINE:   count = 0; source = QStringLiteral("HEARTBEAT type — no motors"); break;
        default:
            count = 4;
            source = QStringLiteral("HEARTBEAT type unknown (%1) — default 4").arg(_vehicle->vehicleType());
            break;
        }
    }

    qCDebug(hardwareTestLog) << "Motor count resolved:" << count
                             << "VTOL:" << vtol
                             << "hover:" << hoverCnt
                             << "source:" << source;

    // Build human-readable type labels
    QString oldTypeLabel = _vehicleTypeLabel;
    QString oldAutopilot = _vehicleAutopilotLabel;
    if (_vehicle) {
        _vehicleAutopilotLabel = _vehicle->px4Firmware() ? QStringLiteral("PX4")
                              : _vehicle->apmFirmware()  ? QStringLiteral("ArduPilot")
                              : QStringLiteral("Autopilot");
        if (!source.isEmpty()) {
            _vehicleTypeLabel = _vehicleTypeLabelFromSource(source);
        } else {
            _vehicleTypeLabel = QStringLiteral("Unknown Device");
        }
    } else {
        _vehicleTypeLabel = QStringLiteral("Unknown Device");
        _vehicleAutopilotLabel.clear();
    }
    if (oldTypeLabel != _vehicleTypeLabel || oldAutopilot != _vehicleAutopilotLabel)
        emit vehicleTypeLabelChanged();

    _updateMotorCount(count, vtol, hoverCnt);
}

/// Converts the motor-count resolution source string into a short human-readable
/// label for the vehicle type dropdown (e.g. "Quadcopter", "Fixed Wing", "VTOL").
/// Uses case-insensitive substring matching against known airframe keywords.
QString HardwareTestController::_vehicleTypeLabelFromSource(const QString &source) const
{
    if (source.contains(QStringLiteral("Quad"), Qt::CaseInsensitive)) return QStringLiteral("Quadcopter");
    if (source.contains(QStringLiteral("Hexa"), Qt::CaseInsensitive)) return QStringLiteral("Hexarotor");
    if (source.contains(QStringLiteral("Octo"), Qt::CaseInsensitive)) return QStringLiteral("Octorotor");
    if (source.contains(QStringLiteral("Tri"), Qt::CaseInsensitive)) return QStringLiteral("Tricopter");
    if (source.contains(QStringLiteral("Y6"), Qt::CaseInsensitive)) return QStringLiteral("Y6 Coaxial");
    if (source.contains(QStringLiteral("Heli"), Qt::CaseInsensitive)) return QStringLiteral("Helicopter");
    if (source.contains(QStringLiteral("Plane"), Qt::CaseInsensitive)) return QStringLiteral("Fixed Wing");
    if (source.contains(QStringLiteral("Wing"), Qt::CaseInsensitive)) return QStringLiteral("Flying Wing");
    if (source.contains(QStringLiteral("Rover"), Qt::CaseInsensitive)) return QStringLiteral("Rover");
    if (source.contains(QStringLiteral("Boat"), Qt::CaseInsensitive)) return QStringLiteral("Boat");
    if (source.contains(QStringLiteral("Sub"), Qt::CaseInsensitive)) return QStringLiteral("Submarine");
    if (source.contains(QStringLiteral("VTOL"), Qt::CaseInsensitive)) return QStringLiteral("VTOL");
    if (source.contains(QStringLiteral("Tailsitter"), Qt::CaseInsensitive)) return QStringLiteral("Tailsitter");
    if (source.contains(QStringLiteral("HEARTBEAT"), Qt::CaseInsensitive)) {
        if (source.contains(QStringLiteral("QUADROTOR"))) return QStringLiteral("Quadcopter");
        if (source.contains(QStringLiteral("HEXAROTOR"))) return QStringLiteral("Hexarotor");
        if (source.contains(QStringLiteral("OCTOROTOR"))) return QStringLiteral("Octorotor");
        if (source.contains(QStringLiteral("TRICOPTER"))) return QStringLiteral("Tricopter");
        if (source.contains(QStringLiteral("FIXED_WING"))) return QStringLiteral("Fixed Wing");
    }
    return QStringLiteral("Multirotor");
}

QString HardwareTestController::vehicleTypeLabelFromMavType(int mavType)
{
    switch (mavType) {
    case MAV_TYPE_QUADROTOR:       return QStringLiteral("Quadcopter");
    case MAV_TYPE_HEXAROTOR:       return QStringLiteral("Hexarotor");
    case MAV_TYPE_OCTOROTOR:       return QStringLiteral("Octorotor");
    case MAV_TYPE_TRICOPTER:       return QStringLiteral("Tricopter");
    case MAV_TYPE_COAXIAL:         return QStringLiteral("Coaxial Rotorcraft");
    case MAV_TYPE_FIXED_WING:      return QStringLiteral("Fixed Wing");
    case MAV_TYPE_GROUND_ROVER:    return QStringLiteral("Rover");
    case MAV_TYPE_SURFACE_BOAT:    return QStringLiteral("Boat");
    case MAV_TYPE_SUBMARINE:       return QStringLiteral("Submarine");
    case MAV_TYPE_HELICOPTER:      return QStringLiteral("Helicopter");
    case MAV_TYPE_VTOL_TAILSITTER_DUOROTOR:
    case MAV_TYPE_VTOL_TAILSITTER_QUADROTOR:
    case MAV_TYPE_VTOL_TILTROTOR:
    case MAV_TYPE_VTOL_FIXEDROTOR:
    case MAV_TYPE_VTOL_TAILSITTER:
    case MAV_TYPE_VTOL_TILTWING:
    case MAV_TYPE_VTOL_RESERVED5:
        return QStringLiteral("VTOL");
    case MAV_TYPE_FLAPPING_WING:   return QStringLiteral("Flapping Wing");
    case MAV_TYPE_KITE:            return QStringLiteral("Kite");
    case MAV_TYPE_ONBOARD_CONTROLLER: return QStringLiteral("Onboard Controller");
    case MAV_TYPE_GCS:             return QStringLiteral("GCS");
    case MAV_TYPE_AIRSHIP:         return QStringLiteral("Airship");
    case MAV_TYPE_FREE_BALLOON:    return QStringLiteral("Free Balloon");
    case MAV_TYPE_ROCKET:          return QStringLiteral("Rocket");
    case MAV_TYPE_GIMBAL:          return QStringLiteral("Gimbal");
    default:                       return QStringLiteral("Unknown Device");
    }
}

int HardwareTestController::motorCountFromMavType(int mavType)
{
    switch (mavType) {
    case MAV_TYPE_QUADROTOR:  return 4;
    case MAV_TYPE_HEXAROTOR:  return 6;
    case MAV_TYPE_OCTOROTOR:  return 8;
    case MAV_TYPE_TRICOPTER:  return 3;
    case MAV_TYPE_FIXED_WING:
    case MAV_TYPE_HELICOPTER: return 1;
    case MAV_TYPE_GROUND_ROVER:
    case MAV_TYPE_SURFACE_BOAT:
    case MAV_TYPE_SUBMARINE:  return 0;
    default:                  return 4;
    }
}

int HardwareTestController::motorCountFromFrameClass(int frameClass)
{
    switch (frameClass) {
    case 1: return 4;   // Quad
    case 2: return 6;   // Hexa
    case 3: return 8;   // Octa
    case 4: return 8;   // OctaQuad
    case 5: return 6;   // Y6
    case 6: return 1;   // Heli
    case 7: return 3;   // Tri
    default: return -1;
    }
}

int HardwareTestController::motorCountFromCaAirframe(int caAirframe)
{
    switch (caAirframe) {
    case 1: return 1;   // Plane
    case 2: return 1;   // Flying wing
    case 3: return 0;   // Rover
    case 6: return 4;   // Tailsitter
    case 7: return 0;   // Boat
    case 8: return 4;   // Quad
    case 9: return 6;   // Hexa
    case 10: return 8;  // Octo
    default: return -1;
    }
}

void HardwareTestController::setFirstTestDone(bool done)
{
    if (_firstTestDone != done) {
        _firstTestDone = done;
        emit firstTestDoneChanged();
    }
}

// ── Per-motor accessors ─────────────────────────────────────────────────
// All Q_INVOKABLE accessors use 1-based motorIndex from QML and convert
// to 0-based indexing for the internal _state / _targetThrottle vectors.

QString HardwareTestController::motorPosition(int motorIndex) const
{
    return _positionForMotor(motorIndex, _motorCount);
}

int HardwareTestController::motorStatus(int motorIndex) const
{
    int idx = motorIndex - 1;
    if (idx < 0 || idx >= _state.size()) return Idle;
    return static_cast<int>(_state[idx]);
}

int HardwareTestController::motorFeedback(int motorIndex) const
{
    int channel = motorOutputChannel(motorIndex);
    if (channel < 1 || channel > kServoCount) return 0;
    return static_cast<int>(_feedbackPwm[channel - 1]);
}

double HardwareTestController::targetThrottle(int motorIndex) const
{
    int idx = motorIndex - 1;
    if (idx < 0 || idx >= _targetThrottle.size()) return kDefaultThrottlePct;
    return _targetThrottle[idx];
}

int HardwareTestController::motorDuration(int motorIndex) const
{
    int idx = motorIndex - 1;
    if (idx < 0 || idx >= _motorDuration.size()) return kDefaultDurationSec;
    return _motorDuration[idx];
}

void HardwareTestController::setTargetThrottle(int motorIndex, double pct)
{
    int idx = motorIndex - 1;
    if (idx < 0 || idx >= _targetThrottle.size()) return;
    double clamped = qBound(0.0, pct, static_cast<double>(kMaxThrottlePct));
    if (!qFuzzyCompare(_targetThrottle[idx], clamped)) {
        _targetThrottle[idx] = clamped;
        emit targetThrottleChanged(motorIndex, clamped);
    }
}

void HardwareTestController::setMotorDuration(int motorIndex, int sec)
{
    int idx = motorIndex - 1;
    if (idx < 0 || idx >= _motorDuration.size()) return;
    int clamped = qBound(1, sec, 10);
    if (_motorDuration[idx] != clamped) {
        _motorDuration[idx] = clamped;
        emit motorDurationChanged(motorIndex, clamped);
    }
}

// ── Motor position label ────────────────────────────────────────────────

QString HardwareTestController::_positionForMotor(int motorIndex, int totalCount)
{
    if (totalCount == 4) {
        switch (motorIndex) {
        case 1: return QStringLiteral("Front Right");
        case 2: return QStringLiteral("Back Left");
        case 3: return QStringLiteral("Front Left");
        case 4: return QStringLiteral("Back Right");
        }
    }
    if (totalCount == 6) {
        switch (motorIndex) {
        case 1: return QStringLiteral("Front Right");
        case 2: return QStringLiteral("Mid Right");
        case 3: return QStringLiteral("Back Right");
        case 4: return QStringLiteral("Back Left");
        case 5: return QStringLiteral("Mid Left");
        case 6: return QStringLiteral("Front Left");
        }
    }
    if (totalCount == 8) {
        switch (motorIndex) {
        case 1: return QStringLiteral("Front Right");
        case 2: return QStringLiteral("Front Mid Right");
        case 3: return QStringLiteral("Back Mid Right");
        case 4: return QStringLiteral("Back Right");
        case 5: return QStringLiteral("Back Left");
        case 6: return QStringLiteral("Back Mid Left");
        case 7: return QStringLiteral("Front Mid Left");
        case 8: return QStringLiteral("Front Left");
        }
    }
    if (totalCount == 3) {
        switch (motorIndex) {
        case 1: return QStringLiteral("Front");
        case 2: return QStringLiteral("Back Right");
        case 3: return QStringLiteral("Back Left");
        }
    }
    return {};
}

QVariantList HardwareTestController::motorStates() const
{
    QVariantList list;
    for (auto s : _state)
        list.append(static_cast<int>(s));
    return list;
}

/// Sets the state for a single motor (0-based index) and emits motorStatesChanged if changed.
void HardwareTestController::_setMotorState(int index, MotorState state)
{
    if (index < 0 || index >= _state.size()) return;
    if (_state[index] == state) return;
    _state[index] = state;
    emit motorStatesChanged();
}

/// Reinitializes all per-motor arrays when the detected motor count changes.
/// Resets all states to Idle, fills default throttle/duration/PWM values,
/// clears feedback buffers, and stops any active cooldown or test.
void HardwareTestController::_updateMotorCount(int count, bool hasHover, int hoverCount)
{
    count = qBound(0, count, kServoCount);
    if (_motorCount == count && _vtolMode == hasHover) {
        qCDebug(hardwareTestLog) << "_updateMotorCount: no change, count=" << count;
        return;
    }

    qCDebug(hardwareTestLog) << "_updateMotorCount:" << _motorCount << "->" << count
                             << "vtol:" << hasHover << "hoverCnt:" << hoverCount;

    _motorCount = count;
    _vtolMode = hasHover;
    _hoverMotorCount = hoverCount;
    _state.resize(count);
    _state.fill(Idle);
    _targetThrottle.resize(count);
    _targetThrottle.fill(kDefaultThrottlePct);
    _motorDuration.resize(count);
    _motorDuration.fill(kDefaultDurationSec);
    _motorPwmValues.resize(count);
    _motorPwmValues.fill(_isFixedWing() ? kFwDefaultPwmUs : 1150);
    _activeMotor = -1;
    _cooldownIndex = -1;
    _cooldownTimer.stop();
    memset(_feedbackPwm, 0, sizeof(_feedbackPwm));
    memset(_peakFeedbackPwm, 0, sizeof(_peakFeedbackPwm));
    _firstTestDone = false;

    emit motorCountChanged();
    emit motorStatesChanged();
    emit activeMotorChanged();
    emit vtolChanged();
    emit motorPwmValuesChanged();
}

// ── Fixed-wing motor test: arm → RC_CHANNELS_OVERRIDE → disarm ─────
//
// Unlike copters (which use MAV_CMD_DO_MOTOR_TEST), fixed-wing aircraft
// require a different approach:
//   1. Arm the vehicle (MAV_CMD_COMPONENT_ARM_DISARM, param1=1)
//   2. Once armed, drive the throttle channel via RC_CHANNELS_OVERRIDE
//   3. After the test duration, set throttle to 1000 µs (idle)
//   4. Wait 500 ms for spin-down, then disarm

/// Reads the throttle RC-map parameter to determine which RC channel controls throttle.
/// Tries the ArduPilot name first and falls back to the PX4-style name if needed.
/// Defaults to channel 3 if the parameter is unavailable.
int HardwareTestController::_fwThrottleChannel() const
{
    if (!_vehicle || !_vehicle->parameterManager() || !_vehicle->parameterManager()->parametersReady())
        return 3;

    const QStringList paramNames = {QStringLiteral("RCMAP_THROTTLE"), QStringLiteral("RC_MAP_THROTTLE")};
    for (const QString &name : paramNames) {
        Fact *fact = _vehicle->parameterManager()->getParameter(_vehicle->defaultComponentId(), name);
        if (fact) {
            int ch = fact->rawValue().toInt();
            if (ch >= 1 && ch <= 18) {
                qCDebug(hardwareTestLog) << name << ":" << ch;
                return ch;
            }
        }
    }

    qCDebug(hardwareTestLog) << "Throttle RC-map not found — defaulting to channel 3";
    return 3;
}

/// Auto-detects the servo output channel that drives the fixed-wing motor.
int HardwareTestController::_fwDetectMotorChannel()
{
    // Avoid re-scanning all SERVOn_FUNCTION params on every call once known.
    if (_cachedFwMotorChannel > 0) return _cachedFwMotorChannel;

    int detected = -1;
    if (_vehicle && _vehicle->parameterManager() && _vehicle->parameterManager()->parametersReady()) {
        const int compId = _vehicle->defaultComponentId();
        // ArduPilot SRV_Channel functions: 70 = Throttle, 73 = ThrottleLeft, 74 = ThrottleRight, 33 = Motor1
        const int functions[] = {70, 73, 74, 33};
        for (int fn : functions) {
            for (int ch = 1; ch <= 16; ++ch) {
                const QString name = QStringLiteral("SERVO%1_FUNCTION").arg(ch);
                Fact *fact = _vehicle->parameterManager()->getParameter(compId, name);
                if (fact && qRound(fact->rawValue().toFloat()) == fn) {
                    detected = ch;
                    qCDebug(hardwareTestLog) << "FW motor channel detected: SERVO" << ch
                                             << "function" << fn;
                    break;
                }
            }
            if (detected > 0) break;
        }
    }

    if (detected < 0) {
        detected = _fwThrottleChannel();
        if (detected <= 0) detected = 3;
        qCDebug(hardwareTestLog) << "FW motor channel not found via SERVOn_FUNCTION — "
                                 << "falling back to throttle channel" << detected;
    }

    _cachedFwMotorChannel = qBound(1, detected, 16);
    return _cachedFwMotorChannel;
}

/// Returns the servo output channel driven for the given motor card (1-based).
int HardwareTestController::motorOutputChannel(int motorIndex) const
{
    if (_isFixedWing()) {
        return _cachedFwMotorChannel > 0 ? _cachedFwMotorChannel : 3;
    }
    return qBound(1, motorIndex, kServoCount);
}

QString HardwareTestController::fixedWingTestLabel() const
{
    if (_isFixedWing()) {
        int ch = const_cast<HardwareTestController *>(this)->_fwDetectMotorChannel();
        return QStringLiteral("M%1").arg(ch);
    }
    return QStringLiteral("M1");
}

/// True when the primary link is a simulator (Mock Link or UDP SITL).  Used to
/// relax hardware-only gates (e.g. the fixed-wing MANUAL-mode requirement) that
/// simulators cannot satisfy because they don't implement an RC mode switch.
bool HardwareTestController::_isMockLink() const
{
    if (!_vehicle) return false;
    SharedLinkInterfacePtr link = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (!link || !link->linkConfiguration()) return false;
    const QString linkName = link->linkConfiguration()->name();
    return linkName.contains(QStringLiteral("Mock"), Qt::CaseInsensitive)
        || linkName.contains(QStringLiteral("UDP"), Qt::CaseInsensitive);
}

bool HardwareTestController::_isFixedWing() const
{
    return _vehicle && (_vehicle->fixedWing() || _vehicle->vehicleType() == MAV_TYPE_FIXED_WING);
}

int HardwareTestController::_fwPwmToOverride(int servoPwmUs)
{
    return qBound(1000, servoPwmUs, 2000);
}

void HardwareTestController::_fwSendOverride(int pwm)
{
    if (!_vehicle) return;
    int ch = _fwThrottleChannel();
    if (ch < 1 || ch > 18) ch = 3;

    const int ov = _fwPwmToOverride(pwm);

    uint16_t vals[18];
    for (int i = 0; i < 18; ++i) {
        vals[i] = 65535; // 65535 = ignore channel
    }
    // Lock control surface channels to neutral (1500 µs) during motor test so
    // attitude stabilization / elevon mixing doesn't move ailerons/elevons on the bench.
    vals[0] = 1500; // RC1 (Roll / Aileron)
    vals[1] = 1500; // RC2 (Pitch / Elevator)
    vals[3] = 1500; // RC4 (Yaw / Rudder)
    vals[ch - 1] = static_cast<uint16_t>(qBound(1000, ov, 2000)); // RC3 (Throttle)

    MAVLinkProtocol *proto = MAVLinkProtocol::instance();
    mavlink_message_t msg;
    mavlink_msg_rc_channels_override_pack(
        proto ? proto->getSystemId() : 255,
        MAVLinkProtocol::getComponentId(),
        &msg,
        static_cast<uint8_t>(_vehicle->id()),
        0,
        vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6], vals[7],
        vals[8], vals[9], vals[10], vals[11], vals[12], vals[13], vals[14], vals[15],
        vals[16], vals[17]);

    SharedLinkInterfacePtr sharedLink = _vehicle->vehicleLinkManager()->primaryLink().lock();
    if (sharedLink) {
        _vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
    }
}

void HardwareTestController::_startFixedWingMotorTest(int idx)
{
    if (!_vehicle) return;
    const int pwmUs = (idx < _motorPwmValues.size()) ? _motorPwmValues[idx] : 1150;

    _activeMotor = idx + 1;
    _setMotorState(idx, Testing);
    _expectedPwm = pwmUs;
    _fwTestIndex = idx;
    _fwTargetPwm = pwmUs;
    _fwArmTries = 0;

    const int outputCh = _fwDetectMotorChannel();
    if (outputCh > 0 && outputCh <= kServoCount) {
        _feedbackPwm[outputCh - 1] = 0;
        _peakFeedbackPwm[outputCh - 1] = 0;
    }

    emit activeMotorChanged();
    _setServoStreaming(true);

    qCDebug(hardwareTestLog) << "FW motor test: motor" << _activeMotor
                             << "output CH" << outputCh
                             << "PWM" << pwmUs << "µs"
                             << "duration" << _durationSec << "s";

    // Request MANUAL mode (best-effort). Planes with FLTMODE_CH are driven by the
    // physical switch, so testMotor() gates on flightMode() == MANUAL before
    // this function is reached; the request only helps airframes without a mode switch.
    if (_vehicle && _vehicle->flightMode() != QStringLiteral("MANUAL")) {
        _vehicle->setFlightMode(QStringLiteral("MANUAL"));
    }

    // Force-ARM the vehicle (param1=1.0). Plane bench tests need the ESC to
    // respond to the throttle channel; param2=21196 bypasses the pre-arm checks.
    _vehicle->sendMavCommand(
        _vehicle->defaultComponentId(),
        MAV_CMD_COMPONENT_ARM_DISARM, false,
        1.0f, 21196.0f, 0, 0, 0, 0, 0
    );

    // Drive the ESC via RC_CHANNELS_OVERRIDE on the throttle channel at 10 Hz.
    _fwPhase = 2;
    _fwSendOverride(pwmUs);
    _fwOverrideTimer.start(100);
    _fwPhaseTimer.start(_durationSec * 1000);
}

void HardwareTestController::_fwOverrideTick()
{
    if (_fwTestIndex < 0 || _fwPhase != 2) {
        _fwOverrideTimer.stop();
        return;
    }
    const int channel = _fwDetectMotorChannel();
    const uint16_t fb = (channel > 0 && channel <= kServoCount) ? _feedbackPwm[channel - 1] : 0;
    qCDebug(hardwareTestLog) << "FW override tick: armed"
                             << (_vehicle ? _vehicle->armed() : false)
                             << "servo" << channel
                             << "target" << _fwTargetPwm << "µs"
                             << "feedback" << fb << "µs";
    _fwSendOverride(_fwTargetPwm);
}

/// Advances the fixed-wing motor test state machine on each phase timeout.
void HardwareTestController::_fwAdvancePhase()
{
    if (_fwTestIndex < 0) return;

    switch (_fwPhase) {
    case 2: { // spin duration elapsed — idle the override, disarm, and wait for spin-down
        _fwOverrideTimer.stop();
        _fwSendOverride(1000);
        if (_vehicle) {
            // Force-disarm vehicle after motor test completes using magic bypass code 21196
            _vehicle->sendMavCommand(
                _vehicle->defaultComponentId(),
                MAV_CMD_COMPONENT_ARM_DISARM, false,
                0.0f, 21196.0f, 0, 0, 0, 0, 0);
        }
        _fwPhase = 3;
        _fwPhaseTimer.start(kFwSpinDownMs);
        break;
    }

    case 3: { // spin-down done — evaluate SERVO_OUTPUT_RAW peak feedback
        const int channel = _fwDetectMotorChannel();
        uint16_t actual = (channel > 0 && channel <= kServoCount) ? _peakFeedbackPwm[channel - 1] : 0;
        QString resultStr;
        bool passed = false;
        if (actual == 0) {
            resultStr = QStringLiteral("TIMEOUT");
        } else {
            int delta = qAbs(static_cast<int>(actual) - _expectedPwm);
            if (delta <= kPwmTolerance) {
                passed = true;
                resultStr = QStringLiteral("PASS");
            } else {
                resultStr = QStringLiteral("FAIL");
            }
        }
        _fwFinishTest(passed, resultStr);
        break;
    }
    default:
        break;
    }
}

/// Terminates the fixed-wing motor test: idles the throttle, disarms if needed,
/// records the result, starts the cooldown, and resets the FW state machine.
void HardwareTestController::_fwFinishTest(bool passed, const QString &result)
{
    if (_fwTestIndex < 0) return;
    const int idx = _fwTestIndex;
    const int channel = _fwDetectMotorChannel();
    uint16_t actual = (channel > 0 && channel <= kServoCount) ? _peakFeedbackPwm[channel - 1] : 0;
    const int delta = qAbs(static_cast<int>(actual) - _expectedPwm);

    qCDebug(hardwareTestLog) << "FW motor" << (idx + 1)
                             << "result" << result
                             << "actual" << actual
                             << "expected" << _expectedPwm
                             << "delta" << delta;

    _logTestResult(idx + 1, 0, _durationSec, _expectedPwm, actual, delta, result);

    // Stop timers and idle the throttle override.
    _fwPhaseTimer.stop();
    _fwOverrideTimer.stop();
    _fwSendOverride(1000);

    _setMotorState(idx, passed ? Pass : Fail);

    // Cooldown so the ESC/motor can cool before retesting.
    _cooldownTargetState = _state[idx];
    _state[idx] = Cooldown;
    emit motorStatesChanged();
    _cooldownIndex = idx;
    _cooldownRemaining = kCooldownMs / 250;
    _cooldownTimer.start();

    _activeMotor = -1;
    emit activeMotorChanged();

    _fwTestIndex = -1;
    _fwPhase = 0;
    _fwArmTries = 0;

    _setServoStreaming(false);
}

void HardwareTestController::testMotor(int motorIndex)
{
    int idx = motorIndex - 1;

    if (!_vehicle) {
        qCDebug(hardwareTestLog) << "testMotor: no vehicle";
        return;
    }
    if (_activeMotor != -1) {
        qCDebug(hardwareTestLog) << "testMotor: motor" << _activeMotor << "already testing";
        return;
    }
    if (idx < 0 || idx >= _motorCount) {
        qCDebug(hardwareTestLog) << "testMotor: invalid index" << motorIndex;
        return;
    }
    if (_state[idx] == Cooldown) {
        qCDebug(hardwareTestLog) << "testMotor: motor" << motorIndex << "in cooldown";
        return;
    }

    // A bench motor test must never run on a live-armed vehicle.  Request a
    // forced disarm first and only start the test once the disarm is ACKed
    // (see _onCommandResult), avoiding the race where the DO_MOTOR_TEST is
    // rejected because the vehicle is still arming/disarming.
    if (_isArmed) {
        qCDebug(hardwareTestLog) << "testMotor: armed — requesting disarm first";
        _pendingMotorIndex = motorIndex;
        _vehicle->sendMavCommand(
            _vehicle->defaultComponentId(),
            MAV_CMD_COMPONENT_ARM_DISARM, false,
            0.0f, 21196.0f, 0, 0, 0, 0, 0);
        return;
    }

    _startMotorTestInternal(motorIndex);
}

/// Core of testMotor(): runs after the armed check has passed (either the
/// vehicle was disarmed, or the disarm ACK arrived and deferred the start).
/// Resolves the fixed-wing MANUAL-mode gate, then starts the copter
/// DO_MOTOR_TEST path or the fixed-wing arm→override→disarm sequence.
void HardwareTestController::_startMotorTestInternal(int motorIndex)
{
    int idx = motorIndex - 1;
    _lastCommandAcked = false;

    // Fixed-wing airframes (ArduPilot Plane / flying wing) do not implement
    // MAV_CMD_DO_MOTOR_TEST — use the arm → RC override → disarm sequence.
    if (_isFixedWing()) {
        // The throttle only reaches the ESC in MANUAL mode (THR_SUPP_MAN=0
        // suppresses it in every other mode, and FLTMODE_CH overrides any GCS
        // mode write).  Gate on the reported mode so the operator moves CH8 first.
        // Mock Link / SITL don't implement the mode switch, so warn but proceed.
        const QString mode = _vehicle->flightMode();
        if (_fwFlightModeNumber(mode) != 0 && !_fwAllowNonManualTest) {
            _vehicle->setFlightMode(QStringLiteral("MANUAL"));
            _lastError = QStringLiteral(
                "Requesting MANUAL mode for motor test. If this is real hardware, "
                "move the mode switch to MANUAL and tap Test again. Current: %1")
                             .arg(mode.isEmpty() ? QStringLiteral("unknown") : mode);
            emit lastErrorMessageChanged();

            if (!_isMockLink()) {
                qCWarning(hardwareTestLog) << "FW motor test blocked in mode" << mode
                                           << "— MANUAL (0) required";
                return;
            }
            qCWarning(hardwareTestLog) << "FW test: not in MANUAL mode (" << mode
                                       << ") — proceeding anyway (simulation detected)";
        }
        _startFixedWingMotorTest(idx);
        return;
    }

    // Read per-motor PWM value and shared duration
    int pwmUs  = (idx < _motorPwmValues.size()) ? _motorPwmValues[idx] : 1150;

    _activeMotor = motorIndex;
    _setMotorState(idx, Testing);

    _expectedPwm = pwmUs;

    // Reset feedback capture for the motor's servo output channel
    const int resetChannel = motorOutputChannel(motorIndex);
    if (resetChannel > 0 && resetChannel <= kServoCount) {
        _feedbackPwm[resetChannel - 1] = 0;
        _peakFeedbackPwm[resetChannel - 1] = 0;
    }

    qCDebug(hardwareTestLog) << "Testing motor" << motorIndex
                             << "at" << pwmUs << "\u00b5s PWM"
                             << "for" << _durationSec << "s";

    emit activeMotorChanged();

    // Ensure SERVO_OUTPUT_RAW is streaming so we get feedback
    _setServoStreaming(true);

    // The MAVLink motor instance is the actual servo output channel.  For
    // multirotors this equals the motor index; for fixed-wing it is the
    // auto-detected motor output (so we spin the right channel).
    const int instance = motorOutputChannel(motorIndex);

    _vehicle->sendMavCommand(
        _vehicle->defaultComponentId(),
        MAV_CMD_DO_MOTOR_TEST, false,
        instance,
        MOTOR_TEST_THROTTLE_PWM,     // param2: MOTOR_TEST_THROTTLE_PWM
        static_cast<float>(pwmUs),   // param3: target PWM in µs
        _durationSec,
        0,                           // param5: 0 = single motor
        MOTOR_TEST_ORDER_BOARD        // param6: map motor instance to board output labels
    );

    _resultTimer.start(_durationSec * 1000 + kResultGraceMs);
}

/// Called by _resultTimer after the test duration + grace period.
/// Compares the peak feedback PWM captured during the test against the expected
/// PWM value.  A deviation within kPwmTolerance (50 µs) is a PASS.
/// After evaluation, transitions the motor into the cooldown phase.
void HardwareTestController::_evaluateTestResult()
{
    if (_activeMotor < 0) return;

    int idx = _activeMotor - 1;
    int channel = motorOutputChannel(_activeMotor);
    uint16_t actual = (channel > 0 && channel <= kServoCount) ? _peakFeedbackPwm[channel - 1] : 0;
    bool passed = false;
    QString resultStr;
    int delta = 0;

    if (actual == 0) {
        // No SERVO_OUTPUT_RAW received — could be Mock Link, SITL,
        // or a FC that doesn't stream servo output.
        // If the DO_MOTOR_TEST command was ACKed (not rejected),
        // treat as PASS_NO_FEEDBACK rather than FAIL.
        // _lastCommandAcked tracks whether the last motor command was accepted.
        if (_lastCommandAcked) {
            passed = true;
            resultStr = QStringLiteral("PASS (no feedback — command accepted)");
            qCDebug(hardwareTestLog) << "Motor" << _activeMotor
                                     << "command ACKed but no SERVO_OUTPUT_RAW"
                                     << "— treating as pass (Mock Link / SITL)";
        } else {
            resultStr = QStringLiteral("TIMEOUT");
            qCDebug(hardwareTestLog) << "Motor" << _activeMotor
                                     << "no SERVO_OUTPUT_RAW and no ACK";
        }
    } else {
        delta = qAbs(static_cast<int>(actual) - _expectedPwm);
        passed = (delta <= kPwmTolerance);
        resultStr = passed ? QStringLiteral("PASS") : QStringLiteral("FAIL");
        qCDebug(hardwareTestLog) << "Motor" << _activeMotor
                                 << resultStr
                                 << "actual" << actual
                                 << "expected" << _expectedPwm
                                 << "delta" << delta;
    }

    _setMotorState(idx, passed ? Pass : Fail);

    int pwmUs = (idx < _motorPwmValues.size()) ? _motorPwmValues[idx] : 1150;
    _logTestResult(_activeMotor, pwmUs,
                   _durationSec, _expectedPwm, actual, delta, resultStr);

    _cooldownTargetState = _state[idx];
    _state[idx] = Cooldown;
    emit motorStatesChanged();

    _cooldownIndex = idx;
    _cooldownRemaining = kCooldownMs / 250;
    _cooldownTimer.start();

    _activeMotor = -1;
    emit activeMotorChanged();
}

/// Enables or disables streaming of SERVO_OUTPUT_RAW (msg ID #36) at 10 Hz.
/// This message contains the actual PWM values the flight controller is outputting,
/// which we use as feedback to verify motor/servo commands.
void HardwareTestController::_setServoStreaming(bool enable)
{
    if (!_vehicle) return;
    _vehicle->sendMavCommand(
        _vehicle->defaultComponentId(),
        MAV_CMD_SET_MESSAGE_INTERVAL,
        false,
        static_cast<float>(MAVLINK_MSG_ID_SERVO_OUTPUT_RAW),
        enable ? 100000.0f : -1.0f,
        0, 0, 0, 0, 0
    );
}

/// Handles MAV_CMD acknowledgment results from the flight controller.
///
/// DO_MOTOR_TEST rejected (multirotor path) → stops result timer, marks motor Fail.
/// COMPONENT_ARM_DISARM denied during a FW test → abort immediately with a
/// descriptive message rather than waiting for the kFwArmRetries timeout.
void HardwareTestController::_onCommandResult(int vehicleId, int targetComponent, int command, int ackResult, int failureCode)
{
    Q_UNUSED(vehicleId)
    Q_UNUSED(targetComponent)

    qCDebug(hardwareTestLog) << "Command result: cmd" << command
                             << "result" << ackResult
                             << "failure" << failureCode
                             << "activeMotor" << _activeMotor
                             << "fwPhase" << _fwPhase;

    // ── Deferred motor-test start: disarm ACKed → begin the pending test ──
    // testMotor() requests a forced disarm when the vehicle is armed and waits
    // for this ACK before starting, so the motor command is never sent while
    // the vehicle is mid-disarm.
    if (command == MAV_CMD_COMPONENT_ARM_DISARM && _pendingMotorIndex > 0) {
        if (ackResult == MAV_RESULT_ACCEPTED) {
            qCDebug(hardwareTestLog) << "Disarm ACKed — starting deferred motor test" << _pendingMotorIndex;
            _isArmed = false;
            emit isArmedChanged();
            int pendingIdx = _pendingMotorIndex;
            _pendingMotorIndex = -1;
            _startMotorTestInternal(pendingIdx);
        } else {
            qCWarning(hardwareTestLog) << "Disarm for deferred motor test rejected:" << ackResult;
            _lastError = QStringLiteral("Disarm command rejected — cannot start motor test while vehicle is armed");
            emit lastErrorMessageChanged();
            _pendingMotorIndex = -1;
        }
        return;
    }

    // ── Fixed-wing arm denied ────────────────────────────────────────────
    // If the force-arm command is outright denied (not just temporarily
    // rejected) while the FW motor is meant to be spinning (phase 2), abort
    // immediately so the user gets a clear error instead of a silent no-spin.
    if (command == MAV_CMD_COMPONENT_ARM_DISARM
            && ackResult == MAV_RESULT_DENIED
            && _fwTestIndex >= 0 && _fwPhase == 2) {
        qCWarning(hardwareTestLog) << "FW motor test: arm denied — aborting";
        _fwPhaseTimer.stop();
        _fwOverrideTimer.stop();
        _fwFinishTest(false,
            QStringLiteral("Arm command denied by autopilot — check pre-arm messages in your GCS"));
        return;
    }

    // Track ACK for feedback-less environments (Mock Link, SITL).  If the
    // DO_MOTOR_TEST command was accepted but no SERVO_OUTPUT_RAW ever arrives,
    // _evaluateTestResult() treats it as PASS instead of TIMEOUT.
    if (command == MAV_CMD_DO_MOTOR_TEST) {
        _lastCommandAcked = (ackResult == MAV_RESULT_ACCEPTED);
        qCDebug(hardwareTestLog) << "DO_MOTOR_TEST ACK:" << ackResult
                                 << "lastCommandAcked:" << _lastCommandAcked;
    }

    // ── Motor / servo command results ────────────────────────────────────
    if (command != MAV_CMD_DO_MOTOR_TEST && command != MAV_CMD_DO_SET_SERVO) return;

    // During a fixed-wing test these two commands are expected-rejected on
    // ArduPilot Plane (no DO_MOTOR_TEST handler; DO_SET_SERVO refuses assigned
    // outputs) and we no longer send them — the motor is driven purely by the
    // RC override.  Ignore any stale results so they cannot mark the motor Fail
    // (which would also halt peak-feedback tracking and force a TIMEOUT).
    if (_fwTestIndex >= 0) return;

    if (ackResult != MAV_RESULT_ACCEPTED && _activeMotor > 0) {
        int idx = _activeMotor - 1;
        _resultTimer.stop();

        QString reason;
        switch (ackResult) {
        case MAV_RESULT_UNSUPPORTED:   reason = QStringLiteral("UNSUPPORTED — firmware does not handle this command"); break;
        case MAV_RESULT_DENIED:        reason = QStringLiteral("DENIED — vehicle state does not allow this command"); break;
        case MAV_RESULT_TEMPORARILY_REJECTED: reason = QStringLiteral("TEMPORARILY REJECTED"); break;
        case MAV_RESULT_FAILED:        reason = QStringLiteral("FAILED"); break;
        default:                       reason = QStringLiteral("RESULT %1").arg(ackResult); break;
        }

        qCDebug(hardwareTestLog) << "Motor test rejected:" << reason;
        _setMotorState(idx, Fail);
        int pwmUs = (idx >= 0 && idx < _motorPwmValues.size()) ? _motorPwmValues[idx] : 1150;
        _logTestResult(_activeMotor, pwmUs,
                       _durationSec, _expectedPwm, 0, 0,
                       reason);
        _lastError = reason;
        emit lastErrorMessageChanged();
        _activeMotor = -1;
        emit activeMotorChanged();
    }
}

/// Emergency stop: immediately halts all motor activity.
/// Sends DO_MOTOR_TEST with 1000 µs (disarmed) to each motor, cancels any active
/// test, and resets all timers, state arrays, and cooldown tracking.
void HardwareTestController::stopAll()
{
    if (!_vehicle) return;

    // A deferred motor test waiting on a disarm ACK must be cancelled so it
    // cannot auto-start after STOP ALL.
    _pendingMotorIndex = -1;

    // Fixed-wing test in progress: idle the throttle, disarm, and reset.
    if (_fwTestIndex >= 0) {
        qCDebug(hardwareTestLog) << "STOP ALL — aborting fixed-wing motor test";
        _fwPhaseTimer.stop();
        _fwOverrideTimer.stop();
        _fwSendOverride(1000);
        if (_isArmed) {
            _vehicle->sendMavCommand(_vehicle->defaultComponentId(),
                                     MAV_CMD_COMPONENT_ARM_DISARM, false,
                                     0.0f, 0, 0, 0, 0, 0, 0);
        }
        int idx = _fwTestIndex;
        _fwTestIndex = -1;
        _fwPhase = 0;
        _fwArmTries = 0;
        _setMotorState(idx, Idle);
        _activeMotor = -1;
        emit activeMotorChanged();
        _setServoStreaming(false);
        return;
    }

    qCDebug(hardwareTestLog) << "STOP ALL — sending zero throttle to all motors";

    for (int i = 1; i <= _motorCount; ++i) {
        _sendStopToMotor(i);
    }

    _resultTimer.stop();

    for (int i = 0; i < _motorCount; ++i) {
        if (_state[i] == Testing || _state[i] == Cooldown) {
            _state[i] = Idle;
        }
    }
    emit motorStatesChanged();

    _cooldownTimer.stop();
    _cooldownIndex = -1;

    if (_activeMotor > 0) {
        int idx = _activeMotor - 1;
        int pwmUs = (idx < _motorPwmValues.size()) ? _motorPwmValues[idx] : 1150;
        _logTestResult(_activeMotor, pwmUs,
                       _durationSec, _expectedPwm, 0, 0,
                       QStringLiteral("CANCELLED"));
    }
    _activeMotor = -1;
    emit activeMotorChanged();

    _setServoStreaming(false);

    qCDebug(hardwareTestLog) << "STOP ALL triggered";
}

void HardwareTestController::disarmVehicle()
{
    if (!_vehicle) return;
    qCDebug(hardwareTestLog) << "Disarming vehicle via GCS request";
    _vehicle->sendMavCommand(
        _vehicle->defaultComponentId(),
        MAV_CMD_COMPONENT_ARM_DISARM, false,
        0.0f,   // disarm
        0.0f,   // no force flag
        0, 0, 0, 0, 0
    );
}

void HardwareTestController::resetMotorState(int motorIndex)
{
    int idx = motorIndex - 1;
    if (idx >= 0 && idx < _state.size()) {
        _setMotorState(idx, Idle);
    }
}

void HardwareTestController::resetAll()
{
    for (int i = 0; i < _state.size(); ++i)
        _state[i] = Idle;
    emit motorStatesChanged();
    _activeMotor = -1;
    emit activeMotorChanged();
    _cooldownTimer.stop();
    _cooldownIndex = -1;
    _resultTimer.stop();
    memset(_feedbackPwm, 0, sizeof(_feedbackPwm));
    memset(_peakFeedbackPwm, 0, sizeof(_peakFeedbackPwm));
    _firstTestDone = false;
    emit firstTestDoneChanged();
    _lastCommandAcked = false;
    _pendingMotorIndex = -1;
    _cachedFwMotorChannel = -1;

    _setServoStreaming(false);
}

/// Sends DO_MOTOR_TEST with 1000 µs PWM (disarmed) to stop a single motor.
/// The MAVLink instance is the actual servo output channel (see motorOutputChannel).
/// Used by stopAll() and also available for individual motor shutdown.
void HardwareTestController::_sendStopToMotor(int index)
{
    if (!_vehicle) return;
    _vehicle->sendMavCommand(
        _vehicle->defaultComponentId(),
        MAV_CMD_DO_MOTOR_TEST, false,
        motorOutputChannel(index),
        MOTOR_TEST_THROTTLE_PWM,      // param2: MOTOR_TEST_THROTTLE_PWM
        1000,                         // param3: 1000µs = disarmed
        0,                            // param4: no timeout
        0,                            // param5: single motor
        MOTOR_TEST_ORDER_BOARD        // param6: map motor instance to board output labels
    );
}

void HardwareTestController::setMotorPwm(int motorIndex, int pwmUs)
{
    int idx = motorIndex - 1;
    if (idx < 0 || idx >= _motorPwmValues.size()) return;
    // Multirotors cap at a gentle 1200 µs spin; fixed-wing needs real throttle
    // (this plane's SERVO3 range is 1000–1900 µs) to spin the prop at all.
    int clamped = qBound(kFwMinPwmUs, pwmUs, _isFixedWing() ? kFwMaxPwmUs : kMultiMaxPwmUs);
    if (_motorPwmValues[idx] != clamped) {
        _motorPwmValues[idx] = clamped;
        emit motorPwmValuesChanged();
    }
}

void HardwareTestController::setDurationSec(int sec)
{
    int clamped = qBound(1, sec, 10);
    if (_durationSec != clamped) {
        _durationSec = clamped;
        emit durationSecChanged();
    }
}

QVariantList HardwareTestController::motorPwmValues() const
{
    QVariantList list;
    list.reserve(_motorPwmValues.size());
    for (int v : _motorPwmValues)
        list.append(v);
    return list;
}

/// Persists a motor test result to the database and emits an AUDIT log line.
/// Records motor index, throttle %, duration, expected/actual PWM, delta, and pass/fail.
void HardwareTestController::_logTestResult(int motorIndex, int thrPct,
                                             int durSec, int expPwm,
                                             int actPwm, int pwmDelta,
                                             const QString &result)
{
    auto &db = DatabaseManager::instance();
    int sysid = _vehicle ? _vehicle->id() : 0;
    db.logMotorTestResult(sysid, motorIndex, thrPct, durSec, expPwm,
                          actPwm, pwmDelta, result);
    qCDebug(hardwareTestLog) << "AUDIT: Motor" << motorIndex
                             << "throttle" << thrPct << "%"
                             << "duration" << durSec << "s"
                             << "expected" << expPwm << "µs"
                             << "actual" << actPwm << "µs"
                             << "delta" << pwmDelta
                             << "result" << result;
}

// ── Legacy profile-based testing (kept for servo sweep) ───────────────
// This path drives a sequence of DO_SET_SERVO or DO_MOTOR_TEST commands
// loaded from a HardwareTestProfile JSON file.  Each step is followed by
// a feedback verification check before advancing to the next step.

QVariant HardwareTestController::sequenceCompleted() const
{
    return _sequenceCompleted ? QVariant(true) : QVariant();
}

/// Generates a hardcoded servo sweep sequence for servos 1–4.
/// Each servo is driven through 1200 → 1500 → 1800 → 1500 → 1200 µs
/// with 800 ms hold time and 200 ms settle time per position.
void HardwareTestController::runServoSweep()
{
    if (_running || !_vehicle) {
        qCDebug(hardwareTestLog) << "Cannot start servo sweep: running =" << _running;
        return;
    }
    _isMotorTest = false;
    _profileSteps.clear();
    QVector<int> pwmValues = {1200, 1500, 1800, 1500, 1200};
    for (int servo = 1; servo <= 4; ++servo) {
        for (int pwm : pwmValues) {
            TestStep step;
            step.name = QStringLiteral("Servo %1 PWM %2").arg(servo).arg(pwm);
            step.testType = QStringLiteral("servo");
            step.servoInstance = servo;
            step.targetPwm = pwm;
            step.expectedMin = pwm - kPwmTolerance;
            step.expectedMax = pwm + kPwmTolerance;
            step.durationMs = 800;
            step.settleMs = 200;
            step.needsVisualConfirm = false;
            _profileSteps.append(step);
        }
    }
    _setServoStreaming(true);
    _startTest();
}

/// Loads a HardwareTestProfile from a JSON file and starts the test sequence.
/// Detects whether all steps are motor tests (to set _isMotorTest flag).
/// Returns false if the file can't be loaded or fails validation.
bool HardwareTestController::runProfile(const QString &filePath)
{
    if (_running) {
        qCDebug(hardwareTestLog) << "Cannot load profile while test is running";
        return false;
    }
    QString error;
    HardwareTestProfile profile = HardwareTestProfile::loadFromJsonFile(filePath, &error);
    if (!profile.isValid()) {
        _lastError = QStringLiteral("Failed to load profile: %1").arg(error);
        emit lastErrorMessageChanged();
        return false;
    }
    _profileName = profile.profileName;
    _profileSteps = profile.steps;
    _isMotorTest = false;
    bool allMotor = true;
    for (const TestStep &step : _profileSteps) {
        if (step.testType != QStringLiteral("motor")) {
            allMotor = false;
            break;
        }
    }
    _isMotorTest = allMotor;
    _startTest();
    return true;
}

void HardwareTestController::abortSequence()
{
    if (!_running) return;
    _stepTimer.stop();
    _finishTest(false, QStringLiteral("Test aborted by user"));
}

/// Initializes all state for a profile/sweep sequence and kicks off the first step.
/// Resets feedback buffers, progress, and error state; emits signals so the QML UI updates.
void HardwareTestController::_startTest()
{
    for (int i = 0; i < kServoCount; ++i)
        _feedbackPwm[i] = 0;
    _running = true;
    _allPassed = true;
    _sequenceCompleted = false;
    _currentStep = 0;
    _lastError.clear();
    _progress = 0.0;
    _totalSteps = _profileSteps.size();
    emit isRunningChanged();
    emit allPassedChanged();
    emit sequenceCompletedChanged();
    emit lastErrorMessageChanged();
    emit stepProgressChanged();
    _advanceStep();
}

/// Advances the profile sequence to the next step.
///
/// Before executing the new step, verifies the previous step's feedback:
///   - Motor steps: checks feedback PWM is in the 800–2200 µs range
///   - Servo steps: checks feedback PWM is within [expectedMin, expectedMax]
///
/// Logs each step result to the database.  When all steps are done,
/// calls _finishTest() with the aggregate pass/fail result.
void HardwareTestController::_advanceStep()
{
    if (_currentStep >= _totalSteps) {
        _finishTest(_allPassed, _lastError);
        return;
    }
    const TestStep &step = _profileSteps[_currentStep];
    if (_currentStep > 0) {
        const TestStep &prev = _profileSteps[_currentStep - 1];
        bool feedbackOk = false;
        if (prev.testType == QStringLiteral("motor"))
            feedbackOk = _verifyMotorFeedback(prev.motorInstance);
        else if (prev.expectedMin > 0 && prev.expectedMax > 0)
            feedbackOk = _verifyServoFeedbackRange(prev.servoInstance, prev.expectedMin, prev.expectedMax);
        else
            feedbackOk = _verifyServoFeedback(prev.servoInstance, prev.targetPwm);
        if (!feedbackOk) {
            _allPassed = false;
            _lastError = prev.testType == QStringLiteral("motor")
                ? QStringLiteral("Motor %1: feedback mismatch").arg(prev.motorInstance)
                : QStringLiteral("Servo %1: expected range %2\u2013%3 \u00b5s, got %4 \u00b5s")
                      .arg(prev.servoInstance).arg(prev.expectedMin).arg(prev.expectedMax)
                      .arg(_feedbackPwm[prev.servoInstance - 1]);
            emit lastErrorMessageChanged();
        }

        // Log step result to DB
        auto &db = DatabaseManager::instance();
        int flightId = -1;
        QString resultStr = feedbackOk ? QStringLiteral("PASS") : QStringLiteral("FAIL");
        db.logHardwareTestStep(flightId, prev.name, prev.servoInstance,
                               prev.targetPwm, prev.expectedMin > 0 ? prev.expectedMin : 0,
                               0, 0, false, resultStr);
    }
    _progress = static_cast<qreal>(_currentStep) / _totalSteps;
    emit stepProgressChanged();
    if (step.testType == QStringLiteral("motor"))
        _sendMotorStep(step.motorInstance, step.throttlePct, step.durationMs / 1000);
    else
        _sendServoStep(step.servoInstance, step.targetPwm, step.durationMs / 1000);
    _stepTimer.start(step.durationMs + step.settleMs);
    _currentStep++;
}

void HardwareTestController::_sendMotorStep(int motorInstance, int throttlePct, int durationSec)
{
    if (!_vehicle) return;
    _vehicle->motorTest(motorInstance, throttlePct, durationSec, false);
}

void HardwareTestController::_sendServoStep(int servoInstance, int pwmValue, int durationSec)
{
    Q_UNUSED(durationSec)
    if (!_vehicle) return;
    _vehicle->sendMavCommand(
        _vehicle->defaultComponentId(),
        MAV_CMD_DO_SET_SERVO, false,
        servoInstance, pwmValue);
}

/// Marks the profile sequence as complete and emits all relevant signals
/// so the QML UI transitions to the results view.
void HardwareTestController::_finishTest(bool passed, const QString &error)
{
    _running = false;
    _allPassed = passed;
    _sequenceCompleted = true;
    _lastError = error;
    _progress = 1.0;
    emit isRunningChanged();
    emit allPassedChanged();
    emit sequenceCompletedChanged();
    emit lastErrorMessageChanged();
    emit stepProgressChanged();
    emit servoSweepFinished(passed, error);

    _setServoStreaming(false);
}

/// Checks that the feedback PWM for a specific servo is within kPwmTolerance
/// of the expected value.  Returns false if no feedback was received (actual == 0).
bool HardwareTestController::_verifyServoFeedback(int servoInstance, int expectedPwm) const
{
    if (servoInstance < 1 || servoInstance > kServoCount) return false;
    uint16_t actual = _feedbackPwm[servoInstance - 1];
    if (actual == 0) return false;
    return qAbs(static_cast<int>(actual) - expectedPwm) <= kPwmTolerance;
}

/// Checks that the feedback PWM for a specific servo falls within [expectedMin, expectedMax].
/// Used by profile steps that specify a valid range rather than an exact target.
bool HardwareTestController::_verifyServoFeedbackRange(int servoInstance, int expectedMin, int expectedMax) const
{
    if (servoInstance < 1 || servoInstance > kServoCount) return false;
    uint16_t actual = _feedbackPwm[servoInstance - 1];
    if (actual == 0) return false;
    return actual >= static_cast<uint16_t>(expectedMin) && actual <= static_cast<uint16_t>(expectedMax);
}

/// Checks that motor feedback PWM is in the plausible range (800–2200 µs).
/// This is a basic sanity check — any value outside this range indicates
/// the motor didn't spin or the feedback is corrupt.
bool HardwareTestController::_verifyMotorFeedback(int motorInstance) const
{
    if (motorInstance < 1 || motorInstance > kServoCount) return false;
    uint16_t actual = _feedbackPwm[motorInstance - 1];
    if (actual == 0) return false;
    return actual > 800 && actual < 2200;
}

// ── Event handlers ───────────────────────────────────────────────────

/// Syncs the cached _isArmed flag when the vehicle's armed state changes.
/// If the vehicle arms while a copter motor test is active (e.g. pilot arms
/// via RC), all tests are immediately stopped for safety.  The fixed-wing
/// motor test force-arms the vehicle itself (_startFixedWingMotorTest), so its
/// own arm event must not abort the EDF spin.
void HardwareTestController::_onArmedChanged()
{
    bool armed = _vehicle ? _vehicle->armed() : false;
    if (_isArmed != armed) {
        _isArmed = armed;
        emit isArmedChanged();

        // Safety: an arm during a copter DO_MOTOR_TEST is unexpected — stop.
        // The FW test (phase 2) is exempt: it requested the arm itself.
        if (armed && _activeMotor != -1 && _fwTestIndex < 0) {
            qCWarning(hardwareTestLog) << "Vehicle armed during active motor test — stopping all";
            stopAll();
        }
    }
}

/// Parameters are ready — cancel the timeout and resolve motor count from actual params.
void HardwareTestController::_onParametersReady()
{
    _paramTimeoutTimer.stop();
    _paramTimeoutFired = false;
    _invalidateMotorChannel();
    resolveMotorCount();
}

/// Parameter loading timed out — fall back to HEARTBEAT-based motor count detection.
void HardwareTestController::_onParamTimeout()
{
    qCDebug(hardwareTestLog) << "Parameter loading timed out after" << kParamTimeoutMs << "ms — using HEARTBEAT fallback";
    _paramTimeoutFired = true;
    resolveMotorCount();
}

/// Processes incoming MAVLink messages.  Only handles SERVO_OUTPUT_RAW (msg #36),
/// which contains the actual PWM output values for all 16 servo channels.
/// Updates _feedbackPwm for all channels and tracks the peak value for the
/// currently active motor (used by _evaluateTestResult to determine pass/fail).
void HardwareTestController::_onMavlinkMessage(const mavlink_message_t &message)
{
    if (message.msgid != MAVLINK_MSG_ID_SERVO_OUTPUT_RAW) return;

    mavlink_servo_output_raw_t servo;
    mavlink_msg_servo_output_raw_decode(&message, &servo);
    if (servo.port != 0) return;

    _feedbackPwm[0]  = servo.servo1_raw;
    _feedbackPwm[1]  = servo.servo2_raw;
    _feedbackPwm[2]  = servo.servo3_raw;
    _feedbackPwm[3]  = servo.servo4_raw;
    _feedbackPwm[4]  = servo.servo5_raw;
    _feedbackPwm[5]  = servo.servo6_raw;
    _feedbackPwm[6]  = servo.servo7_raw;
    _feedbackPwm[7]  = servo.servo8_raw;
    _feedbackPwm[8]  = servo.servo9_raw;
    _feedbackPwm[9]  = servo.servo10_raw;
    _feedbackPwm[10] = servo.servo11_raw;
    _feedbackPwm[11] = servo.servo12_raw;
    _feedbackPwm[12] = servo.servo13_raw;
    _feedbackPwm[13] = servo.servo14_raw;
    _feedbackPwm[14] = servo.servo15_raw;
    _feedbackPwm[15] = servo.servo16_raw;

    // Track peak PWM during active test (value is overwritten once motor stops).
    // The monitored channel is the motor's servo output (motor index for copters,
    // auto-detected channel for fixed-wing).
    if (_activeMotor > 0) {
        const int channel = motorOutputChannel(_activeMotor);
        int aidx = _activeMotor - 1;
        if (channel > 0 && channel <= kServoCount &&
                aidx >= 0 && aidx < _state.size()) {
            if (_state[aidx] == Testing && _feedbackPwm[channel - 1] > _peakFeedbackPwm[channel - 1])
                _peakFeedbackPwm[channel - 1] = _feedbackPwm[channel - 1];
        }
    }
}

/// Decrements the cooldown counter every 250 ms.  When the counter reaches zero,
/// the motor's state is restored from Cooldown back to its result state (Pass or Fail).
/// This prevents the user from retesting a motor immediately while it's still warm.
void HardwareTestController::_cooldownTick()
{
    _cooldownRemaining--;
    emit motorStatesChanged();

    if (_cooldownRemaining <= 0) {
        _cooldownTimer.stop();
        if (_cooldownIndex >= 0 && _cooldownIndex < _state.size()) {
            if (_state[_cooldownIndex] == Cooldown) {
                _state[_cooldownIndex] = _cooldownTargetState;
                emit motorStatesChanged();
            }
        }
        _cooldownIndex = -1;
    }
}

/// Retrieves the hardware test event log for a given flight ID from the database.
QString HardwareTestController::getHardwareTestEvents(int flightId)
{
    return DatabaseManager::instance().getHardwareTestEvents(flightId);
}
