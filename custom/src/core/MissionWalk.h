// Times for each waypoint of an ArduPilot copter mission.
//
// The mission is read from the autopilot (or a Mission Planner .waypoints
// file) and walked command by command the way ArduPilot flies it, with the
// copter's own settings (WP_SPD, WP_SPD_UP, WP_SPD_DN, WP_ACC, the RTL and
// landing settings). The result is a list of steps (climb, move, hold,
// circle, return, land) with their times, the time each waypoint is reached,
// and the servo and gripper commands met on the way (where a payload may be
// released). Nothing here is fixed for one copter: every speed and height
// comes from its parameters, and a setting the walk needs but cannot read
// makes it say so instead of guessing.
//
// How ArduPilot flies, as modelled here:
// - Horizontal moves speed up and slow down along an S-curve: acceleration
//   rises at WP_JERK up to WP_ACC, then the speed is held at WP_SPD (or the
//   mission's speed change). Speeding up from a stop to speed v takes
//   v/a + a/j seconds over half that time's worth of distance at v, so each
//   stop or start costs half that time compared with flying through at v.
// - The copter flies through a waypoint without stopping when it has no hold
//   time and the next navigation command is another waypoint; it stops before
//   a hold, a loiter, a landing or a return. Corners are not slowed here.
// - A move that also changes height takes as long as the slower of the
//   horizontal move and the height change (WP_SPD_UP up, WP_SPD_DN down).
// - Latitude and longitude of 0 mean "here", and a height of 0 on a waypoint
//   means "the present height", as ArduPilot reads them.
// - A return climbs to RTL_ALT_M only if lower, flies home at RTL_SPEED_MS
//   (WP_SPD when 0), holds RTL_LOIT_TIME, then lands: down to LAND_ALT_LOW_M
//   at LAND_SPD_HIGH_MS (WP_SPD_DN when 0), then at LAND_SPD_MS.
// - DO commands (speed change, servo, gripper, jump) take effect when the
//   navigation command before them is complete.
//
// Header-only and without Q_OBJECT, so it needs no CMake or moc change.
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include "PowerTypeModel.h"

namespace MissionWalk {

constexpr double kPi = 3.14159265358979323846;

/// MAVLink command numbers the walk knows.
enum Command : int {
    NavWaypoint = 16,
    NavLoiterUnlimited = 17,
    NavLoiterTurns = 18,
    NavLoiterTime = 19,
    NavReturnToLaunch = 20,
    NavLand = 21,
    NavTakeoff = 22,
    NavLoiterToAlt = 31,
    NavSplineWaypoint = 82,
    NavDelay = 93,
    DoJump = 177,
    DoChangeSpeed = 178,
    DoSetServo = 183,
    DoRepeatServo = 184,
    DoGripper = 211,
};

/// MAVLink frames for a mission item's height.
enum Frame : int {
    FrameGlobal = 0,           ///< Above sea level
    FrameMission = 2,          ///< No position (DO commands)
    FrameGlobalRelative = 3,   ///< Above home
    FrameGlobalInt = 5,
    FrameGlobalRelativeInt = 6,
    FrameTerrain = 10,         ///< Above the ground below
    FrameTerrainInt = 11,
};

/// Commands that take no time and do not change the route: conditions,
/// relays, camera, mount and region-of-interest commands, and the landing
/// sequence marker.
inline bool takesNoTime(int command)
{
    static const QVector<int> none = {112, 114, 115, 181, 182, 189, 195, 196, 197, 201, 202, 203,
                                      204, 205, 206, 214, 1000, 1001, 2000, 2001, 2003, 2004};
    return none.contains(command);
}

/// One mission item, as the autopilot holds it.
struct Item {
    int seq = 0;
    int frame = FrameGlobalRelative;
    int command = NavWaypoint;
    double p1 = 0.0, p2 = 0.0, p3 = 0.0, p4 = 0.0;
    double lat = 0.0, lon = 0.0, alt = 0.0;
    bool autocontinue = true;
};

/// Reads a parameter from the copter: true and the value if it has it.
using Lookup = std::function<bool(const QString &name, double *value)>;

/// The copter's settings the walk uses, in metres, m/s and seconds.
struct Settings {
    double speedMs = -1.0;          ///< WP_SPD
    double climbMs = -1.0;          ///< WP_SPD_UP
    double descentMs = -1.0;        ///< WP_SPD_DN
    double accelMss = -1.0;         ///< WP_ACC
    double accelZMss = -1.0;        ///< WP_ACC_Z (optional: without it height changes start at full speed)
    double jerkMsss = -1.0;         ///< WP_JERK (optional: without it acceleration starts at once)
    double rtlAltM = -1.0;          ///< RTL_ALT_M
    double rtlSpeedMs = -1.0;       ///< RTL_SPEED_MS, 0 = WP_SPD
    double rtlLoiterS = -1.0;       ///< RTL_LOIT_TIME
    double rtlFinalAltM = -1.0;     ///< RTL_ALT_FINAL_M, 0 = land (optional: lands without it)
    double landSpeedMs = -1.0;      ///< LAND_SPD_MS
    double landSpeedHighMs = -1.0;  ///< LAND_SPD_HIGH_MS, 0 = WP_SPD_DN
    double landAltLowM = -1.0;      ///< LAND_ALT_LOW_M
    double circleRateDegS = 0.0;    ///< CIRCLE_RATE (needed only for loiter turns)
    QStringList missing;            ///< Settings needed but not read from the copter
    QHash<QString, QString> source; ///< Which parameter each setting came from

