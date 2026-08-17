#include "ControlSurfaceTestController.h"

#include <QDebug>
#include <QHash>
#include <QSettings>
#include <QtMath>
#include <QtLogging>
#include "Vehicle/Vehicle.h"
#include "FactSystem/ParameterManager.h"
#include "FactSystem/Fact.h"
#include "Comms/MAVLinkProtocol.h"
#include "../utils/DatabaseManager.h"
#include "../core/VehicleProfileManager.h"

Q_DECLARE_LOGGING_CATEGORY(controlSurfaceTestLog)
Q_LOGGING_CATEGORY(controlSurfaceTestLog, "preflight.surfacetest")

/// Parses a Vehicle::flightMode() string into the ArduPilot Plane mode number.
/// QGC renders known modes as friendly names ("STABILIZE", "LOITER") and unknown
/// ones as "Custom:0x%1" (e.g. "Custom:0xb" = RTL/11). Returns -1 when unknown.
static int surfaceFlightModeNumber(const QString &modeName)
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

/// True for modes where the autopilot commands the surfaces itself, so stick
/// override has little or no authority (LOITER/AUTO/RTL/GUIDED/CIRCLE/FBWB/
/// TAKEOFF/LAND and the quadplane variants). Unknown modes are allowed through.
static bool surfaceAutopilotCommanded(int modeNum)
{
    switch (modeNum) {
    case 1:  // CIRCLE
    case 10: // AUTO
    case 11: // RTL
    case 12: // LOITER
    case 13: // TAKEOFF
    case 15: // GUIDED
    case 17: case 18: case 19: case 20: case 21: case 22: case 23:
        return true;
    default:
        return false;
    }
}

ControlSurfaceTestController::ControlSurfaceTestController(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_swapElevonLR = settings.value(QStringLiteral("surfaceTest/swapElevonLR"), false).toBool();

    m_stepTimer.setInterval(800);
    connect(&m_stepTimer, &QTimer::timeout, this, &ControlSurfaceTestController::_advanceSweepStep);

    m_streamTimer.setInterval(50);
    connect(&m_streamTimer, &QTimer::timeout, this, [this]() {
        if (m_activeSurface >= 0 && m_activeSurface < m_surfaceList.size() && m_states[m_activeSurface] == Sweeping) {
            _sendActivePwm(m_currentStreamPwm);
        }
    });

    // Periodically re-force MANUAL flight mode during a sweep so the control
    // surface test overrides the autopilot/CH8 switch position until it finishes.
    m_forceManualTimer.setInterval(1000);
    connect(&m_forceManualTimer, &QTimer::timeout, this, &ControlSurfaceTestController::_forceManualMode);
}

void ControlSurfaceTestController::setSwapElevonLR(bool swap)
{
    if (m_swapElevonLR == swap) return;
    m_swapElevonLR = swap;
    QSettings settings;
    settings.setValue(QStringLiteral("surfaceTest/swapElevonLR"), m_swapElevonLR);
    emit swapElevonLRChanged();
}

void ControlSurfaceTestController::setVehicle(Vehicle *vehicle)
{
    if (m_vehicle == vehicle) return;

    if (m_vehicle) {
        disconnect(m_vehicle, &Vehicle::mavCommandResult, this, &ControlSurfaceTestController::_onCommandResult);
        disconnect(m_vehicle, &Vehicle::mavlinkMessageReceived, this, &ControlSurfaceTestController::_onMavlinkMessage);
        disconnect(m_vehicle, &Vehicle::armedChanged, this, nullptr);
        disconnect(m_vehicle, &Vehicle::flightModeChanged, this, nullptr);
        if (m_vehicle->parameterManager()) {
            disconnect(m_vehicle->parameterManager(), &ParameterManager::parametersReadyChanged,
                       this, &ControlSurfaceTestController::_onParametersReady);
        }
    }

    m_vehicle = vehicle;

    if (m_vehicle) {
        connect(m_vehicle, &Vehicle::mavCommandResult, this, &ControlSurfaceTestController::_onCommandResult);
        connect(m_vehicle, &Vehicle::mavlinkMessageReceived, this, &ControlSurfaceTestController::_onMavlinkMessage);
        connect(m_vehicle, &Vehicle::armedChanged, this, [this](bool armed) {
            if (m_isArmed != armed) {
                m_isArmed = armed;
                emit isArmedChanged();
            }
        });
        connect(m_vehicle, &Vehicle::flightModeChanged, this, [this](const QString &mode) {
            m_flightMode = mode;
            emit flightModeChanged();
        });
        m_flightMode = m_vehicle->flightMode();
        emit flightModeChanged();
        m_isArmed = m_vehicle->armed();
        emit isArmedChanged();
        // Re-map surfaces with the real SERVOx_FUNCTION channels once parameters
        // finish loading (the initial map may use fallback channel guesses).
        if (m_vehicle->parameterManager()) {
            connect(m_vehicle->parameterManager(), &ParameterManager::parametersReadyChanged,
                    this, &ControlSurfaceTestController::_onParametersReady);
        }
    }
}

void ControlSurfaceTestController::_onParametersReady(bool ready)
{
    if (ready && m_activeSurface == -1 && !m_lastVehicleType.isEmpty()) {
        loadSurfacesForVehicle(m_lastVehicleType, m_lastMotorCount);
    }
}

QVariantList ControlSurfaceTestController::surfaces() const
{
    QVariantList list;
    for (const auto &s : m_surfaceList) {
        QVariantMap map;
        map[QStringLiteral("id")] = s.id;
        map[QStringLiteral("label")] = s.label;
        map[QStringLiteral("channel")] = s.channel;
        map[QStringLiteral("rcChannel")] = s.rcChannel;
        map[QStringLiteral("rcChannel2")] = s.rcChannel2;
        map[QStringLiteral("elevonMode")] = s.elevonMode;
        map[QStringLiteral("question")] = s.question;
        map[QStringLiteral("centerPwm")] = s.centerPwm;
        map[QStringLiteral("minPwm")] = s.minPwm;
        map[QStringLiteral("maxPwm")] = s.maxPwm;
        list.append(map);
    }
    return list;
}

QVariantList ControlSurfaceTestController::surfaceStates() const
{
    QVariantList list;
    for (auto st : m_states) {
        list.append(static_cast<int>(st));
    }
    return list;
}

