/**
 * @file PowerTypeModel.h
 * @brief Battery maths for the copter estimates: a hover/cruise current
 *        model, the out-and-back trip to a target, the live time and
 *        distance left, and the pack's charge from its resting voltage.
 *
 * The model is four numbers: hover current at 0 kg, hover extra per kg,
 * cruise current at 0 kg, cruise extra per kg. The motor table fills them
 * (VehicleProfileManager), from the motors' power now in flight or from the
 * remembered hover on the ground.
 *
 * Pure maths: no Qt object, no database. Header-only so it needs no CMake change.
 */

#pragma once

#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace PowerTypeModel {

/// Below this ground speed a second counts as hover (take-off, landing and
/// time at the target included); at or above it, as cruise.
constexpr double kHoverMaxSpeedMs = 3.0;
/// The payload counts as dropped when the copter first comes this close to the target.
constexpr double kDropRadiusM = 20.0;
/// A LiPo pack is full at or above this resting voltage per cell.
constexpr double kFullCellV = 4.15;
/// The in-flight correction of the model current is limited to ±30 %.
constexpr double kLiveRatioMin = 0.70;
constexpr double kLiveRatioMax = 1.30;

/// How much of the model is known. Only usable() matters to the estimates:
/// the motor table sets PayloadNotModelled once it has a current.
enum class FitStatus {
    NotEnoughFlights,     ///< Fewer than two payload-free mixes: no model yet
    TooAlike,             ///< The flights cannot separate hover from cruise
    PayloadNotModelled,   ///< 0 kg currents known; no payload flight yet
    PayloadProportional,  ///< Payload assumed to add the same % in both phases
    Full                  ///< All four numbers learned
};

struct Model {
    double hover0  = 0.0;  ///< A at 0 kg
    double hoverK  = 0.0;  ///< Extra A per kg
    double cruise0 = 0.0;
    double cruiseK = 0.0;
    FitStatus status = FitStatus::NotEnoughFlights;

    bool   usable() const { return status != FitStatus::NotEnoughFlights && status != FitStatus::TooAlike; }
    double hoverA(double payloadKg)  const { return hover0  + hoverK  * payloadKg; }
    double cruiseA(double payloadKg) const { return cruise0 + cruiseK * payloadKg; }
};

// ── The out-and-back trip to a target (before take-off) ──────────────────
// Used by VehicleProfileManager::missionCheck() (the trip's figures; its
// checklist item was replaced by the mission estimate, MissionAssess.h).

struct Mission {
    double payloadKg     = 0.0;
    double targetKm      = 0.0;
    double minAtTarget   = 0.0;
    double outSpeedMs    = 0.0;   ///< Ground speed out (cruise speed, less any headwind)
    double backSpeedMs   = 0.0;   ///< Ground speed back
    double takeoffMin    = 1.0;
    double landMin       = 1.0;
    double usableAh      = 0.0;   ///< Charge above the landing reserve
};

struct MissionResult {
    double takeoffAh = 0.0, outAh = 0.0, targetAh = 0.0, backAh = 0.0, landAh = 0.0;
    double totalAh   = 0.0;
    double totalMin  = 0.0;
    double spareAh   = 0.0;       ///< usableAh − totalAh (negative = short)
    double maxTargetKm = 0.0;     ///< Furthest target this payload can reach and return from
    bool   fits = false;
};

/// Minutes to cover @p km at @p speedMs (0 when the speed is not positive).
inline double minutesFor(double km, double speedMs)
{
    return speedMs > 0.0 ? km * 1000.0 / speedMs / 60.0 : 0.0;
}

