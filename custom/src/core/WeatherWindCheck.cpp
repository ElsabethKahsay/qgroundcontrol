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