void ControlSurfaceTestController::loadSurfacesForVehicle(const QString &vehicleType, int motorCount)
{
    Q_UNUSED(motorCount)
    m_lastVehicleType = vehicleType;
    m_lastMotorCount = motorCount;
    m_surfaceList.clear();
    m_states.clear();
    m_activeSurface = -1;

    // Canonical classification — everything below keys off the VehicleKind.
    VehicleKind kind = VehicleProfileManager::kindFromTypeString(vehicleType);
    // Unknown falls through to the fixed-wing default set (most comprehensive).
    bool isFixedWing = (kind == VehicleKind::FixedWing
                     || kind == VehicleKind::VtolConventional
                     || kind == VehicleKind::Unknown);

    if (kind == VehicleKind::Multirotor) {
        // No control surfaces for multirotor
        emit surfacesChanged();
        emit surfaceStatesChanged();
        emit activeSurfaceChanged();
        return;
    }

    // ArduPilot SERVOn_FUNCTION map (Plane 4.5 — verified against SRV_Channel.h):
    //   2 = Flap, 4 = Aileron, 19 = Elevator, 21 = Rudder, 26 = Steering,
    //   70 = Throttle (motor output — not a surface), 77 = Elevon left,
    //   78 = Elevon right.
    auto *paramMgr = m_vehicle ? m_vehicle->parameterManager() : nullptr;
    int compId = m_vehicle ? m_vehicle->defaultComponentId() : 1;

    // Helper to read ArduPilot SERVOx_FUNCTION
    auto getServoFunction = [paramMgr, compId](int ch) -> int {
        if (!paramMgr || !paramMgr->parametersReady()) return -1;
        QString paramName = QStringLiteral("SERVO%1_FUNCTION").arg(ch);
        Fact *fact = paramMgr->getParameter(compId, paramName);
        return fact ? fact->rawValue().toInt() : -1;
    };

    int aileronCh = -1, elevatorCh = -1, rudderCh = -1, steeringCh = -1;
    int flapCh = -1, elevonLCh = -1, elevonRCh = -1;

    for (int ch = 1; ch <= 16; ++ch) {
        switch (getServoFunction(ch)) {
        case 4:  if (aileronCh < 0) aileronCh = ch; break;   // Aileron
        case 19: if (elevatorCh < 0) elevatorCh = ch; break; // Elevator
        case 21: if (rudderCh < 0) rudderCh = ch; break;     // Rudder
        case 26: if (steeringCh < 0) steeringCh = ch; break; // Steering (nose wheel)
        case 2:  if (flapCh < 0) flapCh = ch; break;         // Flap
        case 77: if (elevonLCh < 0) elevonLCh = ch; break;   // Elevon left
        case 78: if (elevonRCh < 0) elevonRCh = ch; break;   // Elevon right
        default: break;                                       // incl. 70 Throttle → skipped
        }
    }

    // Fallback when params are unavailable or not yet loaded.
    if (isFixedWing && aileronCh < 0 && elevatorCh < 0 && rudderCh < 0 && elevonLCh < 0 && elevonRCh < 0) {
        aileronCh = 1;
        elevatorCh = 2;
        rudderCh = 4;
    }

    const int rollCh  = _rcMapChannel(QStringLiteral("RCMAP_ROLL"),  QStringLiteral("RC_MAP_ROLL"),  1);
    const int pitchCh = _rcMapChannel(QStringLiteral("RCMAP_PITCH"), QStringLiteral("RC_MAP_PITCH"), 2);
    const int yawCh   = _rcMapChannel(QStringLiteral("RCMAP_YAW"),   QStringLiteral("RC_MAP_YAW"),   4);

    // ── Elevon / Flying-wing airframes (SERVO_FUNCTION 77/78) ─────────────────
    //
    // Strategy: drive the RC input channels (roll/pitch) and let the ArduPilot
    // elevon mixer produce the outputs.  Mixer: LEFT = (pitch − roll) × gain,
    // RIGHT = (pitch + roll) × gain, so
    //   Aileron (roll only)  → elevons deflect OPPOSITE
    //   Elevator (pitch only)→ elevons deflect SAME
    //   Left elevon only     → pitch = −roll  (roll = pwm)
    //   Right elevon only    → pitch = +roll  (roll = pwm)
    // MIXING_GAIN is boosted to 1.0 during the sweep so the surfaces travel the
    // full SERVOn_MIN/MAX range (restored afterwards).
    m_stepTimer.setInterval(1000); // 1.0s per sweep step for clear visual verification

    // Elevon / Flying wing channels
    if (elevonLCh > 0 || elevonRCh > 0) {
        const int lCh = (elevonLCh > 0) ? elevonLCh : 1;
        const int rCh = (elevonRCh > 0) ? elevonRCh : 2;

        // Real per-servo travel limits (SERVOn_MIN/MAX/TRIM, e.g. 988–2011 µs).
        const int lMin  = _readServoParam(QStringLiteral("SERVO%1_MIN").arg(lCh), QStringLiteral(""), 988);
        const int lMax  = _readServoParam(QStringLiteral("SERVO%1_MAX").arg(lCh), QStringLiteral(""), 2011);
        const int lTrim = _readServoParam(QStringLiteral("SERVO%1_TRIM").arg(lCh), QStringLiteral(""), 1500);
        const int rMin  = _readServoParam(QStringLiteral("SERVO%1_MIN").arg(rCh), QStringLiteral(""), 988);
        const int rMax  = _readServoParam(QStringLiteral("SERVO%1_MAX").arg(rCh), QStringLiteral(""), 2011);
        const int rTrim = _readServoParam(QStringLiteral("SERVO%1_TRIM").arg(rCh), QStringLiteral(""), 1500);

        // ── 1. Aileron (Roll): opposite-direction deflection of both elevons ──
        {
            m_surfaceList.append({ QStringLiteral("aileron"), QStringLiteral("Aileron (Roll)"),
                                   lCh, rCh, 1,
                                   rollCh, -1, 0,
                                   QStringLiteral("Did the elevons move in OPPOSITE directions (one up, one down)?"),
                                   lTrim, lMin, lMax });
        }

        // ── 2. Elevator (Pitch): same-direction deflection of both elevons ──
        {
            m_surfaceList.append({ QStringLiteral("elevator"), QStringLiteral("Elevator (Pitch)"),
                                   lCh, rCh, 0,
                                   pitchCh, -1, 0,
                                   QStringLiteral("Did BOTH elevons move together in the same direction (both up / both down)?"),
                                   rTrim, rMin, rMax });
        }

        // ── 3. Left elevon isolation ──
        if (elevonLCh > 0) {
            m_surfaceList.append({ QStringLiteral("elevon_l"), QStringLiteral("Left Elevon"),
                                   lCh, rCh, 0,
                                   rollCh, pitchCh, 1,
                                   QStringLiteral("Did the LEFT elevon deflect while the RIGHT elevon stayed centered?"),
                                   lTrim, lMin, lMax });
        }

        // ── 4. Right elevon isolation ──
        if (elevonRCh > 0) {
            m_surfaceList.append({ QStringLiteral("elevon_r"), QStringLiteral("Right Elevon"),
                                   rCh, lCh, 0,
                                   rollCh, pitchCh, 2,
                                   QStringLiteral("Did the RIGHT elevon deflect while the LEFT elevon stayed centered?"),
                                   rTrim, rMin, rMax });
        }
    } else {
        if (aileronCh > 0) {
            int aTrim = _readServoParam(QStringLiteral("SERVO%1_TRIM").arg(aileronCh), QStringLiteral(""), 1500);
            m_surfaceList.append({ QStringLiteral("aileron"), QStringLiteral("Aileron"),
                                   aileronCh, -1, 0,
                                   rollCh, -1, 0,
                                   QStringLiteral("Did the aileron move in the correct direction?"),
                                   aTrim, 750, 2250 });
        }
        if (elevatorCh > 0) {
            int eTrim = _readServoParam(QStringLiteral("SERVO%1_TRIM").arg(elevatorCh), QStringLiteral(""), 1500);
            m_surfaceList.append({ QStringLiteral("elevator"), QStringLiteral("Elevator"),
                                   elevatorCh, -1, 0,
                                   pitchCh, -1, 0,
                                   QStringLiteral("Did the elevator move in the correct direction?"),
                                   eTrim, 750, 2250 });
        }
        if (flapCh > 0) {
            int fTrim = _readServoParam(QStringLiteral("SERVO%1_TRIM").arg(flapCh), QStringLiteral(""), 1500);

            // ArduPilot refuses DO_SET_SERVO on function-assigned channels
            // (Flap = function 2/14), so drive the flap through its RC input
            // channel instead.  FLAP_IN_CHANNEL gives that channel; if unset,
            // fall back to DO_SET_SERVO (which will likely be rejected — the
            // test then reports no-feedback rather than a clean fail).
            int flapRcCh = -1;
            if (paramMgr && paramMgr->parametersReady()) {
                Fact *flapIn = paramMgr->getParameter(compId, QStringLiteral("FLAP_IN_CHANNEL"));
                int ch = flapIn ? flapIn->rawValue().toInt() : 0;
                if (ch >= 1 && ch <= 16) flapRcCh = ch;
            }

            m_surfaceList.append({ QStringLiteral("flap"), QStringLiteral("Flaps"),
                                   flapCh, -1, 0,
                                   flapRcCh, -1, 0,
                                   QStringLiteral("Did the flaps extend correctly?"),
                                   fTrim, 1000, 2000 });
        }
    }

    const int rCh = (rudderCh > 0) ? rudderCh : 4;
    const int yCh = (yawCh > 0) ? yawCh : 4;
    int rTrim = _readServoParam(QStringLiteral("SERVO%1_TRIM").arg(rCh), QStringLiteral(""), 1500);
    m_surfaceList.append({ QStringLiteral("rudder"), QStringLiteral("Rudder"),
                           rCh, -1, 0,
                           yCh, -1, 0,
                           QStringLiteral("Did the rudder turn the correct way?"),
                           rTrim, 750, 2250 });

    if (steeringCh > 0) {
        int sTrim = _readServoParam(QStringLiteral("SERVO%1_TRIM").arg(steeringCh), QStringLiteral(""), 1500);
        m_surfaceList.append({ QStringLiteral("nose_wheel"), QStringLiteral("Nose Wheel"),
                               steeringCh, -1, 0,
                               yCh, -1, 0,
                               QStringLiteral("Did the nose wheel turn the correct way?"),
                               sTrim, 750, 2250 });
    }






    qCWarning(controlSurfaceTestLog) << "Surface map:"
                                     << "elevons" << elevonLCh << "/" << elevonRCh
                                     << "aileron" << aileronCh
                                     << "elevator" << elevatorCh
                                     << "rudder" << rudderCh
                                     << "steering" << steeringCh
                                     << "RCMAP_ROLL" << rollCh
                                     << "RCMAP_PITCH" << pitchCh
                                     << "RCMAP_YAW" << yawCh;
    for (const auto &s : m_surfaceList) {
        QString drive = (s.elevonMode != 0)
                            ? QStringLiteral("RC %1 + RC %2 (elevon isolation)").arg(s.rcChannel).arg(s.rcChannel2)
                            : (s.rcChannel > 0 ? QStringLiteral("RC %1").arg(s.rcChannel) : QStringLiteral("DO_SET_SERVO"));
        qCDebug(controlSurfaceTestLog) << "Surface loaded:" << s.id
                                       << "label" << s.label
                                       << "servo CH" << s.channel
                                       << "rcChannel" << s.rcChannel
                                       << "rcChannel2" << s.rcChannel2
                                       << "drive" << drive;
    }

    m_states.resize(m_surfaceList.size());
    m_states.fill(Idle);

    emit surfacesChanged();
    emit surfaceStatesChanged();
    emit activeSurfaceChanged();
}

