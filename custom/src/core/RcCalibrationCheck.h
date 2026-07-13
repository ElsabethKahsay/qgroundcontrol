#pragma once
#include "AbstractCheck.h"

class TelemetryBridge;

class RcCalibrationCheck : public AbstractCheck {
    Q_OBJECT
public:
    RcCalibrationCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);

    void evaluate() override;
    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;
    QString getCurrentValueString() const override;

    // Expected endpoint ranges for calibrated RC
    static constexpr double kMinEndpointLow  = 800.0;
    static constexpr double kMinEndpointHigh = 1200.0;
    static constexpr double kMaxEndpointLow  = 1800.0;
    static constexpr double kMaxEndpointHigh = 2200.0;
    static constexpr double kTrimCenter      = 1500.0;
    static constexpr double kTrimTolerance   = 150.0;
    static constexpr double kFactoryMin      = 1100.0;
    static constexpr double kFactoryMax      = 1900.0;

private:
    struct RcCalData {
        bool valid = false;
        double min[4] = {};
        double max[4] = {};
        double trim[4] = {};
    };

    RcCalData _readCalParams() const;
    bool _isFactoryDefault(const RcCalData &cal) const;
};
