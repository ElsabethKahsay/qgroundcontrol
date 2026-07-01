#include "MavlinkProtocolCheck.h"

#include "TelemetryBridge.h"

MavlinkProtocolCheck::MavlinkProtocolCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("comm.mavlink.protocol"),
                    QStringLiteral("MAVLink Protocol"),
                    CheckCategory::Communication, CheckType::Auto, true, false, parent)
{
    m_telemetry = telemetry;
}

void MavlinkProtocolCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    uint8_t ver = static_cast<uint8_t>(getTelemetryDouble(QStringLiteral("mavlinkVersion")));

    if (ver == 0) {
        setStatus(CheckStatus::Pending, QStringLiteral("No HEARTBEAT yet"));
        return;
    }

    if (ver >= 3) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("MAVLink %1 — OK").arg(ver));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("MAVLink %1 — upgrade to MAVLink 2 recommended").arg(ver));
    }
}
