#include "MotorSpinCheck.h"

#include <QDebug>

#include "Vehicle/Vehicle.h"

#include "TelemetryBridge.h"

Q_DECLARE_LOGGING_CATEGORY(motorSpinLog)
Q_LOGGING_CATEGORY(motorSpinLog, "preflight.motorspin")

MotorSpinCheck::MotorSpinCheck(TelemetryBridge *telemetry, QObject *parent, int timeoutOverrideMs)
    : AbstractCheck(QStringLiteral("propulsion.motors.spin"),
                    QStringLiteral("Motor Spin"),
                    CheckCategory::Propulsion, CheckType::Action, true, false, parent)
    , m_timeoutMs(timeoutOverrideMs)
{
    m_telemetry = telemetry;
    setStatus(CheckStatus::Pending,
              QStringLiteral("Use the motor test panel to test each motor individually"));
}

void MotorSpinCheck::reset()
{
    AbstractCheck::reset();
    setStatus(CheckStatus::Pending,
              QStringLiteral("Use the motor test panel to test each motor individually"));
    m_pendingTimerStarted = false;
}

/// Begins the operator-response window: the moment the check is first evaluated
/// with live telemetry while still Pending.  Restarted by reset() or whenever
/// the check leaves Pending and comes back.
void MotorSpinCheck::_startPendingTimer()
{
    m_pendingTimer.restart();
    m_pendingTimerStarted = true;
}

bool MotorSpinCheck::_pendingTimedOut() const
{
    if (m_timeoutMs <= 0) {
        m_timeoutMs = configInt(QStringLiteral("motorspin_timeout_ms"), kDefaultMotorSpinTimeoutMs);
    }
    return m_pendingTimer.isValid() && m_pendingTimer.elapsed() >= m_timeoutMs;
}

// Pass: set externally by QML motor test panel via confirm(). Never auto-passes.
// Warning/Fail: vehicle must be disarmed before motor testing. Resets to pending on reset().
// R-05: if the panel is never opened, the pending window expires and the check
// hard-fails with a TIMEOUT message instead of sitting in Pending forever.
void MotorSpinCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        m_pendingTimerStarted = false;
        return;
    }

    if (status() == CheckStatus::Passed)
        return;

    Vehicle *vehicle = m_telemetry ? m_telemetry->vehicle() : nullptr;
    if (vehicle && vehicle->armed()) {
        setStatus(CheckStatus::Failed, QStringLiteral("Disarm vehicle before motor test"));
        m_pendingTimerStarted = false;
        return;
    }

    // Status is set by the QML motor panel via confirm() when tests pass.
    // Do not auto-run motors here.
    if (status() != CheckStatus::Pending) {
        setStatus(CheckStatus::Pending,
                  QStringLiteral("Use the motor test panel to test each motor individually"));
        m_pendingTimerStarted = false;
    }

    if (!m_pendingTimerStarted) {
        _startPendingTimer();
    } else if (_pendingTimedOut()) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Motor spin test not performed (TIMEOUT) — open the motor test panel "
                                 "and spin each motor before arming"));
    }
}
