// Component: FcStatusPanel
// Purpose: Read-only flight-controller status snapshot for the Fly view.
//   Shows armed state, flight mode, battery SOC/voltage/current, link quality,
//   GPS, IMU/compass health, RC and EKF health as simple OK/WARN/FAIL rows.
//   Closed from the header or by the FC-status quick action.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors

import QtQuick
import QtQuick.Layouts
import com.uav.preflight 1.0

Rectangle {
    id: root

    property bool visiblePanel: true
    signal closeRequested()

    readonly property var _tel: typeof TelemetryProvider !== "undefined" ? TelemetryProvider : null
    readonly property bool _connected: _tel ? _tel.isConnected : false

    // ── Status helpers ──
    function _batteryValue() {
        if (!_connected) return "—"
        var soc = _tel.batterySocPct
        var socStr = soc < 0 ? "?" : soc + "%"
        var v = _tel.batteryVoltage
        var vStr = v > 0.1 ? v.toFixed(1) + " V" : "—"
        var aStr = _tel.batteryCurrentSmoothAmps > 0 ? _tel.batteryCurrentSmoothAmps.toFixed(1) + " A"
                                                      : (_tel.batteryCurrentAmps > 0 ? _tel.batteryCurrentAmps.toFixed(1) + " A" : "—")
        return socStr + " \u00B7 " + vStr + " \u00B7 " + aStr
    }

    function _batteryColor() {
        if (!_connected) return Colors.textDisabled
        var soc = _tel.batterySocPct
        if (soc < 0) return Colors.warning
        if (soc < 15) return Colors.error
        if (soc < 30) return Colors.warning
        return Colors.success
    }

    function _linkColor() {
        if (!_connected) return Colors.textDisabled
        var q = _tel.connectionQuality
        if (q <= Config.connQualityDegraded) return Colors.error
        if (q < Config.connQualityGood) return Colors.warning
        return Colors.success
    }

    function _qualityColor(q) {
        if (q <= 0) return Colors.error
        if (q === 1) return Colors.warning
        return Colors.success
    }

    function _ekfMaxRatio() {
        if (!_tel) return 0
        return Math.max(_tel.estimatorVelRatio, _tel.estimatorPosHorizRatio,
                        _tel.estimatorPosVertRatio, _tel.estimatorMagRatio)
    }

    function _ekfLabel() {
        if (!_connected) return "—"
        var m = _ekfMaxRatio()
        if (m > 1.0) return "WARN " + m.toFixed(2)
        return "ok"
    }

    function _ekfColor() {
        if (!_connected) return Colors.textDisabled
        return _ekfMaxRatio() > 1.0 ? Colors.warning : Colors.success
    }

    // ── Rows ──
    component StatusDot: Rectangle {
        property color dotColor: Colors.textDisabled
        width: 8
        height: 8
        radius: width / 2
        color: dotColor
    }

    component PanelRow: RowLayout {
        property string label: ""
        property string value: ""
        property color dotColor: Colors.textDisabled
        property color valueColor: Colors.textPrimary
        Layout.fillWidth: true
        Layout.preferredHeight: 22
        spacing: Config.spacingSmall

        StatusDot {
            dotColor: parent.dotColor
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            text: label
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            Layout.preferredWidth: 90
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            text: value
            font.pixelSize: Config.fontSizeSmall
            font.weight: Font.DemiBold
            color: valueColor
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
    }

    // ── Visual ──
    visible: visiblePanel
    width: 300
    height: bodyCol.implicitHeight + Config.spacingMedium * 2 + header.implicitHeight
    radius: Config.radiusLarge
    color: Qt.rgba(Colors.surface.r, Colors.surface.g, Colors.surface.b, 0.95)
    border.color: Colors.border
    border.width: 1

    ColumnLayout {
        id: bodyCol
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.leftMargin: Config.spacingMedium
        anchors.rightMargin: Config.spacingMedium
        anchors.topMargin: Config.spacingSmall
        spacing: Config.spacingSmall

        // Header
        RowLayout {
            id: header
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: qsTr("FC STATUS")
                font.pixelSize: Config.fontSizeSmall
                font.weight: Font.DemiBold
                color: Colors.textSecondary
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: qsTr("\u2715")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
                Layout.alignment: Qt.AlignVCenter
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeRequested()
                }
            }
        }

        // Empty / disconnected state
        Text {
            visible: !root._connected
            text: qsTr("No vehicle connected")
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textDisabled
            Layout.fillWidth: true
        }

        // Live rows (only when connected)
        PanelRow { label: qsTr("Armed");       value: _tel && _tel.armed ? "ARMED" : "Disarmed"
                   dotColor: _tel && _tel.armed ? Colors.success : Colors.textDisabled
                   valueColor: _tel && _tel.armed ? Colors.success : Colors.textPrimary }
        PanelRow { label: qsTr("Flight mode");  value: _tel ? _tel.flightMode : "—"
                   dotColor: root._connected ? Colors.success : Colors.textDisabled }
        PanelRow { label: qsTr("Battery");      value: root._batteryValue()
                   dotColor: root._batteryColor()
                   valueColor: root._connected ? Colors.textPrimary : Colors.textDisabled }
        PanelRow { label: qsTr("Pre-arm");      value: _tel ? (_tel.preArmOk ? "OK" : (_tel.preArmMessage.length > 0 ? "FAIL" : "WARN")) : "—"
                   dotColor: root._connected ? (_tel.preArmOk ? Colors.success : (_tel.preArmMessage.length > 0 ? Colors.error : Colors.warning))
                                             : Colors.textDisabled
                   valueColor: root._connected ? (root._tel.preArmOk ? Colors.success : Colors.error) : Colors.textDisabled }
        PanelRow { label: qsTr("Link");         value: root._connected ? _tel.connectionQuality + " %" : "—"
                   dotColor: root._linkColor()
                   valueColor: root._connected ? Colors.textPrimary : Colors.textDisabled }
        PanelRow { label: qsTr("GPS");          value: root._connected ? _tel.gpsFixTypeString + " \u00B7 " + _tel.gpsSatellites + " sats" : "—"
                   dotColor: root._connected ? root._qualityColor(_tel.gpsDataQuality) : Colors.textDisabled
                   valueColor: root._connected ? Colors.textPrimary : Colors.textDisabled }
        PanelRow { label: qsTr("IMU");          value: root._connected ? (_tel.imuDataQuality < 2 ? "WARN" : "OK") : "—"
                   dotColor: root._connected ? root._qualityColor(_tel.imuDataQuality) : Colors.textDisabled
                   valueColor: root._connected ? (root._tel.imuDataQuality < 2 ? Colors.warning : Colors.success) : Colors.textDisabled }
        PanelRow { label: qsTr("Compass");      value: root._connected ? (_tel.compassDataQuality < 2 ? "WARN" : "OK") : "—"
                   dotColor: root._connected ? root._qualityColor(_tel.compassDataQuality) : Colors.textDisabled
                   valueColor: root._connected ? (root._tel.compassDataQuality < 2 ? Colors.warning : Colors.success) : Colors.textDisabled }
        PanelRow { label: qsTr("RC");           value: root._connected ? (_tel.rcConnected ? (_tel.rcDataQuality < 2 ? "WARN" : "OK") : "N/A") : "—"
                   dotColor: root._connected ? (_tel.rcConnected ? root._qualityColor(_tel.rcDataQuality) : Colors.textSecondary) : Colors.textDisabled
                   valueColor: root._connected ? (root._tel.rcDataQuality < 2 ? Colors.warning : Colors.success) : Colors.textDisabled }
        PanelRow { label: qsTr("EKF");          value: root._ekfLabel()
                   dotColor: root._ekfColor()
                   valueColor: root._ekfColor() }
    }
}