int ControlSurfaceTestController::_findSurfaceIndex(const QString &surfaceId) const
{
    for (int i = 0; i < m_surfaceList.size(); ++i) {
        if (m_surfaceList[i].id == surfaceId)
            return i;
    }
    return -1;
}

/// Reads an RC function-mapping parameter (ArduPilot RCMAP_*, PX4 RC_MAP_*)
/// returning the RC channel number that drives the given function.
int ControlSurfaceTestController::_rcMapChannel(const QString &apName, const QString &px4Name, int fallback) const
{
    auto *paramMgr = m_vehicle ? m_vehicle->parameterManager() : nullptr;
    int compId = m_vehicle ? m_vehicle->defaultComponentId() : 1;
    if (paramMgr && paramMgr->parametersReady()) {
        const QStringList names = {apName, px4Name};
        for (const QString &name : names) {
            Fact *fact = paramMgr->getParameter(compId, name);
            if (fact) {
                int ch = fact->rawValue().toInt();
                if (ch >= 1 && ch <= 16)
                    return ch;
            }
        }
    }
    return fallback;
}

/// Returns the RC channel that drives the given surface, resolved live from
/// the current parameters. ArduPilot surfaces follow the RC inputs mapped by
/// RCMAP_ROLL/PITCH/YAW; flaps have no RCMAP channel (-1 → DO_SET_SERVO).
/// Elevon cards use RCMAP_ROLL as their primary channel (the second, pitch,
/// is resolved in testSurface()).
int ControlSurfaceTestController::_rcMapForSurface(const QString &id) const
{
    if (id == QStringLiteral("aileron") || id == QStringLiteral("elevon_l") || id == QStringLiteral("elevon_r"))
        return _rcMapChannel(QStringLiteral("RCMAP_ROLL"), QStringLiteral("RC_MAP_ROLL"), 1);
    if (id == QStringLiteral("elevator"))
        return _rcMapChannel(QStringLiteral("RCMAP_PITCH"), QStringLiteral("RC_MAP_PITCH"), 2);
    if (id == QStringLiteral("rudder") || id == QStringLiteral("nose_wheel"))
        return _rcMapChannel(QStringLiteral("RCMAP_YAW"), QStringLiteral("RC_MAP_YAW"), 4);
    return -1;
}

