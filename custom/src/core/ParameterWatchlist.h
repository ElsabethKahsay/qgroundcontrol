#pragma once

#include <QString>
#include <QVector>
#include <QSet>

struct ParameterDef {
    const char *name;
    const char *category;   // matching SRS category code e.g. "SAF", "NAV"
    const char *description;
    float minVal;
    float maxVal;
    float defaultValue;
};

namespace ParameterWatchlist {

inline const QVector<ParameterDef> &all()
{
    static const QVector<ParameterDef> params = {
        // ── Power ──
        {"BAT1_A_PER_V",    "POW", "Current per volt scalar",         0.0f,   100.0f,  17.0f},
        {"BAT1_V_DIV",      "POW", "Voltage divider",                 0.0f,   100.0f,  10.0f},
        {"BATT_MONITOR",    "POW", "Battery monitor enable",          0.0f,   1.0f,    1.0f},
        {"BATT_CAPACITY",   "POW", "Battery full capacity (mAh)",     0.0f,   100000.0f, 5000.0f},
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
        {"BAT_LOW_THR",     "SAF", "Battery low threshold (%)",       0.0f,   100.0f,  20.0f},
        {"BAT_CRIT_THR",    "SAF", "Battery critical threshold (%)",  0.0f,   100.0f,  10.0f},
        {"BAT_EMERGEN_THR", "SAF", "Battery emergency threshold (%)", 0.0f,   100.0f,  5.0f},

        // ── Failsafe Actions ──
        {"BATT_FS_LOW_ACT", "SAF", "Battery low failsafe action",     0.0f,   3.0f,    1.0f},
        {"FS_BATT_ENABLE",  "SAF", "Battery failsafe enable",         0.0f,   1.0f,    0.0f},
        {"FS_THR_ENABLE",   "SAF", "Throttle failsafe enable",        0.0f,   2.0f,    0.0f},
        {"FS_GCS_ENABLE",   "SAF", "GCS failsafe enable",             0.0f,   2.0f,    0.0f},
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
    };
    return params;
}

inline QSet<QString> names()
{
    QSet<QString> s;
    s.reserve(all().size());
    for (const auto &p : all())
        s.insert(QString::fromLatin1(p.name));
    return s;
}

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
