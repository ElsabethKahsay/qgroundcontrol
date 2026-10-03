// Manual check requiring each motor to be individually spin-tested via the motor test panel.
// Stays pending until the user completes all motor tests; fails if vehicle is armed.
// Auto-times-out Pending -> Failed if the motor test panel is never opened, so the
// check cannot sit silently in Pending forever (R-05).
#pragma once
#include "AbstractCheck.h"

#include <QElapsedTimer>

class TelemetryBridge;

class MotorSpinCheck : public AbstractCheck {
    Q_OBJECT
public:
    /// @param timeoutOverrideMs   Explicit Pending timeout for tests; -1 (default)
    ///                            resolves the configurable motorspin_timeout_ms.
    MotorSpinCheck(TelemetryBridge *telemetry, QObject *parent = nullptr, int timeoutOverrideMs = -1);
    void evaluate() override;
    void reset() override;

    static constexpr int kDefaultMotorSpinTimeoutMs = 120000;

private:
    void _startPendingTimer();
    bool _pendingTimedOut() const;

    QElapsedTimer m_pendingTimer;
    bool m_pendingTimerStarted = false;
    mutable int m_timeoutMs = -1;
};
