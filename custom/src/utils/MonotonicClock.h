#pragma once

#include <QElapsedTimer>
#include <QtGlobal>

/// Monotonic time for every age and duration in the preflight code.
///
/// Wall-clock time (QDateTime::currentDateTime()) jumps on NTP / GPS time
/// corrections and DST, which can mark everything stale or keep stale data
/// looking fresh. Ages and durations use MonotonicClock::nowMs(); QDateTime
/// (UTC) is kept only for timestamps that are stored or shown.
///
/// Header-only on purpose: adding a source file to custom/CMakeLists.txt forces
/// a CMake re-run and a precompiled-header rebuild of the whole target.

/// Source of monotonic milliseconds. Tests install a TestClock.
class IClock
{
public:
    virtual ~IClock() = default;
    virtual qint64 nowMs() const = 0;
};

/// Production clock: QElapsedTimer (steady, never jumps).
class SteadyClock final : public IClock
{
public:
    SteadyClock() { m_timer.start(); }
    qint64 nowMs() const override { return m_timer.elapsed(); }

private:
    QElapsedTimer m_timer;
};

/// Test clock: time moves only when the test says so.
class TestClock final : public IClock
{
public:
    qint64 nowMs() const override { return m_nowMs; }
    void advance(qint64 ms) { m_nowMs += ms; }
    void set(qint64 ms) { m_nowMs = ms; }

private:
    qint64 m_nowMs = 1000000; ///< Start well above 0 so "never" (-1) stamps stay distinct.
};

namespace MonotonicClock {

/// Sentinel for "never happened" in millisecond stamps.
constexpr qint64 kNever = -1;

inline SteadyClock &steady()
{
    static SteadyClock clock;
    return clock;
}

inline IClock *&override_()
{
    static IClock *clock = nullptr;
    return clock;
}

/// Monotonic milliseconds from an arbitrary origin.
inline qint64 nowMs()
{
    IClock *c = override_();
    return c ? c->nowMs() : steady().nowMs();
}

/// Milliseconds since @p stampMs, or kNever if the stamp was never set.
inline qint64 ageMs(qint64 stampMs)
{
    return stampMs == kNever ? kNever : nowMs() - stampMs;
}

/// Tests only: route nowMs() through @p clock (nullptr restores the steady clock).
inline void setForTesting(IClock *clock)
{
    override_() = clock;
}

} // namespace MonotonicClock
