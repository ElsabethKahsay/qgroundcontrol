// Component: PreflightToolbarIndicator
// Purpose: Toolbar indicator showing preflight connection/status dot and label. On click,
//   opens the ArmGateDialog if the gate is blocked, or navigates to the Analyze page.
//   Dialog is opened via Loader only on explicit user click (no auto-popups).
// Properties:
//   showIndicator (bool) — whether the indicator row is visible (default: true)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl.Palette
import QGroundControl.ScreenTools
import QGroundControl.Controls
import QGroundControl

Item {
    id: root

    property bool showIndicator: true

    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: indicatorRow.width + ScreenTools.defaultFontPixelWidth * 2

    property var _pfm: PreflightManager
    property var _gate: ArmingGate

    function _stateColor() {
        if (!_pfm) return qgcPal.colorGrey
        switch (_pfm.state) {
        case 0: return qgcPal.colorGrey
        case 1: return qgcPal.buttonHighlight
        case 2: return "#9B59B6"
        case 3: return "#E91E63"
        case 4: return qgcPal.colorGreen
        case 5: return "#009688"
        case 6: return qgcPal.colorRed
        default: return qgcPal.colorGrey
        }
    }

    function _stateLabel() {
        if (!_pfm) return "No Vehicle"
        switch (_pfm.state) {
        case 0: return "Disconnected"
        case 1: return "Connecting..."
        case 2: return "Loading Params"
        case 3: return "Preflight Active"
        case 4: return "Checks Passed"
        case 5: return "Ready to Arm"
        case 6: return "Armed"
        default: return "Unknown"
        }
    }

    function _gateBlocked() {
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
            color: _gateBlocked() ? qgcPal.colorRed : _stateColor()
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            Text {
                text: "Preflight"
                font.pointSize: ScreenTools.smallFontPointSize
                color: qgcPal.text
            }
            Text {
                text: _stateLabel()
                font.pointSize: ScreenTools.smallFontPointSize
                color: _gateBlocked() ? qgcPal.colorRed : qgcPal.buttonText
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

    Loader {
        id: armGateDialogLoader
        active: false
        source: "qrc:/qml/cpts/ArmGateDialog.qml"
        onLoaded: {
            item.criticalFailCount = _pfm ? _pfm.failedChecks : 0
            item.manualFailCount = _pfm ? _pfm.pendingChecks : 0
            item.accepted.connect(function() {
                mainWindow.armVehicleRequest()
            })
            item.closed.connect(function() {
                armGateDialogLoader.active = false
            })
            item.reviewChecksRequested.connect(function() {
                armGateDialogLoader.active = false
            })
            item.open()
        }
    }
}
