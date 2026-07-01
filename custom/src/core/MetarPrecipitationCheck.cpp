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
