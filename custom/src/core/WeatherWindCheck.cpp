#include "WeatherWindCheck.h"

#include "PreflightSettingsManager.h"
#include "TelemetryBridge.h"
#include "WeatherProvider.h"

WeatherWindCheck::WeatherWindCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("env.wind"),
                    QStringLiteral("Wind Speed (METAR)"),
                    CheckCategory::Environment, CheckType::Auto,
                    true, true, parent)
{
    m_telemetry = telemetry;
}

void WeatherWindCheck::evaluate()
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
            if (wp->windSpeed() > 0.0) {
                double sustained = wp->windSpeed();
                double gust = wp->windGust();
                double dir = wp->windDirection();
                double maxSustained = settings ? settings->windThresholdSustained() : 8.0;
                double maxGust = settings ? settings->windThresholdGust() : 10.0;

                QString msg;
                if (gust > 0.0)
                    msg = QStringLiteral("%1/%2 m/s from %3%4 — exceeds %5/%6 m/s threshold — STALE data")
                        .arg(sustained, 0, 'f', 1).arg(gust, 0, 'f', 1)
                        .arg(dir, 0, 'f', 0).arg(QChar(0x00B0))
                        .arg(maxSustained, 0, 'f', 1).arg(maxGust, 0, 'f', 1);
                else
                    msg = QStringLiteral("%1 m/s from %2%3 — exceeds %4 m/s threshold — STALE data")
                        .arg(sustained, 0, 'f', 1).arg(dir, 0, 'f', 0).arg(QChar(0x00B0))
                        .arg(maxSustained, 0, 'f', 1);

                if (sustained > maxSustained || gust > maxGust) {
                    if (sustained > maxSustained * 1.25 || gust > maxGust * 1.25)
                        setStatus(CheckStatus::Failed, msg);
                    else
                        setStatus(CheckStatus::Warning, msg);
                } else {
                    setStatus(CheckStatus::Warning, QStringLiteral("Wind %1 m/s — STALE data").arg(sustained, 0, 'f', 1));
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

    double sustained = wp->windSpeed();
    double gust = wp->windGust();
    double dir = wp->windDirection();
    double maxSustained = settings->windThresholdSustained();
    double maxGust = settings->windThresholdGust();

    if (sustained > maxSustained || gust > maxGust) {
        QString msg;
        if (gust > 0.0)
            msg = QStringLiteral("%1/%2 m/s from %3%4 — exceeds %5/%6 m/s threshold")
                .arg(sustained, 0, 'f', 1).arg(gust, 0, 'f', 1)
                .arg(dir, 0, 'f', 0).arg(QChar(0x00B0))
                .arg(maxSustained, 0, 'f', 1).arg(maxGust, 0, 'f', 1);
        else
            msg = QStringLiteral("%1 m/s from %2%3 — exceeds %4 m/s threshold")
                .arg(sustained, 0, 'f', 1).arg(dir, 0, 'f', 0).arg(QChar(0x00B0))
                .arg(maxSustained, 0, 'f', 1);

        if (sustained > maxSustained * 1.25 || gust > maxGust * 1.25)
            setStatus(CheckStatus::Failed, msg);
        else
            setStatus(CheckStatus::Warning, msg);
        return;
    }

    QString msg = QStringLiteral("%1 m/s").arg(sustained, 0, 'f', 1);
    if (gust > 0.0)
        msg += QStringLiteral(" G%1 m/s").arg(gust, 0, 'f', 1);
    msg += QStringLiteral(" from %1%2 — OK").arg(dir, 0, 'f', 0).arg(QChar(0x00B0));
    setStatus(CheckStatus::Passed, msg);
}
