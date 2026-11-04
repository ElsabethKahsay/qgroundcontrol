/**
 * @file MotorTable.h
 * @brief Copter battery current from a motor maker's test table and each
 *        motor's throttle.
 *
 * A motor table is a test-stand measurement of one motor with one prop at one
 * voltage: for each throttle step, the thrust, the power taken from the supply
 * and the motor speed. The GCS turns it into battery current in flight:
 *
 *   1. each motor's throttle comes from its output signal:
 *      (PWM − MOT_PWM_MIN) ÷ (MOT_PWM_MAX − MOT_PWM_MIN);
 *   2. the motor's speed depends on the voltage it gets, pack voltage ×
 *      throttle, so the table row with the same motor voltage is the one at
 *      throttle × (pack voltage ÷ table voltage);
 *   3. at that row the motor spins at the same speed, gives the same thrust and
 *      takes the same power as on the test stand; only the battery current
 *      differs: current = power ÷ pack voltage;
 *   4. battery current = every motor's current added together, plus the
 *      flight controller, radio and GPS.
 *
 * Between rows the power is read in a straight line. Below the first row it
 * follows the first two rows' power curve down to 0 W at 0 % throttle. Above
 * the last row (a pack above the table's voltage at full throttle) the motor
 * runs faster than the test measured; the power is continued as throttle³
 * (prop power grows with speed³, and speed with throttle), which is more than
 * the test stand's flattening top rows suggest, so the estimate errs towards
 * more current there, and the reading is marked beyond the table.
 *
 * Each table's source CSV (the maker's data as published) is in
 * custom/tools/motor_tables; a test keeps the rows below and the CSV identical.
 *
 * Pure maths: no Qt object, no database. Header-only so it needs no CMake change.
 */

#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include "PowerTypeModel.h"
#include <algorithm>
#include <cmath>

