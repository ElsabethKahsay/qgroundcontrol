#include "MotorSpinCheck.h"

#include <QDebug>

#include "Vehicle/Vehicle.h"

#include "TelemetryBridge.h"

Q_DECLARE_LOGGING_CATEGORY(motorSpinLog)
Q_LOGGING_CATEGORY(motorSpinLog, "preflight.motorspin")

MotorSpinCheck::MotorSpinCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("propulsion.motors.spin"),
                    QStringLiteral("Motor Spin"),
                    CheckCategory::Propulsion, CheckType::Action, true, false, parent)
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
}

void MotorSpinCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (status() == CheckStatus::Passed)
        return;

    Vehicle *vehicle = m_telemetry ? m_telemetry->vehicle() : nullptr;
    if (vehicle && vehicle->armed()) {
        setStatus(CheckStatus::Failed, QStringLiteral("Disarm vehicle before motor test"));
        return;
    }

    // Status is set by the QML motor panel via confirm() when tests pass.
    // Do not auto-run motors here.
    if (status() != CheckStatus::Pending) {
        setStatus(CheckStatus::Pending,
                  QStringLiteral("Use the motor test panel to test each motor individually"));
    }
}
