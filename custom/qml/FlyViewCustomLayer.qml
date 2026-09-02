// Component: FlyViewCustomLayer
// Purpose: Custom Fly View overlay for the Enterprise GCS plugin.
//   - Left-side quick-action column: BatteryStatusChip + FlyQuickActions
//     (Checklist / Takeoff / Land / RTL / Pause / Flight time / FC status / Events)
//   - Telemetry strip: bottom, spanning full width
//   - Compass widget: top-right, overlaid on the map half
//   - Preflight status badge: top-right of map half
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
import cpts 1.0

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
        y:                  ScreenTools.defaultFontPixelHeight * 3.0
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
                text:                   qsTr("NEW")
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
    //  FC REJECTION TOAST — shows when autopilot rejects a command
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id:                 fcRejectionToast
        x:                  (parent.width - width) / 2
        y:                  parent.height - height - ScreenTools.defaultFontPixelHeight * 2
        width:              fcToastRow.width + ScreenTools.defaultFontPixelWidth * 2
        height:             fcToastRow.height + ScreenTools.defaultFontPixelWidth
        radius:             height / 2
        color:              Qt.rgba(0.85, 0.15, 0.15, 0.92)
        border.color:       Colors.error
        border.width:       1
        visible:            false
        z:                  QGroundControl.zOrderWidgets + 2

        Row {
            id:             fcToastRow
            anchors.centerIn: parent
            spacing:        ScreenTools.defaultFontPixelWidth * 0.5

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text:                   "\u26A0\uFE0F"
                font.pixelSize:         ScreenTools.defaultFontPixelHeight * 1.0
            }

            Text {
                id:                 fcToastText
                anchors.verticalCenter: parent.verticalCenter
                text:               ""
                font.pointSize:     ScreenTools.defaultFontPointSize
                color:              Colors.dialogText
                width:              Math.min(implicitWidth, parent.width * 0.6)
                wrapMode:           Text.WordWrap
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked:    fcRejectionToast.visible = false
        }

        Timer {
            id:             fcToastTimer
            interval:       5000
            onTriggered:    fcRejectionToast.visible = false
        }
    }

    Connections {
        target: typeof ArmingGate !== 'undefined' ? ArmingGate : null
        function onCommandRejected(reason) {
            fcToastText.text = reason
            fcRejectionToast.visible = true
            fcToastTimer.restart()
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
            onClicked: _root.openPreflightDialog()
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  LIVE BATTERY CHIP + QUICK ACTIONS — far-left edge of the page
    // ═══════════════════════════════════════════════════════════════════
    readonly property real _qaLeftX: ScreenTools.defaultFontPixelWidth * 1.5

    // Y position just below the stock FlyViewToolStrip bottom edge
    readonly property real _qaTopY: parentToolInsets.topEdgeLeftInset + ScreenTools.defaultFontPixelHeight * 0.6

    Column {
        id:                     leftActionsColumn
        x:                      _qaLeftX
        y:                      _root._qaTopY
        spacing:                ScreenTools.defaultFontPixelHeight * 0.4
        z:                      QGroundControl.zOrderWidgets + 1

        BatteryStatusChip {
            id:                 batteryChip
        }

        FlyQuickActions {
            id:                 quickActions
            hasVehicle:         _activeVehicle != null
            checkedAction:      batteryChip.detailOpen ? "flightTime" : _root._openPanel

            onActionTriggered: (action) => {
                if (action === "flightTime") {
                    batteryChip.toggleDetail()
                } else if (action === "checklist") {
                    _root.openPreflightDialog()
                } else if (action === "forceArm") {
                    // Handled internally by FlyQuickActions (immediate arm).
                } else {
                    _root.togglePanel(action)
                }
            }        }
    }

    // ── Single-overlay state for the FC status / Events panels ──
    property string _openPanel: ""          // "", "fcStatus", "events"

    function togglePanel(which) {
        if (_openPanel === which) {
            _openPanel = ""
        } else {
            _openPanel = which
        }
    }

    FcStatusPanel {
        id:                     fcStatusPanel
        x:                      _qaLeftX
        y:                      leftActionsColumn.y + leftActionsColumn.height + ScreenTools.defaultFontPixelHeight * 0.6
        visiblePanel:           _root._openPanel === "fcStatus"
        z:                      QGroundControl.zOrderWidgets + 1
        onCloseRequested:       _root._openPanel = ""
    }

    EventLogPanel {
        id:                     eventLogPanel
        vehicle:                _activeVehicle
        x:                      _qaLeftX
        y:                      leftActionsColumn.y + leftActionsColumn.height + ScreenTools.defaultFontPixelHeight * 0.6
        visible:                _root._openPanel === "events"
        z:                      QGroundControl.zOrderWidgets + 1
        onCloseRequested:       _root._openPanel = ""
    }

    // ── Battery time estimator overlay ──
    BatteryTimeEstimatorPanel {
        id:                     batteryEstimatePanel
        x:                      _qaLeftX
        y:                      leftActionsColumn.y + leftActionsColumn.height + ScreenTools.defaultFontPixelHeight * 0.6
        width:                  ScreenTools.defaultFontPixelWidth * 32
        visible:                _root._openPanel === "batteryEstimate"
        z:                      QGroundControl.zOrderWidgets + 1
        onCloseRequested:       _root._openPanel = ""
    }



    property bool _preflightAnalyzePending: false
    property url  _preflightAnalyzeSource: "qrc:/qml/cpts/PreflightChecklistView.qml"
    property string _preflightAnalyzeTitle: qsTr("Preflight Checklist")

    function _attemptOpenPreflightAnalyzePage() {
        if (!mainWindow.toolDrawerLoader || !mainWindow.toolDrawerLoader.item || !mainWindow.toolDrawerLoader.item.panelLoader) {
            console.log("FlyView: cannot open preflight analyze page yet")
            return false
        }

        console.log("FlyView: setting preflight analyze page on toolDrawerLoader.item")
        mainWindow.toolDrawerLoader.item.panelLoader.source = _preflightAnalyzeSource
        mainWindow.toolDrawerLoader.item.panelLoader.title = _preflightAnalyzeTitle
        if (typeof mainWindow.toolDrawerLoader.item._selectAnalyzeButton === 'function') {
            mainWindow.toolDrawerLoader.item._selectAnalyzeButton(_preflightAnalyzeTitle, _preflightAnalyzeSource)
        }
        _preflightAnalyzePending = false
        _openAnalyzeTimer.stop()
        return true
    }

    // Opens the preflight checklist from the FlyView button by showing the
    // Analyze Tools drawer and then directing the Analyze view to load the
    // checklist page once the loader is ready.
    Timer {
        id: _openAnalyzeTimer
        interval: 300
        repeat: false
        running: false
        onTriggered: {
            _attemptOpenPreflightAnalyzePage()
        }
    }

    function openPreflightDialog() {
        _preflightAnalyzePending = true
        mainWindow.showAnalyzeTool()
        _attemptOpenPreflightAnalyzePage()
        _openAnalyzeTimer.restart()
    }

    Connections {
        target: mainWindow.toolDrawerLoader
        ignoreUnknownSignals: true
        function onStatusChanged() {
            if (_preflightAnalyzePending && mainWindow.toolDrawerLoader.status === Loader.Ready) {
                _attemptOpenPreflightAnalyzePage()
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  SESSION START DIALOG — opens from the preflight checklist button;
    //  tapping a mode card only selects the mode and closes the dialog.
    // ═══════════════════════════════════════════════════════════════════
    SessionStartDialog {
        id: sessionStartDialog
    }

    // ═══════════════════════════════════════════════════════════════════
    //  TRAINING MODE BANNER — visible when in training mode
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: trainingBanner
        visible: FlightSession.isTraining
        anchors { top: parent.top; topMargin: _toolInsets.topEdgeCenterInset; left: parent.left; right: parent.right }
        height: 40
        z: QGroundControl.zOrderWidgets + 3
        color: Qt.rgba(Colors.warning.r, Colors.warning.g, Colors.warning.b, 0.15)

        Row {
            anchors.centerIn: parent
            spacing: Config.spacingSmall

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("\u26A0")
                font.pixelSize: 18
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("TRAINING MODE \u2014 Arming disabled")
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: Colors.warning
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  TESTING MODE BANNER — visible when in testing mode
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: testingBanner
        visible: FlightSession.isTesting
        anchors { top: parent.top; topMargin: _toolInsets.topEdgeCenterInset; left: parent.left; right: parent.right }
        height: 40
        z: QGroundControl.zOrderWidgets + 3
        color: Qt.rgba(Colors.accentCyan.r, Colors.accentCyan.g, Colors.accentCyan.b, 0.15)

        Row {
            anchors.centerIn: parent
            spacing: Config.spacingSmall

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("\u2699\uFE0F")
                font.pixelSize: 18
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("TESTING MODE \u2014 No audit logging")
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: Colors.accentCyan
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  POST-FLIGHT BANNER — appears when state is PostFlight
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: postFlightBanner
        visible: FlightSession.state === FlightSession.PostFlight
        anchors { top: parent.top; topMargin: _toolInsets.topEdgeCenterInset; left: parent.left; right: parent.right }
        height: 40
        z: QGroundControl.zOrderWidgets + 3
        color: Qt.rgba(Colors.success.r, Colors.success.g, Colors.success.b, 0.15)

        Row {
            anchors.centerIn: parent
            spacing: Config.spacingSmall

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("\u2708\uFE0F")
                font.pixelSize: 18
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Flight complete — complete the post-flight checklist")
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: Colors.success
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  CHECKLIST DIALOG — loads the new multi-step wizard
    // ═══════════════════════════════════════════════════════════════════
    Component {
        id: checklistDialog
        QGCPopupDialog {
            title:   qsTr("Preflight Wizard")
            buttons: Dialog.Close
            modal:   true

            Loader {
                source: "qrc:/qml/pages/PreFlightChecklist.qml"
                width:  ScreenTools.defaultFontPixelWidth * 100
                height: ScreenTools.defaultFontPixelHeight * 50
            }
        }
    }
}