namespace MotorTable {

/// Current drawn by everything that is not a motor (flight controller, radio,
/// GPS), added to the motors' current.
constexpr double kElectronicsA = 1.0;
/// Spare thrust the copter should keep at the end of a normal flight (the
/// reserve voltage, in the site's air): full-throttle thrust ÷ weight. Below
/// it there is too little in hand for gusts and climbs.
constexpr double kMinThrustToWeight = 2.0;
/// Above the last row, power grows with throttle to this power.
constexpr double kBeyondExponent = 3.0;
/// ArduPilot's output functions Motor1 … Motor8 (SERVOn_FUNCTION).
constexpr int kMotor1Function = 33;
constexpr int kMotor8Function = 40;
/// Per-cell voltages used to read the cell count and to plan before take-off.
constexpr double kFullCellV = 4.2;
constexpr double kNominalCellV = 3.7;   ///< used only when the chemistry's curve is not known
/// The GCS asks for each output's signal (SERVO_OUTPUT_RAW) 5 times a second,
/// and counts the charge on every one. This is a message-rate request: it
/// changes no parameter and is gone after a reboot.
constexpr float kMotorOutputIntervalUs = 200000.0f;
/// A gap between two motor-output readings longer than this is not counted
/// as if the last reading had lasted all through it.
constexpr qint64 kMaxReadingGapMs = 1000;
/// Motor outputs older than this are not used.
constexpr qint64 kMotorOutputStaleMs = 3000;
/// A flight's hover is remembered for the next take-off once it has this many
/// 2-second updates of steady hover (30 s).
constexpr int kMinHoverSamples = 15;
/// A sample counts as steady hover only while climbing or sinking slower than this.
constexpr double kHoverMaxClimbMs = 0.5;
/// Internal resistance of one cell group (cells in parallel) until the pack's
/// own is measured in flight: a typical Li-ion or LiPo pack for this current,
/// about 0.15–0.3 V of sag per cell at 60–80 A. Ohms per series cell.
constexpr double kDefaultCellGroupOhm = 0.0042;
/// Steps of charge for the time-left sum, % of the pack.
constexpr double kPackStepPct = 1.0;
/// The drone type's cruise speed: 70 km/h.
constexpr double kCruiseSpeedMs = 70.0 / 3.6;
/// A reading counts as cruise when the ground speed is within this share of
/// the cruise speed and the copter is level (climb as for hover).
constexpr double kCruiseSpeedBand = 0.15;
/// MOT_THST_HOVER's default: a copter that has not learned its hover yet.
constexpr double kDefaultThstHover = 0.35;
/// ArduPilot's ESC range when MOT_PWM_MIN / MOT_PWM_MAX are not usable.
constexpr double kDefaultPwmMin = 1000.0;
constexpr double kDefaultPwmMax = 2000.0;

/// One test-stand row for one motor.
struct Row {
    double throttlePct;   ///< motor throttle, % of the ESC's range
    double thrustG;       ///< thrust, grams
    double powerInW;      ///< power taken from the supply, watts
    double rpm;           ///< motor speed
    double torqueNm;      ///< prop torque
    double powerOutW;     ///< shaft power (torque × speed)
};

/// A table and what it was measured (or derived) for.
struct Table {
    QString id;           ///< stable key stored with a vehicle
    QString name;         ///< what the pilot sees
    double volts = 0.0;   ///< supply voltage the rows are at
    int motors = 0;       ///< motors on the drone type it is for
    int cells = 0;        ///< cells of the pack it is for
    double packCRating = 0.0;  ///< the drone type's pack: continuous discharge rating, × its Ah (0 = not known)
    QVector<Row> rows;    ///< ascending throttle
    bool isValid() const { return volts > 0.0 && rows.size() >= 2; }
};

/// M6215 HighProt 320KV with MSC 22×7.8 props on 6S: the maker's thrust test
/// at 23 V, as published. Source:
/// custom/tools/motor_tables/M6215_HighProt_320KV_MSC22x7.8_23V.csv.
inline const Table &m6215_320kv_22x78_6s()
{
    static const Table t{
        QStringLiteral("m6215-320kv-msc22x7.8-6s"),
        QStringLiteral("M6215 HighProt 320KV · MSC 22×7.8 · 6S quad"),
        23.0, 4, 6, 4.0,   // a 6S Li-ion pack rated 4C continuous
        {
            {33, 1392, 127.5, 2403, 0.37, 93.6},     {35, 1564, 146.9, 2537, 0.42, 110.6},
            {37, 1745, 169.1, 2674, 0.46, 129.8},    {39, 1935, 193.9, 2813, 0.51, 151.2},
            {42, 2230, 235.5, 3017, 0.59, 186.8},    {45, 2533, 281.5, 3215, 0.67, 226.0},
            {48, 2844, 331.1, 3405, 0.75, 268.4},    {51, 3160, 384.0, 3585, 0.84, 313.7},
            {54, 3481, 440.1, 3757, 0.92, 361.5},    {57, 3810, 499.5, 3921, 1.00, 412.1},
            {60, 4147, 562.6, 4079, 1.09, 465.6},    {63, 4494, 630.1, 4233, 1.18, 522.5},
            {66, 4853, 702.7, 4385, 1.27, 583.2},    {69, 5224, 781.3, 4536, 1.37, 648.6},
            {72, 5608, 866.4, 4688, 1.46, 719.0},    {75, 6004, 958.5, 4841, 1.57, 794.9},
            {78, 6408, 1057.4, 4995, 1.68, 876.3},   {81, 6817, 1162.2, 5150, 1.78, 962.3},
            {84, 7223, 1270.5, 5302, 1.89, 1051.1},  {87, 7618, 1378.8, 5445, 2.00, 1139.3},
            {90, 7988, 1482.0, 5573, 2.09, 1222.3},  {100, 8801, 1705.3, 5814, 2.29, 1393.0},
        }};
    return t;
}

/// Every table the GCS knows, for the Vehicles page's choice.
inline QVector<const Table *> all() { return {&m6215_320kv_22x78_6s()}; }

/// The table with this id, or nullptr.
inline const Table *byId(const QString &id)
{
    for (const Table *t : all())
        if (t->id == id)
            return t;
    return nullptr;
}

/// Power at a throttle on the table's own voltage.
struct PowerAt {
    double watts = 0.0;
    bool beyond = false;  ///< above the last row: continued, not measured
};

/// Power one motor takes at `throttle` (0–1) on the table's voltage.
inline PowerAt powerAt(const Table &t, double throttle)
{
    PowerAt out;
    if (!t.isValid() || !std::isfinite(throttle) || throttle <= 0.0)
        return out;
    const double pct = throttle * 100.0;
    const Row &first = t.rows.first();
    const Row &last = t.rows.last();
    if (pct < first.throttlePct) {
        const Row &second = t.rows.at(1);
        const double k = std::log(second.powerInW / first.powerInW) / std::log(second.throttlePct / first.throttlePct);
        out.watts = first.powerInW * std::pow(pct / first.throttlePct, k);
        return out;
    }
    if (pct > last.throttlePct) {
        out.watts = last.powerInW * std::pow(pct / last.throttlePct, kBeyondExponent);
        out.beyond = true;
        return out;
    }
    for (int i = 1; i < t.rows.size(); ++i) {
        const Row &a = t.rows.at(i - 1);
        const Row &b = t.rows.at(i);
        if (pct <= b.throttlePct) {
            const double f = (pct - a.throttlePct) / (b.throttlePct - a.throttlePct);
            out.watts = a.powerInW + f * (b.powerInW - a.powerInW);
            return out;
        }
    }
    out.watts = last.powerInW;
    return out;
}

inline double densityPowerRatio(const Table &t, double motorVolts, double sigma);
inline double densityThrustRatio(const Table &t, double motorVolts, double sigma);
constexpr double kTableDensityRatioDefault = 1.0;

/// One motor's battery current at `throttle` (0–1) on a pack at `packVolts`
/// in air of density ratio `sigma` (the table's density when left out). An
/// unknown pack voltage uses the table's own.
struct MotorCurrent {
    double amps = 0.0;
    bool beyond = false;
};

inline MotorCurrent motorCurrent(const Table &t, double throttle, double packVolts,
                                 double sigma = kTableDensityRatioDefault)
{
    MotorCurrent out;
    if (!t.isValid())
        return out;
    const double v = (std::isfinite(packVolts) && packVolts > 0.0) ? packVolts : t.volts;
    const PowerAt p = powerAt(t, throttle * v / t.volts);
    out.amps = p.watts * densityPowerRatio(t, throttle * v, sigma) / v;
    out.beyond = p.beyond;
    return out;
}

/// Battery current for all motors at their throttles (0–1) plus the electronics.
struct BatteryCurrent {
    double amps = 0.0;
    int motorsBeyond = 0;  ///< motors above the table's last row
};

inline BatteryCurrent batteryCurrent(const Table &t, const QVector<double> &motorThrottles, double packVolts,
                                     double sigma = kTableDensityRatioDefault)
{
    BatteryCurrent out;
    out.amps = kElectronicsA;
    for (double thr : motorThrottles) {
        const MotorCurrent m = motorCurrent(t, thr, packVolts, sigma);
        out.amps += m.amps;
        if (m.beyond)
            ++out.motorsBeyond;
    }
    return out;
}

/// Motor throttle (0–1) from an output signal and the ESC range. −1 when the
/// range is unusable.
inline double pwmToThrottle(double pwm, double pwmMin, double pwmMax)
{
    if (!(pwmMax > pwmMin) || pwmMin <= 0.0)
        return -1.0;
    return std::clamp((pwm - pwmMin) / (pwmMax - pwmMin), 0.0, 1.0);
}

/// Motor throttle (0–1) that gives `thrustG` per motor on the table's voltage,
/// read from the thrust column. −1 outside the table.
inline double throttleForThrust(const Table &t, double thrustG)
{
    if (!t.isValid() || !(thrustG >= t.rows.first().thrustG) || thrustG > t.rows.last().thrustG)
        return -1.0;
    for (int i = 1; i < t.rows.size(); ++i) {
        const Row &a = t.rows.at(i - 1);
        const Row &b = t.rows.at(i);
        if (thrustG <= b.thrustG) {
            const double f = (thrustG - a.thrustG) / (b.thrustG - a.thrustG);
            return (a.throttlePct + f * (b.throttlePct - a.throttlePct)) / 100.0;
        }
    }
    return -1.0;
}

/// Thrust one motor gives at `throttle` (0–1) on the table's voltage. Below the
/// first row the first two rows' curve is continued down to 0 g; above the last
/// row thrust grows with throttle² (thrust grows with speed², speed with
/// throttle), marked beyond.
struct ThrustAt {
    double grams = 0.0;
    bool beyond = false;
};

inline ThrustAt thrustAt(const Table &t, double throttle)
{
    ThrustAt out;
    if (!t.isValid() || !std::isfinite(throttle) || throttle <= 0.0)
        return out;
    const double pct = throttle * 100.0;
    const Row &first = t.rows.first();
    const Row &last = t.rows.last();
    if (pct < first.throttlePct) {
        const Row &second = t.rows.at(1);
        const double k = std::log(second.thrustG / first.thrustG) / std::log(second.throttlePct / first.throttlePct);
        out.grams = first.thrustG * std::pow(pct / first.throttlePct, k);
        return out;
    }
    if (pct > last.throttlePct) {
        out.grams = last.thrustG * std::pow(pct / last.throttlePct, 2.0);
        out.beyond = true;
        return out;
    }
    for (int i = 1; i < t.rows.size(); ++i) {
        const Row &a = t.rows.at(i - 1);
        const Row &b = t.rows.at(i);
        if (pct <= b.throttlePct) {
            const double f = (pct - a.throttlePct) / (b.throttlePct - a.throttlePct);
            out.grams = a.thrustG + f * (b.thrustG - a.thrustG);
            return out;
        }
    }
    out.grams = last.thrustG;
    return out;
}

/// Power one motor takes to give `thrustG`, read from the table's thrust and
/// power columns (the same at any voltage: the same thrust is the same motor
/// speed). Below the first row the first two rows' curve is continued; above
/// the last, power grows with thrust^1.5 (the momentum-theory rule for a prop),
/// marked beyond.
inline PowerAt powerForThrust(const Table &t, double thrustG)
{
    PowerAt out;
    if (!t.isValid() || !std::isfinite(thrustG) || thrustG <= 0.0)
        return out;
    const Row &first = t.rows.first();
    const Row &last = t.rows.last();
    if (thrustG < first.thrustG) {
        const Row &second = t.rows.at(1);
        const double k = std::log(second.powerInW / first.powerInW) / std::log(second.thrustG / first.thrustG);
        out.watts = first.powerInW * std::pow(thrustG / first.thrustG, k);
        return out;
    }
    if (thrustG > last.thrustG) {
        out.watts = last.powerInW * std::pow(thrustG / last.thrustG, 1.5);
        out.beyond = true;
        return out;
    }
    for (int i = 1; i < t.rows.size(); ++i) {
        const Row &a = t.rows.at(i - 1);
        const Row &b = t.rows.at(i);
        if (thrustG <= b.thrustG) {
            const double f = (thrustG - a.thrustG) / (b.thrustG - a.thrustG);
            out.watts = a.powerInW + f * (b.powerInW - a.powerInW);
            return out;
        }
    }
    out.watts = last.powerInW;
    return out;
}

/// Battery current to hover with `thrustG` per motor on `motors` motors at
/// `packVolts`: each motor's power for that thrust ÷ the pack voltage, plus the
/// electronics.
inline double hoverCurrent(const Table &t, double thrustG, int motors, double packVolts)
{
    if (!t.isValid() || motors <= 0 || !(thrustG > 0.0))
        return -1.0;
    const double v = (std::isfinite(packVolts) && packVolts > 0.0) ? packVolts : t.volts;
    return motors * powerForThrust(t, thrustG).watts / v + kElectronicsA;
}

/// Motor throttle (0–1) needed for `thrustG` per motor at `packVolts`: the
/// table's throttle for that thrust × (table voltage ÷ pack voltage). −1
/// outside the table.
inline double hoverThrottleAt(const Table &t, double thrustG, double packVolts)
{
    const double atTable = throttleForThrust(t, thrustG);
    if (atTable < 0.0)
        return -1.0;
    const double v = (std::isfinite(packVolts) && packVolts > 0.0) ? packVolts : t.volts;
    return atTable * t.volts / v;
}

/// Average real thrust per motor from each motor's throttle (0–1) at
/// `packVolts` in air of density ratio `sigma`: each motor's thrust at its
/// throttle × (pack voltage ÷ table voltage), corrected for the density.
inline double averageThrust(const Table &t, const QVector<double> &motorThrottles, double packVolts,
                            double sigma = kTableDensityRatioDefault)
{
    if (!t.isValid() || motorThrottles.isEmpty())
        return -1.0;
    const double v = (std::isfinite(packVolts) && packVolts > 0.0) ? packVolts : t.volts;
    double sum = 0.0;
    for (double thr : motorThrottles)
        sum += thrustAt(t, thr * v / t.volts).grams * densityThrustRatio(t, thr * v, sigma);
    return sum / motorThrottles.size();
}

/// A reading is steady cruise: airborne, at the cruise speed (within
/// kCruiseSpeedBand), neither climbing nor sinking.
inline bool isCruiseSample(double groundSpeedMs, double climbMs, double cruiseSpeedMs = kCruiseSpeedMs)
{
    return std::isfinite(groundSpeedMs) && std::isfinite(climbMs) && cruiseSpeedMs > 0.0
           && std::abs(groundSpeedMs - cruiseSpeedMs) <= kCruiseSpeedBand * cruiseSpeedMs
           && std::abs(climbMs) < kHoverMaxClimbMs;
}

/// The pack, for working out how its voltage falls through the rest of a
/// flight.
struct Pack {
    int cells = 0;               ///< in series
    QString chemistry;           ///< "LiPo", "LiHV", "LiIon" (PowerTypeModel curves)
    double capacityAh = 0.0;
    double resistanceOhm = 0.0;  ///< whole pack; the voltage drops by current × this under load
    bool isValid() const { return cells > 0 && capacityAh > 0.0; }
};

/// Battery current (A) when the motors take `motorWatts` in all from a pack
/// whose resting voltage is `restVolts` and internal resistance `ohm`: the
/// motors' power comes at the loaded voltage, restVolts − current × ohm, and
/// the electronics take kElectronicsA on top. Solves
/// (I − e) × (restVolts − I × ohm) = motorWatts for the smaller root. −1 when
/// no current can deliver that power (the voltage would collapse).
inline double packCurrent(double motorWatts, double restVolts, double ohm)
{
    const double e = kElectronicsA;
    if (!(restVolts > 0.0) || !(motorWatts >= 0.0))
        return -1.0;
    if (!(ohm > 0.0))
        return motorWatts / restVolts + e;
    const double b = restVolts + ohm * e;
    const double disc = b * b - 4.0 * ohm * (e * restVolts + motorWatts);
    if (disc < 0.0)
        return -1.0;
    return (b - std::sqrt(disc)) / (2.0 * ohm);
}

/// The loaded pack voltage at `socPct` charge left while the motors take
/// `motorWatts`: resting voltage on the chemistry's curve − current × ohm.
inline double loadedVolts(const Pack &p, double socPct, double motorWatts)
{
    const double rest = p.cells * PowerTypeModel::restingCellVolts(socPct, p.chemistry);
    const double i = packCurrent(motorWatts, rest, p.resistanceOhm);
    return i < 0.0 ? -1.0 : rest - i * p.resistanceOhm;
}

/// Hours to go from `socFrom` to `socTo` (% charge left) with the motors
/// taking `motorWatts`. The pack voltage falls as it empties, so the same power
/// needs more current later: the charge is stepped through in kPackStepPct
/// steps, each at the current for the voltage in its middle. −1 when the pack
/// cannot deliver the power somewhere on the way.
inline double hoursBetween(const Pack &p, double motorWatts, double socFrom, double socTo)
{
    if (!p.isValid() || !(motorWatts > 0.0))
        return -1.0;
    socFrom = std::clamp(socFrom, 0.0, 100.0);
    socTo = std::clamp(socTo, 0.0, 100.0);
    if (socTo >= socFrom)
        return 0.0;
    double hours = 0.0;
    for (double hi = socFrom; hi > socTo + 1e-9;) {
        const double lo = std::max(socTo, hi - kPackStepPct);
        const double rest = p.cells * PowerTypeModel::restingCellVolts((hi + lo) / 2.0, p.chemistry);
        const double i = packCurrent(motorWatts, rest, p.resistanceOhm);
        if (i <= 0.0)
            return -1.0;
        hours += p.capacityAh * (hi - lo) / 100.0 / i;
        hi = lo;
    }
    return hours;
}

/// The constant current that empties the same charge in the same time as
/// hoursBetween(): charge ÷ hours. The current the time left and the trip are
/// costed with. −1 when the pack cannot deliver the power.
inline double averageCurrent(const Pack &p, double motorWatts, double socFrom, double socTo)
{
    const double h = hoursBetween(p, motorWatts, socFrom, socTo);
    if (!(h > 0.0))
        return -1.0;
    return p.capacityAh * (std::clamp(socFrom, 0.0, 100.0) - std::clamp(socTo, 0.0, 100.0)) / 100.0 / h;
}

/// The pack's resistance seen now: (resting voltage at this charge − the
/// voltage under load) ÷ the current. −1 when it cannot be told (little
/// current, or the reading is not plausible).
inline double observedResistance(const Pack &p, double socPct, double loadedVoltsNow, double currentA)
{
    if (!(p.cells > 0) || !(currentA > 5.0) || !(loadedVoltsNow > 0.0))
        return -1.0;
    const double rest = p.cells * PowerTypeModel::restingCellVolts(socPct, p.chemistry);
    const double r = (rest - loadedVoltsNow) / currentA;
    return (r >= 0.0 && r < 0.5) ? r : -1.0;
}

/// A problem with using this table for the connected copter. "off" stops the
/// estimate; "warn" is shown but the estimate runs.
struct SetupIssue {
    QString level;   ///< "off" or "warn"
    QString text;
};

/// What the GCS knows about the copter, for setupIssues().
struct SetupFacts {
    int motors = 0;              ///< from FRAME_CLASS / the motor outputs; 0 = not known
    int cells = 0;               ///< from MOT_BAT_VOLT_MAX or the pack entered; 0 = not known
    double capacityAh = 0.0;     ///< the pack entered
    double thrustToWeight = -1.0; ///< full-throttle thrust ÷ weight at the reserve voltage; < 0 = not known
    double hoverAvgA = -1.0;     ///< hover current averaged through the pack; < 0 = not known
    double hoverPeakA = -1.0;    ///< hover current by the reserve (the pack's lowest voltage)
    double ratingA = 0.0;        ///< the pack's continuous rating, C × Ah; 0 = not known
    bool outputsFresh = false;   ///< motor output signals arriving
};

/// The table is for one frame and one pack: a different motor count or cell
/// count turns the estimate off, as does no pack size. Spare thrust below
/// kMinThrustToWeight at the reserve, hover above the pack's continuous
/// rating, or no motor output signals, is a warning.
inline QVector<SetupIssue> setupIssues(const Table &t, const SetupFacts &f)
{
    QVector<SetupIssue> out;
    if (f.motors > 0 && f.motors != t.motors)
        out.append({QStringLiteral("off"), QStringLiteral("The motor table is for %1 motors; this copter has %2").arg(t.motors).arg(f.motors)});
    if (f.cells > 0 && f.cells != t.cells)
        out.append({QStringLiteral("off"), QStringLiteral("The motor table is for %1S; this pack is %2S").arg(t.cells).arg(f.cells)});
    if (!(f.capacityAh > 0.0))
        out.append({QStringLiteral("off"), QStringLiteral("No pack size: enter the battery pack")});
    if (f.thrustToWeight > 0.0 && f.thrustToWeight < kMinThrustToWeight)
        out.append({QStringLiteral("warn"), QStringLiteral("Little spare thrust: %1 × the weight at the reserve (want %2)")
                                                .arg(f.thrustToWeight, 0, 'f', 2).arg(kMinThrustToWeight, 0, 'f', 1)});
    // The pack's continuous rating: hover alone should stay under it (climbs
    // and cruise draw more).
    if (f.ratingA > 0.0 && f.hoverAvgA > f.ratingA)
        out.append({QStringLiteral("warn"), QStringLiteral("Hover draws %1 A on average (%2 A by the reserve), above the pack's rating (%3 A)")
                                                .arg(f.hoverAvgA, 0, 'f', 1).arg(f.hoverPeakA, 0, 'f', 1).arg(f.ratingA, 0, 'f', 0)});
    else if (f.ratingA > 0.0 && f.hoverPeakA > f.ratingA)
        out.append({QStringLiteral("warn"), QStringLiteral("Hover reaches %1 A by the reserve, above the pack's rating (%2 A)")
                                                .arg(f.hoverPeakA, 0, 'f', 1).arg(f.ratingA, 0, 'f', 0)});
    if (!f.outputsFresh)
        out.append({QStringLiteral("warn"), QStringLiteral("Motor output signals not arriving: estimating from the overall throttle")});
    return out;
}

// ── Air density ─────────────────────────────────────────────────────────
//
// Thinner air (altitude, heat) changes what the prop does at a given speed:
// thrust and torque are both × σ, the air density ÷ sea-level density
// (1.225 kg/m³), so the shaft power is × σ too. The motor's own losses depend
// on its current (∝ torque) and speed, not on the air. The maker's table is
// taken to be measured at kTableDensityRatio.
//
// The table stays exact at its own density: every result is the table's own
// value × a correction ratio, and the ratio comes from a motor model fitted
// to the table's rows (speed, torque, shaft power, power in), worked out at
// the real density and at the table's density. The model is only ever used
// for that ratio, which is 1 at the table's density.

/// Air density ratio of the maker's test (sea level, 15 °C; to confirm).
constexpr double kTableDensityRatio = kTableDensityRatioDefault;
constexpr double kSeaLevelDensity = 1.225;   ///< kg/m³, standard atmosphere

/// σ from pressure (hPa), temperature (°C) and relative humidity (%), as
/// moist air: dry air plus water vapour (Magnus formula for the vapour
/// pressure). An unknown humidity counts as dry air (humid air is lighter, by
/// under 1 % in cool air).
inline double densityRatio(double pressureHpa, double tempC, double humidityPct = 0.0)
{
    if (!(pressureHpa > 0.0) || !std::isfinite(tempC))
        return -1.0;
    const double tK = tempC + 273.15;
    const double rh = std::isfinite(humidityPct) ? std::clamp(humidityPct, 0.0, 100.0) : 0.0;
    const double vapourHpa = rh / 100.0 * 6.1078 * std::pow(10.0, 7.5 * tempC / (tempC + 237.3));
    const double rho = (pressureHpa - vapourHpa) * 100.0 / (287.058 * tK) + vapourHpa * 100.0 / (461.495 * tK);
    return rho / kSeaLevelDensity;
}

/// Standard-atmosphere pressure (hPa) and temperature (°C) at an altitude (m).
inline double isaPressureHpa(double altitudeM) { return 1013.25 * std::pow(1.0 - 2.25577e-5 * altitudeM, 5.25588); }
inline double isaTempC(double altitudeM) { return 15.0 - 0.0065 * altitudeM; }
/// The altitude (m) at which the standard atmosphere has this pressure.
inline double isaAltitudeM(double pressureHpa) { return 44330.8 * (1.0 - std::pow(pressureHpa / 1013.25, 0.190263)); }

namespace detail {
/// Least squares for y ≈ Σ cₖ xₖ (up to 4 terms), by the normal equations.
inline QVector<double> fit(const QVector<QVector<double>> &x, const QVector<double> &y)
{
    const int m = x.isEmpty() ? 0 : x.first().size();
    QVector<QVector<double>> a(m, QVector<double>(m + 1, 0.0));
    for (int r = 0; r < x.size(); ++r)
        for (int i = 0; i < m; ++i) {
            for (int j = 0; j < m; ++j)
                a[i][j] += x[r][i] * x[r][j];
            a[i][m] += x[r][i] * y[r];
        }
    for (int c = 0; c < m; ++c) {
        int piv = c;
        for (int r = c + 1; r < m; ++r)
            if (std::abs(a[r][c]) > std::abs(a[piv][c]))
                piv = r;
        std::swap(a[c], a[piv]);
        if (std::abs(a[c][c]) < 1e-12)
            return {};
        for (int r = 0; r < m; ++r) {
            if (r == c)
                continue;
            const double f = a[r][c] / a[c][c];
            for (int j = c; j <= m; ++j)
                a[r][j] -= f * a[c][j];
        }
    }
    QVector<double> out(m);
    for (int i = 0; i < m; ++i)
        out[i] = a[i][m] / a[i][i];
    return out;
}

/// A column against speed, read between rows; beyond them the prop laws
/// (thrust and torque ∝ speed², shaft power ∝ speed³) from the nearest row.
inline double byRpm(const Table &t, double rpm, double Row::*col, double exponent)
{
    const Row &first = t.rows.first();
    const Row &last = t.rows.last();
    if (rpm <= 0.0)
        return 0.0;
    if (rpm < first.rpm)
        return first.*col * std::pow(rpm / first.rpm, exponent);
    if (rpm > last.rpm)
        return last.*col * std::pow(rpm / last.rpm, exponent);
    for (int i = 1; i < t.rows.size(); ++i) {
        const Row &a = t.rows.at(i - 1);
        const Row &b = t.rows.at(i);
        if (rpm <= b.rpm)
            return a.*col + (rpm - a.rpm) / (b.rpm - a.rpm) * (b.*col - a.*col);
    }
    return last.*col;
}
} // namespace detail

/// The motor model fitted to the table's rows: losses (power in − shaft power)
/// = a·torque² + b·torque + c + d·speed (winding, ESC, iron); and the motor
/// voltage, table voltage × throttle = e·speed + f·torque + g (back-EMF, the
/// drop with current, the ESC). Used only for density correction ratios.
struct MotorModel {
    double a = 0, b = 0, c = 0, d = 0;   ///< losses, W
    double e = 0, f = 0, g = 0;          ///< motor voltage, V
    bool valid = false;
    double losses(double torque, double rpm) const { return std::max(0.0, a * torque * torque + b * torque + c + d * rpm); }
};

inline MotorModel motorModel(const Table &t)
{
    MotorModel m;
    QVector<QVector<double>> xl, xv;
    QVector<double> yl, yv;
    for (const Row &r : t.rows) {
        xl.append({r.torqueNm * r.torqueNm, r.torqueNm, 1.0, r.rpm});
        yl.append(r.powerInW - r.powerOutW);
        xv.append({r.rpm, r.torqueNm, 1.0});
        yv.append(t.volts * r.throttlePct / 100.0);
    }
    const QVector<double> l = detail::fit(xl, yl);
    const QVector<double> v = detail::fit(xv, yv);
    if (l.size() == 4 && v.size() == 3) {
        m.a = l[0]; m.b = l[1]; m.c = l[2]; m.d = l[3];
        m.e = v[0]; m.f = v[1]; m.g = v[2];
        m.valid = true;
    }
    return m;
}

/// The table's motor model, worked out once.
inline const MotorModel &modelOf(const Table &t)
{
    static const Table *cached = nullptr;
    static MotorModel m;
    if (cached != &t) {
        m = motorModel(t);
        cached = &t;
    }
    return m;
}

/// Power in at speed `rpm` in air of density ratio `sigma`: the shaft power
/// × σ (relative to the table's density) plus the motor's losses at that
/// torque and speed.
inline double modelPowerIn(const Table &t, double rpm, double sigma)
{
    const MotorModel &m = modelOf(t);
    const double k = sigma / kTableDensityRatio;
    const double q = k * detail::byRpm(t, rpm, &Row::torqueNm, 2.0);
    return k * detail::byRpm(t, rpm, &Row::powerOutW, 3.0) + m.losses(q, rpm);
}

/// The motor's speed for a motor voltage (pack voltage × throttle) in air of
/// density ratio `sigma`: thinner air loads the prop less, so it spins faster.
inline double modelRpmForVoltage(const Table &t, double motorVolts, double sigma)
{
    const MotorModel &m = modelOf(t);
    if (!m.valid || !(motorVolts > m.g))
        return 0.0;
    const double k = sigma / kTableDensityRatio;
    double lo = 0.0, hi = 3.0 * t.rows.last().rpm;
    for (int i = 0; i < 60; ++i) {
        const double mid = (lo + hi) / 2.0;
        const double v = m.e * mid + m.f * k * detail::byRpm(t, mid, &Row::torqueNm, 2.0) + m.g;
        (v < motorVolts ? lo : hi) = mid;
    }
    return (lo + hi) / 2.0;
}

/// Correction to the table's power at a motor voltage for air of density
/// ratio `sigma`: the model's power in at that density ÷ at the table's. 1 at
/// the table's density; within about 1 % of 1 at 2000–4000 m (the motor spins
/// faster in thin air, which nearly makes up for the lighter load).
inline double densityPowerRatio(const Table &t, double motorVolts, double sigma)
{
    if (!(sigma > 0.0) || std::abs(sigma - kTableDensityRatio) < 1e-9)
        return 1.0;
    const double n1 = modelRpmForVoltage(t, motorVolts, kTableDensityRatio);
    const double ns = modelRpmForVoltage(t, motorVolts, sigma);
    const double p1 = modelPowerIn(t, n1, kTableDensityRatio);
    return (n1 > 0.0 && ns > 0.0 && p1 > 0.0) ? modelPowerIn(t, ns, sigma) / p1 : 1.0;
}

/// Correction to the table's thrust at a motor voltage for air of density
/// ratio `sigma`: σ × the table's thrust at the faster speed ÷ the table's
/// thrust at its own speed.
inline double densityThrustRatio(const Table &t, double motorVolts, double sigma)
{
    if (!(sigma > 0.0) || std::abs(sigma - kTableDensityRatio) < 1e-9)
        return 1.0;
    const double n1 = modelRpmForVoltage(t, motorVolts, kTableDensityRatio);
    const double ns = modelRpmForVoltage(t, motorVolts, sigma);
    const double t1 = detail::byRpm(t, n1, &Row::thrustG, 2.0);
    return (n1 > 0.0 && ns > 0.0 && t1 > 0.0)
        ? sigma / kTableDensityRatio * detail::byRpm(t, ns, &Row::thrustG, 2.0) / t1 : 1.0;
}

/// Power one motor takes for `thrustG` of real thrust in air of density ratio
/// `sigma`. At the table's density it is powerForThrust() (the thrust and
/// power columns). In thinner air the prop must spin faster for the same
/// thrust (table thrust at that speed = thrust ÷ σ): the table's power for the
/// same thrust × the model's power at the faster speed and real density ÷ at
/// the table's speed and density.
inline PowerAt powerForThrustAt(const Table &t, double thrustG, double sigma)
{
    PowerAt base = powerForThrust(t, thrustG);
    if (!(sigma > 0.0) || std::abs(sigma - kTableDensityRatio) < 1e-9 || !(thrustG > 0.0))
        return base;
    const double k = sigma / kTableDensityRatio;
    auto rpmForTableThrust = [&](double tg) {
        double lo = 0.0, hi = 3.0 * t.rows.last().rpm;
        for (int i = 0; i < 60; ++i) {
            const double mid = (lo + hi) / 2.0;
            (detail::byRpm(t, mid, &Row::thrustG, 2.0) < tg ? lo : hi) = mid;
        }
        return (lo + hi) / 2.0;
    };
    const double n1 = rpmForTableThrust(thrustG);
    const double ns = rpmForTableThrust(thrustG / k);
    const double p1 = modelPowerIn(t, n1, kTableDensityRatio);
    if (p1 > 0.0)
        base.watts *= modelPowerIn(t, ns, sigma) / p1;
    base.beyond = base.beyond || ns > t.rows.last().rpm;
    return base;
}

/// The real thrust per motor that needs `motorWatts` per motor in air of
/// density ratio `sigma` (the inverse of powerForThrustAt()). −1 when it
/// cannot be found.
inline double thrustForPowerAt(const Table &t, double motorWatts, double sigma)
{
    if (!(motorWatts > 0.0))
        return -1.0;
    double lo = 1.0, hi = 3.0 * t.rows.last().thrustG;
    if (powerForThrustAt(t, hi, sigma).watts < motorWatts)
        return -1.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = (lo + hi) / 2.0;
        (powerForThrustAt(t, mid, sigma).watts < motorWatts ? lo : hi) = mid;
    }
    return (lo + hi) / 2.0;
}