/// Costs the legs: take-off and the time at the target hover with the payload,
/// the way out cruises with it, the way back cruises without it (dropped),
/// the landing hovers without it.
inline MissionResult checkMission(const Model &m, const Mission &x)
{
    MissionResult r;
    const double outMin  = minutesFor(x.targetKm, x.outSpeedMs);
    const double backMin = minutesFor(x.targetKm, x.backSpeedMs);
    r.takeoffAh = m.hoverA(x.payloadKg)  * x.takeoffMin  / 60.0;
    r.outAh     = m.cruiseA(x.payloadKg) * outMin        / 60.0;
    r.targetAh  = m.hoverA(x.payloadKg)  * x.minAtTarget / 60.0;
    r.backAh    = m.cruiseA(0.0)         * backMin       / 60.0;
    r.landAh    = m.hoverA(0.0)          * x.landMin     / 60.0;
    r.totalAh   = r.takeoffAh + r.outAh + r.targetAh + r.backAh + r.landAh;
    r.totalMin  = x.takeoffMin + outMin + x.minAtTarget + backMin + x.landMin;
    r.spareAh   = x.usableAh - r.totalAh;
    r.fits      = r.spareAh >= 0.0;

    // Each km of target distance costs one km out (loaded) and one back (empty).
    const double fixedAh = r.takeoffAh + r.targetAh + r.landAh;
    const double perKmAh = (x.outSpeedMs  > 0.0 ? m.cruiseA(x.payloadKg) / (x.outSpeedMs  * 3.6) : 0.0)
                         + (x.backSpeedMs > 0.0 ? m.cruiseA(0.0)         / (x.backSpeedMs * 3.6) : 0.0);
    r.maxTargetKm = perKmAh > 0.0 ? std::max(0.0, (x.usableAh - fixedAh) / perKmAh) : 0.0;
    return r;
}

// ── In flight ────────────────────────────────────────────────────────────

/// Correction of the model current from the live synthetic current (already
/// averaged over 30 s), limited to ±30 %.
inline double liveRatio(double liveSynthA, double factor, double modelA)
{
    if (liveSynthA <= 0.0 || modelA <= 0.0)
        return 1.0;
    return std::clamp(liveSynthA * factor / modelA, kLiveRatioMin, kLiveRatioMax);
}

/// Fallback correction without a synthetic current: thrust follows throttle,
/// power follows thrust^1.5. Limited to ±30 %.
inline double throttleRatio(double throttleNow, double phaseRefThrottle)
{
    if (throttleNow <= 0.0 || phaseRefThrottle <= 0.0)
        return 1.0;
    return std::clamp(std::pow(throttleNow / phaseRefThrottle, 1.5), kLiveRatioMin, kLiveRatioMax);
}

/// What is left of the trip from here. On the way out: the rest of the way
/// to the target (payload aboard), the time left there, the way home from the
/// target (empty) and the landing. On the way back set kmToTarget and
/// minAtTargetLeft to 0, kmHome to the distance home and payloadAboard false.
struct RemainingTrip {
    double kmToTarget      = 0.0;
    double minAtTargetLeft = 0.0;
    double kmHome          = 0.0;  ///< Target → home on the way out; here → home on the way back
    bool   payloadAboard   = false;
    double payloadKg       = 0.0;
    double outSpeedMs      = 0.0;
    double backSpeedMs     = 0.0;
    double landMin         = 1.0;
    double ratio           = 1.0;  ///< liveRatio() or throttleRatio()
};

inline double remainingTripAh(const Model &m, const RemainingTrip &t)
{
    const double kg = t.payloadAboard ? t.payloadKg : 0.0;
    const double ah = m.cruiseA(kg)  * minutesFor(t.kmToTarget, t.outSpeedMs) / 60.0
                    + m.hoverA(kg)   * t.minAtTargetLeft                      / 60.0
                    + m.cruiseA(0.0) * minutesFor(t.kmHome, t.backSpeedMs)    / 60.0
                    + m.hoverA(0.0)  * t.landMin                              / 60.0;
    return ah * t.ratio;
}

/// Ah short of finishing the trip above the reserve; 0 when it still fits.
inline double returnShortAh(double usableLeftAh, double remainingAh)
{
    return std::max(0.0, remainingAh - usableLeftAh);
}

// ── Phase and pack helpers ───────────────────────────────────────────────

inline bool isHover(double groundSpeedMs) { return groundSpeedMs < kHoverMaxSpeedMs; }

