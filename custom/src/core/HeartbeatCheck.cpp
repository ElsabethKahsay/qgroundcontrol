#include "HeartbeatCheck.h"

#include <QDateTime>

#include "TelemetryBridge.h"

HeartbeatCheck::HeartbeatCheck(TelemetryBridge *telemetry, int timeoutSec,
                               QObject *parent)
    : AbstractCheck(QStringLiteral("comm.heartbeat"),
                    QStringLiteral("Heartbeat"),
                    CheckCategory::Communication, CheckType::Auto, true, false, parent)
    , m_timeoutSec(timeoutSec)
{
    m_telemetry = telemetry;
}

void HeartbeatCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    int quality = m_telemetry->connectionQuality();
    bool received = m_telemetry->heartbeatReceived();

    setCurrentValue(quality);

    if (!received) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for first heartbeat"));
        return;
    }

    // Pass if quality >= 10%; warn on weak/timeout; pending if no heartbeat yet.
    if (quality < 10) {
        QString msg = quality <= 0
            ? QStringLiteral("Heartbeat timeout — %1s").arg(m_timeoutSec)
            : QStringLiteral("Weak connection: %1%").arg(quality);
        setStatus(CheckStatus::Warning, msg);
        return;
    }

    setStatus(CheckStatus::Passed,
              QStringLiteral("Connected — %1%").arg(quality));
}
