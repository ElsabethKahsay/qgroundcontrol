#include "RcCalibrationCheck.h"

#include "TelemetryBridge.h"

RcCalibrationCheck::RcCalibrationCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("com.rc.calibration"),
                    QStringLiteral("RC Calibration"),
                    CheckCategory::Communication, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

void RcCalibrationCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_FLTMODE_CH")) &&
        !isParamAvailable(QStringLiteral("param_RC3_MIN"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("RC calibration params not available from vehicle"));
        return;
    }

    QVariantList channels = m_telemetry->property("rcChannelValues").toList();

    if (channels.isEmpty()) {
        setStatus(CheckStatus::Pending, QStringLiteral("No RC channel data"));
        return;
    }

    uint16_t ch1 = static_cast<uint16_t>(channels[0].toUInt());
    uint16_t ch2 = static_cast<uint16_t>(channels[1].toUInt());
    uint16_t ch3 = static_cast<uint16_t>(channels[2].toUInt());
    uint16_t ch4 = static_cast<uint16_t>(channels[3].toUInt());

    bool chOk = ch1 > 0 && ch1 < UINT16_MAX
             && ch2 > 0 && ch2 < UINT16_MAX
             && ch3 > 0 && ch3 < UINT16_MAX
             && ch4 > 0 && ch4 < UINT16_MAX;

    if (chOk) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Ch: %1/%2/%3/%4 — OK")
                      .arg(ch1).arg(ch2).arg(ch3).arg(ch4));
        return;
    }

    setStatus(CheckStatus::Warning, QStringLiteral("RC channels not responding"));
}
