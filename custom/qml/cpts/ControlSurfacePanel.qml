import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

// ── Control Surface Test Launcher ────────────────────────────────────────────
// Compact card for fixed wing / VTOL bench testing of control surfaces.
// MANUAL-mode gate + RC override hold neutralize stabilization while testing;
// the sweep drives servos 1–4 via the legacy profile API.
Rectangle {
    id: root

    implicitHeight: surfaceCol.implicitHeight + Config.spacingMedium * 2
    radius: Config.radiusMedium
    color: Colors.surface
    border.color: Colors.border
    border.width: 1

    readonly property bool _fwLike: VehicleProfileManager.vehicleKind === "FIXED_WING"
                                 || VehicleProfileManager.vehicleKind === "VTOL_CONVENTIONAL"
    readonly property bool _vehicleConnected:
        !!QGroundControl.multiVehicleManager.activeVehicle

    ColumnLayout {
        id: surfaceCol
        anchors { fill: parent; margins: Config.spacingMedium }
        spacing: Config.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingMedium

            Rectangle {
                width: 36; height: 36; radius: 18
                color: Colors.accentDim
                border.color: Colors.accent
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "\u21C4"
                    font.pixelSize: Config.fontSizeBody
                    font.bold: true
                    color: Colors.accent
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: qsTr("Control Surface Test")
                    font.pixelSize: Config.fontSizeBody
                    font.bold: true
                    color: Colors.textPrimary
                }
                Text {
                    text: root._fwLike
                        ? qsTr("Servo sweep \u2014 verify deflection direction and range")
                        : qsTr("Fixed wing / VTOL airframes only")
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                }
            }

            // ── MANUAL mode gate ──
            Button {
                id: manualModeBtn
                property bool _inManual: {
                    var v = QGroundControl.multiVehicleManager.activeVehicle
                    return v ? v.flightMode === "MANUAL" : false
                }
                visible: root._fwLike
                enabled: root._vehicleConnected && !_inManual
                text: _inManual ? qsTr("✓ MANUAL Mode") : qsTr("Switch to MANUAL")
                onClicked: {
                    var v = QGroundControl.multiVehicleManager.activeVehicle
                    if (v) v.flightMode = "MANUAL"
                }
                background: Rectangle {
                    radius: 6
                    color: manualModeBtn._inManual ? "#1A3A2A" : Colors.warning
                    border.color: manualModeBtn._inManual ? Colors.statePass : Colors.warning
                    border.width: 1
                }
                contentItem: Text {
                    text: manualModeBtn.text
                    font.pixelSize: Config.fontSizeSmall
                    font.bold: true
                    color: manualModeBtn._inManual ? Colors.statePass : Colors.background
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                ToolTip.text: qsTr("Motor and surface tests require MANUAL flight mode")
                ToolTip.visible: hovered
            }
        }

        // ── RC override + sweep controls ──
        RowLayout {
            Layout.fillWidth: true
            visible: root._fwLike
            spacing: Config.spacingMedium

            Label {
                text: qsTr("RC Override:")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
                anchors.verticalCenter: parent.verticalCenter
            }
            Switch {
                id: rcOverrideSwitch
                enabled: root._vehicleConnected
                checked: HardwareTestController.rcOverrideActive
                onToggled: HardwareTestController.setRcOverrideActive(checked)
                ToolTip.text: checked
                    ? qsTr("RC override ON — attitude control neutralized for testing")
                    : qsTr("RC override OFF — normal RC input active")
                ToolTip.visible: hovered
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Run Servo Sweep")
                enabled: root._vehicleConnected && !HardwareTestController.isArmed
                onClicked: HardwareTestController.runServoSweep()
                ToolTip.text: qsTr("Sweeps servos 1\u20134 through 1200\u20131800 µs")
                ToolTip.visible: hovered
            }
            Button {
                text: qsTr("STOP ALL")
                enabled: root._vehicleConnected
                onClicked: HardwareTestController.stopAll()
                background: Rectangle {
                    radius: 6
                    color: parent.enabled ? Colors.errorDim : Colors.surfaceLight
                    border.color: parent.enabled ? Colors.error : Colors.border
                    border.width: 1
                }
                contentItem: Text {
                    text: qsTr("STOP ALL")
                    font.pixelSize: Config.fontSizeSmall
                    font.bold: true
                    color: Colors.error
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
