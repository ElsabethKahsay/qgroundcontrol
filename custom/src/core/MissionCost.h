// Battery cost of a timed mission (step 4 of the mission estimate).
//
// MissionWalk gives the flight as steps (climb, move, hold, circle, return,
// land) with their times. Each step is costed here from the motor table:
//
// - Weight: the operator's empty weight plus the payload still aboard. A
//   payload comes off at its drop time (a mission drop command, or the point
//   the operator picked for an RC drop); without a drop time it is carried
//   home, the safe case.
// - Hover power: the table's power for the weight's thrust per motor, in air
//   of the given density ratio (powerForThrustAt).
// - Moving: hover power × the cruise ratio for the leg's airspeed. The
//   airspeed is the ground velocity against the wind (ArduPilot holds the
//   ground speed, so a headwind leg needs more power). The ratio is the one
//   measured on this copter in that 2 m/s speed band. Until it is measured:
//   with the copter's drag constant k known (learned from its lean,
//   WindFromLean.h), 1 + k·airspeed³ ÷ hover power (the hover plus the power
//   to push through the air: head and tail winds count), and in the
//   pessimistic case at least kUnmeasuredPessimisticRatio; with neither
//   known, 1.0 likely and kUnmeasuredPessimisticRatio pessimistic, whatever
//   the airspeed (the wind then changes little). Holding in wind is costed
//   the same way at the wind's speed.
// - Climbing: on top, the power to lift the weight (m × g × climb rate)
//   divided by kClimbEfficiency: half of the extra power is taken as lost.
// - Descending and landing: hover power (the safe side; a descent needs less).
// - The pessimistic case also uses the gust speed instead of the mean wind.
//
// The pack is stepped through 1 % of charge at a time, each at the current
// for its voltage there (the curve of its chemistry and its drop under load),
// times the charger fine-tuning factor. That gives the charge left at the end
// of each step and on arrival at each waypoint, in both cases.
//
// At each waypoint the return home from there is costed too: the copter's RTL
// (as MissionWalk flies it) from that point, at the weight then aboard. The
// last waypoint from which the pessimistic return still lands above the
// reserve is the turn-back point; the first one past it is named.
//
// Each step is also checked against the pack's current limit, the spare
// thrust (full-throttle thrust at the loaded voltage ÷ weight, at least
// MotorTable::kMinThrustToWeight) and the autopilot's BATT_LOW_VOLT and
// BATT_CRT_VOLT. These go to the reasons list; nothing here stops a flight.
//
// Header-only and without Q_OBJECT, so it needs no CMake or moc change.
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>

#include <QString>
#include <QStringList>
#include <QVector>

#include "MissionWalk.h"
#include "MotorTable.h"

