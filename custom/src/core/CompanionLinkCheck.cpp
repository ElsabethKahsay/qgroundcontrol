#include "CompanionLinkCheck.h"

#include "TelemetryBridge.h"

CompanionLinkCheck::CompanionLinkCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("comm.companion.link"),
                    QStringLiteral("Companion Computer"),
                    CheckCategory::Communication, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

void CompanionLinkCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    bool detected = getTelemetryBool(QStringLiteral("companionDetected"));

    if (!detected) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No companion computer detected"));
        return;
    }

    setStatus(CheckStatus::Passed, QStringLiteral("Companion computer heartbeat OK"));
}
