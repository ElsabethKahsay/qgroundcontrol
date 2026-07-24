// Checks video feed health: source configured, stream active, decoding, and receiving frames.
// Fails when video is required for pass but source is missing or not streaming.
#pragma once
#include "AbstractCheck.h"

class VideoFeedCheck : public AbstractCheck {
    Q_OBJECT
public:
    VideoFeedCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
