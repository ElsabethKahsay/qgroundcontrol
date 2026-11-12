// The mission assessment (step 5 of the mission estimate): one result for the
// whole mission, a state for each waypoint, the reasons and how sure it is.
// Advice only: nothing here holds or delays arming.
//
// Whole mission, the worst that applies (custom/MISSION_ESTIMATE_OPEN_ITEMS.md):
//
//   Can't assess       the walk or the costing could not be done (says why)
//   Too heavy          spare thrust below the minimum (2.0) already at the
//                      start (take-off; in flight, now)
//   Can't be done      the likely case runs out before the mission's last point
//   One way only       reaches the last point but the likely case lands below
//                      the lowest acceptable charge (lowestPct, proposed 10 %)
//   Uses the reserve   the likely case lands between lowestPct and the reserve
//   Tight              only the likely case lands above the reserve
//   Fits               the pessimistic case lands above the reserve
//   Comfortable        the pessimistic case lands at least comfortMarginPts
//                      (10 points) above the reserve
//
// Each waypoint (in order of precedence):
//
//   Not reachable                     the likely case runs out before it
//   Autopilot will turn back before   the pessimistic case reaches BATT_LOW_VOLT
//                                     (or BATT_CRT_VOLT) earlier and that
//                                     failsafe has an action (BATT_FS_*_ACT)
//   Reached only on the reserve       the pessimistic case arrives below the reserve
//   Past the turn-back point          from here the pessimistic return lands
//                                     below the reserve
//   Last safe turn-back point         the last waypoint before that
//   On track                          otherwise
//
// Going over the pack's current limit is a warning, not a result: it is a
// reason, and the preflight item shows a green result amber.
//
// How sure: High when the cruise power is measured on this copter and the
// table is proven against the charger; Low when neither; Medium otherwise. It
// goes down one level more when the weather confidence is Low or there is no
// weather (the wind is then not costed).
//
// Header-only and without Q_OBJECT, so it needs no CMake or moc change.
#pragma once

#include <algorithm>
#include <cmath>

#include <QString>
#include <QStringList>
#include <QVector>

#include "MissionCost.h"
#include "SiteWeather.h"

