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

void MetarCeilingCheck::evaluate()
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
