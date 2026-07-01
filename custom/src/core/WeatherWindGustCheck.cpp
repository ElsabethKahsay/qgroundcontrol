#include "WeatherWindGustCheck.h"

#include "PreflightSettingsManager.h"
#include "TelemetryBridge.h"
#include "WeatherProvider.h"

WeatherWindGustCheck::WeatherWindGustCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("env.wind_gust"),
                    QStringLiteral("Wind Gust / Erratic Wind"),
                    CheckCategory::Environment, CheckType::Action,
                    false, true, parent)
{
    m_telemetry = telemetry;
}

void WeatherWindGustCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    auto *wp = WeatherProvider::instance();
    auto *settings = PreflightSettingsManager::instance();

    // Auto-pass if METAR is fresh and gusts are OK
    if (wp && settings && settings->autoWeatherEnabled() && wp->metarFresh()) {
        double gust = wp->windGust();
        double threshold = settings->windThresholdGust();

        if (gust <= 0.0) {
            setStatus(CheckStatus::Passed,
                      QStringLiteral("No gusts reported in METAR — auto-passed"));
            return;
        }
        if (gust <= threshold) {
            setStatus(CheckStatus::Passed,
                      QStringLiteral("Gusts %1 m/s within limit — auto-passed (METAR)")
                          .arg(gust, 0, 'f', 1));
            return;
        }
        // Gusts exceed threshold — fall through to action required
    }

    // No fresh METAR or gusts exceed threshold — require manual confirmation
    setStatus(CheckStatus::Warning,
              QStringLiteral("Confirm no gusty/erratic wind conditions"));
}
