#include "MetarVisibilityCheck.h"

#include "PreflightSettingsManager.h"
#include "TelemetryBridge.h"
#include "WeatherProvider.h"

MetarVisibilityCheck::MetarVisibilityCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("env.visibility"),
                    QStringLiteral("Visibility (METAR)"),
                    CheckCategory::Environment, CheckType::Auto,
                    true, true, parent)
{
    m_telemetry = telemetry;
}

void MetarVisibilityCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    auto *wp = WeatherProvider::instance();
    auto *settings = PreflightSettingsManager::instance();

    if (!wp || !settings || !settings->autoWeatherEnabled()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("Auto-weather disabled"));
        return;
    }

    if (!wp->metarFresh()) {
        if (m_lastEvalTime.isValid() && m_lastEvalTime.secsTo(QDateTime::currentDateTime()) > 30) {
            QString err = wp->lastError();
            if (!err.isEmpty())
                setStatus(CheckStatus::Skipped, QStringLiteral("Weather fetch failed: ") + err);
            else
                setStatus(CheckStatus::Skipped, QStringLiteral("No weather data — configure ICAO in Preflight Settings"));
        } else {
            setStatus(CheckStatus::Pending, QStringLiteral("Waiting for METAR data"));
        }
        return;
    }

    double visKm = wp->visibilityKm();
    double threshold = settings->visibilityThresholdKm();

    if (visKm <= 0.0) {
        setStatus(CheckStatus::Pending, QStringLiteral("Visibility not reported in METAR"));
        return;
    }

    if (visKm < 1.0) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Visibility %1 km — below minimum").arg(visKm, 0, 'f', 1));
        return;
    }
    if (visKm < threshold) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Visibility %1 km — below %2 km threshold")
                      .arg(visKm, 0, 'f', 1).arg(threshold, 0, 'f', 1));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Visibility %1 km — OK").arg(visKm, 0, 'f', 1));
}
