// Component: FlyViewCustomLayer
// Purpose: Custom Fly View overlay layer for the Enterprise GCS plugin.
//   Hosts the TelemetryInfoBox (draggable/collapsible telemetry panel),
//   a prominent Preflight Checklist shortcut button, and the preflight
//   status badge. All overlays keep the map/video feed fully visible.
// Properties:
//   parentToolInsets (var) — insets from the parent Fly View
//   totalToolInsets  (var) — combined insets for child layout
//   mapControl       (var) — reference to the map control
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.MultiVehicleManager
import QGroundControl.ScreenTools
import QGroundControl.Palette

Item {
    id: _root

    property var parentToolInsets
    property var totalToolInsets:   _toolInsets
    property var mapControl

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    QGCToolInsets {
        id:                     _toolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        leftEdgeCenterInset:    parentToolInsets.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
        rightEdgeBottomInset:   parentToolInsets.rightEdgeBottomInset
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     parentToolInsets.topEdgeCenterInset
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        bottomEdgeRightInset:   parentToolInsets.bottomEdgeRightInset
    }

    // ═══════════════════  PREFLIGHT STATUS BADGE  ════════════════════
    // Compact pill badge showing preflight check status (top-right corner)
    Rectangle {
        id: statusBadge
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: ScreenTools.defaultFontPixelWidth * 0.75
        width: badgeRow.width + ScreenTools.defaultFontPixelWidth * 1.5
        height: badgeRow.height + ScreenTools.defaultFontPixelWidth
        radius: height / 2
        color: qgcPal.window
        border.color: qgcPal.text
        visible: _activeVehicle && typeof PreflightManager !== 'undefined'

        Row {
            id: badgeRow
            anchors.centerIn: parent
            spacing: ScreenTools.defaultFontPixelWidth * 0.5

            Rectangle {
                id: statusDot
                width: ScreenTools.defaultFontPixelHeight * 0.6
                height: width
                radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (typeof PreflightChecklistModel === 'undefined') return "#9E9E9E"
                    var f = PreflightChecklistModel.blockingFailedCount
                    var p = typeof PreflightManager !== 'undefined' ? PreflightManager.pendingChecks : 0
                    if (f > 0) return "#E91E63"
                    if (p > 0) return "#FF9800"
                    return "#4CAF50"
                }
            }

            QGCLabel {
                id: statusLabel
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (typeof PreflightManager === 'undefined') return ""
                    var f = PreflightChecklistModel.blockingFailedCount
                    var p = PreflightManager.pendingChecks
                    var t = PreflightManager.totalChecks
                    if (t === 0) return "checks..."
                    if (f > 0) return "\u2716 " + f + " failed"
                    if (p > 0) return p + " pending"
                    return "\u2713 Ready"
                }
                font.pointSize: ScreenTools.defaultFontPointSize
                color: qgcPal.text
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                var obj = checklistDialog.createObject(mainWindow)
                obj.open()
            }
        }
    }

    // ═══════════════  PREFLIGHT CHECKLIST SHORTCUT BUTTON  ═══════════
    // Prominent floating action button on the left side of the Fly View.
    // Opens the full preflight checklist popup. Disabled when no vehicle.
    Rectangle {
        id: preflightBtn
        anchors.left:   parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin:   ScreenTools.defaultFontPixelWidth * 1.5
        anchors.bottomMargin: ScreenTools.defaultFontPixelHeight * 5

        width:  btnRow.width + ScreenTools.defaultFontPixelWidth * 2
        height: ScreenTools.defaultFontPixelHeight * 2.8
        radius: height / 2

        // Gradient-like appearance
        color: {
            if (!_activeVehicle) return Qt.rgba(0.3, 0.3, 0.35, 0.7)
            if (_btnMA.containsPress) return "#065F7C"
            if (_btnMA.containsMouse) return "#0AA8D6"
            return "#0891B2"                           // primary accent
        }
        border.color: _activeVehicle ? "#00D4FF" : Qt.rgba(0.5, 0.5, 0.55, 0.5)
        border.width: 1

        Behavior on color { ColorAnimation { duration: 120 } }

        // subtle shadow
        layer.enabled: true

        Row {
            id: btnRow
            anchors.centerIn: parent
            spacing: ScreenTools.defaultFontPixelWidth * 0.6

            // Checklist icon (unicode clipboard)
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "📋"
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 1.1
                opacity: _activeVehicle ? 1.0 : 0.4
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Preflight Checklist")
                font.pointSize: ScreenTools.defaultFontPointSize * 0.95
                font.weight:    Font.DemiBold
                color: _activeVehicle ? "#FFFFFF" : "#888888"
            }

            // Badge showing failed/pending count
            Rectangle {
                id: countBadge
                anchors.verticalCenter: parent.verticalCenter
                width:  badgeText.width + ScreenTools.defaultFontPixelWidth
                height: ScreenTools.defaultFontPixelHeight * 1.2
                radius: height / 2
                visible: _activeVehicle && typeof PreflightManager !== 'undefined'
                         && (typeof PreflightChecklistModel !== 'undefined')
                color: {
                    if (typeof PreflightChecklistModel === 'undefined') return "transparent"
                    var f = PreflightChecklistModel.blockingFailedCount
                    if (f > 0) return "#E91E63"
                    var p = typeof PreflightManager !== 'undefined' ? PreflightManager.pendingChecks : 0
                    if (p > 0) return "#FF9800"
                    return "#4CAF50"
                }

                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: {
                        if (typeof PreflightChecklistModel === 'undefined') return ""
                        var f = PreflightChecklistModel.blockingFailedCount
                        if (f > 0) return f.toString()
                        var p = typeof PreflightManager !== 'undefined' ? PreflightManager.pendingChecks : 0
                        if (p > 0) return p.toString()
                        return "✓"
                    }
                    font.pointSize: ScreenTools.defaultFontPointSize * 0.8
                    font.weight: Font.Bold
                    color: "#FFFFFF"
                }
            }
        }

        MouseArea {
            id: _btnMA
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: _activeVehicle ? Qt.PointingHandCursor : Qt.ForbiddenCursor
            enabled: !!_activeVehicle

            ToolTip.visible: containsMouse
            ToolTip.text:    _activeVehicle ? qsTr("Open Preflight Test Page")
                                           : qsTr("Connect a vehicle first")
            ToolTip.delay:   400

            onClicked: {
                var obj = checklistDialog.createObject(mainWindow)
                obj.open()
            }
        }
    }

    // ═══════════════════  TELEMETRY INFO BOX  ════════════════════════
    // Draggable, collapsible, resizable telemetry panel — positioned
    // upper-right, below the status badge. Shows altitude, speed,
    // battery, GPS, link quality, heading, flight mode.
    property bool _telemetryVisible: true

    Loader {
        id: telemetryBoxLoader
        anchors.top:    statusBadge.bottom
        anchors.right:  parent.right
        anchors.topMargin:   ScreenTools.defaultFontPixelWidth * 0.5
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 0.75

        active: !!_activeVehicle && _telemetryVisible
        source: "qrc:/qml/cpts/TelemetryInfoBox.qml"

        onLoaded: {
            item.activeVehicle = Qt.binding(function() { return _activeVehicle })
            item.closed.connect(function() { _telemetryVisible = false })
        }
    }

    // ═══════════════════  CHECKLIST DIALOG  ══════════════════════════
    Component {
        id: checklistDialog
        QGCPopupDialog {
            id: dialog
            title: qsTr("Preflight Checklist")
            buttons: StandardButton.Close
            modal: true

            Loader {
                id: checklistLoader
                source: "qrc:/qml/cpts/PreflightChecklistView.qml"
                width: ScreenTools.defaultFontPixelWidth * 80
                height: ScreenTools.defaultFontPixelHeight * 40
            }
        }
    }
}
