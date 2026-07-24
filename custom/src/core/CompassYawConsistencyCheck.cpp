#include "CompassYawConsistencyCheck.h"

#include "TelemetryBridge.h"

CompassYawConsistencyCheck::CompassYawConsistencyCheck(TelemetryBridge *telemetry,
                                                       double maxDeviationDeg,
                                                       QObject *parent)
    : AbstractCheck(QStringLiteral("nav.compass.yaw_consistency"),
                    QStringLiteral("Compass vs GPS Yaw"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_maxDeviationDeg(maxDeviationDeg)
{
    m_telemetry = telemetry;
}

// Fail if |heading - groundCourse| (wrapping at 360) exceeds maxDeviationDeg.
void CompassYawConsistencyCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double heading = getTelemetryDouble(QStringLiteral("heading"));
    double groundCourse = getTelemetryDouble(QStringLiteral("groundCourse"));

    if (qFuzzyIsNull(heading) || groundCourse <= -0.5) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for heading/GPS"));
        return;
    }

    double diff = qAbs(heading - groundCourse);
    if (diff > 180.0)
        diff = 360.0 - diff;

    if (diff <= m_maxDeviationDeg) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Diff %1 deg").arg(diff, 0, 'f', 1));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Diff %1 deg (max %2)")
                      .arg(diff, 0, 'f', 1)
                      .arg(m_maxDeviationDeg, 0, 'f', 1));
    }
}
