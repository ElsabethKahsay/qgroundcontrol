// Component: AlertBanner
// Purpose: Alert banner overlay that displays a message with severity-based styling and color.
//   Supports expand/collapse for long messages and a dismiss button.
// Properties:
//   message (string) — the alert text to display; banner is hidden when empty
//   severity (string) — one of "error", "warning", or "info" (controls color scheme)
//   expanded (bool) — if true, shows full message text; if false, truncates at 80 chars
// Signals:
//   dismissed — emitted when the user clicks the close (✕) button
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Rectangle {
    id: root

    property string message: ""
    property string severity: "info"
    property bool expanded: false

    signal dismissed()

    visible: message.length > 0

    height: expanded ? contentCol.implicitHeight + Config.spacingMedium * 2 : 40
    radius: Config.radiusSmall
    clip: true

    color: severity === "error" ? Colors.errorDim
         : severity === "warning" ? Qt.rgba(0.9, 0.6, 0.0, 0.25)
         : Colors.surfaceLight

    border.color: severity === "error" ? Colors.error
                : severity === "warning" ? Colors.warning
                : Colors.border
    border.width: 1

    Behavior on height {
        NumberAnimation { duration: Config.animationDuration; easing.type: Easing.InOutQuad }
    }

    RowLayout {
        anchors {
            left: parent.left; right: parent.right
            top: parent.top
            margins: Config.spacingSmall
        }
        spacing: Config.spacingSmall

        Text {
            text: severity === "error" ? "\u26A0\uFE0F"
                 : severity === "warning" ? "\u26A0"
                 : "\u2139\uFE0F"
            font.pixelSize: 16
            color: severity === "error" ? Colors.error
                 : severity === "warning" ? Colors.warning
                 : Colors.info
        }

        Text {
            Layout.fillWidth: true
            text: root.expanded ? root.message
                 : root.message.length > 80 ? root.message.substring(0, 77) + "..." : root.message
            color: severity === "error" ? Colors.error
                 : severity === "warning" ? Colors.warning
                 : Colors.textPrimary
            font.pixelSize: 13
            font.bold: severity === "error"
            elide: Text.ElideRight
            maximumLineCount: root.expanded ? 10 : 1
            wrapMode: Text.WordWrap
        }

        ToolButton {
            text: root.expanded ? "\u25B2" : "\u25BC"
            font.pixelSize: 12
            visible: root.message.length > 80
            onClicked: root.expanded = !root.expanded
            contentItem: Text {
                text: parent.text
                color: Colors.textSecondary
                font.pixelSize: 12
            }
            background: Rectangle { color: "transparent" }
        }

        ToolButton {
            text: "\u2715"
            font.pixelSize: 12
            onClicked: root.dismissed()
            contentItem: Text {
                text: parent.text
                color: Colors.textSecondary
                font.pixelSize: 12
            }
            background: Rectangle { color: "transparent" }
        }
    }
}
