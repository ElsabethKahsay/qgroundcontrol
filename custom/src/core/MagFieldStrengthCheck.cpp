#include "MagFieldStrengthCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

MagFieldStrengthCheck::MagFieldStrengthCheck(TelemetryBridge *telemetry,
                                             double minGauss,
                                             double maxGauss,
                                             QObject *parent)
    : AbstractCheck(QStringLiteral("nav.mag.field_strength"),
                    QStringLiteral("Mag Field Strength"),
                    CheckCategory::Navigation, CheckType::Auto, true, false, parent)
    , m_minGauss(minGauss)
    , m_maxGauss(maxGauss)
{
    m_telemetry = telemetry;
}

// Fail if computed field magnitude is outside the min-max range; pass if within bounds.
void MagFieldStrengthCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double x = getTelemetryDouble(QStringLiteral("magFieldX"));
    double y = getTelemetryDouble(QStringLiteral("magFieldY"));
    double z = getTelemetryDouble(QStringLiteral("magFieldZ"));

    if (qIsNaN(x) || qIsNaN(y) || qIsNaN(z)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for mag data"));
        return;
    }

    if (qFuzzyIsNull(x) && qFuzzyIsNull(y) && qFuzzyIsNull(z)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for mag data"));
        return;
    }

    double fieldStrength = qSqrt(x * x + y * y + z * z);

    // Param-driven thresholds
    double minG = m_minGauss;
    double maxG = m_maxGauss;

    // PX4: COM_ARM_MAG_STR is the max field strength for arming
    if (isParamAvailable(QStringLiteral("param_COM_ARM_MAG_STR"))) {
        double magStr = getTelemetryDouble(QStringLiteral("param_COM_ARM_MAG_STR"));
        if (magStr > 0.01)
            maxG = magStr;
    }

    // COMPASS_MAG_FIELD is the expected local field magnitude
    if (isParamAvailable(QStringLiteral("param_COMPASS_MAG_FIELD"))) {
        double expectedField = getTelemetryDouble(QStringLiteral("param_COMPASS_MAG_FIELD"));
        if (expectedField > 0.01) {
            // Use ±20% of expected field per SRS NAV-009
            minG = expectedField * 0.8;
            maxG = expectedField * 1.2;
        }
    }

    if (fieldStrength >= minG && fieldStrength <= maxG) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("%1 G — OK").arg(fieldStrength, 0, 'f', 3));
    } else {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("%1 G (%2-%3 G) — interference detected")
                      .arg(fieldStrength, 0, 'f', 3)
                      .arg(minG, 0, 'f', 2)
                      .arg(maxG, 0, 'f', 2));
    }
}

QString MagFieldStrengthCheck::getRationale() const
{
    return QStringLiteral("Magnetic field strength must be within expected range. Extreme values "
                          "indicate magnetic interference or incorrect compass orientation.");
}

QStringList MagFieldStrengthCheck::getFixSteps() const
{
    return {
        QStringLiteral("Move vehicle away from power lines, rebar, or vehicles"),
        QStringLiteral("Check compass orientation parameter (CAL_MAG0_ROT / COMPASS_ORIENT)"),
        QStringLiteral("Recalibrate compass in a magnetically clean area"),
        QStringLiteral("Verify COM_ARM_MAG_STR threshold is appropriate for your region")
    };
}

QString MagFieldStrengthCheck::getThreshold() const
{
    double minG = m_minGauss;
    double maxG = m_maxGauss;
    return QStringLiteral("%1–%2 G").arg(minG, 0, 'f', 2).arg(maxG, 0, 'f', 2);
}
