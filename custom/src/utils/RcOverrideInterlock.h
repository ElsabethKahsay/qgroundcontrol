#pragma once

#include <QString>

/// Process-wide interlock so the dedicated bench-test controllers cannot drive
/// RC_CHANNELS_OVERRIDE on the same link at the same time.  Only one owner may
/// hold the RC override at a time; a second owner's acquire() is refused so it
/// can surface "another test is driving the RC" and stop instead of clobbering
/// the other controller's sweeps.
///
/// Not thread-safe by design — both owners (ControlSurfaceTestController and
/// HardwareTestController) live and drive RC override from the GUI thread.
namespace RcOverrideInterlock {

namespace {
/// Single shared holder slot.  Kept in one function so acquire/release/owner
/// read and write the SAME state (a `static` inside each of three functions
/// would give three independent copies that never see each other).
inline QString &holder()
{
    static QString h;
    return h;
}
}

/// Returns true if the process-wide RC override is now held by `owner`.
/// Fails (returns false) when another owner currently holds it.
inline bool acquire(const QString &owner)
{
    QString &h = holder();
    if (h.isEmpty()) {
        h = owner;
        return true;
    }
    return h == owner;
}

/// Releases the RC override if it is currently held by `owner`.
inline void release(const QString &owner)
{
    QString &h = holder();
    if (h == owner) {
        h.clear();
    }
}

/// The current RC-override holder, or an empty string when free.
inline QString owner()
{
    return holder();
}

}