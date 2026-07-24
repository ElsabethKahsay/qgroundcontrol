#include "RcModeSwitchCheck.h"

#include "TelemetryBridge.h"

RcModeSwitchCheck::RcModeSwitchCheck(TelemetryBridge *telemetry,
                                     QObject *parent)
    : AbstractCheck(QStringLiteral("com.rc.mode_switch"),
                    QStringLiteral("RC Mode Switch"),
                    CheckCategory::Communication, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

// Passes when the mode switch channel value is between 500 and 2500 (valid PWM range).
// Warns if the channel index is out of range, signal is below 800, or no channel data is available.
void RcModeSwitchCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_FLTMODE_CH"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("FLTMODE_CH not available from vehicle"));
        return;
    }

    double modeCh = getTelemetryDouble(QStringLiteral("param_FLTMODE_CH"));

    QVariantList channels = m_telemetry->property("rcChannelValues").toList();

    if (channels.isEmpty()) {
        setStatus(CheckStatus::Pending, QStringLiteral("No RC channel data"));
        return;
    }

    int chIdx = static_cast<int>(modeCh) - 1;
    if (chIdx < 0 || chIdx >= channels.size()) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("FLTMODE_CH=%1 out of range").arg(static_cast<int>(modeCh)));
        return;
    }

    uint16_t chVal = static_cast<uint16_t>(channels[chIdx].toUInt());

    if (chVal < 800) {
        setStatus(CheckStatus::Warning, QStringLiteral("Mode switch signal lost"));
        return;
    }

    if (chVal > 500 && chVal < 2500) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Mode ch%1=%2").arg(static_cast<int>(modeCh)).arg(chVal));
        return;
    }

    setStatus(CheckStatus::Pending, QStringLiteral("Waiting for RC mode data"));
}
