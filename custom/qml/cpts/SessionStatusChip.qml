// Component: SessionStatusChip
// Purpose: Fly-view toolbar chip showing the connected UAV's name and the
//          current session mode. With no mode selected it reads "Select mode"
//          and opens the session-mode dialog on click.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.ScreenTools

import com.uav.preflight 1.0

Item {
    id: root

    // Read by the toolbar's indicator Loader (corePlugin.toolBarIndicators).
    property bool showIndicator: true

    property var  _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property bool _armed:         _activeVehicle ? _activeVehicle.armed : false
    property bool _noMode:        FlightSession.mode === FlightSession.None

    readonly property string vehicleLabel: {
        if (!_activeVehicle) return qsTr("No vehicle")
        var n = (typeof VehicleRegistry !== 'undefined' && VehicleRegistry)
                ? (VehicleRegistry.currentVehicleName || VehicleRegistry.vehicleName) : ""
        return n && n.length > 0 ? n : qsTr("Vehicle %1").arg(_activeVehicle.id)
    }

    readonly property string modeLabel: {
        switch (FlightSession.mode) {
        case FlightSession.Flight:   return qsTr("FLIGHT")
        case FlightSession.Testing:  return qsTr("TESTING")
        case FlightSession.Training: return qsTr("TRAINING")
        default:                     return qsTr("Select mode")
        }
    }

    readonly property color modeColor: {
        switch (FlightSession.mode) {
        case FlightSession.Flight:   return Colors.statusPass
        case FlightSession.Testing:  return Colors.statusInfo
        case FlightSession.Training: return Colors.statusWarn
        default:                     return Colors.statusFail
        }
    }

    implicitWidth:  row.implicitWidth + ScreenTools.defaultFontPixelWidth * 2
    implicitHeight: ScreenTools.toolbarHeight

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: ScreenTools.defaultFontPixelWidth * 0.8

        // Link dot + UAV name
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            width: ScreenTools.defaultFontPixelHeight * 0.5
            height: width
            radius: width / 2
            color: root._activeVehicle ? Colors.statusPass : Colors.statusPending
        }
        Text {
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 22
            text: root.vehicleLabel
            elide: Text.ElideRight
            font.pointSize: ScreenTools.defaultFontPointSize
            font.bold: true
            color: root._activeVehicle ? Colors.textPrimary : Colors.textSecondary
        }

        // Session mode badge
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 1.4
            Layout.preferredWidth: modeText.implicitWidth + ScreenTools.defaultFontPixelWidth * 1.6
            radius: height / 2
            color: Qt.rgba(root.modeColor.r, root.modeColor.g, root.modeColor.b, 0.18)
            border.color: root.modeColor
            border.width: 1

            Text {
                id: modeText
                anchors.centerIn: parent
                text: root.modeLabel
                font.pointSize: ScreenTools.smallFontPointSize
                font.bold: true
                color: root.modeColor
            }
        }
    }

    // Mode can be chosen (or changed) only while disarmed.
    MouseArea {
        anchors.fill: parent
        enabled: !root._armed
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        ToolTip.visible: containsMouse
        ToolTip.delay: 400
        ToolTip.text: root._noMode ? qsTr("Select a session mode before preflight")
                                   : qsTr("Change session mode")
        onClicked: sessionStartDialog.open()
    }

    SessionStartDialog {
        id: sessionStartDialog
        parent: Overlay.overlay
    }
}