namespace MissionCost {

constexpr double kG = 9.80665;
/// Share of the extra climbing power that lifts the copter; the rest is lost.
constexpr double kClimbEfficiency = 0.5;
/// Cruise ratio in the pessimistic case until this copter's ratio is measured.
constexpr double kUnmeasuredPessimisticRatio = 1.4;

inline double nan() { return std::nan(""); }

/// A payload coming off at a time in the walk.
struct Drop {
    double timeS = 0.0;
    double kg = 0.0;
};

/// The wind at the flight height (from SiteWeather), toward which nothing is
/// assumed when unknown (speed NaN = no wind used).
struct Wind {
    double speedMs = nan();
    double fromDeg = nan();
    double gustMs = nan();
};

/// Everything the costing needs besides the walk.
struct Inputs {
    const MotorTable::Table *table = nullptr;
    int motors = 0;
    MotorTable::Pack pack;
    double sigma = MotorTable::kTableDensityRatio;   ///< air density ÷ the table's
    double calibration = 1.0;                         ///< charger fine-tuning, × current
    double emptyKg = nan();                           ///< without payload
    double payloadKg = 0.0;                           ///< all payload at take-off
    QVector<Drop> drops;                              ///< when parts of it come off
    double startSocPct = 100.0;
    double reserveSocPct = 20.0;
    Wind wind;
    /// This copter's measured cruise ratio for an airspeed (its 2 m/s band),
    /// or <= 0 when not measured. May be empty (nothing measured).
    std::function<double(double airspeedMs)> measuredRatio;
    /// The copter's drag constant from its lean (N per (m/s)²); 0 = not known.
    double dragK = 0.0;
    double currentLimitA = nan();   ///< the pack's continuous current
    double battLowV = nan();        ///< BATT_LOW_VOLT (0 or NaN = not set)
    double battCrtV = nan();        ///< BATT_CRT_VOLT
};

/// One case (likely or pessimistic) of one step.
struct StepCost {
    double motorW = 0.0;
    double socEndPct = 0.0;
    double currentA = 0.0;       ///< battery current at the step's end (its highest)
    double loadedV = 0.0;
    double airspeedMs = 0.0;
    double ratio = 1.0;
    double spareThrust = -1.0;   ///< full-throttle thrust at the loaded voltage ÷ weight; -1 unknown
};

struct ArrivalCost {
    int seq = -1;                ///< -1: the assumed return's landing
    double timeS = 0.0;
    double socLikelyPct = 0.0;
    double socPessimisticPct = 0.0;
    double weightKg = 0.0;       ///< aboard on arrival
    /// Charge left on landing if the copter returns home from here (RTL).
    double returnLandLikelyPct = nan();
    double returnLandPessimisticPct = nan();
    double returnS = nan();
    bool canReturn = true;       ///< the pessimistic return lands above the reserve
};

struct Result {
    bool ok = false;
    QString problem;             ///< why it cannot be costed
    QVector<StepCost> likely;
    QVector<StepCost> pessimistic;
    QVector<ArrivalCost> arrivals;
    double landLikelyPct = nan();
    double landPessimisticPct = nan();
    int turnBackSeq = -2;        ///< the last waypoint with a safe return; -2 = none needed (all safe), -1 = not even the first
    int firstPastSeq = -2;       ///< the first waypoint past the turn-back point; -2 = none
    bool emptied = false;        ///< the pack runs out (or cannot give the power) before the end
    bool ratioMeasured = false;  ///< every moving step had a measured cruise ratio
    /// The pessimistic case goes over the pack's current limit somewhere (a
    /// warning, item 32: the flight is not judged on it).
    bool overCurrent = false;
    /// Spare thrust at the start of the walk (take-off; in flight, now):
    /// full-throttle thrust at the pack's voltage while hovering at the
    /// weight aboard on the starting charge, ÷ that weight. 0 when the pack
    /// cannot even give the hover power.
    double spareAtStart = -1.0;
    QStringList reasons;
};

/// Charge (%) left after drawing `motorW` for `seconds` from `socFrom`,
/// stepping through the pack 1 % at a time. *empty is set when it runs out or
/// cannot give the power (the charge where that happens is returned).
inline double socAfter(const MotorTable::Pack &p, double motorW, double socFrom, double seconds, double calibration,
                       bool *empty)
{
    double hi = std::clamp(socFrom, 0.0, 100.0);
    double left = std::max(0.0, seconds);
    while (left > 1e-9) {
        if (hi <= 1e-9) {
            *empty = true;
            return 0.0;
        }
        const double lo = std::max(0.0, hi - MotorTable::kPackStepPct);
        const double rest = p.cells * PowerTypeModel::restingCellVolts((hi + lo) / 2.0, p.chemistry);
        const double i = MotorTable::packCurrent(motorW, rest, p.resistanceOhm) * calibration;
        if (!(i > 0.0)) {
            *empty = true;
            return hi;
        }
        const double stepS = p.capacityAh * (hi - lo) / 100.0 / i * 3600.0;
        if (stepS >= left)
            return hi - (hi - lo) * left / stepS;
        left -= stepS;
        hi = lo;
    }
    return hi;
}

/// Airspeed (m/s) of a copter moving over the ground at `groundMs` on
/// `bearingDeg` in a wind of `windMs` from `windFromDeg`.
inline double airspeedMs(double groundMs, double bearingDeg, double windMs, double windFromDeg)
{
    if (!std::isfinite(windMs) || windMs <= 0.0)
        return std::max(0.0, groundMs);
    constexpr double d2r = MissionWalk::kPi / 180.0;
    const double from = std::isfinite(windFromDeg) ? windFromDeg : bearingDeg;   // direction unknown: headwind
    // Ground velocity minus the air's velocity (which blows toward from + 180°).
    const double gx = groundMs * std::sin(bearingDeg * d2r);
    const double gy = groundMs * std::cos(bearingDeg * d2r);
    const double wx = -windMs * std::sin(from * d2r);
    const double wy = -windMs * std::cos(from * d2r);
    return std::hypot(gx - wx, gy - wy);
}

inline double bearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double d2r = MissionWalk::kPi / 180.0;
    const double y = std::sin((lon2 - lon1) * d2r) * std::cos(lat2 * d2r);
    const double x = std::cos(lat1 * d2r) * std::sin(lat2 * d2r)
                     - std::sin(lat1 * d2r) * std::cos(lat2 * d2r) * std::cos((lon2 - lon1) * d2r);
    const double b = std::atan2(y, x) / d2r;
    return b < 0 ? b + 360.0 : b;
}

