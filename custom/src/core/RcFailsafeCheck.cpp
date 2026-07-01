#include "RcFailsafeCheck.h"

#include "TelemetryBridge.h"

RcFailsafeCheck::RcFailsafeCheck(TelemetryBridge *telemetry, int minRssi, QObject *parent)
    : AbstractCheck(QStringLiteral("com.rc.failsafe"),
                    QStringLiteral("RC Failsafe"),
                    CheckCategory::Communication, CheckType::Auto, true, false, parent)
    , m_minRssi(minRssi)
{
    m_telemetry = telemetry;
}

void RcFailsafeCheck::evaluate()
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

    bool failsafe = getTelemetryBool(QStringLiteral("rcFailsafe"));
    int rssi = static_cast<int>(getTelemetryDouble(QStringLiteral("rcRssi")));

    if (!connectedVar.toBool()) {
        setStatus(CheckStatus::Warning, QStringLiteral("RC not connected — cannot verify failsafe"));
        return;
    }

    if (failsafe) {
        setStatus(CheckStatus::Failed, QStringLiteral("RC failsafe active (no signal)"));
        return;
    }

    if (rssi < m_minRssi) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("RC RSSI %1% — below %2% threshold")
                      .arg(rssi).arg(m_minRssi));
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("RC link OK, RSSI %1%").arg(rssi));
}