void ControlSurfaceTestController::testSurface(const QString &surfaceId)
{
    // Dispatch to the dedicated per-surface test so each type has its own
    // implementation and can be fixed / verified independently.
    if (surfaceId == QStringLiteral("aileron"))        return testAileron();
    if (surfaceId == QStringLiteral("elevator"))       return testElevator();
    if (surfaceId == QStringLiteral("rudder"))         return testRudder();
    if (surfaceId == QStringLiteral("nose_wheel"))     return testNoseWheel();
    if (surfaceId == QStringLiteral("elevon_l"))       return testElevonLeft();
    if (surfaceId == QStringLiteral("elevon_r"))       return testElevonRight();
    if (surfaceId == QStringLiteral("flap"))           return testFlap();
    qCWarning(controlSurfaceTestLog) << "testSurface: unknown surface id" << surfaceId;
}

void ControlSurfaceTestController::testAileron()
{
    _beginSurfaceTest(_findSurfaceIndex(QStringLiteral("aileron")));
}

void ControlSurfaceTestController::testElevator()
{
    _beginSurfaceTest(_findSurfaceIndex(QStringLiteral("elevator")));
}

void ControlSurfaceTestController::testRudder()
{
    _beginSurfaceTest(_findSurfaceIndex(QStringLiteral("rudder")));
}

void ControlSurfaceTestController::testNoseWheel()
{
    _beginSurfaceTest(_findSurfaceIndex(QStringLiteral("nose_wheel")));
}

void ControlSurfaceTestController::testElevonLeft()
{
    _beginSurfaceTest(_findSurfaceIndex(QStringLiteral("elevon_l")));
}

void ControlSurfaceTestController::testElevonRight()
{
    _beginSurfaceTest(_findSurfaceIndex(QStringLiteral("elevon_r")));
}

void ControlSurfaceTestController::testFlap()
{
    _beginSurfaceTest(_findSurfaceIndex(QStringLiteral("flap")));
}

/// Shared start path for every dedicated surface test: validates state,
/// re-resolves the live RC channels, requests a force-arm when disarmed
/// (ArduPilot Plane only passes RC through to servos while armed), and starts
/// the sweep once armed.
void ControlSurfaceTestController::_beginSurfaceTest(int idx)
{
    if (idx < 0 || idx >= m_surfaceList.size()) return;
    if (m_activeSurface != -1) {
        return; // another surface currently active
    }
    if (m_states[idx] == Cooldown) return;

    // Re-resolve the RC channels from live parameters.
    m_surfaceList[idx].rcChannel = _rcMapForSurface(m_surfaceList[idx].id);
    if (m_surfaceList[idx].elevonMode != 0) {
        m_surfaceList[idx].rcChannel2 = _rcMapChannel(QStringLiteral("RCMAP_PITCH"), QStringLiteral("RC_MAP_PITCH"), 2);
    }

    // ArduPilot Plane only passes RC input through to the servo outputs while
    // ARMED (the servos are pinned at trim when disarmed), so the vehicle must
    // be armed before the sweep.  Request a force-arm and defer the sweep start
    // until the arm is ACKed (see _onCommandResult), so the first PWM already
    // reaches the servos.  Already armed → sweep immediately.
    if (m_vehicle && !m_vehicle->armed()) {
        qCWarning(controlSurfaceTestLog) << "Surface test requesting arm before sweep (mode:" << m_flightMode << ")";
        m_pendingSurfaceIndex = idx;
        m_vehicle->sendMavCommand(
            m_vehicle->defaultComponentId(),
            MAV_CMD_COMPONENT_ARM_DISARM, false,
            1.0f, 21196.0f, 0, 0, 0, 0, 0);
        return;
    }

    _startSurfaceSweep(idx);
}

void ControlSurfaceTestController::_startSurfaceSweep(int idx)
{
    if (!m_vehicle || idx < 0 || idx >= m_surfaceList.size()) return;

    if (m_vehicle && m_vehicle->flightMode() != QStringLiteral("MANUAL")) {
        m_vehicle->setFlightMode(QStringLiteral("MANUAL"));
    }

    qCWarning(controlSurfaceTestLog) << "SURFACE TEST:" << m_surfaceList[idx].id
                                     << "servo CH" << m_surfaceList[idx].channel
                                     << "rc CH" << m_surfaceList[idx].rcChannel
                                     << (m_surfaceList[idx].rcChannel2 > 0 ? QStringLiteral("+ CH%1").arg(m_surfaceList[idx].rcChannel2) : QStringLiteral(""))
                                     << "mode" << m_flightMode
                                     << "vehicle" << (m_vehicle ? "connected" : "NULL");

    // Autopilot-commanded modes (LOITER/AUTO/RTL/GUIDED/CIRCLE/…) drive the
    // surfaces themselves and ignore or heavily suppress the sticks, so the
    // sweep would show little or no motion. Instead of blocking, force MANUAL
    // continuously for the duration of the test so it overrides the mode.
    const int modeNum = surfaceFlightModeNumber(m_flightMode);
    if (surfaceAutopilotCommanded(modeNum)) {
        qCWarning(controlSurfaceTestLog) << "Surface test forcing MANUAL over autopilot-commanded mode" << m_flightMode;
    }
    m_forceManualTimer.start();

    m_sweptPwm = 0;
    m_sweptPwm2 = 0;
    emit sweptPwmChanged();
    _setServoStreaming(true);

    m_activeSurface = idx;
    m_states[idx] = Sweeping;
    m_sweepStep = 0;
    m_sweepMinPwmActual = 1500;
    m_sweepMaxPwmActual = 1500;
    m_sweepMinPwmActual2 = 1500;
    m_sweepMaxPwmActual2 = 1500;
    m_phaseOppositeCount = 0;
    m_phaseSameCount = 0;
    m_sweepVerdict.clear();
    emit sweepVerdictChanged();

    emit activeSurfaceChanged();
    emit surfaceStatesChanged();

    // Start from a clean override slate: release everything left over from a
    // previous test/sweep, then pin the flight axes to neutral so the first
    // REAL command of this sweep is the only thing driving the surfaces.
    _clearRcOverrides();
    _sendCenterRcOverrides();

    // Boost MIXING_GAIN to 1.0 so the elevon mixer drives the surfaces at FULL
    // SERVOn_MIN/MAX travel during the sweep (restored when the sweep ends).
    _setElevonGain(true);

    // Start sweep step timer
    _advanceSweepStep();
    m_stepTimer.start();
}

