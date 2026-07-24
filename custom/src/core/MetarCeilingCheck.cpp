#include "MetarCeilingCheck.h"

#include "PreflightSettingsManager.h"
#include "TelemetryBridge.h"
#include "WeatherProvider.h"

MetarCeilingCheck::MetarCeilingCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("env.ceiling"),
                    QStringLiteral("Cloud Ceiling (METAR)"),
                    CheckCategory::Environment, CheckType::Auto,
                    true, true, parent)
{
    m_telemetry = telemetry;
}

// Pass: no ceiling reported (clear skies) or ceiling >= configured threshold.
// Warning: ceiling below threshold. Fail: ceiling below 60 m absolute minimum.
void MetarCeilingCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    auto *wp = WeatherProvider::instance();
    auto *settings = PreflightSettingsManager::instance();

    // If settings not yet initialized, treat as enabled (default is true)
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
            if (wp->ceilingFt() > 0 || wp->visibilityKm() > 0) {
                // Use cached data with stale warning
                double ceilingFt = wp->ceilingFt();
                if (ceilingFt <= 0) {
                    setStatus(CheckStatus::Warning, QStringLiteral("Ceiling data stale — clear skies (cached)"));
                } else {
                    double ceilingM = ceilingFt * 0.3048;
                    setStatus(CheckStatus::Warning,
                              QStringLiteral("Ceiling %1 ft (%2 m) — STALE data")
                                  .arg(ceilingFt, 0, 'f', 0).arg(ceilingM, 0, 'f', 0));
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

    double ceilingFt = wp->ceilingFt();
    if (ceilingFt <= 0) {
        setStatus(CheckStatus::Passed, QStringLiteral("No ceiling reported — clear skies"));
        return;
    }

    double ceilingM = ceilingFt * 0.3048;
    double thresholdM = settings->ceilingThresholdM();

    if (ceilingM < 60.0) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Ceiling %1 ft (%2 m) — below minimum")
                      .arg(ceilingFt, 0, 'f', 0).arg(ceilingM, 0, 'f', 0));
        return;
    }
    if (ceilingM < thresholdM) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Ceiling %1 ft (%2 m) — below %3 m threshold")
                      .arg(ceilingFt, 0, 'f', 0).arg(ceilingM, 0, 'f', 0)
                      .arg(thresholdM, 0, 'f', 0));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Ceiling %1 ft (%2 m) — OK")
                  .arg(ceilingFt, 0, 'f', 0).arg(ceilingM, 0, 'f', 0));
}
