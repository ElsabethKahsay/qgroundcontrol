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
    // (defaults used as fallbacks when check_config table has no entry)

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
