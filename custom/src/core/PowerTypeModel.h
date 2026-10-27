/**
 * @file PowerTypeModel.h
 * @brief Copter power-type maths: learn hover and cruise current from charger
 *        numbers, cost a payload round trip to a target, and track it in flight.
 *
 * A power type is one way of building a copter's power system (frame, props,
 * cells). Its model is four numbers: hover current at 0 kg, hover extra per kg,
 * cruise current at 0 kg, cruise extra per kg. They are learned from flights:
 * each flight that started on a full pack gives one equation,
 *
 *     charger Ah = Σ over its minutes (phase current at the payload aboard) × time
 *
 * because the mAh the charger puts back equals the charge the flight used.
 * The fit runs in two stages. The 0 kg currents come from the payload-free
 * time of the flights (two flights with a different hover/cruise mix pin them
 * down); the per-kg extras then come from what the payload flights used beyond
 * that. With only one payload flight (or payload flights too alike) the payload
 * is assumed to add the same percentage in both phases.
 *
 * The flight controller's synthetic current (BATT_MONITOR = 25, a straight
 * line in throttle) is corrected by the charger factor: charger mAh ÷ the FC's
 * mAh for the same flights. ArduPilot's own recalibration of BATT_AMP_PERVLT
 * uses the same ratio.
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
/// A pack is full at arming at or above this resting voltage per cell.
constexpr double kFullCellV = 4.15;
/// Plausible per-cell voltage range for the cell-count check.
constexpr double kMinCellV = 3.3;
constexpr double kMaxCellV = 4.35;
/// A flight is used for learning only with at least this share of telemetry.
constexpr double kMinTelemetryCoverage = 0.90;
/// A flight that misses the model by more than this is flagged.
constexpr double kOutlierFraction = 0.20;
/// A type is validated when each of its last kValidationFlights flights is
/// predicted within this fraction.
constexpr double kValidationFraction = 0.10;
constexpr int    kValidationFlights = 3;
/// The in-flight correction of the model current is limited to ±30 %.
constexpr double kLiveRatioMin = 0.70;
constexpr double kLiveRatioMax = 1.30;
/// Two flights whose hover/cruise mixes point almost the same way cannot
/// separate hover from cruise. 1 − cos² of the angle between them must
/// exceed this.
constexpr double kMinSeparation = 0.05;

/// One flight as the recorder stores it. Minutes are split by phase and by
/// whether the payload was aboard (it is dropped at the target).
struct Flight {
    double hoverMinEmpty   = 0.0;   ///< Hover minutes with no payload aboard
    double hoverMinLoaded  = 0.0;   ///< Hover minutes with the payload aboard
    double cruiseMinEmpty  = 0.0;
    double cruiseMinLoaded = 0.0;
    double payloadKg       = 0.0;   ///< Payload carried while "loaded"
    double chargerAh       = -1.0;  ///< Charger number; < 0 = not entered
    double fcAh            = -1.0;  ///< The FC's mAh used / 1000; < 0 = none

    double hoverMin()  const { return hoverMinEmpty + hoverMinLoaded; }
    double cruiseMin() const { return cruiseMinEmpty + cruiseMinLoaded; }
    bool   loaded()    const { return payloadKg > 0.0 && (hoverMinLoaded + cruiseMinLoaded) > 0.0; }
};

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
    QString advice;        ///< What to fly next, when the fit needs more

    bool   usable() const { return status != FitStatus::NotEnoughFlights && status != FitStatus::TooAlike; }
    double hoverA(double payloadKg)  const { return hover0  + hoverK  * payloadKg; }
    double cruiseA(double payloadKg) const { return cruise0 + cruiseK * payloadKg; }

    /// Charge the model says a recorded flight used (Ah).
    double predictAh(const Flight &f) const
    {
        return (hoverA(0.0)  * f.hoverMinEmpty  + hoverA(f.payloadKg)  * f.hoverMinLoaded
              + cruiseA(0.0) * f.cruiseMinEmpty + cruiseA(f.payloadKg) * f.cruiseMinLoaded) / 60.0;
    }
};

namespace detail {
/// Least squares for y ≈ x1·a + x2·b. Returns false when the rows cannot
/// separate x1 from x2 (too few, or their (a, b) directions too alike).
inline bool solve2(const QVector<double> &a, const QVector<double> &b,
                   const QVector<double> &y, double &x1, double &x2)
{
    double saa = 0, sbb = 0, sab = 0, say = 0, sby = 0;
    for (int i = 0; i < a.size(); ++i) {
        saa += a[i] * a[i]; sbb += b[i] * b[i]; sab += a[i] * b[i];
        say += a[i] * y[i]; sby += b[i] * y[i];
    }
    const double det = saa * sbb - sab * sab;
    if (saa <= 0.0 || sbb <= 0.0 || det / (saa * sbb) <= kMinSeparation)
        return false;
    x1 = (say * sbb - sby * sab) / det;
    x2 = (sby * saa - say * sab) / det;
    return true;
}
} // namespace detail

/// Learn the model from accepted flights (each with a charger number).
inline Model fit(const QVector<Flight> &flights)
{
    Model m;

    // Stage 1: 0 kg currents from payload-free flights.
    QVector<double> a, b, y;
    double hoverShare = 0.0;
    for (const Flight &f : flights) {
        if (f.chargerAh < 0.0 || f.loaded())
            continue;
        a << f.hoverMin() / 60.0;
        b << f.cruiseMin() / 60.0;
        y << f.chargerAh;
        const double total = f.hoverMin() + f.cruiseMin();
        hoverShare += total > 0.0 ? f.hoverMin() / total : 0.0;
    }
    if (a.size() < 2) {
        m.advice = QStringLiteral("Fly %1 more flight(s) without payload: one mostly cruise, one mostly hover")
                       .arg(2 - a.size());
        return m;
    }
    if (!detail::solve2(a, b, y, m.hover0, m.cruise0)) {
        m.status = FitStatus::TooAlike;
        m.advice = hoverShare / a.size() >= 0.5
                       ? QStringLiteral("Fly one mostly-cruise flight without payload")
                       : QStringLiteral("Fly one mostly-hover flight without payload");
        return m;
    }

    // Stage 2: per-kg extras from what the payload flights used beyond the
    // 0 kg currents.
    QVector<double> pa, pb, py, pz;
    for (const Flight &f : flights) {
        if (f.chargerAh < 0.0 || !f.loaded())
            continue;
        pa << f.payloadKg * f.hoverMinLoaded / 60.0;
        pb << f.payloadKg * f.cruiseMinLoaded / 60.0;
        py << f.chargerAh - m.predictAh(f);   // hoverK/cruiseK are still 0 here
        pz << (m.hover0 * f.hoverMinLoaded + m.cruise0 * f.cruiseMinLoaded) * f.payloadKg / 60.0;
    }
    if (pa.isEmpty()) {
        m.status = FitStatus::PayloadNotModelled;
        m.advice = QStringLiteral("Fly one flight with a known payload");
        return m;
    }
    if (detail::solve2(pa, pb, py, m.hoverK, m.cruiseK)) {
        m.status = FitStatus::Full;
        return m;
    }
    // Same percentage per kg in both phases: extra = r · base · kg.
    double syz = 0.0, szz = 0.0;
    for (int i = 0; i < py.size(); ++i) { syz += py[i] * pz[i]; szz += pz[i] * pz[i]; }
    const double r = szz > 0.0 ? syz / szz : 0.0;
    m.hoverK  = r * m.hover0;
    m.cruiseK = r * m.cruise0;
    m.status  = FitStatus::PayloadProportional;
    m.advice  = QStringLiteral("Fly one mostly-hover flight with the payload kept aboard");
    return m;
}

/// The flight missed the model by more than kOutlierFraction.
inline bool isOutlier(const Model &m, const Flight &f)
{
    if (!m.usable() || f.chargerAh <= 0.0)
        return false;
    return std::abs(m.predictAh(f) - f.chargerAh) / f.chargerAh > kOutlierFraction;
}

/// At least kValidationFlights accepted flights, and the last ones (the end
/// of @p flights) are each predicted within kValidationFraction.
inline bool isValidated(const Model &m, const QVector<Flight> &flights)
{
    if (!m.usable() || flights.size() < kValidationFlights)
        return false;
    for (int i = flights.size() - kValidationFlights; i < flights.size(); ++i) {
        const Flight &f = flights[i];
        if (f.chargerAh <= 0.0 || std::abs(m.predictAh(f) - f.chargerAh) / f.chargerAh > kValidationFraction)
            return false;
    }
    return true;
}

// ── Synthetic current calibration ────────────────────────────────────────

/// Charger mAh ÷ FC mAh over the flights that have both; 1.0 when none do.
inline double chargerFactor(const QVector<Flight> &flights)
{
    double charger = 0.0, fc = 0.0;
    for (const Flight &f : flights) {
        if (f.chargerAh > 0.0 && f.fcAh > 0.0) { charger += f.chargerAh; fc += f.fcAh; }
    }
    return fc > 0.0 ? charger / fc : 1.0;
}

/// ArduPilot's recalibration: new BATT_AMP_PERVLT = old × (charger mAh ÷ FC mAh).
/// Returns @p oldAmpPerVolt when either number is missing.
inline double suggestedAmpPerVolt(double oldAmpPerVolt, double chargerMah, double fcMah)
{
    return (chargerMah > 0.0 && fcMah > 0.0) ? oldAmpPerVolt * chargerMah / fcMah : oldAmpPerVolt;
}

/// A starting model from the FC's synthetic current per phase (× the charger
/// factor), used until there are enough flights to fit. Payload not modelled.
inline Model seedFromSynthetic(double hoverSynthA, double cruiseSynthA, double factor)
{
    Model m;
    if (hoverSynthA <= 0.0 || cruiseSynthA <= 0.0)
        return m;
    m.hover0  = hoverSynthA * factor;
    m.cruise0 = cruiseSynthA * factor;
    m.status  = FitStatus::PayloadNotModelled;
    m.advice  = QStringLiteral("From synthetic current, not yet learned");
    return m;
}

// ── Mission check (before take-off) ──────────────────────────────────────

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

/// Charge used so far: the FC's mAh × the charger factor. Returns < 0 when
/// the FC reports none (the caller then uses its own running sum).
inline double chargeUsedAh(double fcAh, double factor)
{
    return fcAh >= 0.0 ? fcAh * factor : -1.0;
}

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

// ── Recorder helpers ─────────────────────────────────────────────────────

inline bool isHover(double groundSpeedMs) { return groundSpeedMs < kHoverMaxSpeedMs; }

/// Battery voltage fits the power type's cell count.
inline bool cellsPlausible(double packVolts, int cells)
{
    return cells > 0 && packVolts >= kMinCellV * cells && packVolts <= kMaxCellV * cells;
}

inline bool fullAtArm(double restingVolts, int cells)
{
    // Per cell, with a tiny tolerance: 4.15 × 6 is 24.900000000000002 in
    // floating point, which would refuse a pack at exactly 24.9 V.
    return cells > 0 && restingVolts / cells >= kFullCellV - 1e-9;
}

/// A recorded flight may join the learning: it started on a full pack, has a
/// charger number and enough telemetry.
inline bool usableForLearning(const Flight &f, bool startedFull, double telemetryCoverage)
{
    return startedFull && f.chargerAh > 0.0 && telemetryCoverage >= kMinTelemetryCoverage;
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

/// What the recorder knows about the copter for one tick.
struct TrackSample {
    double groundSpeedMs = std::nan("");
    double altitudeM     = std::nan("");  ///< Relative to home
    double currentA      = -1.0;          ///< FC current (synthetic or measured); < 0 = none
    double throttle      = -1.0;          ///< 0–1; < 0 = none
    double lat           = std::nan("");
    double lon           = std::nan("");
    bool   linkUp        = true;          ///< false while telemetry is lost
};

/// Splits a flight into what the power-type model learns from, one tick at a
/// time: airborne seconds go to hover (below kHoverMaxSpeedMs) or cruise, and
/// to "loaded" until the copter first comes within kDropRadiusM of the target
/// (the payload is dropped there; with no target it stays aboard). Current,
/// throttle and cruise speed are time-weighted averages over the seconds that
/// had them. Seconds without telemetry count against the coverage only.
class FlightTracker {
public:
    void start(double targetLat, double targetLon)
    {
        *this = FlightTracker();
        m_targetLat = targetLat;
        m_targetLon = targetLon;
    }

    void tick(double dtSec, const TrackSample &s)
    {
        if (!(dtSec > 0.0))
            return;
        m_totalSec += dtSec;
        if (!s.linkUp)
            return;
        m_seenSec += dtSec;

        const bool posValid = std::isfinite(s.lat) && std::isfinite(s.lon);
        if (m_loaded && posValid && std::isfinite(m_targetLat) && std::isfinite(m_targetLon)
            && distanceM(s.lat, s.lon, m_targetLat, m_targetLon) <= kDropRadiusM)
            m_loaded = false;

        const double gs = std::isfinite(s.groundSpeedMs) ? s.groundSpeedMs : 0.0;
        const bool airborne = (std::isfinite(s.altitudeM) && s.altitudeM > kAirborneAltM) || !isHover(gs);
        if (!airborne)
            return;

        const double min = dtSec / 60.0;
        Phase &p = isHover(gs) ? m_hover : m_cruise;
        (m_loaded ? p.minLoaded : p.minEmpty) += min;
        if (s.currentA >= 0.0) { p.currentSum += s.currentA * dtSec; p.currentSec += dtSec; }
        if (s.throttle >= 0.0) { p.throttleSum += s.throttle * dtSec; p.throttleSec += dtSec; }
        if (!isHover(gs)) { m_speedSum += gs * dtSec; m_speedSec += dtSec; }
    }

    double hoverMinEmpty()   const { return m_hover.minEmpty; }
    double hoverMinLoaded()  const { return m_hover.minLoaded; }
    double cruiseMinEmpty()  const { return m_cruise.minEmpty; }
    double cruiseMinLoaded() const { return m_cruise.minLoaded; }
    bool   payloadDropped()  const { return !m_loaded; }
    /// Averages; -1 when the phase had no such data.
    double hoverCurrentA()   const { return avg(m_hover.currentSum, m_hover.currentSec); }
    double cruiseCurrentA()  const { return avg(m_cruise.currentSum, m_cruise.currentSec); }
    double hoverThrottle()   const { return avg(m_hover.throttleSum, m_hover.throttleSec); }
    double cruiseThrottle()  const { return avg(m_cruise.throttleSum, m_cruise.throttleSec); }
    double cruiseSpeedMs()   const { return avg(m_speedSum, m_speedSec); }
    /// Share of the flight with telemetry (0 before the first tick).
    double coverage()        const { return m_totalSec > 0.0 ? m_seenSec / m_totalSec : 0.0; }

private:
    struct Phase {
        double minEmpty = 0.0, minLoaded = 0.0;
        double currentSum = 0.0, currentSec = 0.0;
        double throttleSum = 0.0, throttleSec = 0.0;
    };
    static double avg(double sum, double sec) { return sec > 0.0 ? sum / sec : -1.0; }

    double m_targetLat = std::nan("");
    double m_targetLon = std::nan("");
    bool   m_loaded = true;
    Phase  m_hover, m_cruise;
    double m_speedSum = 0.0, m_speedSec = 0.0;
    double m_totalSec = 0.0, m_seenSec = 0.0;
};

} // namespace PowerTypeModel