/// Temporarily boosts MIXING_GAIN to 1.0 (default is 0.5, which halves elevon
/// deflection) so the elevon mixer produces FULL travel during a sweep.  The
/// original value is saved and restored when the sweep ends.
void ControlSurfaceTestController::_setElevonGain(bool boost)
{
    if (!m_vehicle || !m_vehicle->parameterManager()
            || !m_vehicle->parameterManager()->parametersReady()) {
        return;
    }

    auto *mgr = m_vehicle->parameterManager();
    const int compId = m_vehicle->defaultComponentId();
    Fact *fact = mgr->getParameter(compId, QStringLiteral("MIXING_GAIN"));
    if (!fact) return;

    if (boost) {
        m_savedMixingGain = fact->rawValue().toDouble();
        if (!qFuzzyCompare(m_savedMixingGain, 1.0)) {
            fact->setRawValue(1.0);
            qCWarning(controlSurfaceTestLog) << "Surface sweep: MIXING_GAIN" << m_savedMixingGain << "→ 1.0";
        }
    } else if (m_savedMixingGain > 0.0) {
        fact->setRawValue(m_savedMixingGain);
        qCWarning(controlSurfaceTestLog) << "Surface sweep: restored MIXING_GAIN →" << m_savedMixingGain;
        m_savedMixingGain = 0.0;
    }
}

/// Continuously re-issues the MANUAL flight-mode request during a sweep so the
/// control-surface test overrides the autopilot-commanded mode / CH8 switch for
/// its whole duration. Stops as soon as no surface test is active.
void ControlSurfaceTestController::_forceManualMode()
{
    if (m_activeSurface != -1 && m_vehicle && m_vehicle->flightMode() != QStringLiteral("MANUAL")) {
        m_vehicle->setFlightMode(QStringLiteral("MANUAL"));
        qCWarning(controlSurfaceTestLog) << "Re-forcing MANUAL flight mode during surface sweep (current:" << m_vehicle->flightMode() << ")";
    }
    if (m_activeSurface == -1) {
        m_forceManualTimer.stop();
    }
}

/// RC neutral is always 1500 µs regardless of servo trim; the servo trim is
/// only valid for direct DO_SET_SERVO drive (flaps), where it is the output
/// center.
int ControlSurfaceTestController::_sweepCenter(const ControlSurface &surf) const
{
    return (surf.rcChannel > 0) ? 1500 : surf.centerPwm;
}

/// Sweep extremes.  RC-driven surfaces travel from RC neutral to the RC input
/// limits (1000–2000 µs); the mixer turns that into full servo travel.  Direct
/// DO_SET_SERVO surfaces (flaps) sweep their SERVOn limits instead.
int ControlSurfaceTestController::_sweepMin(const ControlSurface &surf) const
{
    return (surf.rcChannel > 0) ? 1000 : surf.minPwm;
}

int ControlSurfaceTestController::_sweepMax(const ControlSurface &surf) const
{
    return (surf.rcChannel > 0) ? 2000 : surf.maxPwm;
}

void ControlSurfaceTestController::_advanceSweepStep()
{
    if (m_activeSurface < 0 || m_activeSurface >= m_surfaceList.size()) {
        m_stepTimer.stop();
        m_streamTimer.stop();
        return;
    }

    const auto &surf = m_surfaceList[m_activeSurface];

    // Hard-release every RC override from the previous step so no stale roll /
    // pitch / yaw command can keep dragging an unwanted surface (e.g. the left
    // elevon moving while the rudder is tested).  Then re-pin the pilot axes to
    // neutral so the elevon mixer always sees centered roll/pitch inputs while
    // only the surface under test is driven.
    _clearRcOverrides();
    _sendCenterRcOverrides();

    switch (m_sweepStep) {
    case 0: // Center first (establish baseline)
        m_currentStreamPwm = _sweepCenter(surf);
        break;
    case 1: // Max deflection
        m_currentStreamPwm = _sweepMax(surf);
        break;
    case 2: // Return to center
        m_currentStreamPwm = _sweepCenter(surf);
        break;
    case 3: // Min deflection
        m_currentStreamPwm = _sweepMin(surf);
        break;
    case 4: // Return to center
        m_currentStreamPwm = _sweepCenter(surf);
        break;
    default:
        // Completed — return to center and stop streaming
        m_stepTimer.stop();
        m_streamTimer.stop();
        m_forceManualTimer.stop();
        _sendActivePwm(_sweepCenter(surf));
        _setElevonGain(false);
        return;
    }

    _sendActivePwm(m_currentStreamPwm);
    if (!m_streamTimer.isActive()) {
        m_streamTimer.start();
    }

    m_sweepStep++;
}

/// Routes the sweep PWM to the correct driver for the active surface type.
void ControlSurfaceTestController::_sendActivePwm(int pwmUs)
{
    if (m_activeSurface < 0 || m_activeSurface >= m_surfaceList.size()) return;
    const auto &surf = m_surfaceList[m_activeSurface];

    if (surf.id == QStringLiteral("flap")) {
        _sendFlapPwm(pwmUs);
        return;
    }
    _sendServoPwm(surf, pwmUs);
}

/// Applies a sweep PWM to one ControlSurface following strict rules:
///   - Elevon isolation (elevonMode 1/2): RCMAP_ROLL + RCMAP_PITCH must arrive
///     in the SAME override so the mixer resolves exactly one elevon to full
///     deflection and the other to ~center.  Left only  → pitch = −roll,
///     right only → pitch = +roll (mirrored around RC center).
///   - Pure single-axis (aileron/elevator/rudder/nose_wheel): ONLY the axis'
///     RCMAP channel is overridden — never elevon channels, never a second axis.
///   - Fallback (flap / unknown): DO_SET_SERVO only when no RCMAP channel exists.
void ControlSurfaceTestController::_sendServoPwm(const ControlSurface &surf, int pwmUs)
{
    if (!m_vehicle) return;
    const int center = surf.centerPwm > 0 ? surf.centerPwm : 1500;
    const int pwm = qBound(800, pwmUs, 2200);

    // ── Elevon isolation (mode 1 = left, 2 = right) ──
    if (surf.elevonMode != 0 && surf.rcChannel > 0 && surf.rcChannel2 > 0) {
        const bool wantRight = (surf.elevonMode == 2) != m_swapElevonLR;
        const int  roll  = pwm;
        const int  pitch = wantRight ? pwm : (2 * center - pwm);
        _sendRcOverrideMulti({{ surf.rcChannel,  roll  },
                              { surf.rcChannel2, pitch }});
        return;
    }

    // ── Pure single-axis (aileron / elevator / rudder / nose_wheel) ──
    if (surf.rcChannel > 0) {
        _sendRcOverride(surf.rcChannel, pwm);
        return;
    }

    // ── Fallback only for surfaces with no RCMAP (e.g. flaps) ──
    if (surf.channel > 0) {
        _sendServoDirectMulti({{ surf.channel, pwm }});
    }
}

