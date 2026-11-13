// The wind from the copter's lean (step 7c of the mission estimate).
//
// The 22-inch copter has no airspeed sensor, and its autopilot's own wind
// estimate is off (EK3_DRAG_BCOEF_X/Y and EK3_DRAG_MCOEF are 0), so the GCS
// works the wind out from how the copter leans:
//
// - In steady, level flight the thrust's sideways part balances the air
//   drag. With the thrust along the body's z axis (from roll, pitch, yaw),
//   its horizontal part per unit of weight is h = -z_h / z_z (north, east).
// - The drag opposes the copter's motion through the air and grows with the
//   square of the airspeed: m·g·h = k·|v_a|·v_a, where v_a = ground velocity -
//   wind, and k is the copter's drag constant (the same at any weight).
// - Unknown: k and the wind (north, east). Each steady sample gives two
//   equations. Flown in one direction only, k and a wind along that line
//   cannot be told apart; samples in different directions (an out-and-back
//   leg, a turn, a hover) can. learn() needs the ground track to turn by
//   kMinSpreadDeg or more.
// - Once k is known (saved per copter), wind() needs only the last minute
//   of steady flight, in any direction. On one straight leg the fit cannot
//   tell a wrong k from a wind along the track (it fits either way), so k
//   must come from learn() on flights that turned; with turns in the window,
//   a wrong k shows as a poor fit and is refused.
//
// Solving: for a given wind, the best k is a plain least-squares ratio; the
// wind is then found by a coarse grid search refined around the best point.
// The fit's RMS error (in units of the lean, tan) says how well it explains
// the samples; a poor fit is not used.
//
// Header-only and without Q_OBJECT, so it needs no CMake or moc change.
#pragma once

#include <algorithm>
#include <cmath>

#include <QVector>

