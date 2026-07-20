#include "HardwareTestController.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QDateTime>

#include "Vehicle/Vehicle.h"
#include "Vehicle/MultiVehicleManager.h"
#include "FactSystem/ParameterManager.h"
#include "FactSystem/Fact.h"
#include "utils/DatabaseManager.h"

Q_LOGGING_CATEGORY(hardwareTestLog, "preflight.hardwaretest")

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
}

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
    }
}

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

    // PX4: read CA_AIRFRAME
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

    // ArduPilot: read FRAME_CLASS
    if (_vehicle->apmFirmware()) {
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

    // Fallback: derive from HEARTBEAT type
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

void HardwareTestController::setFirstTestDone(bool done)
{
    if (_firstTestDone != done) {
        _firstTestDone = done;
        emit firstTestDoneChanged();
    }
}

// ── Per-motor accessors ─────────────────────────────────────────────────

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
    int idx = motorIndex - 1;
    if (idx < 0 || idx >= kServoCount) return 0;
    return static_cast<int>(_feedbackPwm[idx]);
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

void HardwareTestController::_setMotorState(int index, MotorState state)
{
    if (index < 0 || index >= _state.size()) return;
    if (_state[index] == state) return;
    _state[index] = state;
    emit motorStatesChanged();
}

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
    _motorPwmValues.fill(1100);
    _activeMotor = -1;
    _cooldownIndex = -1;
    _cooldownTimer.stop();
    memset(_feedbackPwm, 0, sizeof(_feedbackPwm));
    _firstTestDone = false;

    emit motorCountChanged();
    emit motorStatesChanged();
    emit activeMotorChanged();
    emit vtolChanged();
    emit motorPwmValuesChanged();
}

// ── Individual motor test ─────────────────────────────────────────────

