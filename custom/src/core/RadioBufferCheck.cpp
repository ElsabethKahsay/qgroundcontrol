#include "RadioBufferCheck.h"

#include "TelemetryBridge.h"

RadioBufferCheck::RadioBufferCheck(TelemetryBridge *telemetry, int minTxBuf, QObject *parent)
    : AbstractCheck(QStringLiteral("comm.radio.buffer"),
                    QStringLiteral("Radio TX Buffer"),
                    CheckCategory::Communication, CheckType::Auto, true, false, parent)
    , m_minTxBuf(minTxBuf)
{
    m_telemetry = telemetry;
}

void RadioBufferCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    int rssi = static_cast<int>(getTelemetryDouble(QStringLiteral("radioRssi")));
    int txbuf = static_cast<int>(getTelemetryDouble(QStringLiteral("radioTxBuf")));

    if (rssi == 0 && txbuf == 0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No RADIO_STATUS data"));
        return;
    }

    setCurrentValue(txbuf);

    if (txbuf < m_minTxBuf) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("TX buffer %1% — below %2% threshold").arg(txbuf).arg(m_minTxBuf));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("RSSI %1%, TX buf %2%").arg(rssi).arg(txbuf));
}
