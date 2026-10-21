#pragma once

#include <QString>

class Vehicle;

/// Single disarm path for the bench test controllers (HardwareTestController,
/// ControlSurfaceTestController). Findings B1–B4 of the Phase 1 readiness
/// plan: a stop or close must never disarm a vehicle the test did not arm, and
/// never one that is flying.
namespace BenchDisarm {

/// Sends a PLAIN disarm (MAV_CMD_COMPONENT_ARM_DISARM, param1 = 0, param2 = 0)
/// only when all of these hold:
///  - @p testArmedVehicle: the calling test armed the vehicle itself;
///  - the vehicle is armed and does not report flying;
///  - ground speed is below kForceArmMaxGroundSpeedMs (1 m/s).
/// Otherwise sends nothing, returns false and, if @p reason is given, fills it.
bool requestDisarm(Vehicle *vehicle, bool testArmedVehicle, QString *reason = nullptr);

} // namespace BenchDisarm