namespace WindFromLean {

constexpr double kG = 9.80665;
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxWindMs = 25.0;       ///< search range, each axis
constexpr double kMinSpreadDeg = 90.0;    ///< ground-track spread needed to learn k
constexpr int kMinSamples = 20;
constexpr double kMaxRmsTan = 0.03;       ///< about 1.7° of lean unexplained
constexpr double kMaxSteadyClimbMs = 0.5;
constexpr double kMaxSteadyAccelMss = 0.5;

/// One moment of steady flight.
struct Sample {
    double rollRad = 0.0;
    double pitchRad = 0.0;
    double yawRad = 0.0;
    double velN = 0.0;      ///< ground velocity, m/s
    double velE = 0.0;
    double massKg = 0.0;    ///< aboard at the time
};

/// The thrust's horizontal part per unit of weight (north, east) for an
/// attitude: the body z axis in north-east-down, h = -z_h / z_z.
inline void leanVector(double roll, double pitch, double yaw, double *hN, double *hE)
{
    const double zN = std::cos(yaw) * std::sin(pitch) * std::cos(roll) + std::sin(yaw) * std::sin(roll);
    const double zE = std::sin(yaw) * std::sin(pitch) * std::cos(roll) - std::cos(yaw) * std::sin(roll);
    const double zD = std::cos(pitch) * std::cos(roll);
    *hN = -zN / zD;
    *hE = -zE / zD;
}

struct Fit {
    bool ok = false;
    double k = 0.0;             ///< drag constant, N per (m/s)²
    double windN = 0.0;         ///< wind velocity (toward), m/s
    double windE = 0.0;
    double rmsTan = 0.0;
    int samples = 0;
    double spreadDeg = 0.0;
    double speedMs() const { return std::hypot(windN, windE); }
    /// The direction the wind comes from, degrees (0 = north).
    double fromDeg() const
    {
        double d = std::atan2(-windE, -windN) * 180.0 / kPi;
        return d < 0.0 ? d + 360.0 : d;
    }
};

/// How far the ground tracks of the moving samples spread, degrees (0–180).
inline double trackSpreadDeg(const QVector<Sample> &s)
{
    double best = 0.0;
    for (int i = 0; i < s.size(); ++i) {
        if (std::hypot(s[i].velN, s[i].velE) < 2.0)
            continue;
        for (int j = i + 1; j < s.size(); ++j) {
            if (std::hypot(s[j].velN, s[j].velE) < 2.0)
                continue;
            const double a = std::atan2(s[i].velE, s[i].velN) - std::atan2(s[j].velE, s[j].velN);
            double d = std::fabs(std::remainder(a, 2.0 * kPi)) * 180.0 / kPi;
            best = std::max(best, d);
        }
    }
    return best;
}

namespace detail {

/// Sum of squared lean errors for a wind and k (k <= 0: the best k for this
/// wind is used and returned).
inline double residual(const QVector<Sample> &s, double wN, double wE, double *k)
{
    double num = 0.0, den = 0.0;
    QVector<double> qN(s.size()), qE(s.size()), fN(s.size()), fE(s.size());
    for (int i = 0; i < s.size(); ++i) {
        const double aN = s[i].velN - wN, aE = s[i].velE - wE;
        const double sp = std::hypot(aN, aE);
        qN[i] = sp * aN;
        qE[i] = sp * aE;
        double hN, hE;
        leanVector(s[i].rollRad, s[i].pitchRad, s[i].yawRad, &hN, &hE);
        const double w = s[i].massKg * kG;
        fN[i] = w * hN;
        fE[i] = w * hE;
        num += fN[i] * qN[i] + fE[i] * qE[i];
        den += qN[i] * qN[i] + qE[i] * qE[i];
    }
    if (*k <= 0.0)
        *k = den > 1e-12 ? std::max(0.0, num / den) : 0.0;
    double sum = 0.0;
    for (int i = 0; i < s.size(); ++i) {
        const double w = s[i].massKg * kG;
        const double eN = (fN[i] - *k * qN[i]) / w;
        const double eE = (fE[i] - *k * qE[i]) / w;
        sum += eN * eN + eE * eE;
    }
    return sum;
}

/// Grid search over the wind, refined around the best point.
inline Fit search(const QVector<Sample> &s, double fixedK)
{
    Fit f;
    f.samples = s.size();
    double bestN = 0.0, bestE = 0.0, bestK = fixedK, best = 1e300;
    double step = 2.0, half = kMaxWindMs;
    double cN = 0.0, cE = 0.0;
    for (int round = 0; round < 8; ++round) {
        for (double n = cN - half; n <= cN + half + 1e-9; n += step) {
            for (double e = cE - half; e <= cE + half + 1e-9; e += step) {
                double k = fixedK;
                const double r = residual(s, n, e, &k);
                if (r < best) {
                    best = r;
                    bestN = n;
                    bestE = e;
                    bestK = k;
                }
            }
        }
        cN = bestN;
        cE = bestE;
        half = step * 2.0;
        step /= 4.0;
    }
    f.k = bestK;
    f.windN = bestN;
    f.windE = bestE;
    f.rmsTan = s.isEmpty() ? 0.0 : std::sqrt(best / (2.0 * s.size()));
    return f;
}

} // namespace detail

/// Learns the drag constant and the wind together. Needs kMinSamples steady
/// samples whose ground tracks spread by kMinSpreadDeg or more, and a fit
/// within kMaxRmsTan.
inline Fit learn(const QVector<Sample> &s)
{
    Fit f;
    f.samples = s.size();
    f.spreadDeg = trackSpreadDeg(s);
    if (s.size() < kMinSamples || f.spreadDeg < kMinSpreadDeg)
        return f;
    f = detail::search(s, -1.0);
    f.spreadDeg = trackSpreadDeg(s);
    f.ok = f.k > 0.0 && f.rmsTan <= kMaxRmsTan;
    return f;
}

/// The wind for a known drag constant (from learn() on earlier flights).
inline Fit wind(const QVector<Sample> &s, double k)
{
    Fit f;
    f.samples = s.size();
    if (s.size() < kMinSamples / 2 || !(k > 0.0))
        return f;
    f = detail::search(s, k);
    f.spreadDeg = trackSpreadDeg(s);
    f.ok = f.rmsTan <= kMaxRmsTan;
    return f;
}

/// Airspeed (m/s) from the ground velocity and a wind (toward, m/s); with no
/// wind known, @p fallbackMs (the ground speed: the same in still air).
inline double airspeedMs(double velN, double velE, double windN, double windE, double fallbackMs)
{
    if (!std::isfinite(velN) || !std::isfinite(velE) || !std::isfinite(windN) || !std::isfinite(windE))
        return fallbackMs;
    return std::hypot(velN - windN, velE - windE);
}

/// Steady enough to use: level (climb under kMaxSteadyClimbMs) and not
/// speeding up or slowing down (change of ground velocity under
/// kMaxSteadyAccelMss since the last sample).
inline bool isSteady(double climbMs, double accelMss)
{
    return std::fabs(climbMs) < kMaxSteadyClimbMs && std::fabs(accelMss) < kMaxSteadyAccelMss;
}

} // namespace WindFromLean
