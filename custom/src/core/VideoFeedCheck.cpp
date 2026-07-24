#include "VideoFeedCheck.h"

#include "QGCApplication.h"

#include "PreflightSettingsManager.h"
#include "VideoManager/VideoManager.h"

VideoFeedCheck::VideoFeedCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("com.video.feed"),
                    QStringLiteral("Video Feed Health"),
                    CheckCategory::Communication, CheckType::Auto,
                    true, true, parent)
{
    m_telemetry = telemetry;
}

// Pass: video source configured, streaming, decoding, and receiving frames.
// Warning/Fail: depends on videoRequiredForPass setting — Fail if required, Warning if optional.
void VideoFeedCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    bool videoRequired = false;
    auto *settings = PreflightSettingsManager::instance();
    if (settings)
        videoRequired = settings->videoRequiredForPass();

    auto *vm = VideoManager::instance();
    if (!vm) {
        setStatus(CheckStatus::Error, QStringLiteral("Video manager unavailable"));
        return;
    }

    if (!vm->hasVideo()) {
        if (videoRequired)
            setStatus(CheckStatus::Failed, QStringLiteral("No video source configured"));
        else
            setStatus(CheckStatus::Skipped, QStringLiteral("No video source configured"));
        return;
    }

    if (!vm->streaming()) {
        setStatus(videoRequired ? CheckStatus::Failed : CheckStatus::Warning,
                  QStringLiteral("Video stream not active"));
        return;
    }

    if (!vm->decoding()) {
        setStatus(videoRequired ? CheckStatus::Failed : CheckStatus::Warning,
                  QStringLiteral("Video stream not decoding"));
        return;
    }

    QSize sz = vm->videoSize();
    if (sz.isEmpty() || sz.isNull()) {
        setStatus(videoRequired ? CheckStatus::Failed : CheckStatus::Warning,
                  QStringLiteral("No video frames received"));
        return;
    }

    bool hasGimbal = getTelemetryBool(QStringLiteral("gimbalDetected"));
    QString msg = QStringLiteral("Streaming %1x%2").arg(sz.width()).arg(sz.height());
    if (hasGimbal)
        msg += QStringLiteral(" — gimbal present");

    setStatus(CheckStatus::Passed, msg);
}
