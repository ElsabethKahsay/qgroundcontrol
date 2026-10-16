/**
 * @file RcCheck.h
 * @brief Combined RC transmitter preflight check.
 *
 * Aggregates the individual RC detail checks (arming switch configuration,
 * calibration, throttle minimum, trim centering and channel count) behind a
 * single "RC Check" so the Communication section presents one summary row.
 * Each sub-check is evaluated unchanged and the aggregate status is the most
 * severe sub-check status, which preserves the original arming semantics.
 */

#pragma once
#include "AbstractCheck.h"

#include <QList>

class TelemetryBridge;

class RcCheck : public AbstractCheck {
    Q_OBJECT
public:
    explicit RcCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);

    void evaluate() override;

    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getCurrentValueString() const override;
    QString getThreshold() const override;

private:
    QList<AbstractCheck *> m_subChecks;
};
