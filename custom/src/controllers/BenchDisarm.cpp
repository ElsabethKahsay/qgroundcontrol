#include "BenchDisarm.h"

#include <QLoggingCategory>

#include "Vehicle/Vehicle.h"
#include "FactSystem/Fact.h"
#include "utils/Config.h"

Q_LOGGING_CATEGORY(benchDisarmLog, "preflight.benchdisarm")

namespace BenchDisarm {

bool requestDisarm(Vehicle *vehicle, bool testArmedVehicle, QString *reason)
{
    auto refuse = [reason](const QString &why) {
        qCWarning(benchDisarmLog).noquote() << "Bench disarm not sent:" << why;
        if (reason) *reason = why;
        return false;
    };

    if (!vehicle)
        return refuse(QStringLiteral("no vehicle"));
    if (!vehicle->armed())
        return refuse(QStringLiteral("vehicle is not armed"));
    if (!testArmedVehicle)
        return refuse(QStringLiteral("the vehicle was not armed by this test"));
    if (vehicle->flying())
        return refuse(QStringLiteral("vehicle reports flying"));

    const Fact *gs = vehicle->groundSpeed();
    const double groundSpeed = gs ? gs->rawValue().toDouble() : 0.0;
    if (groundSpeed >= static_cast<double>(kForceArmMaxGroundSpeedMs))
        return refuse(QStringLiteral("ground speed %1 m/s").arg(groundSpeed, 0, 'f', 1));

    vehicle->sendMavCommand(vehicle->defaultComponentId(),
                            MAV_CMD_COMPONENT_ARM_DISARM, false,
                            0.0f,   // disarm
                            0.0f,   // plain disarm, never forced
                            0, 0, 0, 0, 0);
    if (reason) reason->clear();
    return true;
}

} // namespace BenchDisarm
