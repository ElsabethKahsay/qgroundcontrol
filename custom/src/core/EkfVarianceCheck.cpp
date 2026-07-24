#include "EkfVarianceCheck.h"

#include "TelemetryBridge.h"

EkfVarianceCheck::EkfVarianceCheck(TelemetryBridge *telemetry,
                                   double maxVelVariance,
                                   double maxPosHorizVariance,
                                   double maxPosVertVariance,
                                   QObject *parent)
    : AbstractCheck(QStringLiteral("nav.ekf.variance"),
                    QStringLiteral("EKF Variance"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_maxVelVariance(maxVelVariance)
    , m_maxPosHorizVariance(maxPosHorizVariance)
    , m_maxPosVertVariance(maxPosVertVariance)
{
    m_telemetry = telemetry;
}

// Passes when all variance values (vel, horiz, vert, compass) are below their thresholds.
// Fails listing each exceeded variance; uses vehicle COM_ARM_EKF_* params if available, else defaults.
void EkfVarianceCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double velVar = getTelemetryDouble(QStringLiteral("ekfVelVariance"));
    double posHorizVar = getTelemetryDouble(QStringLiteral("ekfPosHorizVariance"));
    double posVertVar = getTelemetryDouble(QStringLiteral("ekfPosVertVariance"));
    double compassVar = getTelemetryDouble(QStringLiteral("ekfCompassVariance"));

    // Check for "all zeros" — means EKF not publishing yet
    if (qFuzzyIsNull(velVar) && qFuzzyIsNull(posHorizVar) && qFuzzyIsNull(posVertVar)) {
        setStatus(CheckStatus::Pending, QStringLiteral("EKF converging"));
        return;
    }

    // Use param-driven thresholds where available
    double maxVel = m_maxVelVariance;
    double maxPosHoriz = m_maxPosHorizVariance;
    double maxPosVert = m_maxPosVertVariance;

    if (isParamAvailable(QStringLiteral("param_COM_ARM_EKF_VEL")))
        maxVel = getTelemetryDouble(QStringLiteral("param_COM_ARM_EKF_VEL"));
    if (isParamAvailable(QStringLiteral("param_COM_ARM_EKF_POS")))
        maxPosHoriz = getTelemetryDouble(QStringLiteral("param_COM_ARM_EKF_POS"));
    if (isParamAvailable(QStringLiteral("param_COM_ARM_EKF_HGT")))
        maxPosVert = getTelemetryDouble(QStringLiteral("param_COM_ARM_EKF_HGT"));

    bool velOk = velVar <= maxVel;
    bool posHorizOk = posHorizVar <= maxPosHoriz;
    bool posVertOk = posVertVar <= maxPosVert;
    bool compassOk = compassVar <= 2.0;

    if (velOk && posHorizOk && posVertOk && compassOk) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Vel %1 Hor %2 Vert %3")
                      .arg(velVar, 0, 'f', 3)
                      .arg(posHorizVar, 0, 'f', 2)
                      .arg(posVertVar, 0, 'f', 2));
    } else {
        QStringList issues;
        if (!velOk) issues << QStringLiteral("Vel=%1").arg(velVar, 0, 'f', 3);
        if (!posHorizOk) issues << QStringLiteral("Horiz=%1").arg(posHorizVar, 0, 'f', 2);
        if (!posVertOk) issues << QStringLiteral("Vert=%1").arg(posVertVar, 0, 'f', 2);
        if (!compassOk) issues << QStringLiteral("Compass=%1").arg(compassVar, 0, 'f', 2);
        setStatus(CheckStatus::Failed, issues.join(QStringLiteral(", ")));
    }
}

QString EkfVarianceCheck::getRationale() const
{
    return QStringLiteral("EKF variance values indicate the estimated uncertainty of the navigation "
                          "solution. High variance means the state estimate is unreliable, which can "
                          "lead to unstable flight or position drift.");
}

QStringList EkfVarianceCheck::getFixSteps() const
{
    return {
        QStringLiteral("Ensure GPS has a strong 3D lock before arming"),
        QStringLiteral("Verify the magnetometer is calibrated and not disturbed"),
        QStringLiteral("Check for excessive vibration affecting the IMU"),
        QStringLiteral("Allow the EKF to converge after power-on — wait 60+ seconds"),
        QStringLiteral("Review COM_ARM_EKF_VEL, COM_ARM_EKF_POS, COM_ARM_EKF_HGT parameters")
    };
}

QString EkfVarianceCheck::getThreshold() const
{
    return QStringLiteral("Vel ≤%1, Horiz ≤%2, Vert ≤%3")
        .arg(m_maxVelVariance, 0, 'f', 3)
        .arg(m_maxPosHorizVariance, 0, 'f', 2)
        .arg(m_maxPosVertVariance, 0, 'f', 2);
}

QString EkfVarianceCheck::getCurrentValueString() const
{
    double velVar = getTelemetryDouble(QStringLiteral("ekfVelVariance"));
    double posHorizVar = getTelemetryDouble(QStringLiteral("ekfPosHorizVariance"));
    double posVertVar = getTelemetryDouble(QStringLiteral("ekfPosVertVariance"));
    return QStringLiteral("Vel %1  Hor %2  Vert %3")
        .arg(velVar, 0, 'f', 3)
        .arg(posHorizVar, 0, 'f', 2)
        .arg(posVertVar, 0, 'f', 2);
}
