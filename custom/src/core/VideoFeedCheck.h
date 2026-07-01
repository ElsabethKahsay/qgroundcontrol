#pragma once
#include "AbstractCheck.h"

class VideoFeedCheck : public AbstractCheck {
    Q_OBJECT
public:
    VideoFeedCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
};