namespace MissionAssess {

enum Result {
    CantAssess = 0,
    TooHeavy,
    CantBeDone,
    OneWayOnly,
    UsesReserve,
    Tight,
    Fits,
    Comfortable,
};

inline QString resultName(int r)
{
    switch (r) {
    case Comfortable: return QStringLiteral("Comfortable");
    case Fits: return QStringLiteral("Fits");
    case Tight: return QStringLiteral("Tight");
    case UsesReserve: return QStringLiteral("Uses the reserve");
    case OneWayOnly: return QStringLiteral("One way only");
    case CantBeDone: return QStringLiteral("Can't be done");
    case TooHeavy: return QStringLiteral("Too heavy to fly safely");
    default: return QStringLiteral("Can't assess");
    }
}

/// green, amber, red or grey, as in the plan's screens.
inline QString resultColour(int r)
{
    switch (r) {
    case Comfortable:
    case Fits: return QStringLiteral("green");
    case Tight:
    case UsesReserve: return QStringLiteral("amber");
    case OneWayOnly:
    case CantBeDone:
    case TooHeavy: return QStringLiteral("red");
    default: return QStringLiteral("grey");
    }
}

enum WaypointState {
    OnTrack = 0,
    LastSafeTurnBack,
    PastTurnBack,
    OnReserve,
    AutopilotTurnsBack,
    NotReachable,
};

inline QString stateName(int s)
{
    switch (s) {
    case LastSafeTurnBack: return QStringLiteral("Last safe turn-back point");
    case PastTurnBack: return QStringLiteral("Past the turn-back point");
    case OnReserve: return QStringLiteral("Reached only on the reserve");
    case AutopilotTurnsBack: return QStringLiteral("Autopilot will turn back before here");
    case NotReachable: return QStringLiteral("Not reachable");
    default: return QStringLiteral("On track");
    }
}

struct Limits {
    double reservePct = 20.0;          ///< item 29
    double lowestPct = 10.0;           ///< item 30 (proposed)
    double comfortMarginPts = 10.0;    ///< Comfortable: this far above the reserve, pessimistic
    double minThrustToWeight = MotorTable::kMinThrustToWeight;   ///< item 33
    double gustLimitMs = std::nan(""); ///< item 28: red above it (Preflight Settings)
    double battLowV = std::nan("");    ///< BATT_LOW_VOLT
    double battCrtV = std::nan("");    ///< BATT_CRT_VOLT
    int battLowAction = 0;             ///< BATT_FS_LOW_ACT (0 = none)
    int battCrtAction = 0;             ///< BATT_FS_CRT_ACT
};

/// What else the assessment needs to know.
struct Context {
    bool cruiseMeasured = false;       ///< the costing used measured cruise ratios
    bool tableProven = false;          ///< the motor table agrees with the charger
    bool weatherKnown = false;         ///< a wind was used in the costing
    int weatherConfidence = SiteWeather::None;
    QStringList weatherReasons;        ///< SiteWeather::Conditions::reasons
    double gustMs = std::nan("");      ///< at the mission's flight height
    double flightHeightM = std::nan("");
    bool hasDrops = false;             ///< a payload comes off during the flight
    bool inFlight = false;             ///< worked out from where the copter is now
};

struct Waypoint {
    int seq = -1;
    int state = OnTrack;
    double timeS = 0.0;
    double socLikelyPct = 0.0;
    double socPessimisticPct = 0.0;
};

struct Assessment {
    int result = CantAssess;
    QString problem;                   ///< for Can't assess
    double landLikelyPct = std::nan("");
    double landPessimisticPct = std::nan("");
    double totalS = 0.0;
    QVector<Waypoint> waypoints;
    QStringList reasons;
    int confidence = SiteWeather::None;   ///< Low / Medium / High
    QStringList confidenceWhy;
    bool weatherRed = false;           ///< gusts above the limit
    bool overCurrent = false;          ///< over the pack's current limit (a warning only)
};

/// The worst-case assessment of `cost` (MissionCost::cost of the walk `w`).
/// `returned` is the same flight with the payload never dropped
/// (MissionCost::costPayloadReturned), used only when ctx.hasDrops.
inline Assessment assess(const MissionWalk::Result &w, const MissionCost::Result &cost,
                         const MissionCost::Result *returned, const Limits &lim, const Context &ctx)
{
    Assessment a;
    if (!w.ok || !cost.ok) {
        a.problem = !w.ok ? w.problem : cost.problem;
        a.reasons << a.problem;
        return a;
    }
    a.totalS = w.totalS;
    a.landLikelyPct = cost.landLikelyPct;
    a.landPessimisticPct = cost.landPessimisticPct;
    a.overCurrent = cost.overCurrent;

    // When the autopilot's own battery failsafe would act (pessimistic case):
    // the first step whose loaded voltage is below a threshold with an action.
    double failsafeS = std::nan("");
    QString failsafeWhat;
    for (int k = 0; k < cost.pessimistic.size() && k < w.steps.size(); ++k) {
        const double v = cost.pessimistic[k].loadedV;
        if (!(v > 0.0))
            continue;
        const bool lowHit = lim.battLowAction != 0 && std::isfinite(lim.battLowV) && lim.battLowV > 0.0 && v < lim.battLowV;
        const bool crtHit = lim.battCrtAction != 0 && std::isfinite(lim.battCrtV) && lim.battCrtV > 0.0 && v < lim.battCrtV;
        if (lowHit || crtHit) {
            failsafeS = w.steps[k].startS;
            failsafeWhat = lowHit ? QStringLiteral("BATT_LOW_VOLT") : QStringLiteral("BATT_CRT_VOLT");
            break;
        }
    }

    // Each waypoint.
    bool runsOutBeforeEnd = false;
    for (const MissionCost::ArrivalCost &ac : cost.arrivals) {
        Waypoint wp;
        wp.seq = ac.seq;
        wp.timeS = ac.timeS;
        wp.socLikelyPct = ac.socLikelyPct;
        wp.socPessimisticPct = ac.socPessimisticPct;
        const bool ownPoint = ac.seq >= 1 && ac.timeS <= w.missionEndS + 1e-6;
        if (ac.socLikelyPct <= 0.5) {
            wp.state = NotReachable;
            if (ownPoint)
                runsOutBeforeEnd = true;
        } else if (std::isfinite(failsafeS) && ac.timeS > failsafeS + 1e-6) {
            wp.state = AutopilotTurnsBack;
        } else if (ac.socPessimisticPct < lim.reservePct) {
            wp.state = OnReserve;
        } else if (!ac.canReturn) {
            wp.state = PastTurnBack;
        } else if (ac.seq == cost.turnBackSeq) {
            wp.state = LastSafeTurnBack;
        }
        a.waypoints.append(wp);
    }

    // The whole mission: the worst that applies.
    const double spare = cost.spareAtStart;
    if (spare >= 0.0 && spare < lim.minThrustToWeight)
        a.result = TooHeavy;
    else if (runsOutBeforeEnd || cost.landLikelyPct <= 0.5)
        a.result = runsOutBeforeEnd ? CantBeDone : OneWayOnly;
    else if (cost.landLikelyPct < lim.lowestPct)
        a.result = OneWayOnly;
    else if (cost.landLikelyPct < lim.reservePct)
        a.result = UsesReserve;
    else if (cost.landPessimisticPct < lim.reservePct)
        a.result = Tight;
    else if (cost.landPessimisticPct < lim.reservePct + lim.comfortMarginPts)
        a.result = Fits;
    else
        a.result = Comfortable;

    // Reasons: the result's own line first, then the costing's, then the rest.
    if (a.result == TooHeavy)
        a.reasons << QStringLiteral("Spare thrust %1 %2, below %3")
                         .arg(spare, 0, 'f', 2)
                         .arg(ctx.inFlight ? QStringLiteral("now") : QStringLiteral("at take-off"))
                         .arg(lim.minThrustToWeight, 0, 'f', 1);
    a.reasons << cost.reasons;
    if (std::isfinite(failsafeS))
        a.reasons << QStringLiteral("The autopilot's battery failsafe (%1) would act at %2 into the flight")
                         .arg(failsafeWhat)
                         .arg(QStringLiteral("%1:%2").arg(int(failsafeS) / 60).arg(int(failsafeS) % 60, 2, 10, QLatin1Char('0')));
    if (ctx.hasDrops && returned && returned->ok && returned->landPessimisticPct < lim.reservePct)
        a.reasons << QStringLiteral("If the payload comes back: lands with %1 % (pessimistic), below the reserve")
                         .arg(returned->landPessimisticPct, 0, 'f', 0);
    if (std::isfinite(ctx.gustMs) && std::isfinite(lim.gustLimitMs) && lim.gustLimitMs > 0.0 && ctx.gustMs > lim.gustLimitMs) {
        a.weatherRed = true;
        a.reasons << QStringLiteral("Gusts %1 m/s at %2 m, above the %3 m/s gust limit")
                         .arg(ctx.gustMs, 0, 'f', 1)
                         .arg(std::isfinite(ctx.flightHeightM) ? QString::number(qRound(ctx.flightHeightM)) : QStringLiteral("flight height"))
                         .arg(lim.gustLimitMs, 0, 'f', 0);
    }
    for (const QString &r : ctx.weatherReasons)
        if (!a.reasons.contains(r))
            a.reasons << r;
    if (!ctx.weatherKnown)
        a.reasons << QStringLiteral("No weather for the site: wind not costed");

    // How sure.
    if (ctx.cruiseMeasured && ctx.tableProven)
        a.confidence = SiteWeather::High;
    else if (!ctx.cruiseMeasured && !ctx.tableProven)
        a.confidence = SiteWeather::Low;
    else
        a.confidence = SiteWeather::Medium;
    if (!ctx.cruiseMeasured)
        a.confidenceWhy << QStringLiteral("cruise power assumed");
    if (!ctx.tableProven)
        a.confidenceWhy << QStringLiteral("motor table not yet proven against the charger");
    if (!ctx.weatherKnown || ctx.weatherConfidence <= SiteWeather::Low) {
        a.confidence = std::max(int(SiteWeather::Low), a.confidence - 1);
        a.confidenceWhy << (ctx.weatherKnown ? QStringLiteral("weather from the airport only")
                                             : QStringLiteral("no weather"));
    }
    return a;
}

} // namespace MissionAssess
