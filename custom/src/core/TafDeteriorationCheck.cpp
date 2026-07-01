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

    if (!wp || !settings || !settings->autoWeatherEnabled()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("Auto-weather disabled"));
        return;
    }

    QString taf = wp->tafString();
    if (taf.isEmpty()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No TAF data"));
        return;
    }

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
