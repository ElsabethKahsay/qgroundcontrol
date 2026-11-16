// Terrain clearance along a mission, for the mission estimate (advice only:
// nothing here holds or delays arming).
//
// A mission flown at heights above home keeps those heights over the ground
// whatever the ground does: where it rises, the copter gets closer to it, and
// on a hillside it flies into it (the autopilot does not climb for terrain on
// such a mission). This checks each leg against the terrain under it.
//
// - The legs come from the mission walk (MissionWalk.h): each step that moves
//   over the ground is a leg from where the step before ended to where it
//   ends, its height above home changing evenly from one end to the other.
//   Climbs and descents on the spot, holds and landings are not legs.
// - The terrain under a leg is a profile: heights above sea level at even
//   spacing from its start to its end (QGroundControl's terrain query gives
//   them; VehicleProfileManager asks for them and keeps them by leg).
// - Home's ground height is taken from the same terrain data (the start of
//   the leg that leaves home), not from the copter's GPS: the mission heights
//   are above home, home is on the ground where the copter armed, and both
//   sides of the comparison then come from one source.
// - The clearance at each terrain point is the planned height above sea level
//   (home's ground + the height above home there) less the terrain height.
//   A leg whose lowest clearance is under the margin is reported with where
//   along it that happens.
//
// Not covered: a circle (loiter turns) is checked only from where it starts
// to where it ends, not around its circumference; missions with heights above
// terrain (frame 10) are not timed by the walk at all.
//
// Header-only and without Q_OBJECT, so it needs no CMake or moc change.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include <QHash>
#include <QString>
#include <QVector>

#include "MissionWalk.h"
#include "PowerTypeModel.h"

namespace TerrainClearance {

/// Clearance under which a leg is reported, metres above the ground.
constexpr double kMarginM = 20.0;
/// A step moving less than this over the ground is not a leg.
constexpr double kMinLegM = 1.0;

/// One leg of the mission: from where the step before ended to where this
/// step ends, heights above home.
struct Leg {
    int seq = -1;                         ///< the mission item the step belongs to (-1: the assumed return)
    double fromLat = 0.0, fromLon = 0.0, fromAltM = 0.0;
    double toLat = 0.0, toLon = 0.0, toAltM = 0.0;

    double lengthM() const { return PowerTypeModel::distanceM(fromLat, fromLon, toLat, toLon); }
    /// Its place on the ground (the terrain under it does not depend on the
    /// heights), the key its terrain profile is kept under.
    QString key() const
    {
        return QStringLiteral("%1,%2>%3,%4").arg(fromLat, 0, 'f', 6).arg(fromLon, 0, 'f', 6)
                                             .arg(toLat, 0, 'f', 6).arg(toLon, 0, 'f', 6);
    }
};

/// The terrain under a leg: heights above sea level from its start to its end,
/// @c spacingM apart, the last two @c finalSpacingM apart (as
/// TerrainPathQuery gives them).
struct Profile {
    bool ok = false;
    double spacingM = 0.0;
    double finalSpacingM = 0.0;
    QVector<double> heights;

