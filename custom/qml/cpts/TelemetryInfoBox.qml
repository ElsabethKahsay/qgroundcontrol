// Component: TelemetryInfoBox
// Purpose: Compact, always-visible telemetry overlay panel for the Fly View.
//   Displays key flight data in a high-contrast, sunlight-readable dark panel.
//   Draggable, collapsible, resizable, and responsive. Uses QGC's native Vehicle
//   object. Integrates with Config.qml theme where applicable.
// Properties:
//   activeVehicle (var) — QGC Vehicle object reference
//   collapsed (bool) — toggle compact/expanded state
// Signals:
//   closed() — emitted when the user closes the panel via the X button
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.Palette

Rectangle {
    id: root

    property var  activeVehicle: null
    property bool collapsed:     false
    signal closed()

    // ── Panel dark theme (sunlight-readable) ─────────────────────────
    readonly property color _bg:        Qt.rgba(0.06, 0.06, 0.14, 0.93)
    readonly property color _headerBg:  Qt.rgba(0.08, 0.09, 0.20, 0.96)
    readonly property color _border:    Qt.rgba(0.30, 0.35, 0.55, 0.45)
    readonly property color _text:      "#E8ECF4"
    readonly property color _label:     "#8B93A7"
    readonly property color _divider:   Qt.rgba(0.30, 0.35, 0.50, 0.30)
    readonly property color _accent:    "#00D4FF"
    readonly property color _good:      "#4ADE80"
    readonly property color _warn:      "#FBBF24"
    readonly property color _crit:      "#F87171"
    readonly property color _info:      "#60A5FA"

    readonly property real _hdrH: ScreenTools.defaultFontPixelHeight * 2.2
    readonly property real _fs:   ScreenTools.defaultFontPointSize * 0.92
    readonly property real _fsL:  ScreenTools.defaultFontPointSize * 0.78

    // ── Resize constraints ────────────────────────────────────────────
    readonly property real _minW: ScreenTools.defaultFontPixelWidth * 18
    readonly property real _maxW: ScreenTools.defaultFontPixelWidth * 38
    readonly property real _minH: _hdrH
    readonly property real _maxH: ScreenTools.defaultFontPixelHeight * 50

    // ── Size ─────────────────────────────────────────────────────────
    width:  Math.max(_minW, Math.min(_savedWidth, _maxW))
    height: collapsed ? _hdrH : Math.max(_minH, Math.min(contentCol.height + _hdrH + ScreenTools.defaultFontPixelWidth * 2, _maxH))
    Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    property real _savedWidth: ScreenTools.defaultFontPixelWidth * 22

    radius: 8
    color:  _bg
    border.color: _border
    border.width: 1
    clip:   true

    // ── Vehicle data (null-safe) ─────────────────────────────────────
    readonly property real   _altRel:  activeVehicle ? activeVehicle.altitudeRelative.rawValue : 0
    readonly property real   _altMSL:  activeVehicle ? activeVehicle.altitudeAMSL.rawValue     : 0
    readonly property real   _gndSpd:  activeVehicle ? activeVehicle.groundSpeed.rawValue      : 0
    readonly property real   _airSpd:  activeVehicle ? activeVehicle.airSpeed.rawValue         : 0
    readonly property real   _hdg:     activeVehicle ? activeVehicle.heading.rawValue          : 0
    readonly property string _mode:    activeVehicle ? activeVehicle.flightMode                : "--"
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
        var m = { 0:"No GPS", 1:"No Fix", 2:"2D", 3:"3D", 4:"DGPS", 5:"RTK Flt", 6:"RTK Fix" }
        return m[l] || "?"
    }

    // ═══════════════════════════  HEADER  ════════════════════════════
    Rectangle {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: _hdrH;  radius: 8;  color: _headerBg

        // flatten bottom corners when expanded
        Rectangle {
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: parent.radius; color: parent.color; visible: !root.collapsed
        }

        MouseArea {
            anchors.fill: parent
            drag.target: root; drag.axis: Drag.XAndYAxis
            cursorShape: Qt.SizeAllCursor
            onDoubleClicked: root.collapsed = !root.collapsed
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin:  ScreenTools.defaultFontPixelWidth * 0.8
            anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 0.5
            spacing: ScreenTools.defaultFontPixelWidth * 0.4

            Rectangle {
                width: 8; height: 8; radius: 4
                color: activeVehicle ? _accent : _label
                SequentialAnimation on opacity {
                    loops: Animation.Infinite; running: !!activeVehicle
                    NumberAnimation { to: 0.4; duration: 1000 }
                    NumberAnimation { to: 1.0; duration: 1000 }
                }
            }

            Text {
                text: "TELEMETRY"; Layout.fillWidth: true
                font.pointSize: _fsL; font.weight: Font.DemiBold
                font.letterSpacing: 1.2; color: _accent
            }

            // Close button
            Rectangle {
                width: 22; height: 22; radius: 4
                color: _closeMA.containsMouse ? Qt.rgba(1,1,1,0.15) : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "\u2715"; font.pixelSize: 11; color: _label
                }
                MouseArea {
                    id: _closeMA; anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.closed()
                }
            }

            // Collapse toggle
            Rectangle {
                width: 22; height: 22; radius: 4
                color: _collapseMA.containsMouse ? Qt.rgba(1,1,1,0.1) : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: root.collapsed ? "\u25BC" : "\u25B2"
                    font.pixelSize: 10; color: _label
                }
                MouseArea {
                    id: _collapseMA; anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.collapsed = !root.collapsed
                }
            }
        }
    }

    // ═══════════════════════════  CONTENT  ═══════════════════════════
    Column {
        id: contentCol
        anchors { top: header.bottom; left: parent.left; right: parent.right }
        anchors.margins: ScreenTools.defaultFontPixelWidth * 0.6
        anchors.topMargin: 3
        spacing: 1
        visible: !root.collapsed
        opacity: root.collapsed ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 150 } }

        // ── Flight mode / heading banner ─────────────────────────────
        Rectangle {
            width: parent.width; height: ScreenTools.defaultFontPixelHeight * 1.8
            radius: 4; color: Qt.rgba(0.10, 0.12, 0.24, 0.6)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8; anchors.rightMargin: 8
                Text { text: "\u2708"; font.pixelSize: 14; color: _accent }
                Text {
                    text: _mode; Layout.fillWidth: true
                    font.pointSize: _fs; font.weight: Font.DemiBold; color: _text
                }
                Text { text: "HDG"; font.pointSize: _fsL; color: _label }
                Text {
                    text: _hdg.toFixed(0) + "\u00B0"
                    font.pointSize: _fs; font.family: "monospace"
                    font.weight: Font.DemiBold; color: _info
                }
            }
        }

        // divider
        Rectangle { width: parent.width; height: 1; color: _divider }

        // ── Altitude ─────────────────────────────────────────────────
        TelRow { lbl: "ALT AGL"; val: _altRel.toFixed(1) + " m"; valColor: _text }
        TelRow { lbl: "ALT MSL"; val: _altMSL.toFixed(1) + " m"; valColor: _text }

        Rectangle { width: parent.width; height: 1; color: _divider }

        // ── Speed ────────────────────────────────────────────────────
        TelRow { lbl: "GND SPD"; val: _gndSpd.toFixed(1) + " m/s"; valColor: _text }
        TelRow { lbl: "AIR SPD"; val: _airSpd.toFixed(1) + " m/s"; valColor: _text }

        Rectangle { width: parent.width; height: 1; color: _divider }

        // ── Battery ──────────────────────────────────────────────────
        TelRow {
            lbl: "BATTERY"
            val: _batPct >= 0 ? _batPct.toFixed(0) + "% \u00B7 " + _batV.toFixed(1) + "V" : "N/A"
            valColor: _battColor(_batPct)
        }

        // ── GPS ──────────────────────────────────────────────────────
        TelRow {
            lbl: "GPS"
            val: _fixStr(_lock) + " \u00B7 " + _sats + " sats"
            valColor: _gpsColor(_lock)
        }

        // ── Link Quality (RSSI) ──────────────────────────────────────
        TelRow {
            lbl: "LINK"
            val: (_rssi > 0 && _rssi <= 100) ? _rssi + "%" : "N/A"
            valColor: _rssiColor(_rssi)
        }
    }

    // ═══════════════════════  RESIZE HANDLE  ════════════════════════
    Rectangle {
        id: resizeHandle
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 16; height: 16
        visible: !root.collapsed
        color: _resizeMA.containsMouse ? Qt.rgba(0,0.83,1,0.3) : "transparent"
        radius: 2

        // Diagonal grip lines
        Canvas {
            anchors.fill: parent
            anchors.margins: 3
            onPaint: {
                var ctx = getContext("2d")
                ctx.strokeStyle = Qt.rgba(0.5,0.6,0.8,0.5)
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(width, 0); ctx.lineTo(width, height); ctx.lineTo(0, height)
                ctx.moveTo(width - 4, 0); ctx.lineTo(width - 4, height - 4); ctx.lineTo(0, height - 4)
                ctx.moveTo(width - 8, 0); ctx.lineTo(width - 8, height - 8); ctx.lineTo(0, height - 8)
                ctx.stroke()
            }
        }

        MouseArea {
            id: _resizeMA
            anchors.fill: parent
            cursorShape: Qt.SizeFDiagCursor
            property real _startX: 0
            property real _startW: 0
            onPressed: {
                _startX = mouse.x
                _startW = root.width
            }
            onPositionChanged: {
                var delta = mouse.x - _startX
                root._savedWidth = Math.max(root._minW, Math.min(_startW + delta, root._maxW))
            }
        }
    }

    // ═══════════════════════  INLINE COMPONENT  ═════════════════════
    component TelRow: Item {
        property string lbl
        property string val
        property color  valColor: _text

        width:  parent.width
        height: ScreenTools.defaultFontPixelHeight * 1.4

        Text {
            anchors.left: parent.left; anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: lbl; font.pointSize: _fsL; font.weight: Font.Medium
            color: _label
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: val; font.pointSize: _fs; font.family: "monospace"
            font.weight: Font.DemiBold; color: valColor
        }
    }
}
