#include "AccelConsistencyCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

AccelConsistencyCheck::AccelConsistencyCheck(TelemetryBridge *telemetry,
                                             double maxDiff,
                                             QObject *parent)
    : AbstractCheck(QStringLiteral("imu.accel.consistency"),
                    QStringLiteral("Accelerometer Consistency"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_maxDiff(maxDiff)
{
    m_telemetry = telemetry;
}

// Fail if max per-axis difference between IMU1 and IMU2 exceeds m_maxDiff m/s^2.
void AccelConsistencyCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double ax1 = getTelemetryDouble(QStringLiteral("accelerometerX"));
    double ay1 = getTelemetryDouble(QStringLiteral("accelerometerY"));
    double az1 = getTelemetryDouble(QStringLiteral("accelerometerZ"));
    double ax2 = getTelemetryDouble(QStringLiteral("accelerometer2X"));
    double ay2 = getTelemetryDouble(QStringLiteral("accelerometer2Y"));
    double az2 = getTelemetryDouble(QStringLiteral("accelerometer2Z"));

    if (qIsNaN(ax1) || qIsNaN(ax2)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for both IMUs"));
        return;
    }

    double dx = qAbs(ax1 - ax2);
    double dy = qAbs(ay1 - ay2);
    double dz = qAbs(az1 - az2);
    double maxDiffVal = qMax(dx, qMax(dy, dz));

    setCurrentValue(maxDiffVal);

    if (maxDiffVal <= m_maxDiff) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("IMU Δ %1 m/s² — OK").arg(maxDiffVal, 0, 'f', 2));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("IMU Δ %1 m/s² exceeds %2 m/s²")
                      .arg(maxDiffVal, 0, 'f', 2).arg(m_maxDiff, 0, 'f', 1));
    }
}
