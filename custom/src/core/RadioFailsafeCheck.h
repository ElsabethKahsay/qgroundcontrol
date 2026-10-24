// Checks that the flight controller is set up to react when the RC link is
// lost. ArduPlane and ArduCopter name this differently, so the check works out
// which one it is talking to from the parameters the vehicle actually has:
//   ArduPlane:  THR_FAILSAFE (0 off, 1 on, 2 detect only), with THR_FS_VALUE,
//               FS_SHORT_ACTN / FS_SHORT_TIMEOUT and FS_LONG_ACTN / FS_LONG_TIMEOUT
//   ArduCopter: FS_THR_ENABLE (0 off, otherwise the action), with FS_THR_VALUE
// Values and meanings follow ArduPlane / ArduCopter 4.5 parameter docs.
#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RadioFailsafeCheck : public AbstractCheck {
    Q_OBJECT
public:
    RadioFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;
    QStringList getFixSteps() const override;

    /// Words for ArduPlane FS_SHORT_ACTN / FS_LONG_ACTN and ArduCopter
    /// FS_THR_ENABLE values (4.5); "unknown value (n)" otherwise.
    static QString planeShortActionName(int value);
    static QString planeLongActionName(int value);
    static QString copterThrottleFailsafeName(int value);

private:
    void evaluatePlane();
    void evaluateCopter();
    double paramOr(const QString &name, double fallback) const;
};
