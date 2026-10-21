#pragma once

/// MAVLink streams whose arrival time TelemetryBridge stamps (F1–F3).
/// A check declares the streams it reads (AbstractCheck::setRequiredStreams);
/// PreflightManager marks it Stale when one of them stops arriving.
/// Header-only (no CMake change needed); kept apart from TelemetryBridge.h so
/// AbstractCheck.h does not pull in the whole bridge.
enum class TelemetryStream : int {
    Heartbeat = 0,      ///< HEARTBEAT from the autopilot component only
    SysStatus,          ///< SYS_STATUS
    BatteryStatus,      ///< BATTERY_STATUS
    GpsRawInt,          ///< GPS_RAW_INT
    Attitude,           ///< ATTITUDE
    RcChannels,         ///< RC_CHANNELS
    EkfStatus,          ///< EKF_STATUS_REPORT
    GlobalPosition,     ///< GLOBAL_POSITION_INT
    Count
};

inline const char *telemetryStreamName(TelemetryStream s)
{
    switch (s) {
    case TelemetryStream::Heartbeat:      return "HEARTBEAT";
    case TelemetryStream::SysStatus:      return "SYS_STATUS";
    case TelemetryStream::BatteryStatus:  return "BATTERY_STATUS";
    case TelemetryStream::GpsRawInt:      return "GPS_RAW_INT";
    case TelemetryStream::Attitude:       return "ATTITUDE";
    case TelemetryStream::RcChannels:     return "RC_CHANNELS";
    case TelemetryStream::EkfStatus:      return "EKF_STATUS_REPORT";
    case TelemetryStream::GlobalPosition: return "GLOBAL_POSITION_INT";
    case TelemetryStream::Count:          break;
    }
    return "?";
}
