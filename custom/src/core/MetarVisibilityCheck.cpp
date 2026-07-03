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

    if (settings && !settings->autoWeatherEnabled()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("Auto-weather disabled in settings"));
        return;
    }

    if (!wp) {
        setStatus(CheckStatus::Skipped, QStringLiteral("Weather service unavailable"));
        return;
    }

    if (!wp->metarFresh()) {
        if (!m_fetchTriggered) {
            m_fetchTriggered = true;
            if (settings && !settings->defaultIcao().isEmpty()) {
                wp->fetchMetar(settings->defaultIcao());
            } else if (m_telemetry) {
                double lat = getTelemetryDouble("gpsLatitude");
                double lon = getTelemetryDouble("gpsLongitude");
                if (qAbs(lat) > 0.01 || qAbs(lon) > 0.01)
                    wp->fetchWeather(lat, lon);
            }
            setStatus(CheckStatus::Pending, QStringLiteral("Fetching METAR data\u2026"));
        }

        if (m_lastEvalTime.isValid() && m_lastEvalTime.secsTo(QDateTime::currentDateTime()) > 10) {
            // Timeout: check for cached data
            if (wp->visibilityKm() > 0.0) {
                double visKm = wp->visibilityKm();
                double threshold = settings ? settings->visibilityThresholdKm() : 5.0;
                if (visKm < 1.0) {
                    setStatus(CheckStatus::Failed,
                              QStringLiteral("Visibility %1 km — below minimum — STALE data").arg(visKm, 0, 'f', 1));
                } else if (visKm < threshold) {
                    setStatus(CheckStatus::Warning,
                              QStringLiteral("Visibility %1 km — below %2 km threshold — STALE data")
                                  .arg(visKm, 0, 'f', 1).arg(threshold, 0, 'f', 1));
                } else {
                    setStatus(CheckStatus::Warning,
                              QStringLiteral("Visibility %1 km — STALE data").arg(visKm, 0, 'f', 1));
                }
            } else {
                m_fetchTriggered = false;
                QString err = wp->lastError();
                if (!err.isEmpty())
                    setStatus(CheckStatus::Skipped, QStringLiteral("Weather fetch failed: ") + err);
                else
                    setStatus(CheckStatus::Skipped, QStringLiteral("No weather data \u2014 configure ICAO in Preflight Settings"));
            }
        } else {
            setStatus(CheckStatus::Pending, QStringLiteral("Fetching METAR data…"));
        }
        return;
    }

    m_fetchTriggered = false;

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
