// Component: ManualCheckCard
// Purpose: Displays a manual confirmation check item with a "Confirm" button for the operator
//   to mark the item as checked. Background color reflects confirmed/unconfirmed status.
// Properties:
//   check (var, required) — the check object with properties {status, label, message, confirm()}
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var check

    implicitHeight: layout.implicitHeight + 24
    radius: 6
    color: check.status === 1 ? Colors.successDim
         : check.status === 2 ? Colors.errorDim
         : Colors.surfaceLight
    border.color: check.status === 1 ? Colors.success
                : check.status === 2 ? Colors.error
                : Colors.border
    border.width: check.status === 1 || check.status === 2 ? 2 : 1

    RowLayout {
        id: layout
        anchors {
            left: parent.left; leftMargin: 12
            right: parent.right; rightMargin: 12
            verticalCenter: parent.verticalCenter
        }
        spacing: 8

        Text {
            text: check.status === 1 ? "\u2713" : check.status === 2 ? "\u2717" : "\u25CB"
            font.pixelSize: 18
            color: check.status === 1 ? Colors.success
                 : check.status === 2 ? Colors.error
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
            }

            Text {
                text: check.message || "Tap to confirm"
                font.pixelSize: 11
                color: Colors.textSecondary
            }
        }

        Button {
            text: check.status === 1 ? "Passed" : "Confirm"
            enabled: check.status !== 1
            highlighted: check.status !== 1
            onClicked: {
                check.confirm("Operator confirmed")
            }
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
