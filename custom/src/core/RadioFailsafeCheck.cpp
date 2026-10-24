#include "RadioFailsafeCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

RadioFailsafeCheck::RadioFailsafeCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("safety.failsafe.radio"),
                    QStringLiteral("Radio Failsafe"),
                    CheckCategory::Safety, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

double RadioFailsafeCheck::paramOr(const QString &name, double fallback) const
{
    const QString key = QStringLiteral("param_") + name;
    return isParamAvailable(key) ? getTelemetryDouble(key) : fallback;
}

QString RadioFailsafeCheck::planeShortActionName(int value)
{
    switch (value) {
    case 0: return QStringLiteral("circle (or no change if already in AUTO/GUIDED/LOITER)");
    case 1: return QStringLiteral("circle");
    case 2: return QStringLiteral("FBWA at zero throttle");
    case 3: return QStringLiteral("no short action");
    case 4: return QStringLiteral("FBWB");
    default: break;
    }
    return QStringLiteral("unknown value (%1)").arg(value);
}

QString RadioFailsafeCheck::planeLongActionName(int value)
{
    switch (value) {
    case 0: return QStringLiteral("continue (RTL if it was in a manual or stabilised mode)");
    case 1: return QStringLiteral("return to launch");
    case 2: return QStringLiteral("glide");
    case 3: return QStringLiteral("deploy parachute");
    case 4: return QStringLiteral("AUTO");
    default: break;
    }
    return QStringLiteral("unknown value (%1)").arg(value);
}

QString RadioFailsafeCheck::copterThrottleFailsafeName(int value)
{
    switch (value) {
    case 0: return QStringLiteral("disabled");
    case 1: return QStringLiteral("always RTL");
    case 2: return QStringLiteral("continue mission in AUTO (removed in 4.0+)");
    case 3: return QStringLiteral("always land");
    case 4: return QStringLiteral("SmartRTL or RTL");
    case 5: return QStringLiteral("SmartRTL or land");
    case 6: return QStringLiteral("AUTO DO_LAND_START or RTL");
    default: break;
    }
    return QStringLiteral("unknown value (%1)").arg(value);
}

void RadioFailsafeCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }
    // Which firmware we are talking to is decided by which parameter exists:
    // only ArduPlane has THR_FAILSAFE, only ArduCopter has FS_THR_ENABLE. That
    // avoids guessing from the vehicle type string.
    if (isParamAvailable(QStringLiteral("param_THR_FAILSAFE"))) {
        evaluatePlane();
        return;
    }
    if (isParamAvailable(QStringLiteral("param_FS_THR_ENABLE"))) {
        evaluateCopter();
        return;
    }
    setStatus(CheckStatus::Skipped,
              QStringLiteral("RC failsafe parameters (THR_FAILSAFE / FS_THR_ENABLE) not available from vehicle"));
}

void RadioFailsafeCheck::evaluatePlane()
{
    const int enable = qRound(getTelemetryDouble(QStringLiteral("param_THR_FAILSAFE")));
    if (enable == 0) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("RC failsafe disabled (THR_FAILSAFE = 0): if the RC link is lost the plane "
                                 "keeps flying on whatever it last received"));
        return;
    }
    if (enable == 2) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("THR_FAILSAFE = 2: RC loss is detected but no failsafe action is taken"));
        return;
    }

    // ArduPlane treats a throttle value below THR_FS_VALUE as "RC lost", so the
    // value must sit below the lowest throttle the radio normally sends, or the
    // failsafe would fire at idle. The throttle channel is RCMAP_THROTTLE.
    const int thrChannel = qRound(paramOr(QStringLiteral("RCMAP_THROTTLE"), 3));
    const double fsValue = paramOr(QStringLiteral("THR_FS_VALUE"), -1);
    const double thrMin = paramOr(QStringLiteral("RC%1_MIN").arg(thrChannel), -1);
    if (fsValue > 0 && thrMin > 0 && fsValue >= thrMin) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("THR_FS_VALUE (%1) is not below the throttle minimum RC%2_MIN (%3): "
                                 "the failsafe could trigger at idle throttle")
                      .arg(fsValue, 0, 'f', 0).arg(thrChannel).arg(thrMin, 0, 'f', 0));
        return;
    }

    const int shortAct = qRound(paramOr(QStringLiteral("FS_SHORT_ACTN"), -1));
    const int longAct = qRound(paramOr(QStringLiteral("FS_LONG_ACTN"), -1));
    const double shortT = paramOr(QStringLiteral("FS_SHORT_TIMEOUT"), -1);
    const double longT = paramOr(QStringLiteral("FS_LONG_TIMEOUT"), -1);
    QString detail;
    if (shortAct >= 0 && shortT > 0)
        detail += QStringLiteral(" after %1 s: %2;").arg(shortT, 0, 'f', 1).arg(planeShortActionName(shortAct));
    if (longAct >= 0 && longT > 0)
        detail += QStringLiteral(" after %1 s: %2").arg(longT, 0, 'f', 0).arg(planeLongActionName(longAct));
    setStatus(CheckStatus::Passed, QStringLiteral("RC failsafe on (THR_FAILSAFE = 1).%1").arg(detail));
}

void RadioFailsafeCheck::evaluateCopter()
{
    const int enable = qRound(getTelemetryDouble(QStringLiteral("param_FS_THR_ENABLE")));
    if (enable == 0) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("RC failsafe disabled (FS_THR_ENABLE = 0)"));
        return;
    }
    const double fsValue = paramOr(QStringLiteral("FS_THR_VALUE"), -1);
    const int thrChannel = qRound(paramOr(QStringLiteral("RCMAP_THROTTLE"), 3));
    const double thrMin = paramOr(QStringLiteral("RC%1_MIN").arg(thrChannel), -1);
    if (fsValue > 0 && thrMin > 0 && fsValue >= thrMin) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("FS_THR_VALUE (%1) is not below the throttle minimum RC%2_MIN (%3): "
                                 "the failsafe could trigger at idle throttle")
                      .arg(fsValue, 0, 'f', 0).arg(thrChannel).arg(thrMin, 0, 'f', 0));
        return;
    }
    setStatus(CheckStatus::Passed,
              QStringLiteral("RC failsafe on: %1 (FS_THR_ENABLE = %2)").arg(copterThrottleFailsafeName(enable)).arg(enable));
}

QStringList RadioFailsafeCheck::getFixSteps() const
{
    return {
        QStringLiteral("On the receiver (ELRS): set its failsafe to \"No pulses\" where it offers one (PWM "
                       "receivers), so a lost link makes the receiver stop sending instead of holding the last "
                       "sticks or a fixed throttle. A serial / CRSF receiver stops sending by itself."),
        QStringLiteral("Plane: set THR_FAILSAFE = 1. Copter: set FS_THR_ENABLE to the action you want (1 = RTL)."),
        QStringLiteral("Plane: set FS_SHORT_ACTN and FS_LONG_ACTN to the actions you want (FS_LONG_ACTN = 1 is "
                       "return to launch)."),
        QStringLiteral("Keep THR_FS_VALUE (plane) / FS_THR_VALUE (copter) well below the throttle minimum RC3_MIN."),
        QStringLiteral("Test it with propellers off: switch the transmitter off; QGC must show an RC failsafe and "
                       "the motor must not spin."),
    };
}
