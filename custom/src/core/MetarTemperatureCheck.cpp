#include "MetarTemperatureCheck.h"

#include "PreflightSettingsManager.h"
#include "TelemetryBridge.h"
#include "WeatherProvider.h"

// Config keys (stored in check_config table, populated by AbstractCheck::configDouble)
static const QString kCfgTempMin = QStringLiteral("temp_min_c");
static const QString kCfgTempMax = QStringLiteral("temp_max_c");

MetarTemperatureCheck::MetarTemperatureCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("env.temperature"),
                    QStringLiteral("Ambient Temperature (METAR)"),
                    CheckCategory::Environment, CheckType::Auto,
                    false, true, parent)
{
    m_telemetry = telemetry;
}

void MetarTemperatureCheck::evaluate()
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
            if (wp->temperature() > -100.0) {
                double temp = wp->temperature();
                double tMin = configDouble(kCfgTempMin, -10.0);
                double tMax = configDouble(kCfgTempMax, 50.0);
                if (temp < tMin || temp > tMax) {
                    setStatus(CheckStatus::Warning,
                              QStringLiteral("Temperature %1 \u00B0C outside operating range (%2 to %3 \u00B0C) — STALE data")
                                  .arg(temp, 0, 'f', 1).arg(tMin, 0, 'f', 0)
                                  .arg(tMax, 0, 'f', 0));
                } else {
                    setStatus(CheckStatus::Warning,
                              QStringLiteral("Temperature %1 \u00B0C — STALE data").arg(temp, 0, 'f', 1));
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

    double temp = wp->temperature();
    double tMin = configDouble(kCfgTempMin, -10.0);
    double tMax = configDouble(kCfgTempMax, 50.0);

    if (temp < tMin || temp > tMax) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Temperature %1 \u00B0C outside operating range (%2 to %3 \u00B0C)")
                      .arg(temp, 0, 'f', 1).arg(tMin, 0, 'f', 0)
                      .arg(tMax, 0, 'f', 0));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Temperature %1 \u00B0C — OK").arg(temp, 0, 'f', 1));
}
