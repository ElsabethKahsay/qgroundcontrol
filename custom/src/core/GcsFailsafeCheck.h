// Verifies the GCS (ground control station) failsafe setting against the
// operator's per-airframe policy. ArduPlane uses FS_GCS_ENABL, ArduCopter uses
// FS_GCS_ENABLE. The policy is stored in check_config; this check never writes
// the vehicle parameter.
#pragma once
#include <functional>

#include "AbstractCheck.h"

class TelemetryBridge;

class GcsFailsafeCheck : public AbstractCheck {
    Q_OBJECT
    /// Operator policy for this airframe: "rc_primary" or "telemetry_primary".
    Q_PROPERTY(QString policy READ policy NOTIFY policyChanged)
public:
    GcsFailsafeCheck(TelemetryBridge *telemetry, QObject *parent = nullptr);
    void evaluate() override;

    /// Policy stored for the current vehicle, or empty when none is stored.
    QString policy() const;
    /// Store the policy for the current vehicle and re-evaluate.
    Q_INVOKABLE bool setPolicy(const QString &policy);

    /// Where the vehicle's hardware UID comes from (the key flight records use).
    /// Defaults to VehicleProfileManager's current device UID; tests replace it.
    void setDeviceUidProvider(std::function<QString()> provider) { m_deviceUid = std::move(provider); }

    /// Vehicle-specific parameter name (FS_GCS_ENABL / FS_GCS_ENABLE), or
    /// empty for a vehicle this check does not support.
    QString parameterName() const;

    QString getRationale() const override;
    QStringList getFixSteps() const override;
    QString getThreshold() const override;

signals:
    void policyChanged();

private:
    enum class VehicleKind { Plane, Copter, Unknown };

    VehicleKind vehicleKind() const;
    int currentVehicleId() const;
    QString deviceUid() const;

    std::function<QString()> m_deviceUid;
    QString settingDescription(double value) const;
    QString actionDescription(double value) const;
    QString timeoutParameterName() const;
    QString actionParameterName() const;
};
