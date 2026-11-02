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
 * the last row the motor runs faster than the test measured; the power is
 * continued as throttle³ (prop power grows with speed³, and speed with
 * throttle), which is more than the test stand's flattening top rows suggest,
 * so the estimate errs towards more current there, and the reading is marked
 * beyond the table.
 *
 * The rows of a derived table (another voltage than the maker measured) are
 * the measured rows moved by step 2: the same speed, thrust and power at
 * throttle × (measured voltage ÷ new voltage), and current = power ÷ new
 * voltage. Each table's source CSV is in custom/tools/motor_tables; a test
 * keeps the rows below and the CSV identical.
 *
 * Pure maths: no Qt object, no database. Header-only so it needs no CMake change.
 */

#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace MotorTable {

/// Current drawn by everything that is not a motor (flight controller, radio,
/// GPS), added to the motors' current.
constexpr double kElectronicsA = 1.0;
/// Hover above this share of motor throttle leaves too little in reserve for
/// manoeuvres and wind: the copter is heavy for its motors.
constexpr double kMaxHoverThrottle = 0.60;
/// Above the last row, power grows with throttle to this power.
constexpr double kBeyondExponent = 3.0;
/// ArduPilot's output functions Motor1 … Motor8 (SERVOn_FUNCTION).
constexpr int kMotor1Function = 33;
constexpr int kMotor8Function = 40;
/// Per-cell voltages used to read the cell count and to plan before take-off.
constexpr double kFullCellV = 4.2;
constexpr double kNominalCellV = 3.7;
/// The GCS asks for each output's signal (SERVO_OUTPUT_RAW) at 2 Hz. This is a
/// message-rate request: it changes no parameter and is gone after a reboot.
constexpr float kMotorOutputIntervalUs = 500000.0f;
/// Motor outputs older than this are not used.
constexpr qint64 kMotorOutputStaleMs = 3000;
/// ArduPilot's ESC range when MOT_PWM_MIN / MOT_PWM_MAX are not usable.
constexpr double kDefaultPwmMin = 1000.0;
constexpr double kDefaultPwmMax = 2000.0;

/// One test-stand row for one motor.
struct Row {
    double throttlePct;   ///< motor throttle, % of the ESC's range
    double thrustG;       ///< thrust, grams
    double powerInW;      ///< power taken from the supply, watts
    double rpm;           ///< motor speed
};

/// A table and what it was measured (or derived) for.
struct Table {
    QString id;           ///< stable key stored with a vehicle
    QString name;         ///< what the pilot sees
    double volts = 0.0;   ///< supply voltage the rows are at
    int motors = 0;       ///< motors on the drone type it is for
    int cells = 0;        ///< cells of the pack it is for
    QVector<Row> rows;    ///< ascending throttle
    bool isValid() const { return volts > 0.0 && rows.size() >= 2; }
};

/// M6215 HighProt 320KV with MSC 22×7.8 props on 8S (30.7 V, 3.83 V per cell,
/// the per-cell voltage of the maker's 23 V test). Derived from the maker's
/// 23 V (6S) table: throttle × 23 ÷ 30.7, current = power ÷ 30.7 V. Source:
/// custom/tools/motor_tables/M6215_HighProt_320KV_MSC22x7.8_8S_30.7V_derived.csv.
/// It covers 24.7–74.9 % throttle; above that the motor would spin faster than
/// the 23 V test measured.
inline const Table &m6215_320kv_22x78_8s()
{
    static const Table t{
        QStringLiteral("m6215-320kv-msc22x7.8-8s"),
        QStringLiteral("M6215 HighProt 320KV · MSC 22×7.8 · 8S quad"),
        30.7, 4, 8,
        {
            {24.7, 1392, 127.5, 2403}, {26.2, 1564, 146.9, 2537}, {27.7, 1745, 169.1, 2674},
            {29.2, 1935, 193.9, 2813}, {31.5, 2230, 235.5, 3017}, {33.7, 2533, 281.5, 3215},
            {36.0, 2844, 331.1, 3405}, {38.2, 3160, 384.0, 3585}, {40.5, 3481, 440.1, 3757},
            {42.7, 3810, 499.5, 3921}, {45.0, 4147, 562.6, 4079}, {47.2, 4494, 630.1, 4233},
            {49.4, 4853, 702.7, 4385}, {51.7, 5224, 781.3, 4536}, {53.9, 5608, 866.4, 4688},
            {56.2, 6004, 958.5, 4841}, {58.4, 6408, 1057.4, 4995}, {60.7, 6817, 1162.2, 5150},
            {62.9, 7223, 1270.5, 5302}, {65.2, 7618, 1378.8, 5445}, {67.4, 7988, 1482.0, 5573},
            {74.9, 8801, 1705.3, 5814},
        }};
    return t;
}

/// Every table the GCS knows, for the Vehicles page's choice.
inline QVector<const Table *> all() { return {&m6215_320kv_22x78_8s()}; }

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

/// One motor's battery current at `throttle` (0–1) on a pack at `packVolts`.
/// An unknown pack voltage uses the table's own.
struct MotorCurrent {
    double amps = 0.0;
    bool beyond = false;
};

inline MotorCurrent motorCurrent(const Table &t, double throttle, double packVolts)
{
    MotorCurrent out;
    if (!t.isValid())
        return out;
    const double v = (std::isfinite(packVolts) && packVolts > 0.0) ? packVolts : t.volts;
    const PowerAt p = powerAt(t, throttle * v / t.volts);
    out.amps = p.watts / v;
    out.beyond = p.beyond;
    return out;
}

/// Battery current for all motors at their throttles (0–1) plus the electronics.
struct BatteryCurrent {
    double amps = 0.0;
    int motorsBeyond = 0;  ///< motors above the table's last row
};

inline BatteryCurrent batteryCurrent(const Table &t, const QVector<double> &motorThrottles, double packVolts)
{
    BatteryCurrent out;
    out.amps = kElectronicsA;
    for (double thr : motorThrottles) {
        const MotorCurrent m = motorCurrent(t, thr, packVolts);
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
