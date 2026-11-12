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
    // Pack waived for this flight: the stored S/P/mAh may be another pack's,
    // so no flight-time estimate is shown, only the charge.
    readonly property bool _packWaived: _profile ? _profile.batteryPackWaived : false
    // SOC + estimated time come from VehicleProfileManager (the shared live
    // estimate refreshed every 2 s) so the chip always agrees with the panel.
    readonly property int _soc: {
        if (_profile && _profile.liveSOC >= 0) return _profile.liveSOC
        if (_tel && _tel.batterySocPct >= 0) return _tel.batterySocPct
        return -1
    }
    readonly property double _timeMin:  _profile ? _profile.liveTimeMins : -1
    // Power-type estimate (copter linked to a type with a learned model):
    // replaces the generic estimate. Before the arm it shows the cruise
    // endurance with the payload; in flight the time left; "RETURN NOW" when
    // the rest of the trip (target, time there, home, land) no longer fits.
    readonly property var  _pl: _profile ? _profile.powerLive : ({})
    // The mission estimate in flight (empty on the ground or with no mission).
    readonly property var  _ml: _profile ? _profile.missionLive : ({})
    readonly property bool _plActive: !!_pl && _pl.active === true && !_packWaived
    readonly property bool _returnNow: _plActive && _pl.returnNow === true
    readonly property bool  _armed:     _tel ? _tel.armed : false
    readonly property double _iSmooth:  _tel ? _tel.batteryCurrentSmoothAmps : 0
    readonly property double _voltage:  _tel ? _tel.batteryVoltage : 0
    readonly property int    _series:       _profile ? _profile.batterySeriesCells : 0
    readonly property int    _parallel:     _profile ? _profile.batteryParallelCells : 0
    readonly property int    _cellMah:      _profile ? _profile.batteryCellMah : 0
    readonly property double _capacityAh:   (_profile && _profile.capacityAh > 0)
        ? _profile.capacityAh
        : (_parallel > 0 && _cellMah > 0 ? (_parallel * _cellMah) / 1000.0 : 0)

    // remaining_Ah = capacity × (SOC/100);
    // usable_Ah = capacity × (SOC/100 − reserve) — the reserve is a share of the
    // whole pack, so usable reaches 0 at the reserve (same rule as the C++ estimate).
    readonly property double _remainingAh:  (_soc >= 0 && _capacityAh > 0)
        ? _capacityAh * (_soc / 100.0)
        : -1
    readonly property double _usableAh: (_profile && _capacityAh > 0)
        ? _profile.usableAhAboveReserveFor(_soc) + 0 * _profile.batteryReserve   // re-evaluate on reserve change
        : -1
    // At or below the reserve while flying: land now.
    readonly property bool _atReserve: _timeShown && _timeMin <= 0

    // Before arming there is no current draw to time against, so the chip
    // shows the pre-flight estimate instead: the selected pack's usable
    // energy (above the reserve) divided by this vehicle's energy use per
    // minute, learned from its past flights or a per-airframe default.
    // It is refreshed every few seconds while connected and disarmed.
    property var _preEst: null
    readonly property bool _preEstShown: _connected && !_armed && !_packWaived && !!_preEst && _preEst.configured
                                         && !_preEst.atReserve
    function _refreshPreEst() {
        if (typeof PowerModel === "undefined" || !PowerModel || !_profile || !_connected || _armed) {
            _preEst = null
            return
        }
        _preEst = PowerModel.preflightEstimate(_profile.currentDeviceUid, _profile.currentPayloadWeightKg)
    }
    Timer {
        id: preEstTimer
        interval: 3000
        repeat: true
        running: root._connected && !root._armed
        triggeredOnStart: true
        onTriggered: root._refreshPreEst()
    }

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

    // Freshness. Primary source is the bridge's BATTERY_STATUS age
    // (VehicleTelemetry.batteryDataValid / batteryStale). When the FC never
    // reports SOC the bridge cannot say, so fall back to "a battery value
    // changed in the last 5 s" (_changeFresh).
    readonly property bool _fresh: _connected && !VehicleTelemetry.batteryStale
                                   && (VehicleTelemetry.batteryDataValid || _changeFresh)
    property bool _changeFresh: _connected && (_voltage > 0.1 || _soc >= 0 || (_tel && _tel.batteryCurrentAmps > 0))
    Timer {
        id: freshnessTimer
        interval: 5000
        repeat: false
        onTriggered: root._changeFresh = false
    }
    Connections {
        target: root._tel
        function onBatterySocPctChanged()               { root._changeFresh = true; freshnessTimer.restart() }
        function onBatteryCurrentSmoothAmpsChanged()    { root._changeFresh = true; freshnessTimer.restart() }
        function onBatteryCurrentAmpsChanged()          { root._changeFresh = true; freshnessTimer.restart() }
        function onBatteryVoltageChanged()              { root._changeFresh = true; freshnessTimer.restart() }
    }

    // ── Label + color per display-state table ──
    readonly property string _label: {
        if (!_connected)            return "— · —"   // no vehicle
        if (!_fresh)                return "STALE"   // stale / no BATTERY_STATUS
        if (_soc < 0)               return "? · —"   // FC not configured
        if (_packWaived)            return _soc + "% · no est."
        if (_returnNow)             return "RETURN NOW"
        if (_plActive && !_armed && _pl.cruiseMin >= 0)  return _soc + "% · ~" + Math.round(_pl.cruiseMin) + " min"
        if (_plActive && _armed && _pl.timeLeftMin >= 0) return _soc + "% · ~" + Math.round(_pl.timeLeftMin) + " min"
        if (_preEstShown)           return _soc + "% · est ~" + Math.round(_preEst.flightTimeMin) + " min"
        if (!_gateOk || _timeMin < 0) return _soc + "% · —"
        if (_timeMin <= 0)          return _soc + "% · RESERVE \u2014 LAND"
        return _soc + "% · ~" + Math.round(_timeMin) + " min"
    }
    readonly property bool _timeShown: _socKnown && _timeMin >= 0 && !_packWaived

    // A distance from the live estimate; "-- km" while it is not known (no
    // mission speed read from the copter yet).
    function _km(km) { return km >= 0 ? Number(km).toFixed(1) + " km" : "-- km" }

    readonly property string _tooltip: {
        if (!_connected)            return "No vehicle connected"
        if (!_fresh)                return "No battery telemetry (stale)"
        if (_soc < 0)               return "Battery SOC not reported by flight controller"
        if (_packWaived)            return "Battery pack waived by " + _profile.batteryPackWaivedBy + " \u2014 no flight-time estimate for this flight"
        if (_returnNow)             return _pl.returnNowFrom === "mission" ? _pl.returnNowWhy
                                         : "The rest of the trip needs " + Number(_pl.tripNeedAh).toFixed(1) + " Ah, "
                                           + Number(_pl.usableAh).toFixed(1) + " Ah left above the reserve: return now"
        if (_plActive && !_armed)   return _pl.typeName + ": cruise ~" + Math.round(_pl.cruiseMin) + " min / "
                                           + root._km(_pl.cruiseKm) + ", hover ~" + Math.round(_pl.hoverMin)
                                           + " min with the payload entered"
                                           + (_pl.startSocPct < 100 ? " (pack not full: about " + Math.round(_pl.startSocPct)
                                                                       + " % from its voltage)" : "")
        if (_plActive)              return _pl.typeName + ": ~" + Math.round(_pl.timeLeftMin) + " min / "
                                           + root._km(_pl.distLeftKm) + " left at the current draw \u2014 tap for detail"
        if (_preEstShown)           return "Pre-flight estimate for the selected pack (" + _preEst.sourceLabel + ") \u2014 the live estimate starts once armed"
        if (!_gateOk)               return "Arm and draw ≥ " + kMinGateA.toFixed(1) + " A to estimate flight time"
        if (!_canComputeTime)       return "Configure battery pack (N_S · N_P · mAh) to estimate flight time"
        if (_atReserve)             return "Battery is at the reserve (" + Math.round((_profile ? _profile.batteryReserve : 0.2) * 100) + " % of the pack): land now"
        return "Estimated flight time at current draw (" + _iSmooth.toFixed(1) + " A smoothed) \u2014 tap for detail"
    }

    readonly property color _textColor: {
        if (_connected && !_fresh) return Colors.statusStale
        if (!_socKnown) return Colors.textDisabled
        if (_atReserve || _returnNow) return Colors.error
        if (_soc > 30)  return Colors.success
        if (_soc >= 15) return Colors.warning
        return Colors.error
    }

    // Flash when SOC < 10 %.
    property bool _flashOn: false
    Timer {
        id: lowSocFlashTimer
        interval: 500
        repeat: true
        running: root._socKnown && root._soc >= 0 && root._soc < 10
        onTriggered: root._flashOn = !root._flashOn
    }

    // Stop repeating timers on teardown.
    Component.onDestruction: {
        freshnessTimer.stop()
        lowSocFlashTimer.stop()
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
        color: Colors.surface
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
            DetailRow { label: qsTr("Pre-flight est.");
                        value: root._preEstShown
                               ? "~" + Math.round(root._preEst.flightTimeMin) + " min / " + root._preEst.rangeKm.toFixed(1) + " km"
                               : "—"
                        valueColor: root._preEstShown && root._preEst.isCalibrated ? Colors.success : Colors.textSecondary }
            DetailRow { label: root._pl.source === "motor table" ? qsTr("Motor table") : qsTr("Power type");
                        objectName: "chipEstimateSource"
                        visible: root._plActive
                        value: !root._plActive ? ""
                               : root._pl.source === "motor table"
                                 ? root._pl.typeName
                                   + (root._pl.motorsBeyond > 0 ? qsTr(" (beyond table)") : "")
                                   + (root._pl.throttleSource === "overall throttle" ? qsTr(" (from the overall throttle)") : "")
                                 : root._pl.typeName + (root._pl.source === "synthetic" ? " (not yet learned)" : "")
                        valueColor: Colors.textPrimary }
            DetailRow { label: qsTr("Left");
                        visible: root._plActive
                        value: root._plActive && root._pl.timeLeftMin >= 0
                               ? "~" + Math.round(root._pl.timeLeftMin) + " min / " + root._km(root._pl.distLeftKm)
                               : "—"
                        valueColor: Colors.textPrimary }
            // The rest of the flight: from the mission estimate while it runs
            // (the same source as RETURN NOW), else the trip to a target.
            DetailRow { label: root._ml.ready === true ? qsTr("Mission") : qsTr("Rest of the trip");
                        objectName: "chipRestOfTrip"
                        visible: root._plActive
                        value: root._ml.ready === true
                               ? (root._ml.returnNow ? qsTr("return now")
                                  : qsTr("lands %1 % (pess. %2 %)").arg(Math.round(root._ml.landLikelyPct))
                                                                  .arg(Math.round(root._ml.landPessimisticPct)))
                               : !root._plActive || !root._pl.tripKnown ? "—"
                               : root._returnNow ? "short by " + Number(root._pl.tripShortAh).toFixed(1) + " Ah"
                               : Number(root._pl.tripNeedAh).toFixed(1) + " of " + Number(root._pl.usableAh).toFixed(1) + " Ah"
                        valueColor: root._returnNow ? Colors.error : Colors.success }
            // The payload: what is still aboard, and how many were released
            // (seen on the release servo output, or the button below).
            DetailRow { label: qsTr("Payload");
                        objectName: "chipPayload"
                        visible: root._plActive && root._armed
                        value: !root._plActive ? ""
                               : (root._pl.payloadAboardKg > 0 ? Number(root._pl.payloadAboardKg).toFixed(1) + " kg aboard"
                                                                : qsTr("none aboard"))
                                 + (root._pl.payloadsReleased > 0
                                    ? qsTr(", %1 of %2 released").arg(root._pl.payloadsReleased).arg(root._pl.payloadCount) : "")
                        valueColor: Colors.textPrimary }
            Text {
                Layout.fillWidth: true
                visible: root._plActive && root._armed && !root._pl.dropOutputSet
                text: qsTr("No release servo output set (Preflight Settings): tap below when a payload is dropped")
                wrapMode: Text.Wrap
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
            }
            Button {
                objectName: "chipPayloadDropped"
                Layout.fillWidth: true
                visible: root._plActive && root._armed && root._pl.payloadAboardKg > 0
                text: qsTr("Payload dropped")
                onClicked: if (root._profile) root._profile.payloadDroppedTap()
            }
            DetailRow { label: qsTr("Based on");
                        visible: root._preEstShown
                        value: root._preEstShown
                               ? root._preEst.sourceLabel + (root._preEst.assumedFull ? ", assuming a full pack" : "")
                               : ""
                        valueColor: Colors.textSecondary }
        }
    }
}