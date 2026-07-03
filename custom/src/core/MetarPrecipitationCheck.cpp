#include "MetarPrecipitationCheck.h"

#include "PreflightSettingsManager.h"
#include "TelemetryBridge.h"
#include "WeatherProvider.h"

MetarPrecipitationCheck::MetarPrecipitationCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("env.precipitation"),
                    QStringLiteral("Precipitation (METAR)"),
                    CheckCategory::Environment, CheckType::Auto,
                    false, true, parent)
{
    m_telemetry = telemetry;
}

void MetarPrecipitationCheck::evaluate()
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
            if (!wp->precipitation().isEmpty()) {
                setStatus(CheckStatus::Warning,
                          QStringLiteral("Precipitation: %1 \u2014 STALE data")
                              .arg(wp->precipitation().join(QStringLiteral(", "))));
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

    QStringList precip = wp->precipitation();
    if (precip.isEmpty()) {
        setStatus(CheckStatus::Passed, QStringLiteral("No precipitation reported"));
        return;
    }

    // Check for severe precip types
    bool severe = false;
    for (const QString &p : precip) {
        if (p == QStringLiteral("TS") || p == QStringLiteral("GR") ||
            p == QStringLiteral("GS") || p == QStringLiteral("SN") ||
            p == QStringLiteral("PL") || p == QStringLiteral("FZ")) {
            severe = true;
            break;
        }
    }

    QString msg = QStringLiteral("Precipitation: %1")
        .arg(precip.join(QStringLiteral(", ")));

    if (severe)
        setStatus(CheckStatus::Warning, msg);
    else
        setStatus(CheckStatus::Warning, msg);
}
