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
    readonly property color surface2:     "#1A1E28"   // slightly lighter surface (weather panel)

    // ── Brand / accent — all derived from skywinAccent ──
    readonly property color primary:      skywinAccent
    readonly property color secondary:    Qt.lighter(skywinAccent, 1.15)
    readonly property color accent:       skywinAccent
    readonly property color accentDim:    Qt.rgba(skywinAccent.r, skywinAccent.g, skywinAccent.b, 0.20)

    // ── Text ──
    readonly property color textPrimary:   "#E0E4EC"    // slightly off-white
    readonly property color textSecondary: "#8B95A8"
    readonly property color textDisabled:  "#4B5263"

    // ══════════════════════════════════════════════════════════════════
    //  SEMANTIC STATUS — the only status colours safety screens should use.
    //  Operators read go/no-go from these, so one meaning = one colour.
    // ══════════════════════════════════════════════════════════════════
    readonly property color statusPass:       "#34d399"
    readonly property color statusPassDim:    "#0a2e1a"
    readonly property color statusFail:       "#fb7185"
    readonly property color statusFailDim:    "#2a1018"
    readonly property color statusWarn:       "#fbbf24"
    readonly property color statusWarnDim:    "#2a200a"
    readonly property color statusStale:      "#fbbf24"   // stale data reads as caution
    readonly property color statusStaleDim:   "#2a200a"
    readonly property color statusPending:    "#8B95A8"
    readonly property color statusInfo:       "#38bdf8"
    readonly property color statusInfoDim:    "#0a2233"

    // ── Status — legacy names, aliases of the semantic tokens ──
    readonly property color success:      statusPass
    readonly property color successDim:   statusPassDim
    readonly property color warning:      statusWarn
    readonly property color warningDim:   statusWarnDim
    readonly property color error:        statusFail
    readonly property color errorDim:     statusFailDim
    readonly property color danger:       "#ef4444"   // destructive actions, not a check status
    readonly property color info:         statusInfo

    // ── Checklist / gate ──
    readonly property color checkWarn:    statusWarn
    readonly property color checkWarnDim: statusWarnDim

    // ── Telemetry subsystem accent colors ──
    readonly property color pastelPink:   "#fb7185"
    readonly property color pastelBlue:   "#38bdf8"
    readonly property color pastelGreen:  "#34d399"
    readonly property color pastelPurple: "#a78bfa"

    // ── Extended status (real-time indicators) ──
    readonly property color testing:      "#FFD700"   // gold — motor/action testing
    readonly property color pass:         statusPass
    readonly property color fail:         statusFail
    readonly property color statePass:    statusPass  // go/no-go pass
    readonly property color stateFail:    statusFail  // go/no-go fail

    // ── Background surfaces (extended) ──
    readonly property color bgPrimary:    "#1F2937"   // card/section header bg

    // ── Extended accent ──
    readonly property color accentCyan:   "#00D4FF"   // bright cyan for indicators

    // ── Button teal states ──
    readonly property color teal:         "#0891B2"   // default state
    readonly property color tealDark:     "#065F7C"   // pressed state

    // ── Extended text ──
    readonly property color textMuted:    "#6B7280"   // tertiary/muted text
    readonly property color textInverse:  "#E8ECF4"   // light text on dark surfaces

    // ── Divider ──
    readonly property color divider:      "#374151"   // section/card dividers

    // ── Dialogs — same surfaces and accent as the rest of the app ──
    // (formerly a separate purple/pink theme)
    readonly property color dialogBg:         background
    readonly property color dialogSurface:    surface
    readonly property color dialogAccent:     accent
    readonly property color dialogHighlight:  secondary   // emphasised label
    readonly property color dialogFocus:      accent      // focus ring
    readonly property color dialogText:       textPrimary

    // ── Airspace zone categories (map fill / border) ──
    readonly property color zoneRegulatory:       "#ef4444"
    readonly property color zoneRegulatoryBorder: "#b91c1c"
    readonly property color zoneObstacle:         "#f97316"
    readonly property color zoneObstacleBorder:   "#c2410c"
    readonly property color zoneRestricted:       "#7f1d1d"
    readonly property color zoneRestrictedBorder: "#991b1b"
    readonly property color zoneTemporary:        "#eab308"
    readonly property color zoneTemporaryBorder:  "#a16207"
    readonly property color zoneOther:            "#9ca3af"
    readonly property color zoneOtherBorder:      "#6b7280"

    // ── Footer ──
    readonly property color footerBg:     "#0E1018"

    // ── Misc ──
    readonly property color transparent: "#00000000"
    readonly property color border:      Qt.rgba(skywinAccent.r, skywinAccent.g, skywinAccent.b, 0.25)
    readonly property color borderLight: Qt.rgba(skywinAccent.r, skywinAccent.g, skywinAccent.b, 0.18)
}
