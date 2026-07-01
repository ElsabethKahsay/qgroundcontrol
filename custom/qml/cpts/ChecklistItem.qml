// Component: ChecklistItem
// Purpose: Single checklist item display with status dot, label, evaluation message, and
//   interactive controls (checkbox for manual items, trigger button for momentary/hardware tests).
// Properties:
//   checked (bool) — whether the item has been checked/confirmed
//   status (string) — current status: "pending", "passed", or "failed"
//   autoItem (bool) — if true, item is auto-evaluated and not user-interactive
//   momentary (bool) — if true, shows a TRIGGER button instead of a checkbox
//   isHardwareTest (bool) — if true, shows a "Run Test" button for hardware tests
//   text (string) — label text displayed for the item
//   evaluationMessage (string) — result message shown after evaluation
//   progressText (string) — progress description shown during operation
//   operationRunning (bool) — whether a test/operation is currently running
//   motorTestProgress (real) — motor test progress value 0.0–1.0
//   motorTestPreValues (string) — pre-test motor values
//   motorTestCurrentValues (string) — current motor values during test
//   reArmFailed (bool) — if true, shows "Re-arm failed" label
// Signals:
//   toggled(bool checked) — emitted when checkbox is toggled
//   trigger() — emitted when trigger/run button is clicked
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Rectangle {
    id: root
    width: parent ? parent.width : 200
    height: contentLayout.height + Config.spacingMedium * 2
             + (motorTestBar.visible ? motorTestBar.height + Config.spacingSmall : 0)
    radius: Config.radiusMedium
    color: Colors.surface

    property bool checked: false
    property string status: "pending"
    property bool autoItem: false
    property bool momentary: false
    property bool isHardwareTest: false
    property string text: ""
    property string evaluationMessage: ""
    property string progressText: ""
    property bool operationRunning: false
    property real motorTestProgress: 0.0
    property string motorTestPreValues: ""
    property string motorTestCurrentValues: ""
    property bool reArmFailed: false

    border.color: checked || status === "passed" ? Colors.success
                 : status === "failed" ? Colors.error
                 : Colors.border
    border.width: checked || status === "passed" || status === "failed" ? 2 : 1

    signal toggled(bool checked)
    signal trigger()

    ColumnLayout {
        id: contentLayout
        anchors {
            left: parent.left; right: parent.right
            top: parent.top
            margins: Config.spacingMedium
        }
        spacing: Config.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingMedium

            Rectangle {
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
                radius: 6
                color: root.checked || root.status === "passed" ? Colors.success
                     : root.status === "failed" ? Colors.error
                     : Colors.textSecondary
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    text: root.text
                    color: Colors.textPrimary
                    font.pixelSize: Config.fontSizeBody
                }

                Label {
                    visible: root.evaluationMessage.length > 0
                    text: root.evaluationMessage
                    color: root.status === "failed" ? Colors.error : Colors.success
                    font.pixelSize: Config.fontSizeSmall
                    font.family: root.evaluationMessage.indexOf("\u00b5s") >= 0 ? "monospace" : "sans-serif"
                    wrapMode: Text.WordWrap
                }

                Label {
                    visible: !root.checked && root.status === "pending" && !root.autoItem && !root.operationRunning && !root.isHardwareTest
                    text: root.evaluationMessage.length > 0 ? root.evaluationMessage : "Tap to confirm"
                    color: Colors.textSecondary
                    font.pixelSize: Config.fontSizeSmall
                }

                Label {
                    visible: root.operationRunning && root.progressText.length > 0
                    text: root.progressText
                    color: Colors.warning
                    font.pixelSize: Config.fontSizeSmall
                    font.italic: true
                }

                RowLayout {
                    visible: root.operationRunning
                    spacing: Config.spacingSmall

                    BusyIndicator {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        running: root.operationRunning
                    }
                }
            }

            CheckBox {
                visible: !root.momentary && !root.isHardwareTest
                checked: root.checked
                enabled: !root.autoItem && !root.operationRunning
                onClicked: root.toggled(checked)
            }

            Button {
                visible: root.momentary && !root.isHardwareTest
                text: root.operationRunning ? "\u23f3" : "TRIGGER"
                enabled: !root.operationRunning
                onClicked: root.trigger()
            }

            Button {
                visible: root.isHardwareTest
                text: root.operationRunning ? "\u23f3" : "Run Test"
                enabled: !root.operationRunning && root.status !== "passed"
                highlighted: root.status === "pending"
                onClicked: root.trigger()
            }

            Label {
                visible: root.reArmFailed
                text: "Re-arm failed"
                color: Colors.error
                font.pixelSize: Config.fontSizeSmall
                font.bold: true
            }
        }
    }
}