/// Motor throttle (0–1) needed for `thrustG` of real thrust per motor at
/// `packVolts` in air of density ratio `sigma`: the table's throttle at the
/// table's density, × the model's motor voltage at the faster speed ÷ at the
/// table's speed. −1 outside the table.
inline double hoverThrottleAtDensity(const Table &t, double thrustG, double packVolts, double sigma)
{
    const double base = hoverThrottleAt(t, thrustG, packVolts);
    if (base < 0.0 || !(sigma > 0.0) || std::abs(sigma - kTableDensityRatio) < 1e-9)
        return base;
    const MotorModel &m = modelOf(t);
    const double k = sigma / kTableDensityRatio;
    auto rpmForTableThrust = [&](double tg) {
        double lo = 0.0, hi = 3.0 * t.rows.last().rpm;
        for (int i = 0; i < 60; ++i) {
            const double mid = (lo + hi) / 2.0;
            (detail::byRpm(t, mid, &Row::thrustG, 2.0) < tg ? lo : hi) = mid;
        }
        return (lo + hi) / 2.0;
    };
    const double n1 = rpmForTableThrust(thrustG);
    const double ns = rpmForTableThrust(thrustG / k);
    const double v1 = m.e * n1 + m.f * detail::byRpm(t, n1, &Row::torqueNm, 2.0) + m.g;
    const double vs = m.e * ns + m.f * k * detail::byRpm(t, ns, &Row::torqueNm, 2.0) + m.g;
    return v1 > 0.0 ? base * vs / v1 : base;
}

