#pragma once

#include <QString>
#include <QVector>
#include <QSet>

/// Static definition of a UAV parameter: name, category code, description, and safe value range.
struct ParameterDef {
    const char *name;
    const char *category;   // matching SRS category code e.g. "SAF", "NAV"
    const char *description;
    float minVal;
    float maxVal;
    float defaultValue;
};

/// Provides the master list of parameters to watch during preflight,
/// organized by category (POW, NAV, COM, SAF, ARM).
/// Used by UavParameterManager to build the watchlist and by the checklist
/// UI to display parameter thresholds and current values.
namespace ParameterWatchlist {

inline const QVector<ParameterDef> &all()
{
    static const QVector<ParameterDef> params = {
        // ── Power ──
        {"BAT1_A_PER_V",    "POW", "Current per volt scalar",         0.0f,   100.0f,  17.0f},
        {"BAT1_V_DIV",      "POW", "Voltage divider",                 0.0f,   100.0f,  10.0f},
        {"BATT_MONITOR",    "POW", "Battery monitor enable",          0.0f,   1.0f,    1.0f},
        {"BATT_CAPACITY",   "POW", "Battery full capacity (mAh)",     0.0f,   100000.0f, 5000.0f},
        {"BATT_AMP_PERVLT", "POW", "Current at full throttle / per volt (A)", 0.0f, 1000.0f, 17.0f},
        {"BATT_CELL_COUNT", "POW", "Battery cell count",              0.0f,   16.0f,   6.0f},
        {"BATT_LOW_VOLT",   "POW", "Battery empty voltage per cell",  0.0f,   5.0f,    3.5f},

        // ── Navigation & Sensors ──
        {"GPS_GNSSMODE",    "NAV", "GPS GNSS mode",                   0.0f,   6.0f,    0.0f},
        {"GPS_HDOP_GOOD",   "NAV", "GPS HDOP threshold",             0.5f,   5.0f,    2.0f},
        {"EKF2_REQ_NSATS",  "NAV", "EKF2 required satellite count",  0.0f,   16.0f,   8.0f},
        {"GPS_MIN_SATS",    "NAV", "ArduPilot min satellites",       0.0f,   16.0f,   6.0f},
        {"EKF2_ENABLE",     "NAV", "EKF2 enable",                     0.0f,   1.0f,    1.0f},
        {"EKF3_ENABLE",     "NAV", "EKF3 enable",                     0.0f,   1.0f,    0.0f},
        {"COMPASS_ENABLE",  "NAV", "Compass enable",                  0.0f,   1.0f,    1.0f},
        {"COMPASS_USE",     "NAV", "Compass use",                     0.0f,   1.0f,    1.0f},
        {"COMPASS_ORIENT",  "NAV", "Compass orientation",             0.0f,   40.0f,   0.0f},
        {"ACCEL_ENABLE",    "NAV", "Accelerometer enable",            0.0f,   1.0f,    1.0f},
        {"GYRO_ENABLE",     "NAV", "Gyroscope enable",                0.0f,   1.0f,    1.0f},
        {"BARO_ENABLE",     "NAV", "Barometer enable",                0.0f,   1.0f,    1.0f},
        {"AHRS_TRIM_X",     "NAV", "Level calibration pitch trim",   -1.0f,   1.0f,    0.0f},
        {"AHRS_TRIM_Y",     "NAV", "Level calibration roll trim",    -1.0f,   1.0f,    0.0f},

        // ── Communication ──
        {"RC_CHAN_CNT",     "COM", "RC channel count",                0.0f,   32.0f,   8.0f},
        {"RC_MAP_THROTTLE", "COM", "RC throttle channel map",         0.0f,   16.0f,   3.0f},
        {"RC_MAP_MODE_SW",  "COM", "RC mode switch channel",          0.0f,   16.0f,   5.0f},
        {"RC_MAP_ARM_SW",   "COM", "RC arm switch channel",           0.0f,   16.0f,   0.0f},
        {"ARMING_RC_ENABLE","COM", "RC arming switch enable",         0.0f,   1.0f,    0.0f},
        {"FLTMODE_CH",      "COM", "Flight mode channel",             0.0f,   16.0f,   5.0f},
        {"RC3_MIN",         "COM", "RC channel 3 minimum PWM",        800.0f, 2200.0f, 1000.0f},

        // ── Safety & Mission ──
        {"FENCE_ENABLE",    "SAF", "Geofence enable",                 0.0f,   1.0f,    0.0f},
        {"FENCE_TYPE",      "SAF", "Geofence type",                   0.0f,   3.0f,    1.0f},
        {"FENCE_ACTION",    "SAF", "Geofence breach action",          0.0f,   4.0f,    1.0f},
        {"FENCE_ALT_MAX",   "SAF", "Geofence max altitude (m)",       0.0f,   10000.0f, 120.0f},
        {"FENCE_RADIUS",    "SAF", "Geofence radius (m)",             0.0f,   100000.0f, 500.0f},
        {"GF_ACTION",       "SAF", "Generic fence action",            0.0f,   4.0f,    0.0f},
        {"GF_MAX_HORIZ_DIST","SAF","Generic fence max horizontal (m)",0.0f,   100000.0f, 500.0f},
        {"GF_MAX_ALT",      "SAF", "Generic fence max altitude (m)",  0.0f,   10000.0f, 120.0f},
        {"RTL_RETURN_ALT",  "SAF", "RTL return altitude (m)",         0.0f,   10000.0f, 30.0f},
        {"RTL_DESCEND_ALT", "SAF", "RTL descend altitude (m)",        0.0f,   10000.0f, 15.0f},
        {"RTL_ALT",         "SAF", "RTL altitude (m)",                0.0f,   10000.0f, 30.0f},
        {"RTL_ALT_TYPE",    "SAF", "RTL altitude type",               0.0f,   2.0f,    0.0f},

        // ── Mission timing (waypoint times and the motor-table cruise speed) ──
        // ArduPilot 4.7 names in metres and m/s, then the older names in cm
        // and cm/s; MissionWalk::Settings reads whichever the copter has.
        {"WP_SPD",          "NAV", "Mission horizontal speed (m/s)",  0.0f,   50.0f,   10.0f},
        {"WP_SPD_UP",       "NAV", "Mission climb speed (m/s)",       0.0f,   20.0f,   2.5f},
        {"WP_SPD_DN",       "NAV", "Mission descent speed (m/s)",     0.0f,   20.0f,   1.5f},
        {"WP_ACC",          "NAV", "Mission horizontal acceleration (m/s/s)", 0.0f, 20.0f, 2.5f},
        {"WP_ACC_Z",        "NAV", "Mission vertical acceleration (m/s/s)",   0.0f, 20.0f, 1.0f},
        {"WP_JERK",         "NAV", "Mission jerk (m/s/s/s)",          0.0f,   20.0f,   1.0f},
        {"WPNAV_SPEED",     "NAV", "Mission horizontal speed (cm/s)", 0.0f,   5000.0f, 1000.0f},
        {"WPNAV_SPEED_UP",  "NAV", "Mission climb speed (cm/s)",      0.0f,   2000.0f, 250.0f},
        {"WPNAV_SPEED_DN",  "NAV", "Mission descent speed (cm/s)",    0.0f,   2000.0f, 150.0f},
        {"WPNAV_ACCEL",     "NAV", "Mission horizontal acceleration (cm/s/s)", 0.0f, 2000.0f, 250.0f},
        {"WPNAV_ACCEL_Z",   "NAV", "Mission vertical acceleration (cm/s/s)",   0.0f, 2000.0f, 100.0f},
        {"WPNAV_JERK",      "NAV", "Mission jerk (m/s/s/s)",          0.0f,   20.0f,   1.0f},
        {"CIRCLE_RATE",     "NAV", "Circle turn rate (deg/s)",        -90.0f, 90.0f,   20.0f},
        {"RTL_ALT_M",       "SAF", "RTL altitude (m)",                0.0f,   10000.0f, 15.0f},
        {"RTL_SPEED_MS",    "SAF", "RTL speed, 0 = mission speed (m/s)", 0.0f, 50.0f,  0.0f},
        {"RTL_SPEED",       "SAF", "RTL speed, 0 = mission speed (cm/s)", 0.0f, 5000.0f, 0.0f},
        {"RTL_LOIT_TIME",   "SAF", "RTL hold above home (ms)",        0.0f,   60000.0f, 5000.0f},
        {"RTL_ALT_FINAL_M", "SAF", "RTL final altitude, 0 = land (m)", 0.0f,  1000.0f, 0.0f},
        {"RTL_ALT_FINAL",   "SAF", "RTL final altitude, 0 = land (cm)", 0.0f, 100000.0f, 0.0f},
        {"LAND_SPD_MS",     "SAF", "Landing final descent speed (m/s)", 0.0f, 10.0f,   0.5f},
        {"LAND_SPEED",      "SAF", "Landing final descent speed (cm/s)", 0.0f, 1000.0f, 50.0f},
        {"LAND_SPD_HIGH_MS","SAF", "Landing descent speed above the low altitude, 0 = mission descent (m/s)", 0.0f, 20.0f, 0.0f},
        {"LAND_SPEED_HIGH", "SAF", "Landing descent speed above the low altitude, 0 = mission descent (cm/s)", 0.0f, 2000.0f, 0.0f},
        {"LAND_ALT_LOW_M",  "SAF", "Altitude where the final landing speed starts (m)", 0.0f, 1000.0f, 10.0f},
        {"LAND_ALT_LOW",    "SAF", "Altitude where the final landing speed starts (cm)", 0.0f, 100000.0f, 1000.0f},
        {"BAT_LOW_THR",     "SAF", "Battery low threshold (%)",       0.0f,   100.0f,  20.0f},
        {"BAT_CRIT_THR",    "SAF", "Battery critical threshold (%)",  0.0f,   100.0f,  10.0f},
        {"BAT_EMERGEN_THR", "SAF", "Battery emergency threshold (%)", 0.0f,   100.0f,  5.0f},

        // ── Failsafe Actions ──
        {"BATT_FS_LOW_ACT", "SAF", "Battery low failsafe action",     0.0f,   3.0f,    1.0f},
        {"FS_BATT_ENABLE",  "SAF", "Battery failsafe enable",         0.0f,   1.0f,    0.0f},
        {"FS_THR_ENABLE",   "SAF", "Throttle failsafe enable",        0.0f,   2.0f,    0.0f},
        {"FS_GCS_ENABLE",   "SAF", "GCS failsafe enable",             0.0f,   2.0f,    0.0f},
        // ArduPlane names (and the related timeouts / actions) that the GCS
        // failsafe, radio failsafe and airspeed checks read. Only parameters
        // listed here are exposed to the checks, so a check whose parameter is
        // missing from this list reports "not available" on a real vehicle.
        // Ranges are the ArduPlane / ArduCopter 4.5 documented ranges.
        {"FS_GCS_ENABL",    "SAF", "GCS failsafe enable (Plane)",     0.0f,   3.0f,    0.0f},
        {"FS_GCS_TIMEOUT",  "SAF", "GCS failsafe timeout (Copter, s)",2.0f,   120.0f,  5.0f},
        {"FS_SHORT_ACTN",   "SAF", "Short failsafe action (Plane)",   0.0f,   4.0f,    0.0f},
        {"FS_SHORT_TIMEOUT","SAF", "Short failsafe timeout (Plane, s)",1.0f,  100.0f,  1.5f},
        {"FS_LONG_ACTN",    "SAF", "Long failsafe action (Plane)",    0.0f,   4.0f,    0.0f},
        {"FS_LONG_TIMEOUT", "SAF", "Long failsafe timeout (Plane, s)",1.0f,   300.0f,  5.0f},
        {"THR_FAILSAFE",    "SAF", "Throttle / RC failsafe (Plane)",  0.0f,   2.0f,    1.0f},
        {"THR_FS_VALUE",    "SAF", "Throttle failsafe PWM (Plane)",   925.0f, 2200.0f, 950.0f},
        {"FS_THR_VALUE",    "SAF", "Throttle failsafe PWM (Copter)",  910.0f, 1100.0f, 975.0f},
        {"RCMAP_THROTTLE",  "COM", "Throttle channel (ArduPilot)",    1.0f,   16.0f,   3.0f},
        {"ARSPD_TYPE",      "NAV", "Airspeed sensor type (Plane)",    0.0f,   100.0f,  0.0f},
        {"FS_EKF_ACTION",   "SAF", "EKF failsafe action",             0.0f,   3.0f,    0.0f},
        {"FS_EKF_THRESH",   "SAF", "EKF failsafe threshold",          0.0f,   100.0f,  0.8f},

        // ── Arming / Airframe ──
        {"ARMING_CHECK",    "ARM", "Arming check bitmask",            0.0f,   1.0f,    1.0f},
        {"COM_ARM_ENABLE",  "ARM", "Arm enable",                      0.0f,   1.0f,    1.0f},
        {"COM_ARM_VIBE",    "ARM", "Vibration arming threshold (m/s^2)", 0.0f, 100.0f,  30.0f},
        {"COM_ARM_EKF_VEL", "ARM", "Max EKF velocity innovation",         0.1f,   10.0f,   0.5f},
        {"COM_ARM_EKF_POS", "ARM", "Max EKF horizontal position innovation", 0.1f, 10.0f, 0.5f},
        {"COM_ARM_EKF_HGT", "ARM", "Max EKF vertical position innovation", 0.1f,   10.0f,   0.5f},
        {"CBRK_ENGINEFAIL", "ARM", "Engine failure circuit breaker",  0.0f,   1.0f,    0.0f},
        {"CBRK_VELLIMIT",   "ARM", "Velocity limit circuit breaker",  0.0f,   1.0f,    0.0f},
        {"SYS_HITL",        "ARM", "Hardware-in-the-loop mode",       0.0f,   1.0f,    0.0f},
        {"FRAME_CLASS",     "ARM", "Frame class (quad=1, hex=2, ...)",0.0f,   10.0f,   0.0f},
        {"FRAME_TYPE",      "ARM", "Frame type (plus=1, x=2, ...)",   0.0f,   10.0f,   1.0f},

        // ── Motors (the motor-table flight-time estimate) ──
        // ESC range that turns each motor output into throttle, the settings
        // that turn the learned hover throttle into motor output, and which
        // output drives which motor.
        {"MOT_PWM_MIN",     "MOT", "Motor PWM output minimum (0 = 1000)", 0.0f, 2200.0f, 1000.0f},
        {"MOT_PWM_MAX",     "MOT", "Motor PWM output maximum (0 = 2000)", 0.0f, 2200.0f, 2000.0f},
        {"MOT_SPIN_MIN",    "MOT", "Motor output at minimum thrust",   0.0f,   1.0f,    0.15f},
        {"MOT_SPIN_MAX",    "MOT", "Motor output at maximum thrust",   0.0f,   1.0f,    0.95f},
        {"MOT_THST_EXPO",   "MOT", "Thrust curve expo",               -1.0f,   1.0f,    0.65f},
        {"MOT_THST_HOVER",  "MOT", "Learned hover throttle",           0.0f,   1.0f,    0.35f},
        {"MOT_HOVER_LEARN", "MOT", "Hover throttle learning (2 = learn and save)", 0.0f, 2.0f, 2.0f},
        {"MOT_BAT_VOLT_MAX","MOT", "Battery voltage compensation maximum (V)", 0.0f, 100.0f, 0.0f},
        {"MOT_BAT_VOLT_MIN","MOT", "Battery voltage compensation minimum (V)", 0.0f, 100.0f, 0.0f},
        {"SERVO1_FUNCTION",  "MOT", "Output 1 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO2_FUNCTION",  "MOT", "Output 2 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO3_FUNCTION",  "MOT", "Output 3 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO4_FUNCTION",  "MOT", "Output 4 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO5_FUNCTION",  "MOT", "Output 5 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO6_FUNCTION",  "MOT", "Output 6 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO7_FUNCTION",  "MOT", "Output 7 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO8_FUNCTION",  "MOT", "Output 8 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO9_FUNCTION",  "MOT", "Output 9 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO10_FUNCTION", "MOT", "Output 10 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO11_FUNCTION", "MOT", "Output 11 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO12_FUNCTION", "MOT", "Output 12 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO13_FUNCTION", "MOT", "Output 13 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO14_FUNCTION", "MOT", "Output 14 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO15_FUNCTION", "MOT", "Output 15 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
        {"SERVO16_FUNCTION", "MOT", "Output 16 function (33-40 = motor 1-8)", 0.0f, 200.0f, 0.0f},
    };
    return params;
}

/// Return all parameter names as a QSet (for building a UavParameterManager watchlist).
inline QSet<QString> names()
{
    QSet<QString> s;
    s.reserve(all().size());
    for (const auto &p : all())
        s.insert(QString::fromLatin1(p.name));
    return s;
}

/// Return parameter names belonging to a specific category code (e.g. "SAF", "NAV").
inline QSet<QString> namesForCategory(const QString &category)
{
    QSet<QString> s;
    for (const auto &p : all()) {
        if (category == QString::fromLatin1(p.category))
            s.insert(QString::fromLatin1(p.name));
    }
    return s;
}

} // namespace ParameterWatchlist