namespace detail {

/// Payload aboard at time t.
inline double payloadAt(const Inputs &in, double t)
{
    double kg = in.payloadKg;
    for (const Drop &d : in.drops)
        if (d.timeS <= t + 1e-9)
            kg -= d.kg;
    return std::max(0.0, kg);
}

/// Costs `steps` (positions start at lat/lon) from `soc`, one case.
/// Returns the charge at the end; fills `out` per step.
inline double costSteps(const QVector<MissionWalk::Step> &steps, const Inputs &in, bool pessimistic, double lat,
                        double lon, double soc, double timeOffsetS, double fixedPayloadKg, QVector<StepCost> *out,
                        bool *empty, bool *allMeasured)
{
    const MotorTable::Table &t = *in.table;
    const double wind = pessimistic && std::isfinite(in.wind.gustMs) ? std::max(in.wind.gustMs, in.wind.speedMs)
                                                                     : in.wind.speedMs;
    auto ratioFor = [&](double air, bool moving, double hoverW) {
        const double m = in.measuredRatio ? in.measuredRatio(air) : -1.0;
        if (m > 0.0)
            return m;
        if (moving && allMeasured)
            *allMeasured = false;
        // Not measured: hover power while holding in calm air; with the drag
        // constant known, the hover plus the drag power; else the defaults.
        if (air < MotorTable::kMinCruiseSpeedMs)
            return 1.0;
        if (in.dragK > 0.0 && hoverW > 0.0) {
            const double drag = 1.0 + in.dragK * air * air * air / hoverW;
            return pessimistic ? std::max(kUnmeasuredPessimisticRatio, drag) : drag;
        }
        return pessimistic ? kUnmeasuredPessimisticRatio : 1.0;
    };
    for (const MissionWalk::Step &st : steps) {
        const double tMid = timeOffsetS + st.startS;
        const double payload = fixedPayloadKg >= 0.0 ? fixedPayloadKg : payloadAt(in, tMid);
        const double kg = in.emptyKg + payload;
        const double hoverW = in.motors * MotorTable::powerForThrustAt(t, kg * 1000.0 / in.motors, in.sigma).watts;
        StepCost c;
        double w = hoverW;
        const bool moving = st.horizM > 0.0 && st.timeS > 0.0;
        if (moving) {
            const double ground = st.horizM / st.timeS;
            const double b = bearingDeg(lat, lon, st.lat, st.lon);
            c.airspeedMs = airspeedMs(ground, b, wind, in.wind.fromDeg);
            c.ratio = ratioFor(c.airspeedMs, true, hoverW);
        } else {
            c.airspeedMs = std::isfinite(wind) ? wind : 0.0;
            c.ratio = ratioFor(c.airspeedMs, false, hoverW);
        }
        w *= c.ratio;
        if (st.climbM > 0.0 && st.timeS > 0.0)
            w += kg * kG * st.climbM / st.timeS / kClimbEfficiency;
        c.motorW = w;
        soc = socAfter(in.pack, w, soc, st.timeS, in.calibration, empty);
        c.socEndPct = soc;
        const double rest = in.pack.cells * PowerTypeModel::restingCellVolts(soc, in.pack.chemistry);
        c.currentA = MotorTable::packCurrent(w, rest, in.pack.resistanceOhm) * in.calibration;
        c.loadedV = c.currentA > 0.0 ? rest - c.currentA / in.calibration * in.pack.resistanceOhm : -1.0;
        if (c.loadedV > 0.0)
            c.spareThrust = in.motors * MotorTable::maxThrustAt(t, c.loadedV, in.sigma).grams / (kg * 1000.0);
        if (out)
            out->append(c);
        lat = st.lat;
        lon = st.lon;
    }
    return soc;
}

} // namespace detail

