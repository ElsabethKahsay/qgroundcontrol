// Payload release: telling from the release servo output when a payload is
// dropped.
//
// The release servo is on an autopilot output (on the 22-inch copter output
// 9 or 10). The GCS watches only that output's PWM in SERVO_OUTPUT_RAW: what
// moves it (a switch on the RC, a mission command, a GCS command) and on
// which RC channel does not matter, so nothing here depends on how the radio
// is set up.
//
// Each one becomes a "Payload released" event:
//
// - The output is set in Preflight Settings ("Release servo output"; 0 = not
//   set, nothing is detected). "Released" is above the middle of the
//   output's range (SERVOn_MIN, SERVOn_MAX from the copter; 1500 µs when they
//   are not read), or below it when "released when the output is low" is set.
// - An output must stay in its new position for kHoldMs before it counts, so a
//   glitch (or a servo test sweeping past) is not a drop.
// - Only a move from off to on counts, in flight (armed and above the
//   airborne height). Each move releases the next payload: with one payload
//   the first move releases it; with two (for example 2 × 2 kg) the first
//   move releases payload 1 and the second move payload 2. Moves after that
//   release nothing more.
// - The output already at "released" when the copter arms is reported (it
//   must be put back; a drop needs a move to "released"). A move to
//   "released" while armed on the ground is reported as "released on the
//   ground", not as a drop.
// - A reading of 0 µs (an output the autopilot does not drive, or no data) is
//   ignored: it is not "off".
//
// Header-only and without Q_OBJECT, so it needs no CMake or moc change.
#pragma once

#include <algorithm>
#include <cmath>

#include <QtGlobal>

namespace PayloadRelease {

constexpr qint64 kHoldMs = 300;        ///< an output position must last this long to count
constexpr double kDefaultMidUs = 1500.0;
constexpr int kMaxPayloads = 2;

struct Config {
    int output = 0;                    ///< 1-based servo output (SERVOn); 0 = not set
    bool onWhenLow = false;            ///< released when the output is low
    double thresholdUs = kDefaultMidUs;
    int payloads = 1;                  ///< 1 or 2
    double totalKg = 0.0;              ///< all payload at take-off
    bool isSet() const { return output > 0; }
    double eachKg() const { return payloads > 0 ? totalKg / payloads : 0.0; }
};

/// The released / not released threshold: the middle of the output's range
/// (SERVOn_MIN, SERVOn_MAX), or 1500 µs when it is not known.
inline double threshold(double minUs, double maxUs)
{
    if (std::isfinite(minUs) && std::isfinite(maxUs) && minUs > 0.0 && maxUs > minUs)
        return (minUs + maxUs) / 2.0;
    return kDefaultMidUs;
}


enum class Seen {
    Nothing,
    Released,      ///< a payload came off (released() says which)
    OnAtArm,       ///< the output was at "released" when the copter armed
    OnOnGround,    ///< moved to "released" while armed on the ground: not a drop
};

class Detector
{
public:
    /// Feeds one reading of the release output (µs). `armed` and
    /// `airborne` are the copter's state now.
    Seen feed(const Config &c, qint64 nowMs, double pwmUs, bool armed, bool airborne)
    {
        Seen out = Seen::Nothing;
        if (armed && !m_wasArmed) {
            m_released = 0;                       // a new flight: all payload aboard
            if (m_known && m_on)
                out = Seen::OnAtArm;
        }
        m_wasArmed = armed;
        if (!c.isSet() || !std::isfinite(pwmUs) || pwmUs <= 0.0)
            return out;

        const bool raw = c.onWhenLow ? pwmUs < c.thresholdUs : pwmUs > c.thresholdUs;
        if (!m_known) {
            // The first reading is where the output is, not a move.
            m_known = true;
            m_on = m_raw = raw;
            m_since = nowMs;
            if (armed && m_on && out == Seen::Nothing)
                out = Seen::OnAtArm;
            return out;
        }
        if (raw != m_raw) {
            m_raw = raw;
            m_since = nowMs;
        }
        if (m_raw == m_on || nowMs - m_since < kHoldMs)
            return out;

        m_on = m_raw;
        if (!m_on || !armed)
            return out;                           // off, or on with the motors off: nothing
        if (!airborne)
            return Seen::OnOnGround;
        if (m_released >= std::clamp(c.payloads, 1, kMaxPayloads))
            return out;                           // everything is already off
        ++m_released;
        return Seen::Released;
    }

    /// The pilot tapped "Payload dropped" (no output set, or as a backup):
    /// the next payload comes off. False when all are already off.
    bool tap(const Config &c)
    {
        if (m_released >= std::clamp(c.payloads, 1, kMaxPayloads))
            return false;
        ++m_released;
        return true;
    }

    int released() const { return m_released; }
    double releasedKg(const Config &c) const
    {
        return std::min(m_released, std::clamp(c.payloads, 1, kMaxPayloads)) * c.eachKg();
    }
    bool releasedNow() const { return m_known && m_on; }

private:
    bool m_known = false;
    bool m_on = false;       ///< the output's settled position (true = released)
    bool m_raw = false;      ///< the latest reading
    qint64 m_since = 0;      ///< when the latest reading's position began
    bool m_wasArmed = false;
    int m_released = 0;
};

} // namespace PayloadRelease
