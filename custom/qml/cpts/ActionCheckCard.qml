// Component: ActionCheckCard
// Purpose: Card for action-based checks (e.g. motor test) with a trigger button. Displays
//   "Running..." state while the action is in progress and shows pass/fail result on completion.
// Properties:
//   check (var, required) — the check object with properties {checkId, status, evaluate()}
//   actionLabel (string) — custom label for the action button (default: "Run")
//   running (bool) — whether the action is currently executing
// Signals:
//   actionTriggered(string checkId) — emitted when user clicks the action button
//   actionCompleted(string checkId, bool success) — emitted when action finishes
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Item {
    id: root

    required property var check

    property string actionLabel: "Run"
    property bool running: false

    signal actionTriggered(string checkId)
    signal actionCompleted(string checkId, bool success)

    implicitHeight: actionBtn.height

    readonly property bool passed: check && check.status === 1
    readonly property bool failed: check && check.status === 2

    Rectangle {
        id: actionBtn
        width: parent.width
        height: 28
        radius: Config.radiusSmall
        color: running ? Colors.surfaceLight
             : passed ? Colors.success
             : failed ? Colors.errorDim
             : Colors.surface
        border.color: passed ? Colors.success
                    : failed ? Colors.error
                    : Colors.border
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: running ? "Running..." : actionLabel
            font.pixelSize: Config.fontSizeSmall
            font.bold: passed
            color: passed ? Colors.background
                 : failed ? Colors.error
                 : Colors.textPrimary
        }

        MouseArea {
            anchors.fill: parent
            enabled: !running && check && !passed
            onClicked: {
                if (check) root.actionTriggered(check.checkId)
            }
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }
}
