#include "RcRssiCheck.h"

#include <QDateTime>

#include "TelemetryBridge.h"

RcRssiCheck::RcRssiCheck(TelemetryBridge *telemetry, int minRcRssi,
                         int minRadioRssi, QObject *parent)
    : AbstractCheck(QStringLiteral("comm.rc.rssi"),
                    QStringLiteral("RC Signal Strength"),
                    CheckCategory::Communication, CheckType::Auto, true, false, parent)
    , m_minRcRssi(minRcRssi)
    , m_minRadioRssi(minRadioRssi)
{
    m_telemetry = telemetry;
}

// Passes when RC RSSI >= m_minRcRssi and radio RSSI (if available) >= m_minRadioRssi.
// Fails if channel data is stale (>5 s) or both links are below threshold; warns for single-link degradation.
void RcRssiCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant connectedVar = getTelemetryVariant(QStringLiteral("rcConnected"));
    if (!connectedVar.isValid()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No RC data reported"));
        return;
    }

    bool connected = connectedVar.toBool();
    int rcRssi = static_cast<int>(getTelemetryDouble(QStringLiteral("rcRssi")));
    int radioRssi = static_cast<int>(getTelemetryDouble(QStringLiteral("radioRssi")));

    setCurrentValue(rcRssi);

    if (!connected) {
        setStatus(CheckStatus::Warning, QStringLiteral("RC not connected"));
        return;
    }

    // Check RC_CHANNELS update staleness
    qint64 lastUsec = static_cast<qint64>(getTelemetryDouble(QStringLiteral("rcLastUpdateUsec")));
    if (lastUsec > 0) {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() * 1000 - lastUsec;
        if (elapsed > 5'000'000) {  // 5 seconds
            setStatus(CheckStatus::Failed,
                      QStringLiteral("RC channels stale (%1s) — RSSI %2%")
                          .arg(elapsed / 1'000'000).arg(rcRssi));
            return;
        }
    }

    bool rcOk = rcRssi >= m_minRcRssi;
    bool radioOk = (radioRssi <= 0) || (radioRssi >= m_minRadioRssi);

    if (rcOk && radioOk) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("RC RSSI %1%, Radio RSSI %2 — OK")
                      .arg(rcRssi).arg(radioRssi));
    } else if (!rcOk && !radioOk) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("RC RSSI %1% (min %2%), Radio RSSI %3 (min %4)")
                      .arg(rcRssi).arg(m_minRcRssi)
                      .arg(radioRssi).arg(m_minRadioRssi));
    } else if (!rcOk) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("RC RSSI %1% below %2% threshold; Radio RSSI %3")
                      .arg(rcRssi).arg(m_minRcRssi).arg(radioRssi));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Radio RSSI %1 below %2 threshold; RC RSSI %3%")
                      .arg(radioRssi).arg(m_minRadioRssi).arg(rcRssi));
    }
}
