#include "GyroBiasCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

GyroBiasCheck::GyroBiasCheck(TelemetryBridge *telemetry,
                             double maxBiasRadS,
                             QObject *parent)
    : AbstractCheck(QStringLiteral("nav.imu.gyro_bias"),
                    QStringLiteral("Gyro Bias"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
    , m_maxBiasRadS(maxBiasRadS)
{
    m_telemetry = telemetry;
}

void GyroBiasCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double gx = getTelemetryDouble(QStringLiteral("gyroX"));
    double gy = getTelemetryDouble(QStringLiteral("gyroY"));
    double gz = getTelemetryDouble(QStringLiteral("gyroZ"));

    if (qIsNaN(gx) || qIsNaN(gy) || qIsNaN(gz)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for gyro data"));
        return;
    }

    // Pass if the gyro bias vector magnitude is within threshold; warn if it exceeds it.
    double bias = qSqrt(gx * gx + gy * gy + gz * gz);

    if (bias <= m_maxBiasRadS) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Bias %1 rad/s — OK").arg(bias, 0, 'f', 4));
        return;
    }

    setStatus(CheckStatus::Warning,
              QStringLiteral("Bias %1 rad/s exceeds %2 rad/s")
                  .arg(bias, 0, 'f', 4)
                  .arg(m_maxBiasRadS, 0, 'f', 2));
}
