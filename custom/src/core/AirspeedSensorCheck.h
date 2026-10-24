// Catches a plane that is set up to use an airspeed sensor the flight
// controller cannot find.
//
// How it decides:
//   * Only fixed-wing aircraft are judged; anything else is Skipped.
//   * ARSPD_TYPE says whether a sensor is configured. 0 means "none": that is
//     a valid setup (ArduPlane then flies on an airspeed estimate), so the
//     check passes and says so.
//   * With ARSPD_TYPE set, the answer comes from the flight controller's own
//     sensor health report (SYS_STATUS). ArduPlane marks the differential
//     pressure sensor (bit 0x10) healthy only when every configured airspeed
//     sensor is found and working; a sensor that failed to start ("MS4525: no
//     sensor found") is never healthy. So: bit set = Passed, bit clear = Failed.
//   * Until the first SYS_STATUS arrives the health mask is all zeros, which
//     would look like a failure, so the check waits instead. A real flight
//     controller always reports some healthy sensor, so a zero mask means
//     "not received yet".
//
// Header-only and without Q_OBJECT so it adds no source file to the build.
#pragma once

#include <QtMath>

#include "AbstractCheck.h"
#include "TelemetryBridge.h"
#include "Vehicle/Vehicle.h"

class AirspeedSensorCheck : public AbstractCheck {
public:
    /// MAV_SYS_STATUS_SENSOR_DIFFERENTIAL_PRESSURE: the airspeed sensor's bit
    /// in SYS_STATUS onboard_control_sensors_health.
    static constexpr uint kDifferentialPressureBit = 0x10;

    explicit AirspeedSensorCheck(TelemetryBridge *telemetry, QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("nav.airspeed.sensor"),
                        QStringLiteral("Airspeed Sensor"),
                        CheckCategory::Navigation, CheckType::Auto,
                        /*mandatory*/ true, /*canOverride*/ false, parent)
    {
        m_telemetry = telemetry;
    }

    void evaluate() override
    {
        if (!hasTelemetry()) {
            setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
            return;
        }
        if (!isFixedWing()) {
            setStatus(CheckStatus::Skipped, QStringLiteral("Not a fixed-wing: no airspeed sensor needed"));
            return;
        }
        if (!isParamAvailable(QStringLiteral("param_ARSPD_TYPE"))) {
            setStatus(CheckStatus::Pending, QStringLiteral("Waiting for the vehicle's parameters (ARSPD_TYPE)"));
            return;
        }
        const int type = qRound(getTelemetryDouble(QStringLiteral("param_ARSPD_TYPE")));
        if (type == 0) {
            setStatus(CheckStatus::Passed,
                      QStringLiteral("No airspeed sensor configured (ARSPD_TYPE = 0): the plane flies on its "
                                     "airspeed estimate"));
            return;
        }
        const uint health = getTelemetryVariant(QStringLiteral("sensorHealth")).toUInt();
        if (health == 0) {
            setStatus(CheckStatus::Pending, QStringLiteral("Waiting for the flight controller's sensor status"));
            return;
        }
        if (health & kDifferentialPressureBit) {
            setStatus(CheckStatus::Passed, QStringLiteral("Airspeed sensor found and healthy (ARSPD_TYPE = %1)").arg(type));
            return;
        }
        setStatus(CheckStatus::Failed,
                  QStringLiteral("Airspeed sensor configured but not found — connect it or set ARSPD_TYPE = 0"));
    }

    QStringList getFixSteps() const override
    {
        return {
            QStringLiteral("If the plane has an airspeed sensor: check its cable and connector (the boot messages "
                           "say \"no sensor found\" when it is not detected), then power-cycle the plane."),
            QStringLiteral("If the plane has no airspeed sensor: set ARSPD_TYPE = 0 (Vehicle Setup → Parameters) "
                           "and reboot the flight controller."),
            QStringLiteral("After the change this check should pass on the next connection."),
        };
    }

private:
    // The vehicle type string can be forced by tests; otherwise ask QGC's
    // Vehicle, which knows the MAV_TYPE from the heartbeat.
    bool isFixedWing() const
    {
        const QString type = getTelemetryVariant(QStringLiteral("vehicleType")).toString().toLower();
        if (!type.isEmpty())
            return type.contains(QStringLiteral("fixed")) || type.contains(QStringLiteral("wing"))
                   || type.contains(QStringLiteral("plane")) || type.contains(QStringLiteral("vtol"));
        Vehicle *vehicle = m_telemetry ? m_telemetry->vehicle() : nullptr;
        return vehicle && vehicle->fixedWing();
    }
};