void HardwareTestController::testMotor(int motorIndex)
{
    int idx = motorIndex - 1;

    if (!_vehicle) {
        qCDebug(hardwareTestLog) << "testMotor: no vehicle";
        return;
    }
    if (_isArmed) {
        qCDebug(hardwareTestLog) << "testMotor: vehicle is armed — cannot test";
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

    // Read per-motor PWM value and shared duration
    int pwmUs  = (idx < _motorPwmValues.size()) ? _motorPwmValues[idx] : 1100;

    _activeMotor = motorIndex;
    _setMotorState(idx, Testing);

    _expectedPwm = pwmUs;

    if (idx >= 0 && idx < kServoCount) {
        _feedbackPwm[idx] = 0;
    }

    qCDebug(hardwareTestLog) << "Testing motor" << motorIndex
                             << "at" << pwmUs << "\u00b5s PWM"
                             << "for" << _durationSec << "s";

    emit activeMotorChanged();

    _vehicle->sendMavCommand(
        _vehicle->defaultComponentId(),
        MAV_CMD_DO_MOTOR_TEST,
        false,
        motorIndex,
        1,                          // param2: 1 = PWM \u00b5s mode (not percentage)
        static_cast<float>(pwmUs), // param3: target PWM in \u00b5s
        _durationSec,
        1,                          // param5: single motor
        0
    );

    _resultTimer.start((_durationSec + 1) * 1000 + 500);
}

void HardwareTestController::_evaluateTestResult()
{
    if (_activeMotor < 0) return;

    int idx = _activeMotor - 1;
    uint16_t actual = _feedbackPwm[idx];
    bool passed = false;
    QString resultStr;
    int delta = 0;

    if (actual == 0) {
        resultStr = QStringLiteral("TIMEOUT");
        qCDebug(hardwareTestLog) << "Motor" << _activeMotor
                                 << "SERVO_OUTPUT_RAW not received";
    } else {
        delta = qAbs(static_cast<int>(actual) - _expectedPwm);
        if (delta <= kPwmTolerance) {
            passed = true;
            resultStr = QStringLiteral("PASS");
            qCDebug(hardwareTestLog) << "Motor" << _activeMotor
                                     << "PASS — actual" << actual
                                     << "expected" << _expectedPwm
                                     << "delta" << delta;
        } else {
            resultStr = QStringLiteral("FAIL");
            qCDebug(hardwareTestLog) << "Motor" << _activeMotor
                                     << "FAIL — actual" << actual
                                     << "expected" << _expectedPwm
                                     << "delta" << delta;
        }
    }

    _setMotorState(idx, passed ? Pass : Fail);

    int pwmUs = (idx < _motorPwmValues.size()) ? _motorPwmValues[idx] : 1100;
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

void HardwareTestController::stopAll()
{
    if (!_vehicle) return;

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
        int pwmUs = (idx < _motorPwmValues.size()) ? _motorPwmValues[idx] : 1100;
        _logTestResult(_activeMotor, pwmUs,
                       _durationSec, _expectedPwm, 0, 0,
                       QStringLiteral("CANCELLED"));
    }
    _activeMotor = -1;
    emit activeMotorChanged();

    qCDebug(hardwareTestLog) << "STOP ALL triggered";
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
    _firstTestDone = false;
    emit firstTestDoneChanged();
}

void HardwareTestController::_sendStopToMotor(int index)
{
    if (!_vehicle) return;
    _vehicle->sendMavCommand(
        _vehicle->defaultComponentId(),
        MAV_CMD_DO_MOTOR_TEST,
        false,
        index,
        1,     // param2: PWM mode
        1000,  // param3: 1000µs = disarmed
        0,
        1,
        0
    );
}

void HardwareTestController::setMotorPwm(int motorIndex, int pwmUs)
{
    int idx = motorIndex - 1;
    if (idx < 0 || idx >= _motorPwmValues.size()) return;
    int clamped = qBound(1000, pwmUs, 1200);
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

QVariant HardwareTestController::sequenceCompleted() const
{
    return _sequenceCompleted ? QVariant(true) : QVariant();
}

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
    _startTest();
}

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

void HardwareTestController::_advanceStep()
{
    if (_currentStep >= _totalSteps) {
        _finishTest(_allPassed, QString());
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
}

bool HardwareTestController::_verifyServoFeedback(int servoInstance, int expectedPwm) const
{
    if (servoInstance < 1 || servoInstance > kServoCount) return false;
    uint16_t actual = _feedbackPwm[servoInstance - 1];
    if (actual == 0) return false;
    return qAbs(static_cast<int>(actual) - expectedPwm) <= kPwmTolerance;
}

bool HardwareTestController::_verifyServoFeedbackRange(int servoInstance, int expectedMin, int expectedMax) const
{
    if (servoInstance < 1 || servoInstance > kServoCount) return false;
    uint16_t actual = _feedbackPwm[servoInstance - 1];
    if (actual == 0) return false;
    return actual >= static_cast<uint16_t>(expectedMin) && actual <= static_cast<uint16_t>(expectedMax);
}

bool HardwareTestController::_verifyMotorFeedback(int motorInstance) const
{
    if (motorInstance < 1 || motorInstance > kServoCount) return false;
    uint16_t actual = _feedbackPwm[motorInstance - 1];
    if (actual == 0) return false;
    return actual > 800 && actual < 2200;
}

// ── Event handlers ───────────────────────────────────────────────────

void HardwareTestController::_onCommandResult(int cmdId, int compId, int mavResult)
{
    Q_UNUSED(compId)
    if (cmdId != MAV_CMD_DO_MOTOR_TEST) return;

    if (mavResult != MAV_RESULT_ACCEPTED) {
        qCDebug(hardwareTestLog) << "MAV_CMD_DO_MOTOR_TEST rejected:" << mavResult;
    }
}

void HardwareTestController::_onArmedChanged()
{
    bool armed = _vehicle ? _vehicle->armed() : false;
    if (_isArmed != armed) {
        _isArmed = armed;
        emit isArmedChanged();
        if (armed && _activeMotor != -1) {
            stopAll();
        }
    }
}

void HardwareTestController::_onParametersReady()
{
    _paramTimeoutTimer.stop();
    _paramTimeoutFired = false;
    resolveMotorCount();
}

void HardwareTestController::_onParamTimeout()
{
    qCDebug(hardwareTestLog) << "Parameter loading timed out after" << kParamTimeoutMs << "ms — using HEARTBEAT fallback";
    _paramTimeoutFired = true;
    resolveMotorCount();
}

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
}

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
