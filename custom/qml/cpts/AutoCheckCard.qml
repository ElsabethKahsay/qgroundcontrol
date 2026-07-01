// Component: AutoCheckCard
// Purpose: Displays a single auto-evaluated preflight check with status icon, label, and message.
//   Background color reflects check status (pending/passed/failed/warning).
// Properties:
//   check (var, required) — the check object with properties {status, label, message}
//   isLast (bool) — whether this card is the last in a list (unused by current layout)
//   stale (bool) — if true, shows a stale-data warning indicator
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var check

    property bool isLast: false
    property bool stale: false

    implicitHeight: layout.implicitHeight + 24
    radius: 6
    color: stale ? Colors.checkWarnDim : statusColor(check.status)
    border.color: stale ? Colors.checkWarn
                : check.status === 1 ? Colors.success
                : check.status === 2 ? Colors.error
                : Colors.border
    border.width: stale || check.status === 1 || check.status === 2 ? 2 : 1

    function statusColor(status) {
        switch (status) {
        case 0: return Colors.surfaceLight   // Pending
        case 1: return Colors.successDim     // Passed
        case 2: return Colors.errorDim       // Failed
        case 3: return Colors.checkWarnDim   // Warning
        case 4: return Colors.errorDim       // Error
        default: return Colors.surfaceLight
        }
    }

    function statusIcon(status) {
        switch (status) {
        case 0: return "\u23F3"  // Pending
        case 1: return "\u2713"  // Passed
        case 2: return "\u2717"  // Failed
        case 3: return "\u26A0"  // Warning
        case 4: return "\u2717"  // Error
        default: return "?"
        }
    }

    RowLayout {
        id: layout
        anchors {
            left: parent.left; leftMargin: 12
            right: parent.right; rightMargin: 12
            verticalCenter: parent.verticalCenter
        }
        spacing: 8

        Text {
            text: statusIcon(check.status)
            font.pixelSize: 18
            color: check.status === 1 ? Colors.success
                 : check.status === 2 ? Colors.error
                 : check.status === 3 ? Colors.checkWarn
                 : Colors.textSecondary
            Layout.alignment: Qt.AlignVCenter
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: check.label
                font.pixelSize: 14
                font.bold: true
                color: Colors.textPrimary
                elide: Text.ElideRight
            }

            Text {
                text: check.message || "Waiting..."
                font.pixelSize: 11
                color: Colors.textSecondary
                elide: Text.ElideRight
                visible: check.message !== ""
            }
        }

        Text {
            text: stale ? "\u26A0 Stale" : ""
            font.pixelSize: 10
            color: Colors.checkWarn
            visible: stale
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
