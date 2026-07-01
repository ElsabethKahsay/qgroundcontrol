#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class EkfStatusFlagsCheck : public AbstractCheck {
    Q_OBJECT
public:
    EkfStatusFlagsCheck(TelemetryBridge *telemetry,
                        uint requiredHealthFlags = 0x01,
                        double maxRatio = 1.0,
                        QObject *parent = nullptr);
    void evaluate() override;

    enum {
        EstAttitude     = 1,       // bit 0
        EstVelHoriz     = 1 << 1,  // bit 1
        EstVelVert      = 1 << 2,  // bit 2
        EstPosHorizRel  = 1 << 3,  // bit 3
        EstPosHorizAbs  = 1 << 4,  // bit 4
        EstPosVertAbs   = 1 << 5,  // bit 5
        EstPosVertAgl   = 1 << 6,  // bit 6
        EstConstPosMode = 1 << 7,  // bit 7
        EstPredHorizRel = 1 << 8,  // bit 8
        EstPredHorizAbs = 1 << 9,  // bit 9
        EstGpsGlitch    = 1 << 10, // bit 10 — fault flag
        EstAccelError   = 1 << 11, // bit 11 — fault flag
    };

private:
    uint m_requiredHealthFlags;
    double m_maxRatio;
};
