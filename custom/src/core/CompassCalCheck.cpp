#include "CompassCalCheck.h"

#include "TelemetryBridge.h"

CompassCalCheck::CompassCalCheck(TelemetryBridge *telemetry,
                                 int maxDeviation, QObject *parent)
    : AbstractCheck(QStringLiteral("airframe.compass.cal"),
                    QStringLiteral("Compass Calibration"),
                    CheckCategory::Airframe, CheckType::Auto, false, false, parent)
    , m_maxDeviation(maxDeviation)
{
    m_telemetry = telemetry;
}

void CompassCalCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    bool healthy = getTelemetryBool(QStringLiteral("compassHealthy"));
    QVariant qualityVar = getTelemetryVariant(QStringLiteral("compassDataQuality"));

    setCurrentValue(healthy);

    if (!healthy) {
        setStatus(CheckStatus::Warning, QStringLiteral("Compass unhealthy — sensor may be initializing"));
        return;
    }

    if (!qualityVar.isValid()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("Compass quality data not reported"));
        return;
    }

    int quality = qualityVar.toInt();
    if (quality < 2) {
        setStatus(CheckStatus::Warning, QStringLiteral("Compass data quality low"));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Compass healthy — quality %1").arg(quality));
}
