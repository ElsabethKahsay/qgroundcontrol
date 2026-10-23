// Step 3 addition (21 Oct): who is flying in a two-radio (master/slave)
// training setup. The trainer switch lives in the instructor's transmitter,
// so the aircraft sees it only on the channel the switch is sent on (on the
// operator's radios SH already reaches the flight controller). This reads
// that channel from RC_CHANNELS.
//
// Rule (operator convention, 23 Oct): the instructor's radio uses switch SH
// for the handover, and SH is sent on a channel. SH HIGH (> 1600 µs) = the
// master / instructor is flying; SH LOW (< 1400 µs) = the trainee is flying.
// Between the two the state does not change (no flicker on a noisy value).
// A radio wired the other way round is handled by setHighIsInstructor(false).
// "Verified" = the channel has been seen in both positions on this connection.
//
// Header-only and no QObject: no CMake or moc change, testable directly.
#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QVariantList>

class TrainerSwitchTracker {
public:
    enum class Control { Unmonitored, Instructor, Student };

    static constexpr int kDefaultChannel = 8;
    static constexpr int kHighUs = 1600;    ///< above: switch high
    static constexpr int kLowUs = 1400;     ///< below: switch low

    void setChannel(int channel)
    {
        if (channel == m_channel)
            return;
        m_channel = channel;
        reset();
    }
    int channel() const { return m_channel; }

    /// true (default): switch high = instructor; false: high = trainee.
    void setHighIsInstructor(bool v)
    {
        if (v == m_highIsInstructor)
            return;
        m_highIsInstructor = v;
        if (m_control != Control::Unmonitored)
            m_control = m_control == Control::Instructor ? Control::Student : Control::Instructor;
    }
    bool highIsInstructor() const { return m_highIsInstructor; }

    /// New connection: positions seen so far are forgotten.
    void reset()
    {
        m_control = Control::Unmonitored;
        m_seenHigh = m_seenLow = false;
    }

    /// Feed RC_CHANNELS values (µs, channel 1 first). Returns true when the
    /// control state changed.
    bool update(const QVariantList &pwm)
    {
        const int idx = m_channel - 1;
        const int us = (idx >= 0 && idx < pwm.size()) ? pwm.at(idx).toInt() : 0;
        const Control before = m_control;
        if (us < 800 || us > 2200) {          // missing, 0 or 65535 (unused)
            m_control = Control::Unmonitored;
            return before != m_control;
        }
        if (us > kHighUs) {
            m_seenHigh = true;
            m_control = m_highIsInstructor ? Control::Instructor : Control::Student;
        } else if (us < kLowUs) {
            m_seenLow = true;
            m_control = m_highIsInstructor ? Control::Student : Control::Instructor;
        }
        // in between: keep the last state (Unmonitored until a clear position)
        return before != m_control;
    }

    Control control() const { return m_control; }
    /// Both switch positions have been seen since the connection started.
    bool verified() const { return m_seenHigh && m_seenLow; }

    static QString name(Control c)
    {
        switch (c) {
        case Control::Instructor: return QStringLiteral("instructor");
        case Control::Student:    return QStringLiteral("student");
        case Control::Unmonitored: break;
        }
        return QStringLiteral("unmonitored");
    }

private:
    int m_channel = kDefaultChannel;
    Control m_control = Control::Unmonitored;
    bool m_highIsInstructor = true;
    bool m_seenHigh = false;
    bool m_seenLow = false;
};

/// "Detect" for the trainer chip: while the operator flicks SH, find the one
/// switch channel (CH5-CH16) that went clearly low and clearly high and
/// changed side at least kMinTransitions times. Sticks (CH1-CH4) and the
/// flight-mode channel are never offered. Pure logic, fed RC_CHANNELS values.
class TrainerChannelDetector {
public:
    enum class Outcome { Running, Found, NoMovement, Several, OnlyModeChannel, NoRc };

    static constexpr int kFirstChannel = 5;
    static constexpr int kLastChannel = 16;
    static constexpr int kMinTransitions = 3;     ///< down-up-down: two flicks
    static constexpr int kEarlyTransitions = 4;   ///< a lone candidate this clear ends it early

    void start(int modeChannel)
    {
        m_modeChannel = modeChannel;
        m_anyRc = false;
        for (Track &t : m_tracks)
            t = Track{};
    }

    /// Feed one RC_CHANNELS sample (µs, channel 1 first).
    void feed(const QVariantList &pwm)
    {
        for (int ch = 1; ch <= qMin<int>(pwm.size(), kLastChannel); ++ch) {
            const int us = pwm.at(ch - 1).toInt();
            if (us < 800 || us > 2200)
                continue;                              // missing / unused
            m_anyRc = true;
            if (ch < kFirstChannel)
                continue;                              // sticks are not switches
            Track &t = m_tracks[ch - kFirstChannel];
            const int side = us > TrainerSwitchTracker::kHighUs ? 1 : us < TrainerSwitchTracker::kLowUs ? 0 : -1;
            if (side < 0)
                continue;
            (side ? t.seenHigh : t.seenLow) = true;
            if (t.side >= 0 && side != t.side)
                ++t.transitions;
            t.side = side;
        }
    }

    /// Channels that behaved like a flicked switch (mode channel included).
    QList<int> candidates() const
    {
        QList<int> out;
        for (int ch = kFirstChannel; ch <= kLastChannel; ++ch) {
            const Track &t = m_tracks[ch - kFirstChannel];
            if (t.seenLow && t.seenHigh && t.transitions >= kMinTransitions)
                out << ch;
        }
        return out;
    }

    /// True when one non-mode channel is clearly the switch already.
    bool clearEarly() const
    {
        const QList<int> c = usable();
        return c.size() == 1 && m_tracks[c.first() - kFirstChannel].transitions >= kEarlyTransitions
               && candidates().size() == 1;
    }

    /// The verdict at the end of the detection window.
    Outcome finish(int *channel = nullptr, QList<int> *several = nullptr) const
    {
        if (!m_anyRc)
            return Outcome::NoRc;
        const QList<int> all = candidates();
        const QList<int> c = usable();
        if (c.size() == 1) {
            if (channel) *channel = c.first();
            return Outcome::Found;
        }
        if (c.size() > 1) {
            if (several) *several = c;
            return Outcome::Several;
        }
        return all.isEmpty() ? Outcome::NoMovement : Outcome::OnlyModeChannel;
    }

private:
    struct Track {
        int side = -1;          ///< 0 low, 1 high, -1 not yet clear
        bool seenLow = false;
        bool seenHigh = false;
        int transitions = 0;
    };

    QList<int> usable() const
    {
        QList<int> c = candidates();
        c.removeAll(m_modeChannel);
        return c;
    }

    Track m_tracks[kLastChannel - kFirstChannel + 1];
    int m_modeChannel = -1;
    bool m_anyRc = false;
};
