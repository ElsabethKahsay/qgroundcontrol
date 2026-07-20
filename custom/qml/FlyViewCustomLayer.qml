// Component: FlyViewCustomLayer
// Purpose: Custom Fly View overlay for the Enterprise GCS plugin.
//   - Telemetry strip: bottom, spanning full width
//   - Compass widget: top-right, overlaid on the map half
//   - Preflight checklist button (bottom-left of video half)
//   - Preflight status badge (top-right of map half)
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
import QGroundControl.FlightMap

import com.uav.preflight 1.0

Item {
    id: _root

    property var parentToolInsets
    property var totalToolInsets:   _toolInsets
    property var mapControl

    // Passed from FlyView.qml so we know where each half is
    property real videoHalfWidth:   width * 0.5   // fallback if not bound
    property bool videoOnLeft:      true           // true = video on left

    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    // Derived geometry — the "video panel" left edge and the "map panel" left edge
    readonly property real _videoPanelLeft: videoOnLeft ? 0 : (width - videoHalfWidth)
    readonly property real _mapPanelLeft:   videoOnLeft ? videoHalfWidth : 0
    readonly property real _panelWidth:     videoHalfWidth

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

    // ═══════════════════════════════════════════════════════════════════
    //  TELEMETRY FLOATING BOX — self-positioning, draggable
    //  The component manages its own x/y via QSettings and internal
    //  drag logic. We fill the parent so relative coordinates work.
    // ═══════════════════════════════════════════════════════════════════
    FlyViewTelemetryStrip {
        id:            telemetryStrip
        anchors.fill:  parent
        activeVehicle: _activeVehicle
        z:             QGroundControl.zOrderWidgets + 1
    }

    // ═══════════════════════════════════════════════════════════════════
    //  COMPASS — top-right corner of the map panel
    // ═══════════════════════════════════════════════════════════════════
    Item {
        id:     compassContainer
        x:      _mapPanelLeft + _panelWidth - compassWidget.size - _compassMargin
        y:      _compassMargin
        width:  compassWidget.size
        height: compassWidget.size
        z:      QGroundControl.zOrderWidgets + 1

        readonly property real _compassMargin: ScreenTools.defaultFontPixelWidth * 1.5

        // Semi-transparent dark backdrop
        Rectangle {
            anchors.fill:   parent
            radius:         parent.width / 2
            color:          Qt.rgba(Colors.background.r, Colors.background.g, Colors.background.b, 0.60)
            border.color:   Qt.rgba(Colors.accentCyan.r, Colors.accentCyan.g, Colors.accentCyan.b, 0.25)
            border.width:   1
        }

        QGCCompassWidget {
            id:       compassWidget
            vehicle:  _activeVehicle
            size:     ScreenTools.defaultFontPixelHeight * 9
            anchors.centerIn: parent

            // Override background to be transparent (backdrop above handles it)
            color:          Colors.transparent
            border.width:   0
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  NEW VEHICLE BANNER — top-center, dismissed on tap
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id:                 newVehicleBanner
        x:                  (parent.width - width) / 2
        y:                  ScreenTools.defaultFontPixelHeight * 0.4
        width:              bannerRow.width + ScreenTools.defaultFontPixelWidth * 2
        height:             bannerRow.height + ScreenTools.defaultFontPixelWidth
        radius:             height / 2
        color:              Qt.rgba(Colors.background.r, Colors.background.g, Colors.background.b, 0.92)
        border.color:       Colors.accentCyan
        border.width:       1
        visible:            false
        z:                  QGroundControl.zOrderWidgets + 2

        Row {
            id:             bannerRow
            anchors.centerIn: parent
            spacing:        ScreenTools.defaultFontPixelWidth * 0.5

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:                   qsTr("🆕")
                font.pixelSize:         ScreenTools.defaultFontPixelHeight * 1.0
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:                   VehicleRegistry.isKnownVehicle
                                        ? "Known vehicle connected: " + VehicleRegistry.vehicleName
                                        : "New vehicle registered — tap to rename"
                font.pointSize:         ScreenTools.defaultFontPointSize
                color:                  Colors.textInverse
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                newVehicleBanner.visible = false
                if (!VehicleRegistry.isKnownVehicle) {
                    renameDialog.open()
                }
            }
        }

        Timer {
            id:             bannerTimer
            interval:       60000
            onTriggered:    newVehicleBanner.visible = false
        }

        // ── Inline rename dialog for new vehicles ──────────────────────
        Dialog {
            id:                renameDialog
            title:             qsTr("Rename Vehicle")
            standardButtons:   Dialog.Save | Dialog.Cancel
            modal:             true
            closePolicy:       Popup.CloseOnEscape
            x:                 Math.round((parent.width - width) / 2)
            y:                 Math.round((parent.height - height) / 2)
            width:             300
            height:            column.implicitHeight + header.implicitHeight + footer.implicitHeight + 40

            Column {
                id:   column
                anchors.fill: parent
                spacing: 8

                Text {
                    text:          qsTr("Enter a friendly name for this vehicle:")
                    font.pointSize: ScreenTools.defaultFontPointSize * 0.9
                    color:         Colors.textInverse
                    wrapMode:      Text.WordWrap
                    width:         parent.width
                }

                TextField {
                    id:                nameField
                    width:             parent.width
                    text:              VehicleRegistry.vehicleName
                    color:             Colors.dialogText
                    background: Rectangle {
                        color:  Colors.surface
                        radius: 4
                        border.color: Colors.accentCyan
                        border.width: 1
                    }
                }
            }

            onAccepted: {
                var fp = VehicleRegistry.currentFingerprint
                if (fp.length > 0 && nameField.text.trim().length > 0) {
                    Database.updateVehicleName(fp, nameField.text.trim())
                }
            }
        }
    }

    Connections {
        target: VehicleRegistry
        function onNewVehicleRegistered(vehicleId) {
            newVehicleBanner.visible = true
            bannerTimer.restart()
        }
        function onKnownVehicleConnected(vehicleId) {
            if (!VehicleRegistry.isKnownVehicle) return
            newVehicleBanner.visible = true
            bannerTimer.restart()
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  PREFLIGHT STATUS BADGE — top-right of map panel
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id:                 statusBadge
        x:                  _mapPanelLeft + _panelWidth - width - ScreenTools.defaultFontPixelWidth
        y:                  ScreenTools.defaultFontPixelHeight * 0.4
        width:              badgeRow.width + ScreenTools.defaultFontPixelWidth * 1.5
        height:             badgeRow.height + ScreenTools.defaultFontPixelWidth
        radius:             height / 2
        color:              Qt.rgba(0.05, 0.05, 0.12, 0.90)
        border.color:       Qt.rgba(0.25, 0.30, 0.50, 0.50)
        border.width:       1
        visible:            _activeVehicle && typeof PreflightManager !== 'undefined'
        z:                  QGroundControl.zOrderWidgets + 1

        Row {
            id:                 badgeRow
            anchors.centerIn:   parent
            spacing:            ScreenTools.defaultFontPixelWidth * 0.5

            Rectangle {
                width:                  ScreenTools.defaultFontPixelHeight * 0.6
                height:                 width
                radius:                 width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (typeof PreflightChecklistModel === 'undefined') return Colors.textDisabled
                    var f = PreflightChecklistModel.blockingFailedCount
                    var p = typeof PreflightManager !== 'undefined' ? PreflightManager.pendingChecks : 0
                    if (f > 0) return Colors.dialogFocus
                    if (p > 0) return Colors.warning
                    return Colors.success
                }
            }

            QGCLabel {
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
                color:          Colors.textInverse
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

    // ═══════════════════════════════════════════════════════════════════
    //  PREFLIGHT BUTTON — bottom-left of the video panel
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id:                     preflightBtn
        x:                      _videoPanelLeft + ScreenTools.defaultFontPixelWidth * 1.5
        y:                      parent.height - height - ScreenTools.defaultFontPixelHeight * 2
        width:                  btnRow.width + ScreenTools.defaultFontPixelWidth * 2
        height:                 ScreenTools.defaultFontPixelHeight * 2.6
        radius:                 height / 2
        z:                      QGroundControl.zOrderWidgets + 1

        color: {
            if (!_activeVehicle) return Qt.rgba(Colors.textDisabled.r, Colors.textDisabled.g, Colors.textDisabled.b, 0.70)
            if (_btnMA.containsPress)  return Colors.tealDark
            if (_btnMA.containsMouse)  return Colors.tealLight
            return Colors.teal
        }
        border.color: _activeVehicle ? Colors.accentCyan : Qt.rgba(0.5, 0.5, 0.55, 0.5)
        border.width: 1

        Behavior on color { ColorAnimation { duration: 120 } }

        layer.enabled: true

        Row {
            id:                 btnRow
            anchors.centerIn:   parent
            spacing:            ScreenTools.defaultFontPixelWidth * 0.6

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:                   qsTr("📋")
                font.pixelSize:         ScreenTools.defaultFontPixelHeight * 1.1
                opacity:                _activeVehicle ? 1.0 : 0.4
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:                   qsTr("Preflight Checklist")
                font.pointSize:         ScreenTools.defaultFontPointSize * 0.95
                font.weight:            Font.DemiBold
                color:                  _activeVehicle ? Colors.dialogText : Colors.textDisabled
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width:                  badgeText.width + ScreenTools.defaultFontPixelWidth
                height:                 ScreenTools.defaultFontPixelHeight * 1.2
                radius:                 height / 2
                visible:                _activeVehicle && typeof PreflightManager !== 'undefined'
                                        && (typeof PreflightChecklistModel !== 'undefined')
                color: {
                    if (typeof PreflightChecklistModel === 'undefined') return "transparent"
                    var f = PreflightChecklistModel.blockingFailedCount
                    if (f > 0) return Colors.dialogFocus
                    var p = typeof PreflightManager !== 'undefined' ? PreflightManager.pendingChecks : 0
                    if (p > 0) return Colors.warning
                    return Colors.success
                }

                Text {
                    id:                 badgeText
                    anchors.centerIn:   parent
                    text: {
                        if (typeof PreflightChecklistModel === 'undefined') return ""
                        var f = PreflightChecklistModel.blockingFailedCount
                        if (f > 0) return f.toString()
                        var p = typeof PreflightManager !== 'undefined' ? PreflightManager.pendingChecks : 0
                        if (p > 0) return p.toString()
                        return "✓"
                    }
                    font.pointSize: ScreenTools.defaultFontPointSize * 0.8
                    font.weight:    Font.Bold
                    color:          Colors.dialogText
                }
            }
        }

        MouseArea {
            id:             _btnMA
            anchors.fill:   parent
            hoverEnabled:   true
            cursorShape:    _activeVehicle ? Qt.PointingHandCursor : Qt.ForbiddenCursor
            enabled:        !!_activeVehicle

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

    // ═══════════════════════════════════════════════════════════════════
    //  CHECKLIST DIALOG
    // ═══════════════════════════════════════════════════════════════════
    Component {
        id: checklistDialog
        QGCPopupDialog {
            title:   qsTr("Preflight Checklist")
            buttons: Dialog.Close
            modal:   true

            Loader {
                source: "qrc:/qml/cpts/PreflightChecklistView.qml"
                width:  ScreenTools.defaultFontPixelWidth * 80
                height: ScreenTools.defaultFontPixelHeight * 40
            }
        }
    }
}
