#include "MotorSpinCheck.h"

#include <QDebug>

#include "Vehicle/Vehicle.h"

#include "TelemetryBridge.h"

Q_DECLARE_LOGGING_CATEGORY(motorSpinLog)
Q_LOGGING_CATEGORY(motorSpinLog, "preflight.motorspin")

static constexpr int kMotorCount = 4;
static constexpr int kThrottlePct = 28;
static constexpr int kSpinDurationSec = 2;
static constexpr int kStepIntervalMs = 3500;
static constexpr int kFeedbackDelayMs = 1500;

MotorSpinCheck::MotorSpinCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("propulsion.motors.spin"),
                    QStringLiteral("Motor Spin"),
                    CheckCategory::Propulsion, CheckType::Action, true, false, parent)
{
    m_telemetry = telemetry;
    connect(&m_testTimer, &QTimer::timeout, this, &MotorSpinCheck::_runNextMotor);
    setStatus(CheckStatus::Pending,
              QStringLiteral("Click Run to spin motors one at a time"));
}

void MotorSpinCheck::reset()
{
    m_testTimer.stop();
    m_currentMotor = 0;
    m_testCompleted = false;
    AbstractCheck::reset();
    setStatus(CheckStatus::Pending,
              QStringLiteral("Click Run to spin motors one at a time"));
}

bool MotorSpinCheck::_checkMotorFeedback(int motorIndex)
{
    QVariant outputsVar = getTelemetryVariant(QStringLiteral("motorOutputs"));
    if (!outputsVar.isValid())
        return false;

    QVariantList outputs = outputsVar.toList();
    if (outputs.isEmpty())
        return false;

    int activeCount = 0;
    int requestedPwm = -1;
    int limit = qMin(outputs.size(), kMotorCount);

    for (int i = 0; i < limit; ++i) {
        uint16_t pwm = static_cast<uint16_t>(outputs[i].toUInt());
        if (pwm > 1100 && pwm < 2000) {
            activeCount++;
            if (i == motorIndex)
                requestedPwm = pwm;
        }
    }

    if (activeCount <= 0)
        return false;

    QStringList pwmStrs;
    for (int i = 0; i < limit; ++i)
        pwmStrs << outputs[i].toString();

    QString msg;
    if (motorIndex >= 0 && requestedPwm > 0) {
        msg = QStringLiteral("Motor %1 spinning at %2 us — PWM: %3")
                  .arg(motorIndex + 1).arg(requestedPwm).arg(pwmStrs.join(QStringLiteral(", ")));
    } else {
        msg = QStringLiteral("%1 motor(s) active — PWM %2")
                  .arg(activeCount).arg(pwmStrs.join(QStringLiteral(", ")));
    }

    setCurrentValue(QStringLiteral("PWM: %1").arg(pwmStrs.join(QStringLiteral(", "))));
    setStatus(CheckStatus::Passed, msg);

    return true;
}

void MotorSpinCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (status() == CheckStatus::Passed)
        return;

    if (m_testCompleted)
        return;

    if (m_testTimer.isActive())
        return;

    _startTestSequence();
}

void MotorSpinCheck::_startTestSequence()
{
    Vehicle *vehicle = m_telemetry ? m_telemetry->vehicle() : nullptr;
    if (!vehicle) {
        setStatus(CheckStatus::Failed, QStringLiteral("No vehicle reference"));
        return;
    }

    if (vehicle->armed()) {
        setStatus(CheckStatus::Failed, QStringLiteral("Disarm vehicle before motor test"));
        return;
    }

    setCurrentValue(QStringLiteral("Running motor test"));
    m_currentMotor = 0;
    _runNextMotor();
}

void MotorSpinCheck::_runNextMotor()
{
    Vehicle *vehicle = m_telemetry ? m_telemetry->vehicle() : nullptr;
    if (!vehicle) {
        m_testTimer.stop();
        m_currentMotor = 0;
        setStatus(CheckStatus::Failed, QStringLiteral("Lost vehicle reference during test"));
        return;
    }

    if (m_currentMotor >= kMotorCount) {
        qCDebug(motorSpinLog) << "All" << kMotorCount << "motors tested, completing sequence";
        m_testTimer.stop();
        m_currentMotor = 0;

        m_testCompleted = true;

        // One final check in case feedback data arrived late
        if (_checkMotorFeedback())
            return;

        setStatus(CheckStatus::Pending,
                  QStringLiteral("All motors tested at %1% throttle — verify visually, then click Confirm if OK")
                      .arg(kThrottlePct));
        return;
    }

    int motorInstance = m_currentMotor;
    qCDebug(motorSpinLog) << "Sending motor test: motor" << motorInstance << "throttle" << kThrottlePct << "% duration" << kSpinDurationSec << "s";
    vehicle->motorTest(motorInstance + 1, kThrottlePct, kSpinDurationSec, false);

    setStatus(CheckStatus::Pending,
              QStringLiteral("Testing motor %1 of %2 at %3% throttle...")
                  .arg(motorInstance + 1).arg(kMotorCount).arg(kThrottlePct));

    m_currentMotor++;

    // Record motor feedback after SERVO_OUTPUT_RAW has time to arrive
    QTimer::singleShot(kFeedbackDelayMs, this, [this, motorInstance]() {
        bool found = _checkMotorFeedback(motorInstance);
        qCDebug(motorSpinLog) << "Feedback for motor" << motorInstance << (found ? "found" : "not found");
    });

    m_testTimer.start(kStepIntervalMs);
}