/// Resting cell voltage at or above which a pack counts as full: 4.15 V for
/// LiPo and Li-ion (charged to 4.20 V; they rest a little below that), 4.30 V
/// for high-voltage LiPo ("LiHV").
inline double fullCellVolts(const QString &chemistry)
{
    if (chemistry.compare(QLatin1String("LiHV"), Qt::CaseInsensitive) == 0)
        return 4.30;
    // Li-ion here is charged to 4.20 V per cell; it rests a little below that.
    if (chemistry.compare(QLatin1String("LiIon"), Qt::CaseInsensitive) == 0)
        return 4.15;
    return kFullCellV;
}

namespace detail {
struct CurvePoint { double v, pct; };
/// Resting cell voltage against charge left, for each chemistry, on a typical
/// discharge curve: LiPo; LiHV is LiPo 0.15 V higher; Li-ion ("LiIon") is used
/// from 4.20 V (full) to 2.9 V (empty) per cell, with the usual Li-ion shape
/// (steeper at both ends, flatter in the middle). A pack's own curve, from its
/// cell maker or learned from flights, is better than these.
inline const CurvePoint *curve(const QString &chemistry, int &n)
{
    static const CurvePoint lipo[] = {
        {3.27, 0}, {3.61, 5}, {3.69, 10}, {3.71, 15}, {3.73, 20}, {3.75, 25}, {3.77, 30}, {3.79, 35},
        {3.80, 40}, {3.82, 45}, {3.84, 50}, {3.85, 55}, {3.87, 60}, {3.91, 65}, {3.95, 70}, {3.98, 75},
        {4.02, 80}, {4.08, 85}, {4.11, 90}, {4.15, 95}, {4.20, 100} };
    static const CurvePoint liion[] = {
        {2.90, 0}, {3.20, 5}, {3.35, 10}, {3.48, 20}, {3.57, 30}, {3.65, 40}, {3.73, 50},
        {3.81, 60}, {3.90, 70}, {4.00, 80}, {4.08, 90}, {4.20, 100} };
    if (chemistry.compare(QLatin1String("LiIon"), Qt::CaseInsensitive) == 0) {
        n = int(sizeof(liion) / sizeof(CurvePoint));
        return liion;
    }
    n = int(sizeof(lipo) / sizeof(CurvePoint));
    return lipo;
}
inline double hvShift(const QString &chemistry)
{
    return chemistry.compare(QLatin1String("LiHV"), Qt::CaseInsensitive) == 0 ? 0.15 : 0.0;
}
} // namespace detail

/// Charge left (%) from the resting cell voltage (no load), on the
/// chemistry's curve (detail::curve). Rough (about ±15 %): for a pack that was
/// not full at arming, in place of assuming it full. -1 when the voltage is
/// not a cell voltage.
inline double restingSocPct(double cellVolts, const QString &chemistry)
{
    if (!std::isfinite(cellVolts) || cellVolts < 2.5 || cellVolts > 4.5)
        return -1.0;
    int n = 0;
    const detail::CurvePoint *c = detail::curve(chemistry, n);
    const double v = cellVolts - detail::hvShift(chemistry);
    if (v <= c[0].v)
        return 0.0;
    if (v >= c[n - 1].v)
        return 100.0;
    for (int i = 1; i < n; ++i) {
        if (v <= c[i].v) {
            const double t = (v - c[i - 1].v) / (c[i].v - c[i - 1].v);
            return c[i - 1].pct + t * (c[i].pct - c[i - 1].pct);
        }
    }
    return 100.0;
}

/// The other way round: the resting cell voltage at `socPct` charge left, on
/// the same curve.
inline double restingCellVolts(double socPct, const QString &chemistry)
{
    int n = 0;
    const detail::CurvePoint *c = detail::curve(chemistry, n);
    const double p = std::clamp(socPct, 0.0, 100.0);
    double v = c[n - 1].v;
    for (int i = 1; i < n; ++i) {
        if (p <= c[i].pct) {
            const double t = (p - c[i - 1].pct) / (c[i].pct - c[i - 1].pct);
            v = c[i - 1].v + t * (c[i].v - c[i - 1].v);
            break;
        }
    }
    return v + detail::hvShift(chemistry);
}

