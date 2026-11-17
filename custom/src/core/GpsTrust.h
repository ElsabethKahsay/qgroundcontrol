// Whether the copter's GPS can be trusted right now: its position and its
// speed over the ground. Everything in the GCS that needs where the copter
// is or how fast it moves over the ground (the in-flight mission estimate,
// RETURN NOW from it, the distance left, the wind from the lean and the
// cruise power per airspeed) reads this; the battery count does not need it.
//
// A reading is good with a 3D fix (or better), at least kMinSatellites
// satellites, an HDOP of at most kMaxHdop, the receiver's own horizontal
// accuracy (when it reports one) at most kMaxHAccM, and no "GPS glitching"
// flag from ArduPilot's EKF. Interference shows up two ways: the receiver
// loses the satellites for a moment and finds a few again, many times a
// second (24 satellites on the ground, 0 to 8 in the air), or it keeps them
// but its position wanders (accuracy 33 m with 8 to 27 satellites). The
// limits were checked against four real flights: three with interference,
// each flagged for 40 to 70 % of the flight, and one clean, never flagged.
// So:
//   - one bad reading makes the GPS untrusted at once;
//   - it is trusted again only after kTrustAgainS of good readings in a row.
// The first reading from a vehicle decides at once (there is nothing to
// recover from yet).
//
// GPS readings that stop (none for kMissingS) while the vehicle is still
// heard (a heartbeat within kLinkFreshS) count as a bad reading: the
// receiver or its cable has gone. Nothing heard at all is the link, not the
// GPS, and is said elsewhere (the data shows as stale).
//
// Header-only (Qt only for qint64): the bridge feeds it every GPS message,
// and checks every 200 ms whether they have stopped.
#pragma once

#include <algorithm>
#include <cmath>

#include <QtGlobal>

namespace GpsTrust {

constexpr int kMinFixType = 3;        // GPS_FIX_TYPE_3D_FIX
constexpr int kMinSatellites = 6;
constexpr double kMaxHdop = 5.0;      // above it the position can be off by tens of metres
constexpr double kMaxHAccM = 8.0;     // a clean flight stays under 3 m
constexpr double kTrustAgainS = 10.0;
constexpr qint64 kMissingMs = 3000;
constexpr qint64 kLinkFreshMs = 2000;

struct Reading {
    int fixType = 0;
    int satellites = 0;
    double hdop = std::nan("");       // not reported: not good
    double hAccM = std::nan("");      // not reported (older receivers): not judged
    bool ekfGlitching = false;        // ArduPilot EKF_STATUS_REPORT: EKF_GPS_GLITCHING
    bool missing = false;             // no reading for kMissingMs (readingsStopped)
};

inline bool readingGood(const Reading &r)
{
    return !r.missing && r.fixType >= kMinFixType && r.satellites >= kMinSatellites && std::isfinite(r.hdop)
           && r.hdop <= kMaxHdop && !(std::isfinite(r.hAccM) && r.hAccM > kMaxHAccM) && !r.ekfGlitching;
}

struct State {
    bool known = false;               // a reading has been judged for this vehicle
    bool trusted = false;
    double goodS = 0.0;               // good readings in a row while untrusted
};

/// Judges one reading, @p dtS after the previous one; returns whether the
/// GPS is trusted now.
inline bool step(State &s, const Reading &r, double dtS)
{
    if (!readingGood(r)) {
        s.known = true;
        s.trusted = false;
        s.goodS = 0.0;
        return false;
    }
    if (!s.known) {
        s.known = true;
        s.trusted = true;
        return true;
    }
    if (!s.trusted) {
        s.goodS += std::max(0.0, dtS);
        // A small tolerance: readings every 0.2 s add up to 9.9999999 s.
        if (s.goodS >= kTrustAgainS - 1e-6)
            s.trusted = true;
    }
    return s.trusted;
}

/// GPS readings have stopped while the vehicle is still heard. Ages in ms,
/// below 0 for never (MonotonicClock::kNever): never a reading is not a stop.
inline bool readingsStopped(qint64 gpsAgeMs, qint64 heartbeatAgeMs)
{
    return gpsAgeMs > kMissingMs && heartbeatAgeMs >= 0 && heartbeatAgeMs <= kLinkFreshMs;
}

/// The reason in a few words, for the log ("too few satellites").
inline const char *whyNotGood(const Reading &r)
{
    if (r.missing)
        return "no GPS reading for 3 s";
    if (r.fixType < kMinFixType)
        return "no 3D fix";
    if (r.satellites < kMinSatellites)
        return "too few satellites";
    if (!std::isfinite(r.hdop) || r.hdop > kMaxHdop)
        return "HDOP too high";
    if (std::isfinite(r.hAccM) && r.hAccM > kMaxHAccM)
        return "position accuracy too poor";
    if (r.ekfGlitching)
        return "the EKF reports a GPS glitch";
    return "";
}

} // namespace GpsTrust