/// The most thrust one motor gives (full throttle) on a pack at `packVolts`
/// in air of density ratio `sigma`: the table at the matching row (throttle
/// 1 × pack voltage ÷ table voltage; above the table's top on a pack above its
/// voltage, marked beyond), corrected for the density.
inline ThrustAt maxThrustAt(const Table &t, double packVolts, double sigma = kTableDensityRatioDefault)
{
    ThrustAt out;
    if (!t.isValid() || !(packVolts > 0.0))
        return out;
    out = thrustAt(t, packVolts / t.volts);
    out.grams *= densityThrustRatio(t, packVolts, sigma);
    return out;
}

/// The flight controller's settings that turn its hover throttle into motor output.
struct HoverParams {
    double thstHover = -1.0;  ///< MOT_THST_HOVER
    double spinMin = 0.15;    ///< MOT_SPIN_MIN
    double spinMax = 0.95;    ///< MOT_SPIN_MAX
    double expo = 0.65;       ///< MOT_THST_EXPO
    double battVoltMax = 0.0; ///< MOT_BAT_VOLT_MAX (0: no voltage compensation)
    double battVoltMin = 0.0; ///< MOT_BAT_VOLT_MIN
};

/// Motor throttle (0–1) at hover, the way ArduPilot computes its output:
/// output = spin_min + (spin_max − spin_min) × curve(MOT_THST_HOVER) ÷ (pack
/// voltage ÷ MOT_BAT_VOLT_MAX), with curve(x) = ((e − 1) + √((1 − e)² + 4·e·x)) ÷ 2e
/// for MOT_THST_EXPO = e. (ArduPilot scales the throttle by its lift-max gain and
/// the curve by lift max, so the two cancel.) The pack voltage is clamped to
/// MOT_BAT_VOLT_MIN (at least 0.6 × MOT_BAT_VOLT_MAX) … MOT_BAT_VOLT_MAX, as
/// ArduPilot does. −1 when MOT_THST_HOVER is unknown.
inline double hoverMotorThrottle(const HoverParams &p, double packVolts)
{
    if (!(p.thstHover > 0.0) || !(p.spinMax > p.spinMin))
        return -1.0;
    const double h = std::clamp(p.thstHover, 0.0, 1.0);
    const double e = std::clamp(p.expo, -1.0, 1.0);
    double ratio = std::fabs(e) < 1e-6 ? h
                                       : ((e - 1.0) + std::sqrt(std::max(0.0, (1.0 - e) * (1.0 - e) + 4.0 * e * h))) / (2.0 * e);
    if (p.battVoltMax > 0.0 && std::isfinite(packVolts) && packVolts > 0.0) {
        const double vMin = std::max(p.battVoltMin, 0.6 * p.battVoltMax);
        if (vMin < p.battVoltMax) {
            const double filt = std::clamp(packVolts, vMin, p.battVoltMax) / p.battVoltMax;
            ratio /= filt;
        }
    }
    return p.spinMin + (p.spinMax - p.spinMin) * std::clamp(ratio, 0.0, 1.0);
}

