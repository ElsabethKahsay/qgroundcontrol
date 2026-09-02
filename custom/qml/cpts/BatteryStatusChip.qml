// Component: BatteryStatusChip
// Purpose: Live battery SOC + remaining-flight-time chip for the Fly view.
//   Reads SOC/current from TelemetryProvider (BATTERY_STATUS) and the pack
//   capacity from VehicleProfileManager; never recalculates SOC from voltage.
//   Display states:
//     no signal / stale (no BATTERY_STATUS in 5 s)  → "— · —"
//     FC not configured (battery_remaining = -1)    → "? · —"
//     disarmed or current below gate (3 A)          → "87% · —"
//     armed + current streaming + capacity known    → "87% · ~29 min"
//   Tapping the chip toggles a lightweight detail popover (SOC, smoothed
//   current, voltage, usable Ah, reserve %, pack source, Live/Stale) that is
//   allowed to coexist with the panel quick actions.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

import QGroundControl
import QGroundControl.ScreenTools

Rectangle {
    id: root

    // Minimum gate current (A) below which flight time is never shown.
    readonly property double kMinGateA: 3.0

    // ── Live data sources ──
    readonly property var _tel: typeof TelemetryProvider !== "undefined" ? TelemetryProvider : null
    readonly property var _profile: typeof VehicleProfileManager !== "undefined" ? VehicleProfileManager : null

    readonly property bool _connected: _tel ? _tel.isConnected : false
    // SOC + estimated time come from VehicleProfileManager (the shared live
    // estimate refreshed every 2 s) so the chip always agrees with the panel.
    readonly property int _soc: {
        if (_profile && _profile.liveSOC >= 0) return _profile.liveSOC
        if (_tel && _tel.batterySocPct >= 0) return _tel.batterySocPct
        return -1
    }
    readonly property double _timeMin:  _profile ? _profile.liveTimeMins : -1
    readonly property bool  _armed:     _tel ? _tel.armed : false
    readonly property double _iSmooth:  _tel ? _tel.batteryCurrentSmoothAmps : 0
    readonly property double _voltage:  _tel ? _tel.batteryVoltage : 0
    readonly property int    _series:       _profile ? _profile.batterySeriesCells : 0
    readonly property int    _parallel:     _profile ? _profile.batteryParallelCells : 0
    readonly property int    _cellMah:      _profile ? _profile.batteryCellMah : 0
    readonly property double _capacityAh:   (_profile && _profile.capacityAh > 0)
        ? _profile.capacityAh
        : (_parallel > 0 && _cellMah > 0 ? (_parallel * _cellMah) / 1000.0 : 0)

    // remaining_Ah = capacity × (SOC/100); usable_Ah = remaining × (1 − reserve)
    readonly property double _remainingAh:  (_soc >= 0 && _capacityAh > 0)
        ? _capacityAh * (_soc / 100.0)
        : -1
    readonly property double _usableAh: (_remainingAh >= 0 && _profile)
        ? _remainingAh * (1.0 - _profile.batteryReserve)
        : (_remainingAh >= 0 ? _remainingAh * 0.8 : -1)

    readonly property bool _gateOk: _connected && _armed && _iSmooth >= kMinGateA
    readonly property bool _socKnown: _connected && _fresh && _soc >= 0
    readonly property bool _canComputeTime:
        _socKnown && _gateOk && _capacityAh > 0 && _iSmooth > 0

    readonly property string _packLabel: {
        if (_series > 0 && _parallel > 0 && _cellMah > 0) {
            return _series + "S \u00D7 " + _parallel + "P \u00D7 " + _cellMah + " mAh"
        }
        return "Not configured"
    }

    Connections {
        target: _profile
        ignoreUnknownSignals: true
        function onBatteryConfigChanged() {}
        function onLiveDataChanged() {}
    }

    readonly property string _stateLabel: !_connected ? "Offline" : (!_fresh ? "Stale" : "Live")

    // Detail popover toggling (opened by chip tap or the Flight-time quick action).
    property bool detailOpen: false
    function toggleDetail() { detailOpen = !detailOpen }

    // Freshness: dynamic property connected to telemetry & live signals.
    property bool _fresh: _connected && (_voltage > 0.1 || _soc >= 0 || (_tel && _tel.batteryCurrentAmps > 0))
    Timer {
        id: freshnessTimer
        interval: 5000
        repeat: false
        onTriggered: root._fresh = false
    }
    Connections {
        target: root._tel
        function onBatterySocPctChanged()               { root._fresh = true; freshnessTimer.restart() }
        function onBatteryCurrentSmoothAmpsChanged()    { root._fresh = true; freshnessTimer.restart() }
        function onBatteryCurrentAmpsChanged()          { root._fresh = true; freshnessTimer.restart() }
        function onBatteryDataValidChanged()            { root._fresh = true; freshnessTimer.restart() }
        function onBatteryVoltageChanged()              { root._fresh = true; freshnessTimer.restart() }
    }

    // ── Label + color per display-state table ──
    readonly property string _label: {
        if (!_connected)            return "— · —"   // no vehicle
        if (!_fresh)                return "— · —"   // stale / no BATTERY_STATUS
        if (_soc < 0)               return "? · —"   // FC not configured
        if (!_gateOk || _timeMin < 0) return _soc + "% · —"
        return _soc + "% · ~" + Math.round(_timeMin) + " min"
    }
    readonly property bool _timeShown: _socKnown && _timeMin >= 0

    readonly property string _tooltip: {
        if (!_connected)            return "No vehicle connected"
        if (!_fresh)                return "No battery telemetry (stale)"
        if (_soc < 0)               return "Battery SOC not reported by flight controller"
        if (!_gateOk)               return "Arm and draw ≥ " + kMinGateA.toFixed(1) + " A to estimate flight time"
        if (!_canComputeTime)       return "Configure battery pack (N_S · N_P · mAh) to estimate flight time"
        return "Estimated flight time at current draw (" + _iSmooth.toFixed(1) + " A smoothed) \u2014 tap for detail"
    }

    readonly property color _textColor: {
        if (!_socKnown) return Colors.textDisabled
        if (_soc > 30)  return Colors.success
        if (_soc >= 15) return Colors.warning
        return Colors.error
    }

    // Flash when SOC < 10 %.
    property bool _flashOn: false
    Timer {
        interval: 500
        repeat: true
        running: root._socKnown && root._soc >= 0 && root._soc < 10
        onTriggered: root._flashOn = !root._flashOn
    }

    // Reusable label/value row used by the detail popover.
    component DetailRow: RowLayout {
        property string label: ""
        property string value: ""
        property color valueColor: Colors.textPrimary
        spacing: Config.spacingSmall
        Layout.fillWidth: true

        Text {
            text: label
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            Layout.preferredWidth: 130
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
    width: ScreenTools.defaultFontPixelWidth * 15
    height: ScreenTools.defaultFontPixelHeight * 2.0
    radius: height / 2
    color: Colors.surface
    border.color: Qt.rgba(root._textColor.r, root._textColor.g, root._textColor.b, 0.6)
    border.width: 1.5
    z: root.detailOpen ? 9999 : (QGroundControl.zOrderWidgets + 2)

    // Pulsing opacity while flashing at < 10 % SOC.
    opacity: root._flashOn ? 0.45 : 1.0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    RowLayout {
        id: col
        anchors.centerIn: parent
        spacing: ScreenTools.defaultFontPixelWidth * 0.5

        Rectangle {
            Layout.preferredWidth: 12
            Layout.preferredHeight: 18
            radius: 2
            color: Colors.transparent
            border.color: root._textColor
            border.width: 1.5
            visible: root._connected

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottomMargin: 2
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                height: root._socKnown ? Math.max(2, parent.height * (root._soc / 100.0) - 2) : 3
                radius: 1
                color: root._textColor
            }
        }

        ColumnLayout {
            spacing: 0
            Layout.alignment: Qt.AlignVCenter

            Text {
                text: root._label
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 1.1
                font.bold: true
                color: root._textColor
            }
            Text {
                visible: root._timeShown
                text: qsTr("Live")
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.6
                color: Colors.textSecondary
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor

        ToolTip.visible: containsMouse && !root.detailOpen
        ToolTip.text: root._tooltip
        ToolTip.delay: 400

        onClicked: root.toggleDetail()
    }

    // ── Detail popover (lightweight; may coexist with FC/Events panels) ──
    Rectangle {
        id: detailBox
        visible: root.detailOpen
        x: 0
        y: root.height + ScreenTools.defaultFontPixelHeight * 0.35
        width: 280
        height: detailCol.implicitHeight + Config.spacingMedium * 2
        radius: Config.radiusLarge
        color: "#12151C"
        border.color: Colors.skywinAccent
        border.width: 1
        z: 1000
        clip: false

        ColumnLayout {
            id: detailCol
            anchors { left: parent.left; right: parent.right; top: parent.top }
            anchors.leftMargin: Config.spacingMedium
            anchors.rightMargin: Config.spacingMedium
            anchors.topMargin: Config.spacingMedium
            spacing: Config.spacingSmall

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("BATTERY DETAIL")
                    font.pixelSize: Config.fontSizeSmall
                    font.weight: Font.DemiBold
                    color: Colors.textSecondary
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("\u2715")
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.alignment: Qt.AlignVCenter
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.detailOpen = false
                    }
                }
            }

            DetailRow { label: qsTr("State");        value: root._stateLabel
                        valueColor: root._fresh && root._connected ? Colors.success
                                  : (!root._connected ? Colors.textDisabled : Colors.warning) }
            DetailRow { label: qsTr("SOC");          value: root._soc < 0 ? "—" : root._soc + " %"
                        valueColor: root._textColor }
            DetailRow { label: qsTr("Current (avg)"); value: root._fresh ? root._iSmooth.toFixed(1) + " A" : "—"
                        valueColor: Colors.textPrimary }
            DetailRow { label: qsTr("Voltage");       value: root._voltage > 0.1 ? root._voltage.toFixed(1) + " V" : "—"
                        valueColor: Colors.textPrimary }
            DetailRow { label: qsTr("Full capacity");  value: root._capacityAh > 0 ? root._capacityAh.toFixed(2) + " Ah" : "—"
                        valueColor: Colors.textPrimary }
            DetailRow { label: qsTr("Remaining");      value: root._remainingAh >= 0 ? root._remainingAh.toFixed(2) + " Ah" : "—"
                        valueColor: Colors.textPrimary }
            DetailRow { label: qsTr("Usable (−reserve)"); value: root._usableAh >= 0 ? root._usableAh.toFixed(2) + " Ah" : "—"
                        valueColor: Colors.textPrimary }
            DetailRow { label: qsTr("Reserve");        value: _profile && _profile.batteryReserve > 0
                                                       ? Math.round(_profile.batteryReserve * 100) + " %" : "—"
                        valueColor: Colors.textPrimary }
            DetailRow { label: qsTr("Pack");           value: root._packLabel
                        valueColor: Colors.textPrimary }
            DetailRow { label: qsTr("Est. flight time");
                        value: root._timeMin >= 0 ? "~" + Math.round(root._timeMin) + " min" : "—"
                        valueColor: root._timeShown ? Colors.success : Colors.textSecondary }
        }
    }
}