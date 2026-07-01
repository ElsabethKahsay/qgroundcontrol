// Component: Colors
// Purpose: Color palette singleton defining the application's enterprise light theme. Provides
//   semantic color names for backgrounds, text, status indicators, and checklist/gate states.
// Properties: (all readonly color)
//   background, surface, surfaceLight — background/surface colors
//   primary, secondary, accent, accentDim — brand/accent colors
//   textPrimary, textSecondary, textDisabled — text colors
//   success, successDim, warning, warningDim, error, errorDim, danger, info — status colors
//   checkWarn, checkWarnDim — checklist warning/gate override colors
//   pastelPink, pastelBlue, pastelGreen, pastelPurple — telemetry subsystem label colors
//   footerBg — footer background color
//   transparent, border, borderLight — misc utility colors
pragma Singleton
import QtQuick

QtObject {
    // ── Enterprise light theme ──
    readonly property color background:   "#f1f5f9"
    readonly property color surface:      "#ffffff"
    readonly property color surfaceLight: "#f8fafc"

    // ── Brand / accent ──
    readonly property color primary:      "#0891b2"
    readonly property color secondary:    "#7c3aed"
    readonly property color accent:       "#0891b2"
    readonly property color accentDim:    "#cffafe"

    // ── Text ──
    readonly property color textPrimary:   "#0f172a"
    readonly property color textSecondary: "#475569"
    readonly property color textDisabled:  "#94a3b8"

    // ── Status — enterprise palette ──
    readonly property color success:      "#059669"
    readonly property color successDim:   "#d1fae5"
    readonly property color warning:      "#d97706"
    readonly property color warningDim:   "#fef3c7"
    readonly property color error:        "#dc2626"
    readonly property color errorDim:     "#ffe4e6"
    readonly property color danger:       "#ef4444"
    readonly property color info:         "#0891b2"

    // ── Checklist / gate ──
    readonly property color checkWarn:    "#7c3aed"
    readonly property color checkWarnDim: "#ede9fe"

    // ── Pastel accent aliases (used for telemetry subsystem labels) ──
    readonly property color pastelPink:   "#db2777"
    readonly property color pastelBlue:   "#2563eb"
    readonly property color pastelGreen:  "#16a34a"
    readonly property color pastelPurple: "#9333ea"

    // ── Footer ──
    readonly property color footerBg:     "#e2e8f0"

    // ── Misc ──
    readonly property color transparent: "#00000000"
    readonly property color border:      "#e2e8f0"
    readonly property color borderLight: "#cbd5e1"
}