/// Above this relative altitude (or at cruise speed) the copter is airborne.
/// Time armed on the ground draws idle current, not hover current, so it is
/// left out of the hover and cruise minutes.
constexpr double kAirborneAltM = 1.0;

/// Great-circle distance in metres.
inline double distanceM(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double kEarthRadiusM = 6371000.0;
    constexpr double kRad = 3.14159265358979323846 / 180.0;
    const double dLat = (lat2 - lat1) * kRad;
    const double dLon = (lon2 - lon1) * kRad;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
                   + std::cos(lat1 * kRad) * std::cos(lat2 * kRad) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(a)));
}

// ── Live estimate (Fly view) ─────────────────────────────────────────────

/// What the Fly view knows every update.
struct LiveInput {
    Model  model;
    double factor        = 1.0;     ///< Charger factor (charger mAh ÷ FC mAh)
    double capacityAh    = 0.0;     ///< The selected pack
    double reserve       = 0.20;    ///< Share of the pack never planned
    double fcMahUsed     = -1.0;    ///< FC counter (synthetic or measured); < 0 = none
    double socPct        = -1.0;    ///< Fallback when there is no mAh counter
    double currentA      = -1.0;    ///< FC current, smoothed; < 0 = none
    double groundSpeedMs = std::nan("");
    double altitudeM     = std::nan("");
    double lat = std::nan(""), lon = std::nan("");
    double homeLat = std::nan(""), homeLon = std::nan("");
    double targetLat = std::nan(""), targetLon = std::nan("");
    bool   payloadAboard = false;   ///< Until the copter first reached the target
    double payloadKg     = 0.0;
    double minAtTarget   = 0.0;
    double cruiseSpeedMs = 10.0;    ///< The copter's mission speed (WP_SPD); ≤ 0 = not known
    double landMin       = 1.0;
    bool   armed         = false;
    /// Charge the pack had when the count started (%): 100 for a full pack,
    /// else estimated from its resting voltage (restingSocPct).
    double startSocPct   = 100.0;
    /// Correction from the throttle instead of the FC current (when the FC
    /// reports no current that follows throttle); < 0 = not used.
    double throttleRatio = -1.0;
};

struct LiveResult {
    bool   valid       = false;     ///< A model, a pack and a charge reading
    bool   airborne    = false;
    bool   hover       = true;
    double usableAh    = -1.0;      ///< Charge left above the reserve
    double currentA    = -1.0;      ///< Expected current now (model × live ratio)
    double ratio       = 1.0;
    double timeLeftMin = -1.0;      ///< At the expected current, down to the reserve
    double distLeftKm  = -1.0;      ///< Time left at the ground speed (cruise speed when hovering)
    double hoverMin    = -1.0;      ///< Endurance hovering / cruising with the payload aboard
    double cruiseMin   = -1.0;
    double cruiseKm    = -1.0;
    bool   tripKnown   = false;     ///< Position and home known
    double tripNeedAh  = -1.0;      ///< The rest of the trip: to the target, time there, home, land
    double tripShortAh = 0.0;
    bool   returnNow   = false;     ///< Airborne and the rest of the trip no longer fits
};

