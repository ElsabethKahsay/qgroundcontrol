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

    // ── Extended status (real-time indicators) ──
    readonly property color testing:      "#FFD700"   // gold — motor/action testing
    readonly property color pass:         "#4ADE80"   // bright green — passed
    readonly property color fail:         "#F87171"   // soft red — failed

    // ── Background surfaces (extended) ──
    readonly property color bgPrimary:    "#1F2937"   // card/section header bg
    readonly property color bgSecondary:  "#F3F4F6"   // light alternate bg

    // ── Extended accent ──
    readonly property color accentCyan:   "#00D4FF"   // bright cyan for indicators

    // ── Button teal states ──
    readonly property color teal:         "#0891B2"   // default state
    readonly property color tealLight:    "#0AA8D6"   // hover state
    readonly property color tealDark:     "#065F7C"   // pressed state

    // ── Extended text ──
    readonly property color textMuted:    "#6B7280"   // tertiary/muted text
    readonly property color textInverse:  "#E8ECF4"   // light text on dark surfaces

    // ── Divider ──
    readonly property color divider:      "#374151"   // section/card dividers

    // ── Purple dialog theme ──
    readonly property color dialogBg:         "#1A0A2E"   // very dark purple bg
    readonly property color dialogSurface:    "#2D1B4E"   // dialog card surface
    readonly property color dialogAccent:     "#9B59B6"   // purple borders/icons
    readonly property color dialogHighlight:  "#F8BBD0"   // light purple label
    readonly property color dialogFocus:      "#E91E63"   // pink focus ring
    readonly property color dialogText:       "#FFFFFF"   // white text

    // ── Checklist-specific ──
    readonly property color checkFailLight:   "#EF9A9A"
    readonly property color checkPassLight:   "#A5D6A7"
    readonly property color checkAccent:      "#CE93D8"

    // ── Footer ──
    readonly property color footerBg:     "#0E1018"

    // ── Misc ──
    readonly property color transparent: "#00000000"
    readonly property color border:      Qt.rgba(skywinAccent.r, skywinAccent.g, skywinAccent.b, 0.25)
    readonly property color borderLight: Qt.rgba(skywinAccent.r, skywinAccent.g, skywinAccent.b, 0.18)
}
