// Component: FlyViewTelemetryStrip
// Purpose: Compact horizontal telemetry bar for the Fly View landing layout.
//   Displays key flight data in a slim, always-visible dark strip at the top.
//   Sunlight-readable, high-contrast design. Uses QGC's native Vehicle object.
// Properties:
//   activeVehicle (var) — QGC Vehicle object reference
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors

import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.Palette

Rectangle {
    id: root

    property var activeVehicle: null

    // ── Theme ────────────────────────────────────────────────────────
    readonly property color _bg:      Qt.rgba(0.05, 0.05, 0.12, 0.95)
    readonly property color _border:  Qt.rgba(0.25, 0.30, 0.50, 0.40)
    readonly property color _text:    "#E8ECF4"
    readonly property color _label:   "#8B93A7"
    readonly property color _accent:  "#00D4FF"
    readonly property color _good:    "#4ADE80"
    readonly property color _warn:    "#FBBF24"
    readonly property color _crit:    "#F87171"

    readonly property real _fs:   ScreenTools.defaultFontPointSize * 0.88
    readonly property real _fsL:  ScreenTools.defaultFontPointSize * 0.74

    height: ScreenTools.defaultFontPixelHeight * 2.4
    color:  _bg
    border.color: _border
    border.width: 1

    // ── Vehicle data (null-safe) ─────────────────────────────────────
    readonly property real   _altRel:  activeVehicle ? activeVehicle.altitudeRelative.rawValue : 0
    readonly property real   _gndSpd:  activeVehicle ? activeVehicle.groundSpeed.rawValue      : 0
    readonly property real   _hdg:     activeVehicle ? activeVehicle.heading.rawValue          : 0
    readonly property string _mode:    activeVehicle ? activeVehicle.flightMode                : "--"
    readonly property bool   _armed:   activeVehicle ? activeVehicle.armed                    : false
    readonly property int    _rssi:    activeVehicle ? activeVehicle.rcRSSI                    : 0
    readonly property int    _sats:    activeVehicle && activeVehicle.gps ? activeVehicle.gps.count.rawValue : 0
    readonly property int    _lock:    activeVehicle && activeVehicle.gps ? activeVehicle.gps.lock.rawValue  : 0
    readonly property real   _batPct:  activeVehicle && activeVehicle.batteries && activeVehicle.batteries.count > 0
                                           ? activeVehicle.batteries.get(0).percentRemaining.rawValue : -1
    readonly property real   _batV:    activeVehicle && activeVehicle.batteries && activeVehicle.batteries.count > 0
                                           ? activeVehicle.batteries.get(0).voltage.rawValue : 0

    // ── Color helpers ────────────────────────────────────────────────
    function _battColor(p) { return p < 0 ? _label : p >= 50 ? _good : p >= 20 ? _warn : _crit }
    function _rssiColor(r) { return (r <= 0 || r > 100) ? _label : r >= 70 ? _good : r >= 40 ? _warn : _crit }
    function _gpsColor(l)  { return l >= 3 ? _good : l >= 2 ? _warn : _crit }
    function _fixStr(l) {
        var m = { 0:"No GPS", 1:"No Fix", 2:"2D", 3:"3D", 4:"DGPS", 5:"RTK F", 6:"RTK" }
        return m[l] || "?"
    }

    // ── Layout ───────────────────────────────────────────────────────
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin:  ScreenTools.defaultFontPixelWidth
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth
        spacing: ScreenTools.defaultFontPixelWidth * 0.3

        // ── Connection dot + mode ────────────────────────────────────
        Rectangle {
            width: 8; height: 8; radius: 4
            Layout.alignment: Qt.AlignVCenter
            color: activeVehicle ? (_armed ? _good : _accent) : _crit
            SequentialAnimation on opacity {
                loops: Animation.Infinite; running: !!activeVehicle
                NumberAnimation { to: 0.4; duration: 1000 }
                NumberAnimation { to: 1.0; duration: 1000 }
            }
        }

        TelCell {
            label: "MODE"
            value: _armed ? _mode + " ✦" : _mode
            valueColor: _armed ? _good : _text
            Layout.preferredWidth: implicitWidth + ScreenTools.defaultFontPixelWidth
        }

        _separator {}

        // ── Altitude ─────────────────────────────────────────────────
        TelCell {
            label: "ALT"
            value: _altRel.toFixed(1) + " m"
            valueColor: _text
        }

        _separator {}

        // ── Speed ────────────────────────────────────────────────────
        TelCell {
            label: "GND"
            value: _gndSpd.toFixed(1) + " m/s"
            valueColor: _text
        }

        _separator {}

        // ── Heading ──────────────────────────────────────────────────
        TelCell {
            label: "HDG"
            value: _hdg.toFixed(0) + "°"
            valueColor: _accent
        }

        _separator {}

        // ── Battery ──────────────────────────────────────────────────
        TelCell {
            label: "BAT"
            value: _batPct >= 0 ? _batPct.toFixed(0) + "% · " + _batV.toFixed(1) + "V" : "N/A"
            valueColor: _battColor(_batPct)
        }

        _separator {}

        // ── GPS ──────────────────────────────────────────────────────
        TelCell {
            label: "GPS"
            value: _fixStr(_lock) + " · " + _sats + " sat"
            valueColor: _gpsColor(_lock)
        }

        _separator {}

        // ── Link ─────────────────────────────────────────────────────
        TelCell {
            label: "LINK"
            value: (_rssi > 0 && _rssi <= 100) ? _rssi + "%" : "N/A"
            valueColor: _rssiColor(_rssi)
        }

        Item { Layout.fillWidth: true }
    }

    // ── Inline sub-components ────────────────────────────────────────
    component TelCell: Item {
        property string label
        property string value
        property color  valueColor: _text

        implicitWidth: _valText.implicitWidth + ScreenTools.defaultFontPixelWidth * 0.5
        implicitHeight: parent ? parent.height : 30
        Layout.alignment: Qt.AlignVCenter

        Column {
            anchors.centerIn: parent
            spacing: 1

            Text {
                text: label
                font.pointSize: _fsL
                font.weight: Font.Medium
                font.letterSpacing: 0.8
                color: _label
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                id: _valText
                text: value
                font.pointSize: _fs
                font.family: "monospace"
                font.weight: Font.DemiBold
                color: valueColor
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    component _separator: Rectangle {
        Layout.alignment: Qt.AlignVCenter
        width: 1
        height: root.height * 0.5
        color: _border
    }
}