/// Output channels (1-based) of motors 1 … N in motor order, from each
/// channel's SERVOn_FUNCTION (index 0 = SERVO1). A motor without a channel is
/// left out, so a short list means some motor's output is not known.
inline QVector<int> motorChannels(const QVector<int> &servoFunctions)
{
    QVector<int> out;
    for (int fn = kMotor1Function; fn <= kMotor8Function; ++fn) {
        for (int ch = 0; ch < servoFunctions.size(); ++ch) {
            if (servoFunctions.at(ch) == fn) {
                out.append(ch + 1);
                break;
            }
        }
    }
    return out;
}

/// The ESC range ArduPilot sends motor outputs in: MOT_PWM_MIN … MOT_PWM_MAX
/// when MOT_PWM_MIN ≥ 1 and MOT_PWM_MAX is above it, else 1000 … 2000 (its
/// defaults, and what it uses for digital outputs such as DShot).
struct EscRange { double min = kDefaultPwmMin; double max = kDefaultPwmMax; };

inline EscRange escRange(double motPwmMin, double motPwmMax)
{
    if (motPwmMin >= 1.0 && motPwmMax > motPwmMin)
        return {motPwmMin, motPwmMax};
    return {};
}

/// Each motor's throttle (0–1) in motor order from the 16 output signals.
/// `channels` are the motor outputs from motorChannels(); with none (the
/// output functions are not known) motors 1 … `motorsExpected` are taken to be
/// on outputs 1 … N, ArduPilot's default. Empty when a motor's output is
/// missing or 0 (not sent), or fewer motors than expected are known.
inline QVector<double> motorThrottles(const QVector<int> &outputs, QVector<int> channels, int motorsExpected,
                                      const EscRange &range)
{
    if (channels.isEmpty())
        for (int ch = 1; ch <= motorsExpected; ++ch)
            channels.append(ch);
    QVector<double> out;
    if (channels.isEmpty() || (motorsExpected > 0 && channels.size() < motorsExpected))
        return out;
    for (int ch : channels) {
        if (ch < 1 || ch > outputs.size() || outputs.at(ch - 1) <= 0)
            return {};
        out.append(pwmToThrottle(outputs.at(ch - 1), range.min, range.max));
    }
    return out;
}

/// Cell count from MOT_BAT_VOLT_MAX (set to full voltage, 4.2 V per cell), or
/// 0 when it is not set.
inline int cellsFromVoltMax(double battVoltMax)
{
    return battVoltMax > 0.0 ? static_cast<int>(std::lround(battVoltMax / kFullCellV)) : 0;
}

} // namespace MotorTable