    /// Distance along the leg of height @p i.
    double alongM(int i) const
    {
        if (i <= 0)
            return 0.0;
        if (i >= heights.size() - 1)
            return spacingM * (heights.size() - 2) + finalSpacingM;
        return spacingM * i;
    }
};

/// The legs of a walk, from @p startLat / @p startLon at @p startAltM above
/// home (before take-off: home, on the ground).
inline QVector<Leg> legsFrom(const MissionWalk::Result &w, double startLat, double startLon, double startAltM)
{
    QVector<Leg> legs;
    double lat = startLat, lon = startLon, alt = startAltM;
    for (const MissionWalk::Step &st : w.steps) {
        if (st.horizM >= kMinLegM) {
            Leg l;
            l.seq = st.seq;
            l.fromLat = lat;
            l.fromLon = lon;
            l.fromAltM = alt;
            l.toLat = st.lat;
            l.toLon = st.lon;
            l.toAltM = st.altM;
            if (l.lengthM() >= kMinLegM)
                legs.append(l);
        }
        lat = st.lat;
        lon = st.lon;
        alt = st.altM;
    }
    return legs;
}

/// The lowest clearance over a leg's terrain, metres; @p alongM (when given)
/// gets where along the leg it is. NaN when the profile has no heights.
inline double lowestClearance(const Leg &leg, const Profile &p, double groundAtHomeM, double *alongM = nullptr)
{
    const int n = int(p.heights.size());
    if (!p.ok || n < 1 || !std::isfinite(groundAtHomeM))
        return std::numeric_limits<double>::quiet_NaN();
    const double total = n >= 2 ? p.alongM(n - 1) : 0.0;
    double lowest = std::numeric_limits<double>::infinity();
    double where = 0.0;
    for (int i = 0; i < n; ++i) {
        const double d = p.alongM(i);
        const double f = total > 0.0 ? std::clamp(d / total, 0.0, 1.0) : 0.0;
        const double planned = groundAtHomeM + leg.fromAltM + f * (leg.toAltM - leg.fromAltM);
        const double c = planned - p.heights[i];
        if (c < lowest) {
            lowest = c;
            where = d;
        }
    }
    if (alongM)
        *alongM = where;
    return lowest;
}

/// A leg that comes closer to the ground than the margin.
struct Low {
    int seq = -1;
    double clearanceM = 0.0;
    double alongM = 0.0;
    double lat = 0.0, lon = 0.0;     ///< where along the leg
};

struct Result {
    int legs = 0;                    ///< legs checked
    int legsKnown = 0;               ///< of those, with terrain
    bool homeKnown = false;          ///< home's ground height found in the terrain
    double groundAtHomeM = std::numeric_limits<double>::quiet_NaN();
    double lowestM = std::numeric_limits<double>::quiet_NaN();   ///< over the legs with terrain
    QVector<Low> lows;               ///< under the margin, in leg order
    bool complete() const { return homeKnown && legs > 0 && legsKnown == legs; }
};

/// Home's ground height: the first terrain height of a leg that starts at
/// home (within 1 m), from @p profiles. NaN when none has terrain.
inline double groundAtHome(const QVector<Leg> &legs, const QHash<QString, Profile> &profiles, double homeLat,
                           double homeLon)
{
    for (const Leg &l : legs) {
        if (PowerTypeModel::distanceM(l.fromLat, l.fromLon, homeLat, homeLon) > 1.0)
            continue;
        const auto it = profiles.constFind(l.key());
        if (it != profiles.constEnd() && it->ok && !it->heights.isEmpty())
            return it->heights.first();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

/// Each leg against its terrain (@p profiles by Leg::key), with home's ground
/// at @p groundAtHomeM (NaN: not known, nothing can be checked).
inline Result check(const QVector<Leg> &legs, const QHash<QString, Profile> &profiles, double groundAtHomeM,
                    double marginM = kMarginM)
{
    Result r;
    r.legs = int(legs.size());
    r.groundAtHomeM = groundAtHomeM;
    r.homeKnown = std::isfinite(groundAtHomeM);
    if (!r.homeKnown)
        return r;
    for (const Leg &l : legs) {
        const auto it = profiles.constFind(l.key());
        if (it == profiles.constEnd() || !it->ok)
            continue;
        double along = 0.0;
        const double c = lowestClearance(l, *it, groundAtHomeM, &along);
        if (!std::isfinite(c))
            continue;
        ++r.legsKnown;
        if (!std::isfinite(r.lowestM) || c < r.lowestM)
            r.lowestM = c;
        if (c < marginM) {
            Low low;
            low.seq = l.seq;
            low.clearanceM = c;
            low.alongM = along;
            const double len = l.lengthM();
            const double f = len > 0.0 ? std::clamp(along / len, 0.0, 1.0) : 0.0;
            low.lat = l.fromLat + f * (l.toLat - l.fromLat);
            low.lon = l.fromLon + f * (l.toLon - l.fromLon);
            r.lows.append(low);
        }
    }
    return r;
}

} // namespace TerrainClearance
