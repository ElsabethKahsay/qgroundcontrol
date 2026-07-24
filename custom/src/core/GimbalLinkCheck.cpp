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

// Pass: gimbal detected with known mode and active video stream.
// Warning: gimbal detected but mode unknown, or video-only without gimbal heartbeat.
// Fail: gimbal detected but no video. Skipped: no gimbal fitted.
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
    int mode = static_cast<int>(getTelemetryDouble(QStringLiteral("gimbalMode")));
    bool videoStreaming = _isVideoStreaming();

    if (detected) {
        if (!videoStreaming) {
            setStatus(CheckStatus::Failed,
                      QStringLiteral("Gimbal detected but no video stream"));
            return;
        }
        if (mode >= 0) {
            setStatus(CheckStatus::Passed,
                      QStringLiteral("Gimbal detected, mode: %1, video active").arg(mode));
        } else {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("Gimbal detected, mode unknown"));
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
