#include "MetarTemperatureCheck.h"

#include "PreflightSettingsManager.h"
#include "TelemetryBridge.h"
#include "WeatherProvider.h"

static constexpr double TEMP_MIN_C = -10.0;
static constexpr double TEMP_MAX_C = 45.0;

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

    double temp = wp->temperature();

    if (temp < TEMP_MIN_C || temp > TEMP_MAX_C) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Temperature %1 \u00B0C outside operating range (%2 to %3 \u00B0C)")
                      .arg(temp, 0, 'f', 1).arg(TEMP_MIN_C, 0, 'f', 0)
                      .arg(TEMP_MAX_C, 0, 'f', 0));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Temperature %1 \u00B0C — OK").arg(temp, 0, 'f', 1));
}