/// Flaps: ArduPilot refuses MAV_CMD_DO_SET_SERVO on function-assigned channels
/// (function 14 = Flap), so prefer RC_CHANNELS_OVERRIDE when an RC input channel
/// drives the flap (FLAP_IN_CHANNEL, resolved during surface loading).  Falls
/// back to DO_SET_SERVO only when no RC channel is available — in that case
/// ArduPilot Plane will likely reject the command and the test reports
/// TIMEOUT / no-feedback rather than a clean fail.
void ControlSurfaceTestController::_sendFlapPwm(int pwmUs)
{
    if (m_activeSurface < 0 || m_activeSurface >= m_surfaceList.size()) return;
    const auto &surf = m_surfaceList[m_activeSurface];

    if (surf.rcChannel > 0) {
        _sendRcOverride(surf.rcChannel, pwmUs);
        return;
    }

    if (surf.channel > 0) {
        qCWarning(controlSurfaceTestLog)
            << "Flap test via DO_SET_SERVO on CH" << surf.channel
            << "— may be rejected if function 14 is assigned";
        _sendServoDirectMulti({{ surf.channel, pwmUs }});
    }
}




/// Sends MAV_CMD_DO_SET_SERVO to multiple channels at once.
void ControlSurfaceTestController::_sendServoDirectMulti(const QVector<QPair<int,int>> &chPwms)
{
    if (!m_vehicle) return;
    for (const auto &pair : chPwms) {
        const int ch  = pair.first;
        const int pwm = qBound(800, pair.second, 2200); // generous clamp — firmware will cap to SERVOn_MIN/MAX
        qCDebug(controlSurfaceTestLog) << "DO_SET_SERVO CH" << ch << "=" << pwm << "µs";
        m_vehicle->sendMavCommand(
            m_vehicle->defaultComponentId(),
            MAV_CMD_DO_SET_SERVO,
            false,
            static_cast<float>(ch),
            static_cast<float>(pwm),
            0, 0, 0, 0, 0
        );
    }
}

/// Reads a numeric SERVO parameter (e.g. SERVO1_MIN) from the vehicle parameter manager.
/// Falls back to 'fallback' if the parameter is not available.
int ControlSurfaceTestController::_readServoParam(const QString &paramName, const QString &, int fallback) const
{
    auto *paramMgr = m_vehicle ? m_vehicle->parameterManager() : nullptr;
    int compId = m_vehicle ? m_vehicle->defaultComponentId() : 1;
    if (!paramMgr || !paramMgr->parametersReady()) return fallback;
    Fact *fact = paramMgr->getParameter(compId, paramName);
    if (!fact) return fallback;
    int val = fact->rawValue().toInt();
    return (val > 0) ? val : fallback;
}


/// Sends RC_CHANNELS_OVERRIDE setting one RC channel to a PWM value (0 = ignore).
/// Overrides expire after RC_OVERRIDE_TIME on ArduPilot, so callers refresh as needed.
void ControlSurfaceTestController::_sendRcOverride(int rcChannel, int pwmUs)
{
    _sendRcOverrideMulti({{ rcChannel, pwmUs }});
}

/// Sends RC_CHANNELS_OVERRIDE with several RC channels set in a single message
/// (required for elevon isolation, where roll and pitch must arrive together —
/// the elevon mixer sees both inputs in the same packet, not sequentially).
void ControlSurfaceTestController::_sendRcOverrideMulti(const QVector<QPair<int, int>> &overrides)
{
    if (!m_vehicle) return;

    // All 18 channels default to 65535 (UINT16_MAX = ignore / pass-through)
    uint16_t vals[18];
    for (int i = 0; i < 18; ++i) {
        vals[i] = 65535;
    }

    QStringList log;
    for (const auto &o : overrides) {
        const int ch = qBound(1, o.first, 18);
        vals[ch - 1] = static_cast<uint16_t>(qBound(1000, o.second, 2000));
        log << QStringLiteral("RC%1=%2").arg(ch).arg(vals[ch - 1]);
    }

    // Pack ALL channel values into ONE RC_CHANNELS_OVERRIDE message.  Do NOT
    // loop _sendRcOverride() here — separate messages break elevon mixing.
    MAVLinkProtocol *proto = MAVLinkProtocol::instance();
    mavlink_message_t msg;
    mavlink_msg_rc_channels_override_pack(
        proto ? proto->getSystemId() : 255,
        MAVLinkProtocol::getComponentId(),
        &msg,
        static_cast<uint8_t>(m_vehicle->id()),
        0, // target component 0 = broadcast to all components
        vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6], vals[7],
        vals[8], vals[9], vals[10], vals[11], vals[12], vals[13], vals[14], vals[15],
        vals[16], vals[17]);

    SharedLinkInterfacePtr sharedLink = m_vehicle->vehicleLinkManager()->primaryLink().lock();
    if (sharedLink) {
        m_vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
    }

    // Log exactly what was sent (mandatory for field verification of which
    // RC channels the flight controller was commanded to).
    qCDebug(controlSurfaceTestLog) << "RC override:"
                                   << log.join(QStringLiteral(", "));
}

/// Cancels all active RC overrides (all 18 channels = 0 → release control
/// back to the RC receiver / autopilot).  Sent unconditionally before every
/// new step so no override from a previous test can keep dragging surfaces.
void ControlSurfaceTestController::_clearRcOverrides()
{
    if (!m_vehicle) return;
    MAVLinkProtocol *proto = MAVLinkProtocol::instance();
    mavlink_message_t msg;
    const uint16_t zero[18] = {0};
    mavlink_msg_rc_channels_override_pack(
        proto ? proto->getSystemId() : 255,
        MAVLinkProtocol::getComponentId(),
        &msg,
        static_cast<uint8_t>(m_vehicle->id()),
        0,
        zero[0], zero[1], zero[2], zero[3], zero[4], zero[5], zero[6], zero[7],
        zero[8], zero[9], zero[10], zero[11], zero[12], zero[13], zero[14], zero[15],
        zero[16], zero[17]);

    SharedLinkInterfacePtr sharedLink = m_vehicle->vehicleLinkManager()->primaryLink().lock();
    if (sharedLink) {
        m_vehicle->sendMessageOnLinkThreadSafe(sharedLink.get(), msg);
    }
    qCDebug(controlSurfaceTestLog) << "RC override: release ALL channels";
}