    bool complete() const { return missing.isEmpty(); }

    /// Reads each setting under its ArduPilot 4.7 name (metres, m/s), else
    /// its older name (centimetres, cm/s) converted.
    static Settings read(const Lookup &get)
    {
        Settings s;
        auto pick = [&](const char *key, const char *now, const char *older, double olderScale, double *out,
                        bool required) {
            double v = 0.0;
            if (get(QString::fromLatin1(now), &v)) {
                *out = v;
                s.source.insert(QString::fromLatin1(key), QString::fromLatin1(now));
            } else if (older && get(QString::fromLatin1(older), &v)) {
                *out = v * olderScale;
                s.source.insert(QString::fromLatin1(key), QString::fromLatin1(older));
            } else if (required) {
                s.missing << QString::fromLatin1(now);
            }
        };
        pick("speed", "WP_SPD", "WPNAV_SPEED", 0.01, &s.speedMs, true);
        pick("climb", "WP_SPD_UP", "WPNAV_SPEED_UP", 0.01, &s.climbMs, true);
        pick("descent", "WP_SPD_DN", "WPNAV_SPEED_DN", 0.01, &s.descentMs, true);
        pick("accel", "WP_ACC", "WPNAV_ACCEL", 0.01, &s.accelMss, true);
        pick("accelZ", "WP_ACC_Z", "WPNAV_ACCEL_Z", 0.01, &s.accelZMss, false);
        pick("jerk", "WP_JERK", "WPNAV_JERK", 1.0, &s.jerkMsss, false);
        pick("rtlAlt", "RTL_ALT_M", "RTL_ALT", 0.01, &s.rtlAltM, true);
        pick("rtlSpeed", "RTL_SPEED_MS", "RTL_SPEED", 0.01, &s.rtlSpeedMs, true);
        pick("rtlLoiter", "RTL_LOIT_TIME", nullptr, 1.0, &s.rtlLoiterS, true);
        pick("rtlFinal", "RTL_ALT_FINAL_M", "RTL_ALT_FINAL", 0.01, &s.rtlFinalAltM, false);
        pick("landSpeed", "LAND_SPD_MS", "LAND_SPEED", 0.01, &s.landSpeedMs, true);
        pick("landSpeedHigh", "LAND_SPD_HIGH_MS", "LAND_SPEED_HIGH", 0.01, &s.landSpeedHighMs, true);
        pick("landAltLow", "LAND_ALT_LOW_M", "LAND_ALT_LOW", 0.01, &s.landAltLowM, true);
        pick("circleRate", "CIRCLE_RATE", nullptr, 1.0, &s.circleRateDegS, false);
        if (s.rtlLoiterS >= 0.0)
            s.rtlLoiterS /= 1000.0;   // RTL_LOIT_TIME is in milliseconds
        // Speeds of 0 are not usable where no other speed stands in for them.
        if (!(s.speedMs > 0.0) && !s.missing.contains(QStringLiteral("WP_SPD")))
            s.missing << QStringLiteral("WP_SPD");
        if (!(s.climbMs > 0.0) && !s.missing.contains(QStringLiteral("WP_SPD_UP")))
            s.missing << QStringLiteral("WP_SPD_UP");
        if (!(s.descentMs > 0.0) && !s.missing.contains(QStringLiteral("WP_SPD_DN")))
            s.missing << QStringLiteral("WP_SPD_DN");
        if (!(s.landSpeedMs > 0.0) && !s.missing.contains(QStringLiteral("LAND_SPD_MS")))
            s.missing << QStringLiteral("LAND_SPD_MS");
        return s;
    }
};

/// The copter's mission speed (WP_SPD, or WPNAV_SPEED in cm/s on older
/// firmware); -1 when it has not been read.
inline double missionSpeedMs(const Lookup &get)
{
    double v = 0.0;
    if (get(QStringLiteral("WP_SPD"), &v))
        return v > 0.0 ? v : -1.0;
    if (get(QStringLiteral("WPNAV_SPEED"), &v))
        return v > 0.0 ? v / 100.0 : -1.0;
    return -1.0;
}

/// Seconds to speed up from a stop to @p v (or slow down from it), with the
/// acceleration @p a reached at the rate @p j (no limit when j ≤ 0).
inline double accelTimeS(double v, double a, double j)
{
    if (!(v > 0.0) || !(a > 0.0))
        return 0.0;
    if (j > 0.0)
        return v > a * a / j ? v / a + a / j : 2.0 * std::sqrt(v / j);
    return v / a;
}

/// Seconds to move @p d metres at up to @p v, starting and/or ending stopped.
/// Each stop or start costs half its speed-up time; a move too short to reach
/// @p v peaks at the speed whose speed-up and slow-down fill it.
inline double moveTimeS(double d, double v, double a, double j, bool startStopped, bool endStopped)
{
    if (!(d > 0.0))
        return 0.0;
    if (!(v > 0.0))
        return std::numeric_limits<double>::infinity();
    const int phases = (startStopped ? 1 : 0) + (endStopped ? 1 : 0);
    if (phases == 0 || !(a > 0.0))
        return d / v;
    const double ta = accelTimeS(v, a, j);
    // A speed-up from 0 to v covers v × ta / 2 (the speed rises evenly in time
    // on average).
    if (d >= phases * v * ta / 2.0)
        return d / v + phases * ta / 2.0;
    double lo = 0.0, hi = v;
    for (int i = 0; i < 60; ++i) {
        const double mid = (lo + hi) / 2.0;
        (phases * mid * accelTimeS(mid, a, j) / 2.0 < d ? lo : hi) = mid;
    }
    return phases * accelTimeS(lo, a, j);
}

/// What one step of the walk is.
enum class StepKind { Climb, Move, Hold, Circle, Return, Land };

/// One step: a stretch of the flight flown one way.
struct Step {
    StepKind kind = StepKind::Move;
    int seq = -1;              ///< The mission item it belongs to
    double startS = 0.0;       ///< Time since the start of the walk when it begins
    double timeS = 0.0;
    double horizM = 0.0;       ///< Distance over the ground
    double climbM = 0.0;       ///< Height change, + up
    bool hover = false;        ///< Holding position (hold, delay, the hold above home)
    double lat = 0.0, lon = 0.0, altM = 0.0;   ///< Where it ends; height above home
};

/// A waypoint (navigation item) reached.
struct Arrival {
    int seq = -1;
    int command = 0;
    double timeS = 0.0;
    double lat = 0.0, lon = 0.0, altM = 0.0;
};

/// A servo or gripper command met on the way: where a payload may be
/// released.
struct Event {
    int seq = -1;
    int command = 0;
    double p1 = 0.0, p2 = 0.0;   ///< Servo output and PWM, or gripper number and action
    double timeS = 0.0;
    double lat = 0.0, lon = 0.0, altM = 0.0;
};

/// Where the walk starts: on the ground at home before take-off, or (in
/// flight) where the copter is, heading for @p nextSeq.
struct Start {
    double lat = std::nan("");
    double lon = std::nan("");
    double altM = 0.0;          ///< Above home
    int nextSeq = 1;            ///< The first item to fly (item 0 is home)
    bool moving = false;        ///< Already at speed (no speed-up on the first move)
    double speedMs = -1.0;      ///< The speed in force (a speed change already met); -1 = WP_SPD
    /// A mission that ends in the air (no return or landing at the end) is
    /// followed by the return the pilot is expected to switch to (RTL from
    /// the last point). ArduCopter itself would only hover there.
    bool assumeReturn = true;
};

struct Result {
    bool ok = false;
    QString problem;            ///< Why it cannot be timed, naming the item
    QVector<Step> steps;
    QVector<Arrival> arrivals;
    QVector<Event> events;
    double totalS = 0.0;
    bool endsLanded = false;    ///< The flight ends on the ground (a return or a landing)
    bool returnAssumed = false; ///< The return was added (steps and arrival with seq -1), not in the mission
    double missionEndS = 0.0;   ///< When the mission's own items are done
};

/// A navigation command (flown to, arrived at): MAVLink numbers below 100.
/// DO and condition commands (100 and up) are done on the way.
inline bool isNavCommand(int command)
{
    return command >= 0 && command < 100;
}

/// Seconds for a height change of @p dz metres (+ up), starting and ending
/// stopped.
inline double heightTimeS(double dz, const Settings &s)
{
    const double v = dz > 0.0 ? s.climbMs : s.descentMs;
    return moveTimeS(std::abs(dz), v, s.accelZMss, 0.0, true, true);
}

/// Seconds to land from @p altM: down to LAND_ALT_LOW_M at the higher
/// landing speed, then at LAND_SPD_MS.
inline double landTimeS(double altM, const Settings &s)
{
    if (!(altM > 0.0))
        return 0.0;
    const double high = s.landSpeedHighMs > 0.0 ? s.landSpeedHighMs : s.descentMs;
    const double low = std::max(0.0, s.landAltLowM);
    const double above = std::max(0.0, altM - low);
    return above / high + std::min(altM, low) / s.landSpeedMs;
}

/// Walks @p items (item 0 is home and is not flown) from @p start.
/// @p homeAmslM is home's height above sea level, needed only for items with
/// sea-level heights.
inline Result walk(const QVector<Item> &items, const Settings &s, double homeLat, double homeLon,
                   const Start &start = Start(), double homeAmslM = std::nan(""))
{
    Result r;
    if (!s.complete()) {
        r.problem = QStringLiteral("Settings not read from the copter: %1").arg(s.missing.join(QStringLiteral(", ")));
        return r;
    }
    if (!std::isfinite(homeLat) || !std::isfinite(homeLon)) {
        r.problem = QStringLiteral("Home position not known");
        return r;
    }

    double lat = std::isfinite(start.lat) ? start.lat : homeLat;
    double lon = std::isfinite(start.lon) ? start.lon : homeLon;
    double alt = start.altM;
    double t = 0.0;
    double speed = start.speedMs > 0.0 ? start.speedMs : s.speedMs;
    bool stopped = !start.moving;

    auto indexOf = [&items](int seq) {
        for (int i = 0; i < items.size(); ++i)
            if (items[i].seq == seq)
                return i;
        return -1;
    };
    auto isNav = [](int c) { return isNavCommand(c); };
    auto add = [&](StepKind kind, int seq, double dt, double horiz, double climb, bool hover) {
        Step st;
        st.kind = kind;
        st.seq = seq;
        st.startS = t;
        st.timeS = dt;
        st.horizM = horiz;
        st.climbM = climb;
        st.hover = hover;
        t += dt;
        st.lat = lat;
        st.lon = lon;
        st.altM = alt;
        r.steps.append(st);
    };
    auto arrive = [&](const Item &it) {
        Arrival a;
        a.seq = it.seq;
        a.command = it.command;
        a.timeS = t;
        a.lat = lat;
        a.lon = lon;
        a.altM = alt;
        r.arrivals.append(a);
    };
    // A height for an item: above home, from its frame. NaN when it cannot
    // be worked out (r.problem says why).
    auto heightOf = [&](const Item &it) -> double {
        switch (it.frame) {
        case FrameGlobalRelative:
        case FrameGlobalRelativeInt:
            return it.alt;
        case FrameGlobal:
        case FrameGlobalInt:
            if (!std::isfinite(homeAmslM)) {
                r.problem = QStringLiteral("Item %1 has a sea-level height, and home's height is not known").arg(it.seq);
                return std::nan("");
            }
            return it.alt - homeAmslM;
        case FrameTerrain:
        case FrameTerrainInt:
            r.problem = QStringLiteral("Item %1 is at a height above terrain; terrain data is not used yet").arg(it.seq);
            return std::nan("");
        default:
            r.problem = QStringLiteral("Item %1 has height frame %2, which is not handled").arg(it.seq).arg(it.frame);
            return std::nan("");
        }
    };
    // Flies to an item's position and height (0, 0 = here; height 0 = the
    // present height), stopping at the end when @p endStopped.
    auto flyTo = [&](const Item &it, bool endStopped, StepKind kind, double v) -> bool {
        double tLat = lat, tLon = lon;
        if (!(it.lat == 0.0 && it.lon == 0.0)) {
            tLat = it.lat;
            tLon = it.lon;
        }
        double tAlt = alt;
        if (it.alt != 0.0) {
            tAlt = heightOf(it);
            if (!std::isfinite(tAlt))
                return false;
        }
        const double d = PowerTypeModel::distanceM(lat, lon, tLat, tLon);
        const double dz = tAlt - alt;
        const double th = moveTimeS(d, v, s.accelMss, s.jerkMsss, stopped, endStopped);
        const double tv = heightTimeS(dz, s);
        lat = tLat;
        lon = tLon;
        alt = tAlt;
        if (d > 0.0 || dz != 0.0)
            add(kind, it.seq, std::max(th, tv), d, dz, false);
        stopped = endStopped || d <= 0.0;
        return true;
    };
    auto land = [&](int seq) {
        add(StepKind::Land, seq, landTimeS(alt, s), 0.0, -alt, false);
        alt = 0.0;
        stopped = true;
        r.endsLanded = true;
    };
    // ArduCopter's RTL: climb to RTL_ALT_M if lower, fly home, hold
    // RTL_LOIT_TIME, then land (or stay at RTL_ALT_FINAL_M when set).
    auto returnHome = [&](int seq) {
        if (alt < s.rtlAltM)
            add(StepKind::Climb, seq, heightTimeS(s.rtlAltM - alt, s), 0.0, s.rtlAltM - alt, false);
        alt = std::max(alt, s.rtlAltM);
        const double v = s.rtlSpeedMs > 0.0 ? s.rtlSpeedMs : s.speedMs;
        const double d = PowerTypeModel::distanceM(lat, lon, homeLat, homeLon);
        const double th = moveTimeS(d, v, s.accelMss, s.jerkMsss, stopped, true);
        lat = homeLat;
        lon = homeLon;
        if (d > 0.0)
            add(StepKind::Return, seq, th, d, 0.0, false);
        stopped = true;
        if (s.rtlLoiterS > 0.0)
            add(StepKind::Hold, seq, s.rtlLoiterS, 0.0, 0.0, true);
        if (s.rtlFinalAltM > 0.0) {
            // Stays in the air above home at RTL_ALT_FINAL_M.
            add(StepKind::Climb, seq, heightTimeS(s.rtlFinalAltM - alt, s), 0.0, s.rtlFinalAltM - alt, false);
            alt = s.rtlFinalAltM;
            return;
        }
        land(seq);
    };

    // Jumps left per DO_JUMP item, as ArduPilot counts them.
    QHash<int, int> jumpsLeft;
    int i = indexOf(start.nextSeq);
    if (i < 0) {
        r.problem = QStringLiteral("Item %1 is not in the mission").arg(start.nextSeq);
        return r;
    }
    int guard = 0;
    while (i >= 0 && i < items.size()) {
        if (++guard > 10000) {
            r.problem = QStringLiteral("The mission loops too many times to time");
            return r;
        }
        const Item &it = items[i];
        if (it.seq == 0) {   // home
            ++i;
            continue;
        }
        // The copter stops at a waypoint unless it has no hold and the next
        // navigation command is another waypoint.
        auto nextNavIsWaypoint = [&]() {
            for (int k = i + 1; k < items.size(); ++k) {
                const int c = items[k].command;
                if (c == DoJump)
                    return false;   // a jump: assume a stop (the target may be anything)
                if (isNav(c))
                    return c == NavWaypoint || c == NavSplineWaypoint;
            }
            return false;
        };
        switch (it.command) {
        case NavTakeoff: {
            const double target = heightOf(it);
            if (!std::isfinite(target))
                return r;
            if (target > alt)
                add(StepKind::Climb, it.seq, heightTimeS(target - alt, s), 0.0, target - alt, false);
            alt = std::max(alt, target);
            stopped = true;
            arrive(it);
            break;
        }
        case NavWaypoint:
        case NavSplineWaypoint: {
            const bool hold = it.p1 > 0.0;
            if (!flyTo(it, hold || !nextNavIsWaypoint(), StepKind::Move, speed))
                return r;
            arrive(it);
            if (hold)
                add(StepKind::Hold, it.seq, it.p1, 0.0, 0.0, true);
            break;
        }
        case NavLoiterTime:
            if (!flyTo(it, true, StepKind::Move, speed))
                return r;
            arrive(it);
            add(StepKind::Hold, it.seq, std::max(0.0, it.p1), 0.0, 0.0, true);
            break;
        case NavLoiterToAlt:
            // ArduCopter flies to the point and its height together and stops
            // there, finishes any height still left on the spot (WP_SPD_UP /
            // WP_SPD_DN), then goes on to the next item.
            if (!flyTo(it, true, StepKind::Move, speed))
                return r;
            arrive(it);
            break;
        case NavLoiterTurns: {
            if (!flyTo(it, true, StepKind::Move, speed))
                return r;
            arrive(it);
            const double turns = std::max(0.0, it.p1);
            const double radius = std::abs(it.p3);
            const double rate = std::abs(s.circleRateDegS);
            if (!(rate > 0.0)) {
                r.problem = QStringLiteral("Item %1 circles, and CIRCLE_RATE is not read from the copter").arg(it.seq);
                return r;
            }
            // ArduPilot turns at CIRCLE_RATE, slowed where that would exceed
            // the mission speed on the circle.
            const double degPerS = radius > 0.0 ? std::min(rate, speed / radius * 180.0 / kPi) : rate;
            add(StepKind::Circle, it.seq, turns * 360.0 / degPerS, turns * 2.0 * kPi * radius, 0.0, radius <= 0.0);
            stopped = true;
            break;
        }
        case NavLoiterUnlimited:
            r.problem = QStringLiteral("Item %1 loiters with no end").arg(it.seq);
            return r;
        case NavDelay:
            if (it.p1 < 0.0) {
                r.problem = QStringLiteral("Item %1 waits for a time of day").arg(it.seq);
                return r;
            }
            add(StepKind::Hold, it.seq, it.p1, 0.0, 0.0, true);
            stopped = true;
            arrive(it);
            break;
        case NavReturnToLaunch:
            returnHome(it.seq);
            arrive(it);
            r.ok = true;
            r.totalS = r.missionEndS = t;
            return r;
        case NavLand:
            if (!(it.lat == 0.0 && it.lon == 0.0)) {
                Item there = it;
                there.alt = 0.0;   // fly there at the present height, then land
                if (!flyTo(there, true, StepKind::Move, speed))
                    return r;
            }
            land(it.seq);
            arrive(it);
            r.ok = true;
            r.totalS = r.missionEndS = t;
            return r;
        case DoJump: {
            const int target = static_cast<int>(std::lround(it.p1));
            const int repeats = static_cast<int>(std::lround(it.p2));
            if (repeats < 0) {
                r.problem = QStringLiteral("Item %1 jumps back with no end").arg(it.seq);
                return r;
            }
            if (!jumpsLeft.contains(it.seq))
                jumpsLeft.insert(it.seq, repeats);
            if (jumpsLeft[it.seq] > 0) {
                --jumpsLeft[it.seq];
                const int j = indexOf(target);
                if (j < 0) {
                    r.problem = QStringLiteral("Item %1 jumps to item %2, which is not in the mission").arg(it.seq).arg(target);
                    return r;
                }
                i = j;
                continue;
            }
            break;
        }
        case DoChangeSpeed:
            if (it.p2 > 0.0)
                speed = it.p2;
            break;
        case DoSetServo:
        case DoRepeatServo:
        case DoGripper: {
            Event e;
            e.seq = it.seq;
            e.command = it.command;
            e.p1 = it.p1;
            e.p2 = it.p2;
            e.timeS = t;
            e.lat = lat;
            e.lon = lon;
            e.altM = alt;
            r.events.append(e);
            break;
        }
        default:
            if (!takesNoTime(it.command)) {
                r.problem = QStringLiteral("Item %1 is command %2, which is not handled").arg(it.seq).arg(it.command);
                return r;
            }
            break;
        }
        ++i;
    }
    // The mission ran out of items in the air: ArduPilot holds position
    // there until the pilot takes over. The return the pilot is expected to
    // fly is added unless the caller asked for the mission alone.
    r.ok = true;
    r.missionEndS = t;
    r.endsLanded = false;
    if (start.assumeReturn) {
        returnHome(-1);
        Item home;
        home.seq = -1;
        home.command = NavReturnToLaunch;
        arrive(home);
        r.returnAssumed = true;
    }
    r.totalS = t;
    return r;
}

/// Reads a Mission Planner / QGroundControl .waypoints file ("QGC WPL 110",
/// then one tab-separated item per line). Empty with @p error set when it is
/// not one.
inline QVector<Item> parseWaypoints(const QString &text, QString *error = nullptr)
{
    QVector<Item> out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.isEmpty() || !lines.first().trimmed().startsWith(QStringLiteral("QGC WPL 110"))) {
        if (error)
            *error = QStringLiteral("Not a .waypoints file (no \"QGC WPL 110\" line)");
        return out;
    }
    for (int n = 1; n < lines.size(); ++n) {
        const QString line = lines[n].trimmed();
        if (line.isEmpty())
            continue;
        const QStringList f = line.split(QRegularExpression(QStringLiteral("\\s+")));
        bool ok = f.size() == 12;
        QVector<double> v;
        for (int k = 0; ok && k < 12; ++k) {
            v.append(f[k].toDouble(&ok));
        }
        if (!ok) {
            if (error)
                *error = QStringLiteral("Line %1 is not a mission item").arg(n + 1);
            return {};
        }
        Item it;
        it.seq = static_cast<int>(v[0]);
        it.frame = static_cast<int>(v[2]);
        it.command = static_cast<int>(v[3]);
        it.p1 = v[4];
        it.p2 = v[5];
        it.p3 = v[6];
        it.p4 = v[7];
        it.lat = v[8];
        it.lon = v[9];
        it.alt = v[10];
        it.autocontinue = v[11] != 0.0;
        out.append(it);
    }
    return out;
}

}   // namespace MissionWalk
