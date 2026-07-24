#include "BaroAltConsistencyCheck.h"

#include <QtMath>

#include "TelemetryBridge.h"

BaroAltConsistencyCheck::BaroAltConsistencyCheck(TelemetryBridge *telemetry,
                                                 double maxDeltaM, QObject *parent)
    : AbstractCheck(QStringLiteral("nav.baro.alt_consistency"),
                    QStringLiteral("GPS/Baro Alt Consistency"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
    , m_maxDeltaM(maxDeltaM)
{
    m_telemetry = telemetry;
}

void BaroAltConsistencyCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double gpsAlt = getTelemetryDouble(QStringLiteral("gpsAltitude"));
    double relAlt = getTelemetryDouble(QStringLiteral("altitudeRelative"));

    if (qIsNaN(gpsAlt) || qIsNaN(relAlt)) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for GPS/baro altitude data"));
        return;
    }

    // Pass if GPS and baro altitudes differ by no more than m_maxDeltaM; warn otherwise.
    double delta = qAbs(gpsAlt - relAlt);
    setCurrentValue(delta);

    if (delta <= m_maxDeltaM) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Δ%1 m — GPS %2 m / Baro %3 m — OK")
                      .arg(delta, 0, 'f', 1)
                      .arg(gpsAlt, 0, 'f', 1)
                      .arg(relAlt, 0, 'f', 1));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Δ%1 m exceeds %2 m — GPS/baro divergence")
                      .arg(delta, 0, 'f', 1).arg(m_maxDeltaM, 0, 'f', 1));
    }
}

QString BaroAltConsistencyCheck::getRationale() const
{
    return QStringLiteral("GPS altitude and barometric altitude should agree within a few meters. "
                          "Large divergence indicates GPS error, baro drift, "
                          "or incorrect geoid model settings.");
}

QStringList BaroAltConsistencyCheck::getFixSteps() const
{
    return {
        QStringLiteral("Check barometer vent hole is clear of debris"),
        QStringLiteral("Verify GPS antenna has clear sky view"),
        QStringLiteral("Allow GPS to settle for 30+ seconds after power-on"),
        QStringLiteral("Check for magnetic interference near baro sensor")
    };
}

QString BaroAltConsistencyCheck::getThreshold() const
{
    return QStringLiteral("Max Δ%1 m").arg(m_maxDeltaM, 0, 'f', 1);
}
