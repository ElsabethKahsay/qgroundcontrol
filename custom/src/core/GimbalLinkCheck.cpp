#include "GimbalLinkCheck.h"

#include "QGCApplication.h"

#include "TelemetryBridge.h"
#include "VideoManager/VideoManager.h"

GimbalLinkCheck::GimbalLinkCheck(TelemetryBridge *telemetry,
                                 QObject *parent)
    : AbstractCheck(QStringLiteral("com.gimbal.link"),
                    QStringLiteral("Camera/Gimbal Link"),
                    CheckCategory::Communication, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

static bool _isVideoStreaming()
{
    auto *vm = VideoManager::instance();
    return vm && vm->hasVideo() && vm->streaming() && vm->decoding();
}

// Advisory check (design: gimbal link is not safety critical).
// Pass: gimbal detected with known mode and active video stream.
// Warning: gimbal detected but no video stream or unknown mode, or video-only
//          without a gimbal heartbeat. Skipped: no gimbal fitted.
void GimbalLinkCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    QVariant detectedVar = getTelemetryVariant(QStringLiteral("gimbalDetected"));
    if (!detectedVar.isValid()) {
        setStatus(CheckStatus::Skipped, QStringLiteral("No gimbal data — not fitted"));
        return;
    }

    bool detected = detectedVar.toBool();
    double modeD = getTelemetryDouble(QStringLiteral("gimbalMode"));
    bool videoStreaming = _isVideoStreaming();

    if (detected) {
        if (!videoStreaming) {
            // Advisory, not safety critical: a gimbal present without a live
            // video feed is a payload warning, never an arming blocker. It is
            // reported as Warning because updateCriticalChecks marks every auto
            // check mandatory, which would otherwise turn a Failed into a gate.
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Gimbal detected but no video stream"));
            return;
        }
        if (qIsNaN(modeD) || modeD < 0.0) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Gimbal detected, mode unknown"));
        } else {
            int mode = static_cast<int>(modeD);
            setStatus(CheckStatus::Passed,
                      QStringLiteral("Gimbal detected, mode: %1, video active").arg(mode));
        }
    } else {
        if (videoStreaming) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Video active — no gimbal HEARTBEAT (FPV-only mode)"));
        } else {
            setStatus(CheckStatus::Skipped,
                      QStringLiteral("No gimbal detected — sensor not fitted"));
        }
    }
}