/// Pins RCMAP_ROLL / RCMAP_PITCH / RCMAP_YAW to neutral (1500 µs) in ONE
/// RC_CHANNELS_OVERRIDE before a new command is applied.  This guarantees the
/// elevon mixer sees centered roll/pitch inputs while only the surface under
/// test is driven, so a stale or missing override cannot drag the wrong
/// surface (e.g. no elevator override while testing roll must not leave the
/// elevons deflected from a previous step).
void ControlSurfaceTestController::_sendCenterRcOverrides()
{
    if (!m_vehicle) return;
    const int rollCh  = _rcMapChannel(QStringLiteral("RCMAP_ROLL"),  QStringLiteral("RC_MAP_ROLL"),  -1);
    const int pitchCh = _rcMapChannel(QStringLiteral("RCMAP_PITCH"), QStringLiteral("RC_MAP_PITCH"), -1);
    const int yawCh   = _rcMapChannel(QStringLiteral("RCMAP_YAW"),   QStringLiteral("RC_MAP_YAW"),   -1);

    QVector<QPair<int,int>> centers;
    if (rollCh > 0)  centers.append({ rollCh,  1500 });
    if (pitchCh > 0) centers.append({ pitchCh, 1500 });
    if (yawCh > 0)   centers.append({ yawCh,   1500 });
    if (!centers.isEmpty()) {
        _sendRcOverrideMulti(centers);
    }
}

/// Requests (or stops) SERVO_OUTPUT_RAW streaming from the flight controller so
/// the sweep can show live feedback of what the servo output channel actually
/// outputs during a sweep.  Mirrors HardwareTestController::_setServoStreaming().
void ControlSurfaceTestController::_setServoStreaming(bool enable)
{
    if (!m_vehicle) return;
    m_vehicle->sendMavCommand(
        m_vehicle->defaultComponentId(),
        MAV_CMD_SET_MESSAGE_INTERVAL,
        false,
        static_cast<float>(MAVLINK_MSG_ID_SERVO_OUTPUT_RAW),
        enable ? 100000.0f : -1.0f,
        0, 0, 0, 0, 0
    );
}

void ControlSurfaceTestController::confirmSurfaceDirection(const QString &surfaceId, bool correct)
{
    int idx = _findSurfaceIndex(surfaceId);
    if (idx < 0) return;

    m_streamTimer.stop();
    m_forceManualTimer.stop();
    _setServoStreaming(false);
    m_sweptPwm = 0;
    m_sweptPwm2 = 0;
    emit sweptPwmChanged();
    _clearRcOverrides();
    // Re-center the tested surface (and any paired channel) so it doesn't stay
    // pinned at the last sweep position.
    if (m_activeSurface >= 0 && m_activeSurface < m_surfaceList.size()) {
        _sendActivePwm(_sweepCenter(m_surfaceList[m_activeSurface]));
    }
    m_states[idx] = correct ? Pass : Fail;

    if (!correct) {
        m_lastErrorMessage = QStringLiteral("Surface moved in wrong direction — check servo reversals in flight controller");
        emit lastErrorMessageChanged();
    }

    DatabaseManager::instance().logSurfaceTestResult(
        0, surfaceId, m_surfaceList[idx].channel,
        m_sweepMinPwmActual, m_sweepMaxPwmActual,
        correct ? 1 : 0, correct ? QStringLiteral("PASS") : QStringLiteral("FAIL")
    );

    if (m_activeSurface == idx) {
        m_activeSurface = -1;
        emit activeSurfaceChanged();
    }

    emit surfaceStatesChanged();

    _setElevonGain(false);

    // The vehicle was armed purely for the bench sweep — disarm it now so the
    // rig is left safe and the EDF cannot be driven by a stray override.
    if (m_vehicle && m_vehicle->armed()) {
        m_vehicle->sendMavCommand(
            m_vehicle->defaultComponentId(),
            MAV_CMD_COMPONENT_ARM_DISARM, false,
            0.0f, 21196.0f, 0, 0, 0, 0, 0);
        m_isArmed = false;
        emit isArmedChanged();
    }
}

void ControlSurfaceTestController::stopAllSurfaces()
{
    // A sweep deferred on an arm ACK must be cancelled so it cannot auto-start.
    m_pendingSurfaceIndex = -1;

    m_stepTimer.stop();
    m_streamTimer.stop();
    m_forceManualTimer.stop();
    _setServoStreaming(false);
    m_sweptPwm = 0;
    m_sweptPwm2 = 0;
    emit sweptPwmChanged();
    // Cancel RC overrides (surfaces return to autopilot control) and re-center
    // every surface that was driven directly via DO_SET_SERVO (no RC input).
    _clearRcOverrides();
    for (const auto &surf : m_surfaceList) {
        if (surf.channel > 0 && surf.rcChannel <= 0) {
            _sendServoDirectMulti({{ surf.channel, surf.centerPwm }});
        }
    }
    m_activeSurface = -1;
    emit activeSurfaceChanged();

    _setElevonGain(false);

    // The vehicle was armed purely for the bench sweep — disarm it now.
    if (m_vehicle && m_vehicle->armed()) {
        m_vehicle->sendMavCommand(
            m_vehicle->defaultComponentId(),
            MAV_CMD_COMPONENT_ARM_DISARM, false,
            0.0f, 21196.0f, 0, 0, 0, 0, 0);
        m_isArmed = false;
        emit isArmedChanged();
    }
}

