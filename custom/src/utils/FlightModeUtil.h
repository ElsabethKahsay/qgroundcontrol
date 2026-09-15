#pragma once

// ============================================================================
// FlightModeUtil — Shared ArduPilot flight-mode string→number parsing.
//
// QGC renders known flight modes as friendly names ("STABILIZE", "LOITER")
// and unknown ones as "Custom:0x%1" (e.g. "Custom:0xb" = RTL/11). Both the
// hardware-test and control-surface-test controllers need the numeric mode;
// this is the single source of truth so the maps cannot drift (R10).
// ============================================================================

#include <QHash>
#include <QString>

namespace FlightModeUtil {

// Parses a Vehicle::flightMode() string into the ArduPilot Plane mode number.
// Returns -1 when the mode is unknown.
inline int planeModeNumber(const QString &modeName)
{
    if (modeName.startsWith(QStringLiteral("Custom:0x"))) {
        bool ok = false;
        const int n = modeName.mid(9).toInt(&ok, 16);
        return ok ? n : -1;
    }
    static const QHash<QString, int> planeModes = {
        { QStringLiteral("MANUAL"),      0 },
        { QStringLiteral("CIRCLE"),      1 },
        { QStringLiteral("STABILIZE"),   2 },
        { QStringLiteral("TRAINING"),    3 },
        { QStringLiteral("ACRO"),        4 },
        { QStringLiteral("FBWA"),        5 },
        { QStringLiteral("CRUISE"),      6 },
        { QStringLiteral("AUTOTUNE"),    7 },
        { QStringLiteral("AUTO"),       10 },
        { QStringLiteral("RTL"),        11 },
        { QStringLiteral("LOITER"),     12 },
        { QStringLiteral("TAKEOFF"),    13 },
        { QStringLiteral("GUIDED"),     15 },
        { QStringLiteral("QSTABILIZE"), 17 },
        { QStringLiteral("QHOVER"),     18 },
        { QStringLiteral("QLOITER"),    19 },
        { QStringLiteral("QLAND"),      20 },
        { QStringLiteral("QRTL"),       21 },
    };
    return planeModes.value(modeName.toUpper(), -1);
}

} // namespace FlightModeUtil