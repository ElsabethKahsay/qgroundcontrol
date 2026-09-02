import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl
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

        // ── Arm / Disarm / Mode controls (always visible, stacked) ──
        Column {
            Layout.fillWidth: true
            spacing: 8

            // FORCE ARM
            Button {
                id: forceArmBtn
                width: parent.width
                property bool _isArmed:
                    QGroundControl.multiVehicleManager.activeVehicle
                        ? QGroundControl.multiVehicleManager.activeVehicle.armed
                        : false
                text: _isArmed ? "✓ Armed" : "Force Arm"
                enabled: !_isArmed
                onClicked: forceArmDialog.open()
                background: Rectangle {
                    color: parent._isArmed ? "#1A3A2A" : Colors.warning
                    radius: 6
                }
                contentItem: Label {
                    text: parent.text
                    color: "white"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // DISARM
            Button {
                width: parent.width
                height: forceArmBtn.height
                text: "Disarm"
                enabled: {
                    var v = QGroundControl.multiVehicleManager.activeVehicle
                    return v ? v.armed : false
                }
                onClicked: disarmDialog.open()
                background: Rectangle {
                    color: Colors.stateFail
                    radius: 6
                }
                contentItem: Label {
                    text: "Disarm"
                    color: "white"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // MODE CHANGE — MANUAL / AUTO
            Button {
                id: modeButton
                width: parent.width
                height: forceArmBtn.height
                property string _mode: {
                    var v = QGroundControl.multiVehicleManager.activeVehicle
                    return v ? v.flightMode : ""
                }
                property bool _isManual: _mode === "MANUAL" || _mode === "Manual"
                text: _isManual ? "✓ MANUAL" : "→ MANUAL"
                onClicked: {
                    var v = QGroundControl.multiVehicleManager.activeVehicle
                    if (!v) return
                    v.flightMode = _isManual ? "AUTO" : "MANUAL"
                }
                background: Rectangle {
                    color: parent._isManual ? "#1A3A2A" : Colors.surface2
                    border.color: parent._isManual ? Colors.statePass : Colors.accent
                    border.width: 1.5
                    radius: 6
                }
                contentItem: Label {
                    text: parent.text
                    color: parent._isManual ? Colors.statePass : Colors.textPrimary
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
                ToolTip.text: _isManual
                    ? "Click to switch to AUTO"
                    : "Switch to MANUAL for surface/motor tests"
                ToolTip.visible: hovered
            }
        }

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
                    if (v && typeof v.setFlightMode === "function") {
                        v.setFlightMode("MANUAL")
                    }
                    if (typeof ControlSurfaceTestController !== "undefined") {
                        ControlSurfaceTestController.switchToManual()
                    }
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

    // ── Force Arm confirmation dialog ────────────────────────────────
    Dialog {
        id: forceArmDialog
        modal: true
        title: "Force Arm"
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 360
        contentItem: Label {
            text: "Force arm bypasses all preflight checks.\n"
                + "Ensure area is clear and props are safe."
            wrapMode: Text.Wrap
        }
        footer: DialogButtonBox {
            Button {
                text: "Cancel"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: "Force Arm"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    var v = QGroundControl.multiVehicleManager.activeVehicle
                    if (!v) return
                    v.sendCommand(1, 400, true, 1.0, 21196.0, 0, 0, 0, 0, 0)
                }
                background: Rectangle { color: Colors.warning; radius: 6 }
                contentItem: Label {
                    text: "Force Arm"
                    color: "white"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    // ── Disarm confirmation dialog ───────────────────────────────────
    Dialog {
        id: disarmDialog
        modal: true
        title: "Disarm Vehicle"
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 360
        contentItem: Label {
            text: "Disarm the vehicle?\nEnsure motors have stopped before approaching."
            wrapMode: Text.Wrap
        }
        footer: DialogButtonBox {
            Button {
                text: "Cancel"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: "Disarm"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: {
                    var v = QGroundControl.multiVehicleManager.activeVehicle
                    if (!v) return
                    v.sendCommand(1, 400, true, 0.0, 0, 0, 0, 0, 0, 0)
                }
                background: Rectangle { color: Colors.stateFail; radius: 6 }
                contentItem: Label {
                    text: "Disarm"
                    color: "white"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