void ControlSurfaceTestController::_onCommandResult(int vehicleId, int targetComponent, int command, int ackResult, int failureCode)
{
    Q_UNUSED(vehicleId)
    Q_UNUSED(targetComponent)
    Q_UNUSED(failureCode)

    // Deferred surface-sweep start: arm ACKed → begin the pending sweep.
    // testSurface() requests a force-arm when the vehicle is disarmed and waits
    // for this ACK before starting, so the first RC override already reaches
    // the servos instead of being dropped while the vehicle is still disarmed.
    if (command == MAV_CMD_COMPONENT_ARM_DISARM && m_pendingSurfaceIndex >= 0) {
        if (ackResult == MAV_RESULT_ACCEPTED) {
            qCWarning(controlSurfaceTestLog) << "Arm ACKed — starting deferred surface sweep" << m_pendingSurfaceIndex;
            m_isArmed = true;
            emit isArmedChanged();
            int pendingIdx = m_pendingSurfaceIndex;
            m_pendingSurfaceIndex = -1;
            _startSurfaceSweep(pendingIdx);
        } else {
            qCWarning(controlSurfaceTestLog) << "Arm for surface sweep rejected:" << ackResult;
            m_lastErrorMessage = QStringLiteral("Arm command rejected — cannot move control surfaces while disarmed");
            emit lastErrorMessageChanged();
            m_pendingSurfaceIndex = -1;
        }
        return;
    }

    if (command == MAV_CMD_DO_SET_SERVO && ackResult != MAV_RESULT_ACCEPTED && m_activeSurface >= 0) {
        qWarning() << "MAV_CMD_DO_SET_SERVO rejected with result:" << ackResult;
    }
}

void ControlSurfaceTestController::_onMavlinkMessage(const mavlink_message_t &message)
{
    if (message.msgid == MAVLINK_MSG_ID_SERVO_OUTPUT_RAW) {
        mavlink_servo_output_raw_t raw;
        mavlink_msg_servo_output_raw_decode(&message, &raw);

        // Always keep a live CH1-CH4 readout so the operator can see which
        // outputs the flight controller is driving (even outside a sweep).
        const QString rawStr = QStringLiteral("CH1=%1 CH2=%2 CH3=%3 CH4=%4")
            .arg(raw.servo1_raw).arg(raw.servo2_raw).arg(raw.servo3_raw).arg(raw.servo4_raw);
        if (rawStr != m_servoOutputsRaw) {
            m_servoOutputsRaw = rawStr;
            emit servoOutputsRawChanged();
        }

        if (m_activeSurface >= 0) {
            const auto &surf = m_surfaceList[m_activeSurface];
            auto pwmOnChannel = [&raw](int ch) -> uint16_t {
                switch (ch) {
                case 1:  return raw.servo1_raw;
                case 2:  return raw.servo2_raw;
                case 3:  return raw.servo3_raw;
                case 4:  return raw.servo4_raw;
                case 5:  return raw.servo5_raw;
                case 6:  return raw.servo6_raw;
                case 7:  return raw.servo7_raw;
                case 8:  return raw.servo8_raw;
                case 9:  return raw.servo9_raw;
                case 10: return raw.servo10_raw;
                case 11: return raw.servo11_raw;
                case 12: return raw.servo12_raw;
                case 13: return raw.servo13_raw;
                case 14: return raw.servo14_raw;
                case 15: return raw.servo15_raw;
                case 16: return raw.servo16_raw;
                default: return 0;
                }
            };

            const uint16_t pwm  = pwmOnChannel(surf.channel);
            const uint16_t pwm2 = (surf.channel2 > 0) ? pwmOnChannel(surf.channel2) : 0;

            if (pwm > 800 && pwm < 2200) {
                if (pwm < m_sweepMinPwmActual) m_sweepMinPwmActual = pwm;
                if (pwm > m_sweepMaxPwmActual) m_sweepMaxPwmActual = pwm;
            }
            if (pwm2 > 800 && pwm2 < 2200) {
                if (pwm2 < m_sweepMinPwmActual2) m_sweepMinPwmActual2 = pwm2;
                if (pwm2 > m_sweepMaxPwmActual2) m_sweepMaxPwmActual2 = pwm2;
            }

            // Phase comparison: with both elevon channels actually moving, do they
            // deflect in OPPOSITE (aileron-roll) or the SAME (pitch) direction?
            if (surf.channel2 > 0 && pwm > 800 && pwm < 2200 && pwm2 > 800 && pwm2 < 2200) {
                const int d1 = static_cast<int>(pwm)  - surf.centerPwm;
                const int d2 = static_cast<int>(pwm2) - surf.centerPwm;
                if (qAbs(d1) > 100 && qAbs(d2) > 100) {
                    if ((d1 < 0 && d2 > 0) || (d1 > 0 && d2 < 0)) {
                        m_phaseOppositeCount++;
                    } else {
                        m_phaseSameCount++;
                    }
                }
            }

            if (pwm != m_sweptPwm || pwm2 != m_sweptPwm2) {
                m_sweptPwm = pwm;
                m_sweptPwm2 = pwm2;
                emit sweptPwmChanged();
            }

            _updateSweepVerdict();
        }
    }
}

/// Builds the human-readable sweep verdict from the observed servo output
/// ranges.  Lets the operator see at a glance whether the flight controller is
/// driving both elevon channels and in which relative direction, instead of
/// having to judge it from tiny servo movements.
void ControlSurfaceTestController::_updateSweepVerdict()
{
    if (m_activeSurface < 0 || m_activeSurface >= m_surfaceList.size()) {
        if (!m_sweepVerdict.isEmpty()) {
            m_sweepVerdict.clear();
            emit sweepVerdictChanged();
        }
        return;
    }

    const auto &surf = m_surfaceList[m_activeSurface];

    QString verdict;
    if (m_sweepMinPwmActual < m_sweepMaxPwmActual) {
        verdict += QStringLiteral("CH%1 %2→%3").arg(surf.channel).arg(m_sweepMinPwmActual).arg(m_sweepMaxPwmActual);
        if (surf.channel2 > 0) {
            verdict += QStringLiteral("  CH%1 %2→%3")
                .arg(surf.channel2).arg(m_sweepMinPwmActual2).arg(m_sweepMaxPwmActual2);
        }
    }

    if (surf.channel2 > 0 && (m_phaseOppositeCount > 0 || m_phaseSameCount > 0)) {
        if (surf.id == QStringLiteral("aileron")) {
            verdict += m_phaseOppositeCount > m_phaseSameCount
                ? QStringLiteral("  ✓ OPPOSITE (roll)")
                : QStringLiteral("  ✗ SAME (roll lost)");
        } else if (surf.id == QStringLiteral("elevator")) {
            verdict += QStringLiteral("  SAME (pitch ok)");
        } else {
            // elevon_l / elevon_r isolation — the non-target elevon must stay put.
            const int otherRange = m_sweepMaxPwmActual2 - m_sweepMinPwmActual2;
            verdict += otherRange < 100
                ? QStringLiteral("  ✓ other STAYS")
                : QStringLiteral("  ✗ other MOVES");
        }
    }

    if (verdict != m_sweepVerdict) {
        m_sweepVerdict = verdict;
        emit sweepVerdictChanged();
    }
}