/// Charge left above the reserve (the FC's mAh used × the charger factor, or
/// its %), the expected current for the phase the copter is in (the model,
/// corrected by the live FC current within ±30 % while flying), time and
/// distance left, and whether the rest of the trip still fits: with the
/// payload aboard, the way to the target, the time there and the way home
/// empty; after the drop (or with no target), the way home. RETURN NOW only
/// while airborne.
inline LiveResult liveEstimate(const LiveInput &in)
{
    LiveResult r;
    if (!in.model.usable() || in.capacityAh <= 0.0)
        return r;
    double usedAh = -1.0;
    if (in.fcMahUsed >= 0.0)
        usedAh = in.fcMahUsed / 1000.0 * in.factor;
    else if (in.socPct >= 0.0)
        usedAh = in.capacityAh * (1.0 - in.socPct / 100.0);
    if (usedAh < 0.0)
        return r;
    r.valid = true;
    // A counter (FC mAh or the GCS's own count) starts at the arm: what it
    // counts comes off the charge the pack had then. The FC's % already is
    // the charge left.
    const double startAh = in.fcMahUsed >= 0.0 ? in.capacityAh * std::clamp(in.startSocPct, 0.0, 100.0) / 100.0
                                               : in.capacityAh;
    r.usableAh = std::max(0.0, startAh - usedAh - in.reserve * in.capacityAh);

    const double gs = std::isfinite(in.groundSpeedMs) ? in.groundSpeedMs : 0.0;
    r.hover = isHover(gs);
    r.airborne = (std::isfinite(in.altitudeM) && in.altitudeM > kAirborneAltM) || !r.hover;
    const double kg = in.payloadAboard ? in.payloadKg : 0.0;
    const double modelA = r.hover ? in.model.hoverA(kg) : in.model.cruiseA(kg);
    if (!(in.armed && r.airborne))
        r.ratio = 1.0;
    else if (in.throttleRatio > 0.0)
        r.ratio = std::clamp(in.throttleRatio, kLiveRatioMin, kLiveRatioMax);
    else
        r.ratio = liveRatio(in.currentA, in.factor, modelA);
    r.currentA = modelA * r.ratio;
    // Distances need a speed: the ground speed while moving, the cruise speed
    // while hovering. With no cruise speed known (the copter's WP_SPD not
    // read yet) they stay unknown (-1) and the trip is not costed.
    const bool speedKnown = in.cruiseSpeedMs > 0.0;
    if (r.currentA > 0.0) {
        r.timeLeftMin = r.usableAh / r.currentA * 60.0;
        if (!r.hover || speedKnown)
            r.distLeftKm = r.timeLeftMin * 60.0 * (r.hover ? in.cruiseSpeedMs : gs) / 1000.0;
    }
    if (in.model.hoverA(kg) > 0.0)
        r.hoverMin = r.usableAh / in.model.hoverA(kg) * 60.0;
    if (in.model.cruiseA(kg) > 0.0) {
        r.cruiseMin = r.usableAh / in.model.cruiseA(kg) * 60.0;
        if (speedKnown)
            r.cruiseKm = r.cruiseMin * 60.0 * in.cruiseSpeedMs / 1000.0;
    }

    const bool posValid = std::isfinite(in.lat) && std::isfinite(in.lon);
    const bool homeValid = std::isfinite(in.homeLat) && std::isfinite(in.homeLon);
    if (!posValid || !homeValid || !speedKnown)
        return r;
    RemainingTrip t;
    t.payloadKg = in.payloadKg;
    t.outSpeedMs = in.cruiseSpeedMs;
    t.backSpeedMs = in.cruiseSpeedMs;
    t.landMin = in.landMin;
    t.ratio = r.ratio;
    if (in.payloadAboard && std::isfinite(in.targetLat) && std::isfinite(in.targetLon)) {
        t.payloadAboard = true;
        t.kmToTarget = distanceM(in.lat, in.lon, in.targetLat, in.targetLon) / 1000.0;
        t.minAtTargetLeft = in.minAtTarget;
        t.kmHome = distanceM(in.targetLat, in.targetLon, in.homeLat, in.homeLon) / 1000.0;
    } else {
        t.kmHome = distanceM(in.lat, in.lon, in.homeLat, in.homeLon) / 1000.0;
    }
    r.tripKnown = true;
    r.tripNeedAh = remainingTripAh(in.model, t);
    r.tripShortAh = returnShortAh(r.usableAh, r.tripNeedAh);
    r.returnNow = in.armed && r.airborne && r.tripShortAh > 0.0;
    return r;
}

} // namespace PowerTypeModel
