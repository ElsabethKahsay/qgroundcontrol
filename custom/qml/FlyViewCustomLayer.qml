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
    // Named so the live driver can find this layer and invoke the operator's
    // real "open the pre-flight checklist" entry point, proving the checklist
    // page (and the panels it hosts) is reachable from the normal Fly flow.
    objectName: "flyViewCustomLayer"

    property var parentToolInsets
    property var totalToolInsets:   _toolInsets
    property var mapControl

    // Passed from FlyView.qml so we know where each half is
    property real videoHalfWidth:   width * 0.5   // fallback if not bound
    property bool videoOnLeft:      true           // true = video on left

    // Active vehicle comes from the QGC MultiVehicleManager singleton; null when disconnected.
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
            // Auto-dismiss the banner after one minute of no interaction.
            interval:       60000
            onTriggered:    newVehicleBanner.visible = false
        }

        // Stop the banner timer if the banner is torn down mid-timeout.
        Component.onDestruction: bannerTimer.stop()

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
            // 5 s auto-hide for the rejection toast, reset on each new rejection.
            interval:       5000
            onTriggered:    fcRejectionToast.visible = false
        }

        // Stop the toast timer if the toast is torn down mid-timeout.
        Component.onDestruction: fcToastTimer.stop()
    }

    // ArmingGate (C++) is the source of truth for arming rejections; it is optional in plain QGC.
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
                // Badge priority: blocking failures > pending checks > all clear.
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

        // Mission chip: the next waypoint, the time to it, the charge on
        // arrival (pessimistic) and the result, coloured by the result; "paused"
        // while the pilot flies, "RTL"/"LAND" while coming home; RETURN NOW in
        // red when the next waypoint is past the turn-back point. Shown in
        // flight while the mission estimate runs (VehicleProfileManager.missionLive).
        Rectangle {
            id:                 missionChip
            objectName:         "missionChip"
            readonly property var _vpm: typeof VehicleProfileManager !== "undefined" ? VehicleProfileManager : null
            readonly property var _m: _vpm ? _vpm.missionLive : ({})
            readonly property bool _shown: _m.live === true && (_m.ready === true || _m.chargeUnknown === true || _m.stale === true)
            readonly property var _next: (_m.waypoints || []).length > 0 ? _m.waypoints[0] : null
            readonly property color _colour: _m.stale ? Colors.statusStale
                                             : _m.chargeUnknown ? Colors.warning
                                             : _m.returnNow ? Colors.error
                                             : _m.colour === "green" ? Colors.success
                                             : _m.colour === "amber" ? Colors.warning
                                             : _m.colour === "red" ? Colors.error : Colors.textSecondary
            readonly property string text: {
                if (_m.stale) return qsTr("STALE · link lost")
                if (_m.chargeUnknown) return qsTr("Can't assess: %1").arg(_m.problem || "charge unknown (joined in flight)")
                if (!_shown || !_next) return ""
                var gps = _m.gpsPoor ? qsTr("GPS POOR · ") : ""
                if (_m.returnNow) return gps + qsTr("RETURN NOW · item %1").arg(_next.seq)
                var where = _m.returning ? (_m.returningLand ? qsTr("LAND") : qsTr("RTL"))
                          : _next.seq >= 0 ? qsTr("WP %1").arg(_next.seq) : qsTr("Home")
                var when = _m.mode === "paused" ? qsTr("paused") : _next.timeText
                return gps + where + " · " + when + " · " + Math.round(_next.socPessimisticPct) + " % · " + _m.resultName
            }
            visible:            _shown
            width:              chipText.implicitWidth + ScreenTools.defaultFontPixelWidth * 2
            height:             ScreenTools.defaultFontPixelHeight * 1.6
            radius:             height / 2
            color:              Colors.surface
            border.color:       Qt.rgba(_colour.r, _colour.g, _colour.b, 0.6)
            border.width:       1

            Text {
                id:                 chipText
                anchors.centerIn:   parent
                text:               missionChip.text
                font.pixelSize:     ScreenTools.defaultFontPixelHeight * 0.85
                font.bold:          true
                color:              missionChip._colour
            }
            ToolTip.visible:    chipMouse.containsMouse
            ToolTip.text:       missionChip._m.stale
                                ? qsTr("No heartbeat for 5 s: the numbers are frozen, not live.")
                                : missionChip._m.chargeUnknown
                                ? qsTr("Can't assess: %1").arg(missionChip._m.problem || "charge unknown (joined in flight)")
                                : (missionChip._m.gpsPoor ? qsTr("GPS unreliable: the position, the distances, the times and RETURN NOW may be wrong.\n") : "")
                                + (missionChip._m.returnNow ? missionChip._m.returnNowWhy
                                : qsTr("Lands with %1 % (pessimistic %2 %), %3 left")
                                      .arg(Math.round(missionChip._m.landLikelyPct || 0))
                                      .arg(Math.round(missionChip._m.landPessimisticPct || 0))
                                      .arg(missionChip._m.totalText || "--"))
            MouseArea {
                id:             chipMouse
                anchors.fill:   parent
                hoverEnabled:   true
                cursorShape:    Qt.PointingHandCursor
                onClicked:      _root.togglePanel("mission")
            }
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

    // ── Single-overlay state for the FC status / Events / Mission panels ──
    property string _openPanel: ""          // "", "fcStatus", "events", "mission"

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

    // ── Mission panel (opened from the mission chip) ──
    // The live list: the items reached this flight in grey (real time and
    // charge, the take-off prediction small beside them), the next one
    // highlighted with its countdown, the ones ahead predicted, a line under
    // the last safe turn-back point; the reasons, the wind in use and where it
    // comes from, and "Payload dropped" (asks to confirm) for an RC drop the
    // release output does not show. One rich-text table, like the checklist
    // dialogs.
    Rectangle {
        id:                     missionPanel
        objectName:             "missionPanel"
        readonly property var _vpm: typeof VehicleProfileManager !== "undefined" ? VehicleProfileManager : null
        readonly property var _m: _vpm ? _vpm.missionLive : ({})
        readonly property var _pl: _vpm ? _vpm.powerLive : ({})
        property bool _confirmDrop: false
        visible:                _root._openPanel === "mission" && _m.live === true
        x:                      _qaLeftX
        y:                      leftActionsColumn.y + leftActionsColumn.height + ScreenTools.defaultFontPixelHeight * 0.6
        z:                      QGroundControl.zOrderWidgets + 1
        width:                  ScreenTools.defaultFontPixelWidth * 46
        height:                 panelCol.implicitHeight + ScreenTools.defaultFontPixelHeight
        radius:                 Config.radiusLarge
        color:                  Qt.rgba(Colors.background.r, Colors.background.g, Colors.background.b, 0.92)
        border.color:           _m.stale ? Colors.statusStale : Colors.accentCyan
        onVisibleChanged:       _confirmDrop = false

        function _pct(v) { return (v === undefined || v === null || isNaN(v) || v < 0) ? "--" : Math.round(v) + " %" }

        ColumnLayout {
            id:                 panelCol
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: ScreenTools.defaultFontPixelWidth }
            spacing:            ScreenTools.defaultFontPixelHeight * 0.3

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text:           qsTr("MISSION · %1").arg(missionPanel._m.resultName || "")
                    font.bold:      true
                    color:          missionChip._colour
                    Layout.fillWidth: true
                }
                Text {
                    text:           "\u2715"
                    color:          Colors.textSecondary
                    MouseArea { anchors.fill: parent; onClicked: _root._openPanel = "" }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode:       Text.WordWrap
                color:          Colors.textPrimary
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.8
                text: missionPanel._m.stale
                      ? qsTr("STALE · link lost: the last estimate is frozen, not live.")
                      : missionPanel._m.chargeUnknown
                      ? qsTr("Can't assess: %1").arg(missionPanel._m.problem || "charge unknown (joined in flight)")
                      : missionPanel._m.mode === "paused"
                      ? qsTr("Paused: the pilot is flying. Charge as if the mission went on.")
                      : qsTr("Lands with %1 (pessimistic %2), %3 left")
                            .arg(missionPanel._pct(missionPanel._m.landLikelyPct))
                            .arg(missionPanel._pct(missionPanel._m.landPessimisticPct))
                            .arg(missionPanel._m.totalText || "--")
            }
            Text {
                objectName:     "missionPanelTable"
                Layout.fillWidth: true
                textFormat:     Text.RichText
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.8
                color:          Colors.textPrimary
                text: {
                    var m = missionPanel._m
                    var html = "<table width='100%' cellspacing='1'>"
                    var passed = m.passed || []
                    for (var i = 0; i < passed.length; ++i) {
                        var p = passed[i]
                        html += "<tr><td><font color='" + Colors.textSecondary + "'>" + p.seq + "</font></td><td><font color='"
                              + Colors.textSecondary + "'>" + qsTr("reached") + " " + p.timeText + "</font> <small>(" + qsTr("plan")
                              + " " + (p.predictedTimeText || "--") + ")</small></td><td align='right'><font color='"
                              + Colors.textSecondary + "'>" + missionPanel._pct(p.socPct) + "</font> <small>("
                              + missionPanel._pct(p.predictedSocPct) + ")</small></td></tr>"
                    }
                    var w = m.waypoints || []
                    for (var j = 0; j < w.length; ++j) {
                        var a = w[j]
                        var c = a.state === 0 || a.state === 1 ? Colors.textPrimary : Colors.error
                        var label = a.seq >= 0 ? a.seq + " " + a.what : a.what
                        html += "<tr><td colspan='1'>" + (j === 0 ? "<b>&#9654;</b>" : "") + "</td><td><font color='" + c + "'>"
                              + (j === 0 ? "<b>" + label + " · " + a.timeText + "</b>" : label + " · " + a.timeText)
                              + "</font></td><td align='right'><font color='" + c + "'>" + missionPanel._pct(a.socPessimisticPct)
                              + "</font></td></tr>"
                        if (a.seq === m.turnBackSeq)
                            html += "<tr><td colspan='3'><font color='" + Colors.warning + "'><small>&#8212;&#8212; "
                                  + qsTr("last safe turn-back point") + " &#8212;&#8212;</small></font></td></tr>"
                    }
                    return html + "</table>"
                }
            }
            Text {
                Layout.fillWidth: true
                visible:        (missionPanel._m.reasons || []).length > 0
                wrapMode:       Text.WordWrap
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.7
                color:          missionPanel._m.returnNow || missionPanel._m.weatherRed ? Colors.error : Colors.textSecondary
                text:           (missionPanel._m.returnNow ? missionPanel._m.returnNowWhy + "\n" : "")
                                + (missionPanel._m.reasons || []).join("\n")
            }
            Text {
                Layout.fillWidth: true
                wrapMode:       Text.WordWrap
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.7
                color:          Colors.textSecondary
                text:           qsTr("Wind in use: %1").arg(missionPanel._m.weatherText || "--")
            }
            Button {
                objectName:     "missionPanelDropButton"
                Layout.fillWidth: true
                visible:        (missionPanel._pl.payloadAboardKg || 0) > 0
                text:           missionPanel._confirmDrop
                                ? qsTr("Confirm: payload %1 dropped").arg((missionPanel._pl.payloadsReleased || 0) + 1)
                                : qsTr("Payload %1 dropped").arg((missionPanel._pl.payloadsReleased || 0) + 1)
                onClicked: {
                    if (!missionPanel._confirmDrop) { missionPanel._confirmDrop = true; return }
                    missionPanel._confirmDrop = false
                    if (missionPanel._vpm) missionPanel._vpm.payloadDroppedTap()
                }
            }
        }
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



    // Opens Analyze Tools on its Preflight Checklist page. AnalyzeView reads
    // Config.analyzePreflightRequested on load (or live, if already open), and
    // still asks for a session mode first when none is selected. The previous
    // code reached into mainWindow.toolDrawerLoader.item.panelLoader, which are
    // ids, not properties, so it always fell through to the first Analyze page.
    function openPreflightDialog() {
        Config.analyzePreflightRequested = true
        mainWindow.showAnalyzeTool()
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
        // Safety: training mode (C++ FlightSession) forbids arming, hence the banner.
        visible: false
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
    //  TRAINER CHIP — who has control in a two-radio Training session
    //  (instructor's trainer switch mixed onto a spare channel, default CH8)
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: trainerChip
        readonly property var _plugin: QGroundControl.corePlugin
        readonly property string _control: _plugin && _plugin.trainerControl !== undefined ? _plugin.trainerControl : "unmonitored"
        readonly property bool _student: _control === "student"
        readonly property bool _monitored: _control !== "unmonitored"
        visible: FlightSession.isTraining && !!QGroundControl.multiVehicleManager.activeVehicle
        anchors { top: parent.top; topMargin: _toolInsets.topEdgeCenterInset + 8; horizontalCenter: parent.horizontalCenter }
        z: QGroundControl.zOrderWidgets + 3
        width: trainerChipText.implicitWidth + 24
        height: trainerChipText.implicitHeight + 12
        radius: height / 2
        color: !_monitored ? Colors.surface : _student ? Colors.warning : Colors.success
        border.color: _monitored ? color : Colors.border
        border.width: 1

        Text {
            id: trainerChipText
            anchors.centerIn: parent
            font.pixelSize: Config.fontSizeBody
            font.bold: true
            color: trainerChip._monitored ? Colors.background : Colors.textSecondary
            text: trainerChip._plugin && trainerChip._plugin.trainerChannelConflict
                  ? qsTr("Trainer handover not monitored \u2014 ") + trainerChip._plugin.trainerChannelConflict
                  : !trainerChip._monitored
                  ? qsTr("Trainer handover not monitored \u2014 no CH%1 trainer mix").arg(trainerChip._plugin ? trainerChip._plugin.trainerChannel : 8)
                  : (trainerChip._student ? qsTr("STUDENT FLYING") : qsTr("INSTRUCTOR FLYING"))
                    // Unverified: both switch positions not yet seen on this link.
                    + (trainerChip._plugin && !trainerChip._plugin.trainerVerified
                       ? qsTr("  (flick SH once to confirm the channel)") : "")
        }

        // Tap: choose the channel the instructor's radio mixes the switch onto.
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: trainerChannelPopup.open()
        }

        Popup {
            id: trainerChannelPopup
            closePolicy: trainerChip._plugin && trainerChip._plugin.trainerDetectRunning
                         ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
            onClosed: if (trainerChip._plugin && !trainerChip._plugin.trainerDetectRunning) trainerChip._plugin.cancelTrainerDetect()
            y: parent.height + 6
            x: (parent.width - width) / 2
            padding: 12
            background: Rectangle { color: Colors.surface; border.color: Colors.border; radius: Config.radiusSmall }
            ColumnLayout {
                spacing: 6
                Text {
                    text: qsTr("Trainer switch (SH) channel")
                    font.bold: true
                    font.pixelSize: Config.fontSizeBody
                    color: Colors.textPrimary
                }
                Text {
                    Layout.preferredWidth: 300
                    wrapMode: Text.WordWrap
                    text: qsTr("The channel the instructor's SH switch reaches the flight controller on. "
                               + "Default: SH low = instructor (master) flying, SH high = trainee flying. "
                               + "Not the flight-mode channel.")
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                }
                SpinBox {
                    id: channelSpin
                    from: 5; to: 16
                    value: trainerChip._plugin ? trainerChip._plugin.trainerChannel : 8
                    onValueModified: if (trainerChip._plugin) trainerChip._plugin.trainerChannel = value
                }
                CheckBox {
                    // For a radio wired the other way round.
                    text: qsTr("SH high = instructor")
                    checked: trainerChip._plugin ? trainerChip._plugin.trainerHighIsInstructor : true
                    onToggled: if (trainerChip._plugin) trainerChip._plugin.trainerHighIsInstructor = checked
                }

                // Detect: flick SH while the GCS watches CH5-CH16 for it.
                RowLayout {
                    spacing: 8
                    Button {
                        text: trainerChip._plugin && trainerChip._plugin.trainerDetectRunning
                              ? qsTr("Detecting\u2026 %1 s").arg(trainerChip._plugin.trainerDetectSecondsLeft)
                              : qsTr("Detect")
                        enabled: !!trainerChip._plugin && !trainerChip._plugin.trainerDetectRunning
                        onClicked: trainerChip._plugin.startTrainerDetect()
                    }
                    Button {
                        visible: !!trainerChip._plugin && trainerChip._plugin.trainerDetectRunning
                        text: qsTr("Cancel")
                        onClicked: trainerChip._plugin.cancelTrainerDetect()
                    }
                }
                Text {
                    Layout.preferredWidth: 300
                    visible: text.length > 0
                    wrapMode: Text.WordWrap
                    text: trainerChip._plugin ? trainerChip._plugin.trainerDetectStatus : ""
                    font.pixelSize: Config.fontSizeSmall
                    font.bold: true
                    color: trainerChip._plugin && trainerChip._plugin.trainerDetectFound > 0 ? Colors.success
                         : trainerChip._plugin && trainerChip._plugin.trainerDetectRunning ? Colors.textPrimary
                         : Colors.warning
                }
                RowLayout {
                    visible: !!trainerChip._plugin && trainerChip._plugin.trainerDetectFound > 0
                    spacing: 8
                    Button {
                        text: qsTr("Use CH%1").arg(trainerChip._plugin ? trainerChip._plugin.trainerDetectFound : 0)
                        highlighted: true
                        onClicked: trainerChip._plugin.acceptTrainerDetect()
                    }
                    Button {
                        text: qsTr("Cancel")
                        onClicked: trainerChip._plugin.cancelTrainerDetect()
                    }
                }
            }
        }
    }

    // Handover toast under the trainer chip: appears for 3 s on each
    // instructor <-> trainee switch and fades by itself (no button).
    Rectangle {
        id: trainerToast
        property string text: ""
        property bool student: false
        anchors { top: trainerChip.bottom; topMargin: 6; horizontalCenter: trainerChip.horizontalCenter }
        z: QGroundControl.zOrderWidgets + 4
        width: toastText.implicitWidth + 28
        height: toastText.implicitHeight + 14
        radius: height / 2
        color: student ? Colors.warning : Colors.success
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        Text {
            id: toastText
            anchors.centerIn: parent
            text: trainerToast.text
            font.pixelSize: Config.fontSizeBody
            font.bold: true
            color: Colors.background
        }
        Timer {
            id: toastTimer
            interval: 3000
            onTriggered: trainerToast.opacity = 0
        }
        Connections {
            target: QGroundControl.corePlugin
            ignoreUnknownSignals: true
            function onTrainerHandoverAnnounced(text, student) {
                trainerToast.text = text
                trainerToast.student = student
                trainerToast.opacity = 1
                toastTimer.restart()
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  TESTING MODE BANNER — visible when in testing mode
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: testingBanner
        // Testing mode disables audit logging, so its banner stays highly visible.
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
    //  DATABASE BANNER — flight logging is off; stays until restart
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: databaseBanner
        visible: typeof Database !== "undefined" && Database.startupError !== ""
        anchors {
            top: parent.top
            topMargin: _toolInsets.topEdgeCenterInset + (testingBanner.visible ? testingBanner.height : 0)
            left: parent.left; right: parent.right
        }
        height: dbBannerText.implicitHeight + 16
        z: QGroundControl.zOrderWidgets + 4
        color: Qt.rgba(Colors.danger.r, Colors.danger.g, Colors.danger.b, 0.85)

        Text {
            id: dbBannerText
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Config.spacingSmall }
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("\u26A0 FLIGHT LOGGING IS OFF \u2014 %1").arg(typeof Database !== "undefined" ? Database.startupError : "")
            font.pixelSize: Config.fontSizeBody
            font.bold: true
            color: Colors.background   // text on a status colour, as on the PASS/FAIL badges
        }
    }

    // First unexpected write failure after start-up: records from now on may
    // be incomplete, but flying and arming are unaffected (RC-primary).
    Rectangle {
        id: dbWriteBanner
        visible: typeof Database !== "undefined" && Database.runtimeError !== ""
        anchors {
            top: databaseBanner.visible ? databaseBanner.bottom : parent.top
            topMargin: databaseBanner.visible ? 0
                       : _toolInsets.topEdgeCenterInset + (testingBanner.visible ? testingBanner.height : 0)
            left: parent.left; right: parent.right
        }
        height: dbWriteBannerText.implicitHeight + 16
        z: QGroundControl.zOrderWidgets + 4
        color: Qt.rgba(Colors.danger.r, Colors.danger.g, Colors.danger.b, 0.85)

        Text {
            id: dbWriteBannerText
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Config.spacingSmall }
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("\u26A0 A FLIGHT RECORD WRITE FAILED \u2014 %1. Records from now on may be incomplete. Details in db_write_errors.log.")
                      .arg(typeof Database !== "undefined" ? Database.runtimeError : "")
            font.pixelSize: Config.fontSizeBody
            font.bold: true
            color: Colors.background
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    //  POST-FLIGHT BANNER — appears when state is PostFlight
    // ═══════════════════════════════════════════════════════════════════
    Rectangle {
        id: postFlightBanner
        // FORK PATCH: post-flight is parked for Phase 1 (kPostFlightEnabled =
        // false in Config.h). Restore the binding below when it is re-enabled.
        visible: false // FlightSession.state === FlightSession.PostFlight
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
