// Component: Colors
// Purpose: Skywin GCS dark color palette — steel blue accent, near-black surfaces.
pragma Singleton
import QtQuick

QtObject {
    // ══════════════════════════════════════════════════════════════════
    //  ACCENT — single source of truth for the entire application
    // ══════════════════════════════════════════════════════════════════
    readonly property color skywinAccent:    "#3B82A0"   // steel blue / dark teal

    // ── Dark near-black background surfaces ──
    readonly property color background:   "#0B0D12"
    readonly property color surface:      "#12151C"
    readonly property color surfaceLight: "#1A1E28"

    // ── Brand / accent — all derived from skywinAccent ──
    readonly property color primary:      skywinAccent
    readonly property color secondary:    Qt.lighter(skywinAccent, 1.15)
    readonly property color accent:       skywinAccent
    readonly property color accentDim:    Qt.rgba(skywinAccent.r, skywinAccent.g, skywinAccent.b, 0.20)

    // ── Text ──
    readonly property color textPrimary:   "#E0E4EC"    // slightly off-white
    readonly property color textSecondary: "#8B95A8"
    readonly property color textDisabled:  "#4B5263"

    // ── Status — safety colors untouched ──
    readonly property color success:      "#34d399"
    readonly property color successDim:   "#0a2e1a"
    readonly property color warning:      "#fbbf24"
    readonly property color warningDim:   "#2a200a"
    readonly property color error:        "#fb7185"
    readonly property color errorDim:     "#2a1018"
    readonly property color danger:       "#ef4444"
    readonly property color info:         "#38bdf8"

    // ── Checklist / gate ──
    readonly property color checkWarn:    "#fbbf24"     // use yellow for caution, not orange
    readonly property color checkWarnDim: "#2a200a"

    // ── Telemetry subsystem accent colors ──
    readonly property color pastelPink:   "#fb7185"
    readonly property color pastelBlue:   "#38bdf8"
    readonly property color pastelGreen:  "#34d399"
    readonly property color pastelPurple: "#a78bfa"

    // ── Footer ──
    readonly property color footerBg:     "#0E1018"

    // ── Misc ──
    readonly property color transparent: "#00000000"
    readonly property color border:      Qt.rgba(skywinAccent.r, skywinAccent.g, skywinAccent.b, 0.25)
    readonly property color borderLight: Qt.rgba(skywinAccent.r, skywinAccent.g, skywinAccent.b, 0.18)
}
