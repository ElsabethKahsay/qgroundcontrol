#include "MotorCountCheck.h"

#include <cmath>

#include "TelemetryBridge.h"

MotorCountCheck::MotorCountCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("airframe.motor_count"),
                    QStringLiteral("Motor Count"),
                    CheckCategory::Airframe, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

// Pass: detected motor count matches expected (from FRAME_CLASS). Warns on mismatch.
// Fail: FRAME_CLASS != 1 (Quad). Falls back to 4-motor expectation if params unknown.
void MotorCountCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    if (!isParamAvailable(QStringLiteral("param_FRAME_CLASS")) &&
        !isParamAvailable(QStringLiteral("param_MOT_COUNT"))) {
        setStatus(CheckStatus::Skipped,
                  QStringLiteral("Frame parameters not available from vehicle"));
        return;
    }

    int expected = expectedMotorCount();
    int actual = static_cast<int>(getTelemetryDouble(QStringLiteral("motorCount")));

    // Override expected from explicit MOT_COUNT param if available
    if (expected <= 0 && isParamAvailable(QStringLiteral("param_MOT_COUNT"))) {
        double motCount = getTelemetryDouble(QStringLiteral("param_MOT_COUNT"));
        if (motCount > 0)
            expected = static_cast<int>(motCount);
    }

    if (expected <= 0) {
        // Fall back to simple quadcopter assumption
        expected = 4;
    }

    QString msg = QStringLiteral("Expected %1 motors, detected %2")
                      .arg(expected).arg(actual);

    // Per SRS PRO-001: FRAME_CLASS must equal 1 (Quad) for quadcopter
    double fc = getTelemetryDouble(QStringLiteral("param_FRAME_CLASS"));
    if (!std::isnan(fc) && fc > 0.0 && fc != 1.0) {
        setStatus(CheckStatus::Failed,
                  QStringLiteral("FRAME_CLASS=%1 — expected 1 (Quad)").arg(static_cast<int>(fc)));
        return;
    }

    if (actual == expected) {
        setStatus(CheckStatus::Passed, msg);
    } else if (actual > 0 && actual != expected) {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("%1 — mismatch").arg(msg));
    } else {
        setStatus(CheckStatus::Warning,
                  QStringLiteral("%1 — no motor data").arg(msg));
    }
}

int MotorCountCheck::expectedMotorCount() const
{
    double fc = getTelemetryDouble(QStringLiteral("param_FRAME_CLASS"));
    if (std::isnan(fc) || fc <= 0.0) return 0;
    int frameClass = static_cast<int>(fc);
    // ArduPilot FRAME_CLASS: 1=Quad, 2=Hex, 3=Octo, 4=Tri, 5=Bicopter, 6=Heli
    // PX4: similar convention
    switch (frameClass) {
    case 1: return 4;  // Quad
    case 2: return 6;  // Hex
    case 3: return 8;  // Octo
    case 4: return 3;  // Tri
    case 5: return 2;  // Bicopter
    default: return 0;
    }
}

QString MotorCountCheck::getRationale() const
{
    return QStringLiteral("The motor count must match the airframe configuration. A quadcopter "
                          "requires exactly 4 motors for controlled flight.");
}

QStringList MotorCountCheck::getFixSteps() const
{
    return {
        QStringLiteral("Verify the airframe configuration matches the physical vehicle (FRAME_CLASS, FRAME_TYPE)"),
        QStringLiteral("Check that all ESCs and motors are properly connected to the autopilot"),
        QStringLiteral("Ensure FRAME_CLASS is set to 1 (Quad) in parameters"),
        QStringLiteral("Inspect motor output wiring against the autopilot output map")
    };
}

QString MotorCountCheck::getThreshold() const
{
    return QStringLiteral("4 motors, FRAME_CLASS=1 (Quad)");
}

QString MotorCountCheck::getCurrentValueString() const
{
    int motorCount = static_cast<int>(getTelemetryDouble(QStringLiteral("motorCount")));
    if (motorCount <= 0)
        return QStringLiteral("No data");
    return QStringLiteral("%1 motors").arg(motorCount);
}
