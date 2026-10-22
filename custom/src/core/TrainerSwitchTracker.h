// Step 3 addition (21 Oct): who is flying in a two-radio (master/slave)
// training setup. The trainer switch lives in the instructor's transmitter,
// so the aircraft cannot see it unless the radio copies the switch onto a
// spare channel with a mix (EdgeTX: Mixes -> CH8 = trainer switch). This
// reads that channel from RC_CHANNELS.
//
// Rule: the first valid value seen on a connection is the instructor position
// (the instructor holds control at the start); a move of more than
// kDeltaUs away from it means the student has control, a return to within
// kReturnUs means the instructor has it again. That works for any mix
// output (987 -> 1500, 1000 -> 2000, or reversed). Until the channel has
// moved once it is "unverified": a channel that never moves may not be the
// trainer switch at all.
//
// Header-only and no QObject: no CMake or moc change, testable directly.
#pragma once

#include <QString>
#include <QVariantList>

class TrainerSwitchTracker {
public:
    enum class Control { Unmonitored, Instructor, Student };

    static constexpr int kDefaultChannel = 8;
    static constexpr int kDeltaUs = 300;    ///< away from the start position: student
    static constexpr int kReturnUs = 150;   ///< back within this of the start: instructor

    void setChannel(int channel)
    {
        if (channel == m_channel)
            return;
        m_channel = channel;
        reset();
    }
    int channel() const { return m_channel; }

    /// New connection: forget the start position.
    void reset()
    {
        m_baseline = -1;
        m_control = Control::Unmonitored;
        m_verified = false;
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
            m_baseline = -1;
            return before != m_control;
        }
        if (m_baseline < 0) {
            m_baseline = us;
            m_control = Control::Instructor;
            return before != m_control;
        }
        const int delta = qAbs(us - m_baseline);
        if (m_control != Control::Student && delta > kDeltaUs) {
            m_control = Control::Student;
            m_verified = true;
        } else if (m_control == Control::Student && delta < kReturnUs) {
            m_control = Control::Instructor;
        }
        return before != m_control;
    }

    Control control() const { return m_control; }
    /// The channel has moved at least once since the connection started.
    bool verified() const { return m_verified; }

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
    int m_baseline = -1;
    Control m_control = Control::Unmonitored;
    bool m_verified = false;
};
