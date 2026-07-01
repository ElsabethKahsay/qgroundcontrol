#include "OpticalFlowCheck.h"

#include "TelemetryBridge.h"

OpticalFlowCheck::OpticalFlowCheck(TelemetryBridge *telemetry,
                                   int minQuality,
                                   QObject *parent)
    : AbstractCheck(QStringLiteral("nav.optical_flow.quality"),
                    QStringLiteral("Optical Flow Health"),
                    CheckCategory::Navigation, CheckType::Auto, false, false, parent)
    , m_minQuality(minQuality)
{
    m_telemetry = telemetry;
}

void OpticalFlowCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant qualityVar = getTelemetryVariant(QStringLiteral("opticalFlowQuality"));
    if (!qualityVar.isValid()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No optical flow data — sensor not fitted"));
        return;
    }

    int quality = qualityVar.toInt();
    if (quality < 0) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No optical flow sensor detected"));
        return;
    }

    if (quality >= m_minQuality) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Optical flow quality %1/%2").arg(quality).arg(255));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("Optical flow quality %1 below %2 — sensor may be obstructed")
                      .arg(quality).arg(m_minQuality));
    }
}
