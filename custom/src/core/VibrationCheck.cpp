#include "VibrationCheck.h"

#include "TelemetryBridge.h"

VibrationCheck::VibrationCheck(TelemetryBridge *telemetry,
                               double maxVibration,
                               QObject *parent)
    : AbstractCheck(QStringLiteral("nav.imu.vibration"),
                    QStringLiteral("Vibration"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_maxVibration(maxVibration)
{
    m_telemetry = telemetry;
}

void VibrationCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double x = getTelemetryDouble(QStringLiteral("vibrationX"));
    double y = getTelemetryDouble(QStringLiteral("vibrationY"));
    double z = getTelemetryDouble(QStringLiteral("vibrationZ"));
    uint clipping = static_cast<uint>(getTelemetryDouble(QStringLiteral("vibrationClipping")));

    if (clipping != 0) {
        setStatus(CheckStatus::Failed, QStringLiteral("Clipping detected"));
        return;
    }

    if (qFuzzyIsNull(x) && qFuzzyIsNull(y) && qFuzzyIsNull(z)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for vibration data"));
        return;
    }

    double threshold = isParamAvailable(QStringLiteral("param_COM_ARM_VIBE"))
        ? getTelemetryDouble(QStringLiteral("param_COM_ARM_VIBE"))
        : m_maxVibration;

    bool xOk = x <= threshold;
    bool yOk = y <= threshold;
    bool zOk = z <= threshold;

    if (xOk && yOk && zOk) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("X%1 Y%2 Z%3")
                      .arg(x, 0, 'f', 1)
                      .arg(y, 0, 'f', 1)
                      .arg(z, 0, 'f', 1));
    } else {
        QStringList issues;
        if (!xOk) issues << QStringLiteral("X=%1").arg(x, 0, 'f', 1);
        if (!yOk) issues << QStringLiteral("Y=%1").arg(y, 0, 'f', 1);
        if (!zOk) issues << QStringLiteral("Z=%1").arg(z, 0, 'f', 1);
        setStatus(CheckStatus::Failed, issues.join(QStringLiteral(", ")));
    }
}

QString VibrationCheck::getRationale() const
{
    return QStringLiteral("Excessive vibration degrades IMU measurements, causing unreliable "
                          "attitude estimation, EKF drift, and potential in-flight instability.");
}

QStringList VibrationCheck::getFixSteps() const
{
    return {
        QStringLiteral("Balance all propeller and motor assemblies"),
        QStringLiteral("Use vibration-dampening mounts for the flight controller"),
        QStringLiteral("Tighten loose hardware — screws, standoffs, frame arms"),
        QStringLiteral("Verify propellers are not bent, chipped, or unbalanced"),
        QStringLiteral("Adjust COM_ARM_VIBE parameter if vibration threshold is too tight")
    };
}

QString VibrationCheck::getThreshold() const
{
    return QStringLiteral("X,Y,Z ≤%1").arg(m_maxVibration, 0, 'f', 1);
}

QString VibrationCheck::getCurrentValueString() const
{
    double x = getTelemetryDouble(QStringLiteral("vibrationX"));
    double y = getTelemetryDouble(QStringLiteral("vibrationY"));
    double z = getTelemetryDouble(QStringLiteral("vibrationZ"));
    return QStringLiteral("X%1  Y%2  Z%3")
        .arg(x, 0, 'f', 1).arg(y, 0, 'f', 1).arg(z, 0, 'f', 1);
}
