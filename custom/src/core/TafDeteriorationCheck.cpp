#include "TafDeteriorationCheck.h"

#include "PreflightSettingsManager.h"
#include "TelemetryBridge.h"
#include "WeatherProvider.h"

TafDeteriorationCheck::TafDeteriorationCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("env.taf_deterioration"),
                    QStringLiteral("TAF Deterioration"),
                    CheckCategory::Environment, CheckType::Auto,
                    false, true, parent)
{
    m_telemetry = telemetry;
}

void TafDeteriorationCheck::evaluate()
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

    QString taf = wp->tafString();
    if (taf.isEmpty()) {
        if (!m_fetchTriggered) {
            m_fetchTriggered = true;
            if (settings && !settings->defaultIcao().isEmpty()) {
                wp->fetchTaf(settings->defaultIcao());
            } else if (m_telemetry) {
                double lat = getTelemetryDouble("gpsLatitude");
                double lon = getTelemetryDouble("gpsLongitude");
                if (qAbs(lat) > 0.01 || qAbs(lon) > 0.01)
                    wp->fetchWeather(lat, lon);
            }
            setStatus(CheckStatus::Pending, QStringLiteral("Fetching TAF data\u2026"));
        }

        if (m_lastEvalTime.isValid() && m_lastEvalTime.secsTo(QDateTime::currentDateTime()) > 10) {
            m_fetchTriggered = false;
            QString err = wp->lastError();
            if (!err.isEmpty())
                setStatus(CheckStatus::Skipped, QStringLiteral("Weather fetch failed: ") + err);
            else
                setStatus(CheckStatus::Skipped, QStringLiteral("No TAF data \u2014 configure ICAO in Preflight Settings"));
        } else {
            setStatus(CheckStatus::Pending, QStringLiteral("Fetching TAF data\u2026"));
        }
        return;
    }

    m_fetchTriggered = false;

    if (wp->tafDeteriorating()) {
        QStringList summary = wp->tafSummary();
        setStatus(CheckStatus::Warning,
                  QStringLiteral("TAF shows worsening conditions: %1")
                      .arg(summary.join(QStringLiteral("; "))));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("No deterioration in next 6 hours"));
}