/// Costs the walk `w` of a mission (from MissionWalk::walk) for the inputs.
/// `s` and home are those the walk used; they time the return from each
/// waypoint.
inline Result cost(const MissionWalk::Result &w, const MissionWalk::Settings &s, double homeLat, double homeLon,
                   const Inputs &in)
{
    Result r;
    if (!w.ok) {
        r.problem = w.problem;
        return r;
    }
    if (!in.table || !in.table->isValid()) {
        r.problem = QStringLiteral("No motor table linked");
        return r;
    }
    if (!(in.motors > 0)) {
        r.problem = QStringLiteral("Number of motors not known");
        return r;
    }
    if (!in.pack.isValid()) {
        r.problem = QStringLiteral("Battery pack not known");
        return r;
    }
    if (!std::isfinite(in.emptyKg) || in.emptyKg <= 0.0) {
        r.problem = QStringLiteral("Weight missing: enter the copter's weight without payload");
        return r;
    }

    // Spare thrust at the start (take-off, or now in flight), at the hover.
    {
        const double kg = in.emptyKg + in.payloadKg;
        const double hoverW = in.motors * MotorTable::powerForThrustAt(*in.table, kg * 1000.0 / in.motors, in.sigma).watts;
        const double v = MotorTable::loadedVolts(in.pack, in.startSocPct, hoverW);
        r.spareAtStart = v > 0.0 ? in.motors * MotorTable::maxThrustAt(*in.table, v, in.sigma).grams / (kg * 1000.0) : 0.0;
    }

    // Both cases through the whole flight.
    bool emptyL = false, emptyP = false, measured = true;
    r.landLikelyPct = detail::costSteps(w.steps, in, false, homeLat, homeLon, in.startSocPct, 0.0, -1.0, &r.likely,
                                        &emptyL, &measured);
    r.landPessimisticPct = detail::costSteps(w.steps, in, true, homeLat, homeLon, in.startSocPct, 0.0, -1.0,
                                             &r.pessimistic, &emptyP, nullptr);
    r.ratioMeasured = measured;
    r.emptied = emptyL || emptyP;

    // Charge on arrival: the end of the last step that finishes by then.
    auto socAt = [&](const QVector<StepCost> &costs, double t) {
        double soc = in.startSocPct;
        for (int k = 0; k < w.steps.size(); ++k) {
            if (w.steps[k].startS + w.steps[k].timeS > t + 1e-6)
                break;
            soc = costs[k].socEndPct;
        }
        return soc;
    };

    // The return home from each waypoint, at the weight then aboard.
    QVector<MissionWalk::Item> rtl(2);
    rtl[0].seq = 0;
    rtl[0].lat = homeLat;
    rtl[0].lon = homeLon;
    rtl[1].seq = 1;
    rtl[1].command = MissionWalk::NavReturnToLaunch;
    for (const MissionWalk::Arrival &a : w.arrivals) {
        ArrivalCost ac;
        ac.seq = a.seq;
        ac.timeS = a.timeS;
        ac.socLikelyPct = socAt(r.likely, a.timeS);
        ac.socPessimisticPct = socAt(r.pessimistic, a.timeS);
        const double payload = detail::payloadAt(in, a.timeS);
        ac.weightKg = in.emptyKg + payload;
        const bool landed = a.command == MissionWalk::NavReturnToLaunch || a.command == MissionWalk::NavLand;
        if (!landed) {
            MissionWalk::Start from;
            from.lat = a.lat;
            from.lon = a.lon;
            from.altM = a.altM;
            from.nextSeq = 1;
            const MissionWalk::Result back = MissionWalk::walk(rtl, s, homeLat, homeLon, from);
            if (back.ok) {
                bool e1 = false, e2 = false;
                ac.returnS = back.totalS;
                ac.returnLandLikelyPct = detail::costSteps(back.steps, in, false, a.lat, a.lon, ac.socLikelyPct,
                                                           0.0, payload, nullptr, &e1, nullptr);
                ac.returnLandPessimisticPct = detail::costSteps(back.steps, in, true, a.lat, a.lon,
                                                                ac.socPessimisticPct, 0.0, payload, nullptr, &e2,
                                                                nullptr);
                ac.canReturn = !e2 && ac.returnLandPessimisticPct >= in.reserveSocPct;
            }
        }
        r.arrivals.append(ac);
    }

    // The turn-back point: the last waypoint before the first one whose
    // pessimistic return lands below the reserve.
    for (int k = 0; k < r.arrivals.size(); ++k) {
        if (!r.arrivals[k].canReturn) {
            r.firstPastSeq = r.arrivals[k].seq;
            r.turnBackSeq = k > 0 ? r.arrivals[k - 1].seq : -1;
            break;
        }
    }

    // Reasons, each named once at the first step where it happens.
    auto stepName = [&](int k) {
        const int seq = w.steps[k].seq;
        return seq < 0 ? QStringLiteral("the assumed return") : QStringLiteral("item %1").arg(seq);
    };
    bool overCurrent = false, lowThrust = false, low = false, crt = false;
    for (int k = 0; k < r.pessimistic.size(); ++k) {
        const StepCost &c = r.pessimistic[k];
        if (!overCurrent && std::isfinite(in.currentLimitA) && in.currentLimitA > 0.0 && c.currentA > in.currentLimitA) {
            overCurrent = true;
            r.overCurrent = true;
            r.reasons << QStringLiteral("Battery current %1 A at %2, above the pack's %3 A")
                             .arg(c.currentA, 0, 'f', 0).arg(stepName(k)).arg(in.currentLimitA, 0, 'f', 0);
        }
        if (!lowThrust && c.spareThrust > 0.0) {
            const double spare = c.spareThrust;
            if (spare < MotorTable::kMinThrustToWeight) {
                lowThrust = true;
                r.reasons << QStringLiteral("Spare thrust %1 at %2, below %3")
                                 .arg(spare, 0, 'f', 2).arg(stepName(k)).arg(MotorTable::kMinThrustToWeight, 0, 'f', 1);
            }
        }
        if (!low && std::isfinite(in.battLowV) && in.battLowV > 0.0 && c.loadedV > 0.0 && c.loadedV < in.battLowV) {
            low = true;
            r.reasons << QStringLiteral("Voltage reaches BATT_LOW_VOLT (%1 V) at %2: the autopilot's low-battery action starts")
                             .arg(in.battLowV, 0, 'f', 1).arg(stepName(k));
        }
        if (!crt && std::isfinite(in.battCrtV) && in.battCrtV > 0.0 && c.loadedV > 0.0 && c.loadedV < in.battCrtV) {
            crt = true;
            r.reasons << QStringLiteral("Voltage reaches BATT_CRT_VOLT (%1 V) at %2")
                             .arg(in.battCrtV, 0, 'f', 1).arg(stepName(k));
        }
    }
    if (r.emptied)
        r.reasons << QStringLiteral("The battery runs out before the end");
    if (r.firstPastSeq >= -1)
        r.reasons << (r.turnBackSeq >= 0
                          ? QStringLiteral("Past the turn-back point at item %1: from there the return lands below the reserve")
                                .arg(r.firstPastSeq)
                          : QStringLiteral("Even from the first waypoint the return lands below the reserve"));
    if (r.landPessimisticPct < in.reserveSocPct)
        r.reasons << QStringLiteral("Lands with %1 % (pessimistic), below the %2 % reserve")
                         .arg(r.landPessimisticPct, 0, 'f', 0).arg(in.reserveSocPct, 0, 'f', 0);
    if (!r.ratioMeasured)
        r.reasons << (in.dragK > 0.0
                          ? QStringLiteral("Cruise power not measured yet on this copter: hover plus the drag from its "
                                           "lean (at least %1× hover pessimistic)").arg(kUnmeasuredPessimisticRatio, 0, 'f', 1)
                          : QStringLiteral("Cruise power not measured yet on this copter: %1× hover assumed (pessimistic); "
                                           "the wind is hardly costed until it or the drag is measured")
                                .arg(kUnmeasuredPessimisticRatio, 0, 'f', 1));
    if (w.returnAssumed)
        r.reasons << QStringLiteral("The mission has no return: the RTL from its last point is assumed");
    r.ok = true;
    return r;
}

/// The "if the payload comes back" case: the same flight with nothing
/// dropped.
inline Result costPayloadReturned(const MissionWalk::Result &w, const MissionWalk::Settings &s, double homeLat,
                                  double homeLon, Inputs in)
{
    in.drops.clear();
    return cost(w, s, homeLat, homeLon, in);
}

} // namespace MissionCost
