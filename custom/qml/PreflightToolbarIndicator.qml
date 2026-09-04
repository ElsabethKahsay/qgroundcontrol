// Component: PreflightToolbarIndicator
// Purpose: Toolbar indicator showing preflight connection/status dot and label. On click,
//   opens the ArmGateDialog if the gate is blocked, or navigates to the Analyze page.
//   Dialog is opened via Loader only on explicit user click (no auto-popups).
// Properties:
//   showIndicator (bool) — whether the indicator row is visible (default: true)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl.ScreenTools
import QGroundControl.Controls
import QGroundControl
import com.uav.preflight 1.0
import cpts 1.0 as ADL

Item {
    id: root

    property bool showIndicator: true

    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: indicatorRow.width + ScreenTools.defaultFontPixelWidth * 2

    property var _pfm: PreflightManager
    property var _gate: ArmingGate

    function _stateColor() {
        var v = QGroundControl.multiVehicleManager.activeVehicle
        if (v && v.armed) return Colors.success
        if (!_pfm) return Colors.textDisabled
        switch (_pfm.state) {
        case 0: return Colors.textDisabled
        case 1: return Colors.accent
        case 2: return Colors.dialogAccent
        case 3: return Colors.dialogFocus
        case 4: return Colors.success
        case 5: return Colors.teal
        case 6: return Colors.error
        default: return Colors.textDisabled
        }
    }

    function _stateLabel() {
        var v = QGroundControl.multiVehicleManager.activeVehicle
        if (!v) return qsTr("No Vehicle")
        if (v.armed && v.flying) return qsTr("Flying")
        if (v.armed) return qsTr("Armed")
        var s = FlightSession.state
        if (s >= FlightSession.ReadyToArm) return qsTr("Ready")
        return qsTr("Not Ready")
    }

    function _gateBlocked() {
        var v = QGroundControl.multiVehicleManager.activeVehicle
        if (v && v.armed) return false
        return _gate && !_gate.armingAllowed && _gate.denialReason && _gate.denialReason.length > 0
    }

    Connections {
        target: _gate
        onArmingAllowedChanged: {
            // Popup only opens on explicit user click, not automatically
        }
        onDenialReasonChanged: {
            // Popup only opens on explicit user click, not automatically
        }
    }

    Row {
        id: indicatorRow
        anchors.centerIn: parent
        spacing: ScreenTools.defaultFontPixelWidth / 2

        Rectangle {
            id: statusDot
            width: 12
            height: 12
            radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: _gateBlocked() ? Colors.error : _stateColor()
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            Text {
                text: FlightSession.mode === FlightSession.None ? qsTr("Preflight")
                      : FlightSession.sessionSummary
                font.pointSize: ScreenTools.smallFontPointSize
                color: Colors.textPrimary
                elide: Text.ElideRight
                width: 160
            }
            Text {
                text: _stateLabel()
                font.pointSize: ScreenTools.smallFontPointSize
                color: _gateBlocked() ? Colors.error : Colors.textPrimary
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (_gateBlocked()) {
                armGateDialogLoader.active = true
            } else {
                mainWindow.showAnalyzeTool()
            }
        }
    }

    ADL.ArmGateDialogLoader {
        id: armGateDialogLoader
        criticalFailCount: _pfm ? _pfm.failedChecks : 0
        manualFailCount: _pfm ? _pfm.pendingChecks : 0
        onAccepted: {
            mainWindow.armVehicleRequest()
        }
    }
}
