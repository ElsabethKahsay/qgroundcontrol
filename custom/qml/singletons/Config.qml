pragma Singleton
import QtQuick
import QtQuick.Window

QtObject {
    // Compact GCS — smaller fonts, tighter grid, unique styling
    readonly property int fontSizeH1: 16
    readonly property int fontSizeH2: 13
    readonly property int fontSizeH3: 12
    readonly property int fontSizeBody: 11
    readonly property int fontSizeSmall: 10
    readonly property int spacingSmall: 6
    readonly property int spacingMedium: 10
    readonly property int spacingLarge: 14
    readonly property int spacingXLarge: 20
    readonly property int radiusSmall: 3
    readonly property int radiusMedium: 6
    readonly property int radiusLarge: 10

    // Animation durations (ms)
    readonly property int animFast: 150
    readonly property int animNormal: 200
    readonly property int animSlow: 300

    // Responsive breakpoints
    readonly property int breakpointNarrow: 900
    readonly property int breakpointSingle: 1000
    readonly property int breakpointSplit: 1400

    // Layout panel widths
    readonly property int kMinPanelWidth: 320
    readonly property int kCollapsedHeight: 40
    readonly property int kExpandedItemHeight: 60

    // Font scaling factor (guarded — Screen not always available outside Window context)
    readonly property real fontScale: typeof Screen !== 'undefined' && Screen !== null && Screen.pixelDensity > 0
        ? Math.min(1.3, Math.max(0.8, Screen.pixelDensity / 4.0))
        : 1.0

    // Font family (loaded from C++ at startup)
    readonly property string fontFamily: "Abel"

    // Connection quality thresholds
    readonly property int connQualityGood: 80
    readonly property int connQualityDegraded: 1

    // MAVLink defaults
    readonly property string defaultMavlinkUrl: "udp://:14550"
    readonly property string ardupilotSitlUrl: "udp://127.0.0.1:14550"

    // Maintenance thresholds
    readonly property int kMaintWarningPercent: 80
    readonly property int kMaintCriticalPercent: 100

    // Motor / PWM thresholds
    readonly property int kPwmMin: 800
    readonly property int kPwmMax: 2200
    readonly property int kPwmRangeMin: 1000
    readonly property int kPwmRangeMax: 2000
    readonly property int kMotorCountQuad: 4
    readonly property int kMotorCountHexa: 6
    readonly property int kMotorCountOcta: 8
    readonly property int kMotorCountFixedWing: 1
    readonly property int kMotorCountVtolQuad: 4

    // EKF thresholds
    readonly property real kEkfVarianceMax: 1.0

    // Layout ratios
    readonly property real kTelemetryPanelRatio: 0.35
    readonly property real kChecklistPanelRatio: 0.65

    // Telemetry row constants
    readonly property int kMinRowHeight: 56
    readonly property int kCategoryVerticalSpacing: 12
    readonly property color kCategoryDividerColor: "#334155"
    readonly property int kCategoryDividerHeight: 1
    readonly property int kHealthGridColumns: 2
    readonly property int kTelemetryRowPadding: 12
    readonly property int kHealthItemSize: 80

    // Footer
    readonly property int kFooterHeight: 48
}
