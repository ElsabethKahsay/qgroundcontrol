// Component: FlyViewTelemetryStrip
// Purpose: Floating 3-column telemetry info box for the Fly View.
//   Replaces the narrow bottom bar with a transparent floating rectangle
//   containing FLIGHT / NAVIGATION / SYSTEM columns.
//   Supports drag-to-reposition, QSettings persistence, right-click menu.
// Properties:
//   activeVehicle (var) — QGC Vehicle object reference
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.settings 1.0

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.Palette

Item {
    id: root

    property var activeVehicle: null

    // Geometry — callers anchor/position this Item; the visual box
    // lives inside and reports its real height via boxHeight.
    implicitWidth:  _box.implicitWidth
    implicitHeight: _box.implicitHeight
    property real boxHeight: _box.implicitHeight

    // ── Persist position (offset from bottom-left of parent) ──────────
    Settings {
        id:       _settings
        category: "TelemetryFloatingBox"
        property real offsetX: 16
        property real offsetY: 16
        property bool locked:  false
        property bool threeColumns: true
    }

    // Draggable position, clamped inside parent
    property real _boxX: _settings.offsetX
    property real _boxY: _settings.offsetY

    function _clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
    function _resetPosition() {
        _settings.offsetX = 16
        _settings.offsetY = 16
        _boxX = 16
        _boxY = 16
    }

    // ── Theme tokens (Larger Size & Layout) ───────────────────────────
    readonly property color _bg:      Qt.rgba(0.078, 0.078, 0.118, 0.78)
    readonly property color _border:  Qt.rgba(1, 1, 1, 0.13)
    readonly property color _divider: Qt.rgba(1, 1, 1, 0.09)
    readonly property color _text:    "#F0F4FF"
    readonly property color _label:   Qt.rgba(1, 1, 1, 0.55)
    readonly property color _unit:    Qt.rgba(1, 1, 1, 0.45)
    readonly property color _accent:  "#00D4FF"
    readonly property color _good:    "#4ADE80"
    readonly property color _warn:    "#FBBF24"
    readonly property color _crit:    "#F87171"

    readonly property real _pad:       18 // Increased padding (15% bigger)
    readonly property real _colGap:    18 // Increased gap between columns
    readonly property real _labelSize: ScreenTools.defaultFontPointSize * 0.94
    readonly property real _valSize:   ScreenTools.defaultFontPointSize * 1.32
    readonly property real _unitSize:  ScreenTools.defaultFontPointSize * 0.98

    // ── Vehicle data (bind directly to trigger QML notifications) ──────
    readonly property real   _altRel:   activeVehicle ? activeVehicle.altitudeRelative.rawValue : 0
    readonly property real   _gndSpd:   activeVehicle ? activeVehicle.groundSpeed.rawValue      : 0
    readonly property real   _climbRate: activeVehicle ? activeVehicle.climbRate.rawValue        : 0
    readonly property real   _hdg:      activeVehicle ? activeVehicle.heading.rawValue          : 0
    readonly property string _mode:     activeVehicle ? activeVehicle.flightMode                : "--"
    readonly property bool   _armed:    activeVehicle ? activeVehicle.armed                    : false
    readonly property int    _rssi:     activeVehicle ? activeVehicle.rcRSSI                    : 0
    readonly property int    _sats:     activeVehicle && activeVehicle.gps ? activeVehicle.gps.count.rawValue : 0
    readonly property int    _lock:     activeVehicle && activeVehicle.gps ? activeVehicle.gps.lock.rawValue  : 0
    readonly property real   _batPct:   activeVehicle && activeVehicle.batteries && activeVehicle.batteries.count > 0
                                            ? activeVehicle.batteries.get(0).percentRemaining.rawValue : -1
    readonly property real   _batV:     activeVehicle && activeVehicle.batteries && activeVehicle.batteries.count > 0
                                            ? activeVehicle.batteries.get(0).voltage.rawValue : 0
    readonly property real   _batCur:   activeVehicle && activeVehicle.batteries && activeVehicle.batteries.count > 0
                                            ? activeVehicle.batteries.get(0).current.rawValue : -1
    readonly property real   _distHome:   activeVehicle && activeVehicle.distanceToHome ? activeVehicle.distanceToHome.rawValue : 0
    readonly property real   _flightDist: activeVehicle && activeVehicle.flightDistance ? activeVehicle.flightDistance.rawValue : 0
    readonly property real   _currLat:    activeVehicle && activeVehicle.gps ? activeVehicle.gps.latitude.rawValue : 0
    readonly property real   _currLon:    activeVehicle && activeVehicle.gps ? activeVehicle.gps.longitude.rawValue : 0
    readonly property real   _planDist:   _haversine(
        _currLat, _currLon,
        VehicleProfileManager.planLatitude,
        VehicleProfileManager.planLongitude)

    // ── Haversine distance (meters) between two lat/lon points ─────
    function _haversine(lat1, lon1, lat2, lon2) {
        if (!activeVehicle || lat2 === 0 && lon2 === 0) return 0
        var R = 6371000
        var dLat = (lat2 - lat1) * Math.PI / 180
        var dLon = (lon2 - lon1) * Math.PI / 180
        var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
                Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
                Math.sin(dLon / 2) * Math.sin(dLon / 2)
        return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a))
    }

    // ── Safe formatting helper to prevent NaN or undefined display ───
    function _formatVal(val, decimals) {
        if (!activeVehicle || val === undefined || val === null || isNaN(val)) return "--"
        return val.toFixed(decimals)
    }

    // ── Color helpers ─────────────────────────────────────────────────
    function _battColor(p) { return (!activeVehicle || p === undefined || p === null || isNaN(p) || p < 0) ? _label : p >= 40 ? _good : p >= 20 ? _warn : _crit }
    function _rssiColor(r) { return (!activeVehicle || r === undefined || r === null || isNaN(r) || r <= 0 || r > 100) ? _label : r >= 70 ? _good : r >= 40 ? _warn : _crit }
    function _gpsColor(l)  { return (!activeVehicle || l === undefined || l === null || isNaN(l) || l < 2) ? _crit : l >= 3 ? _good : _warn }
    
    function _fixStr(l) {
        if (!activeVehicle || l === undefined || l === null || isNaN(l)) return "No GPS"
        var m = { 0:"No GPS", 1:"No Fix", 2:"2D Fix", 3:"3D Fix", 4:"DGPS", 5:"RTK Float", 6:"RTK Fixed" }
        return m[l] !== undefined ? m[l] : "No GPS"
    }

    // Compass arrow symbol for heading direction
    function _hdgArrow(deg) {
        if (!activeVehicle || deg === undefined || deg === null || isNaN(deg)) return "↑"
        var arrows = ["↑","↗","→","↘","↓","↙","←","↖"]
        return arrows[Math.round(deg / 45) % 8]
    }

    // ═══════════════════════════════════════════════════════════════════
    //  FLOATING BOX
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: _box

        // Clamp using root width/height directly to prevent initial layout loops
        x: _clamp(_boxX, 0, root.width - width)
        y: _clamp(root.height - height - _boxY, 0, root.height - height)

        implicitWidth:  _contentRow.implicitWidth  + root._pad * 2
        implicitHeight: _contentRow.implicitHeight + root._pad * 2

        radius:      8
        color:       root._bg
        border.color: root._border
        border.width: 1
        z:            QGroundControl.zOrderWidgets + 1

        // Frosted-glass shimmer rectangle
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Qt.rgba(1, 1, 1, 0.03)
            z: -1
        }

        // ── Hover brightness ─────────────────────────────────────────
        opacity: _dragMA.containsMouse ? 0.92 : 0.82
        Behavior on opacity { NumberAnimation { duration: 150 } }

        // ── Drag logic ───────────────────────────────────────────────
        MouseArea {
            id:          _dragMA
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape:     _settings.locked ? Qt.ArrowCursor : Qt.SizeAllCursor
            drag.target:     _settings.locked ? null : _box
            drag.axis:       Drag.XAndYAxis
            drag.minimumX:   0
            drag.maximumX:   root.width  - _box.width
            drag.minimumY:   0
            drag.maximumY:   root.height - _box.height

            // Persist position after drag ends
            onReleased: function(mouse) {
                if (!_settings.locked) {
                    _settings.offsetX = _box.x
                    // Store as bottom-offset
                    _settings.offsetY = root.height - _box.y - _box.height
                    _boxX = _box.x
                    _boxY = _settings.offsetY
                }
            }

            onClicked: function(mouse) {
                if (mouse.button === Qt.RightButton) {
                    _contextMenu.popup()
                }
            }

            // Pass through clicks outside text areas
            propagateComposedEvents: true
        }

        // ── Right-click context menu ──────────────────────────────────
        Menu {
            id: _contextMenu
            MenuItem {
                text: _settings.locked ? qsTr("Unlock Position") : qsTr("Lock Position")
                onTriggered: _settings.locked = !_settings.locked
            }
            MenuItem {
                text: _settings.threeColumns ? qsTr("Switch to 2 Columns") : qsTr("Switch to 3 Columns")
                onTriggered: _settings.threeColumns = !_settings.threeColumns
            }
            MenuItem {
                text: qsTr("Reset Position")
                onTriggered: root._resetPosition()
            }
        }

        // ── Content: columns separated by dividers ──────────────────
        RowLayout {
            id: _contentRow
            anchors {
                left:    parent.left
                top:     parent.top
                margins: root._pad
            }
            spacing: 0

            // ── Column 1: FLIGHT ─────────────────────────────────────
            TelColumn {
                header: "FLIGHT"
                rows: _settings.threeColumns ? [
                    { label: "Mode",  value: (_armed ? "✦ " : "") + _mode,
                      vcolor: _armed ? _good : _text },
                    { label: "Alt",   value: _formatVal(_altRel, 1), unit: "m",   vcolor: _text },
                    { label: "Speed", value: _formatVal(_gndSpd, 1), unit: "m/s", vcolor: _text },
                    { label: "Vspd",  value: (_climbRate >= 0 ? "↑" : "↓") + _formatVal(Math.abs(_climbRate), 1), unit: "m/s", vcolor: _climbRate > 0.5 ? _accent : _climbRate < -0.5 ? _warn : _text }
                ] : [
                    { label: "Mode",  value: (_armed ? "✦ " : "") + _mode,
                      vcolor: _armed ? _good : _text },
                    { label: "Alt",   value: _formatVal(_altRel, 1), unit: "m",   vcolor: _text },
                    { label: "Gnd",   value: _formatVal(_gndSpd, 1), unit: "m/s", vcolor: _text },
                    { label: "Hdg",   value: _formatVal(_hdg, 0),    unit: "°",   vcolor: _accent }
                ]
            }

            // Vertical divider 1
            Rectangle {
                width: 1
                Layout.fillHeight: true
                Layout.topMargin:    4
                Layout.bottomMargin: 4
                Layout.leftMargin:   root._colGap
                Layout.rightMargin:  root._colGap
                color: root._divider
            }

            // ── Column 2: NAVIGATION (only shown in 3-column mode) ──
            TelColumn {
                visible: _settings.threeColumns
                header: "NAVIGATION"
                rows: [
                    { label: "GPS",
                      value: activeVehicle && _sats > 0 ? (_fixStr(_lock)) : "No Fix",
                      vcolor: _gpsColor(_lock) },
                    { label: "Sats",  value: activeVehicle ? _sats.toString() : "--", unit: "sat", vcolor: _gpsColor(_lock) },
                    { label: "Hdg",
                      value: _hdgArrow(_hdg) + " " + _formatVal(_hdg, 0),
                      unit: "°",    vcolor: _accent },
                    { label: "Dist",  value: _formatVal(_distHome, 0),
                      unit: "m",    vcolor: _text },
                    { label: "Flight", value: _formatVal(_flightDist, 0),
                      unit: "m",    vcolor: _accent },
                    { label: "Plan",  value: _planDist > 0 ? _formatVal(_planDist, 0) : "--",
                      unit: _planDist > 0 ? "m" : "", vcolor: _accent },
                    { label: "Tgt",  value: "--",
                      vcolor: _label }
                ]
            }

            // Vertical divider 2 (only shown in 3-column mode)
            Rectangle {
                visible: _settings.threeColumns
                width: 1
                Layout.fillHeight: true
                Layout.topMargin:    4
                Layout.bottomMargin: 4
                Layout.leftMargin:   root._colGap
                Layout.rightMargin:  root._colGap
                color: root._divider
            }

            // ── Column 3: SYSTEM ─────────────────────────────────────
            TelColumn {
                header: "SYSTEM"
                rows: _settings.threeColumns ? [
                    { label: "Bat",
                      value: activeVehicle && _batPct >= 0 ? _batPct.toFixed(0) : "--",
                      unit: "%",    vcolor: _battColor(_batPct) },
                    { label: "Volt",
                      value: _formatVal(_batV, 1),
                      unit: "V",    vcolor: _battColor(_batPct) },
                    { label: "Link",
                      value: activeVehicle && _rssi > 0 && _rssi <= 100 ? _rssi.toString() : "--",
                      unit: activeVehicle && _rssi > 0 && _rssi <= 100 ? "%" : "",
                      vcolor: _rssiColor(_rssi) },
                    { label: "Curr",
                      value: _formatVal(_batCur, 1),
                      unit: "A",    vcolor: _text }
                ] : [
                    { label: "Bat",
                      value: activeVehicle && _batPct >= 0 ? _batPct.toFixed(0) + "% (" + _formatVal(_batV, 1) + "V)" : "--",
                      vcolor: _battColor(_batPct) },
                    { label: "GPS",
                      value: activeVehicle ? _sats + " sat (" + _fixStr(_lock) + ")" : "--",
                      vcolor: _gpsColor(_lock) },
                    { label: "Link",
                      value: activeVehicle && _rssi > 0 && _rssi <= 100 ? _rssi + "%" : "N/A",
                      vcolor: _rssiColor(_rssi) },
                    { label: "Home",
                      value: _formatVal(_distHome, 0),
                      unit: "m",    vcolor: _accent },
                    { label: "Tgt",
                      value: "--",
                      vcolor: _label }
                ]
            }
        }

        // ── Locked indicator (small lock icon overlay) ────────────────
        Text {
            anchors.top:   parent.top
            anchors.right: parent.right
            anchors.margins: 4
            text:          _settings.locked ? "🔒" : ""
            font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.85
            opacity:       0.5
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  INLINE COMPONENT: TelColumn
    //  Displays a header + list of { label, value, unit, vcolor } rows.
    // ═══════════════════════════════════════════════════════════════════
    component TelColumn: ColumnLayout {
        id: _col
        property string header: ""
        property var    rows:   []

        spacing: 0

        // Header label
        Text {
            Layout.bottomMargin: 4
            text:            _col.header
            font.pointSize:  root._labelSize * 0.95
            font.weight:     Font.DemiBold
            font.letterSpacing: 1.2
            color:           root._label
        }

        // Underline beneath header
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color:  root._divider
            Layout.bottomMargin: 5
        }

        // Data rows
        Repeater {
            model: _col.rows

            RowLayout {
                spacing: 4
                Layout.topMargin: 3

                Text {
                    text:           modelData.label + ":"
                    font.pointSize: root._labelSize
                    color:          root._label
                    font.weight:    Font.Normal
                }
                Text {
                    text:           modelData.value !== undefined ? modelData.value : "--"
                    font.pointSize: root._valSize
                    font.family:    "monospace"
                    font.weight:    Font.DemiBold
                    color:          modelData.vcolor !== undefined ? modelData.vcolor : root._text
                }
                Text {
                    visible:        (modelData.unit !== undefined && modelData.unit !== "")
                    text:           modelData.unit !== undefined ? modelData.unit : ""
                    font.pointSize: root._unitSize
                    color:          root._unit
                }
            }
        }
    }
}
