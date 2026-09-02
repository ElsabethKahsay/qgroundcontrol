// Component: FlyQuickActions
// Purpose: Compact quick-action button column for the custom Fly view.
//   Buttons are visually consistent pill-shaped controls that mirror the
//   look of the standard QGC ToolStrip (which remains visible on the left).
//   This column sits above/alongside it and adds:
//     Checklist, Flight time, FC status, Events, and Force Arm.
//   Takeoff/Land/RTL/Pause are handled by the standard QGC ToolStrip.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

import QGroundControl
import QGroundControl.ScreenTools

Item {
    id: root

    /// Currently open action ("", "flightTime", "fcStatus", "events").
    property string checkedAction: ""
    /// True while a vehicle is connected; disables non-force-arm buttons.
    property bool hasVehicle: false
    /// True while the vehicle supports takeoff and is on the ground (kept for compat).
    property bool takeoffEnabled: false

    signal actionTriggered(string action)

    implicitWidth: btnCol.implicitWidth
    implicitHeight: btnCol.implicitHeight
    enabled: hasVehicle

    readonly property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    // Explicit armed tracker — updated via Connections so QML re-evaluates
    // even when the vehicle object itself doesn't change reference.
    property bool _vehicleArmed: (_activeVehicle && _activeVehicle.armed) ? true : false
    readonly property bool _isArmed: _vehicleArmed

    Connections {
        target: _activeVehicle
        ignoreUnknownSignals: true
        function onArmedChanged(armed) { root._vehicleArmed = armed }
    }
    // Reset when vehicle disconnects / changes
    Connections {
        target: QGroundControl.multiVehicleManager
        function onActiveVehicleChanged() { if (!_activeVehicle) root._vehicleArmed = false }
    }

    // ── Shared pill button component ────────────────────────────────────
    component QuickButton: Rectangle {
        id: btn
        property string action: ""
        property string glyph:  ""
        property string label:  ""
        property string tooltip: ""
        property bool   isChecked:   root.checkedAction === action
        property bool   isActiveBtn: enabled
        // Danger style (red tint) for force arm
        property bool   isDanger: false

        width:  ScreenTools.defaultFontPixelWidth * 15
        height: ScreenTools.defaultFontPixelHeight * 2.0
        radius: height / 2
        opacity: isActiveBtn ? 1.0 : 0.40

        color: {
            if (!isActiveBtn)      return Colors.surface
            if (isDanger) {
                if (_btnMA.containsMouse) return Qt.rgba(0.85, 0.15, 0.15, 0.90)
                return Qt.rgba(0.70, 0.10, 0.10, 0.82)
            }
            if (isChecked)         return Colors.teal
            if (_btnMA.containsMouse) return Colors.surfaceLight
            return Colors.surface
        }
        border.color: {
            if (!isActiveBtn) return Colors.border
            if (isDanger)     return Qt.rgba(1.0, 0.35, 0.35, 0.80)
            if (isChecked)    return Colors.accentCyan
            if (_btnMA.containsMouse) return Colors.accentCyan
            return Qt.rgba(Colors.accentCyan.r, Colors.accentCyan.g, Colors.accentCyan.b, 0.38)
        }
        border.width: 1

        Behavior on color       { ColorAnimation { duration: 110 } }
        Behavior on border.color{ ColorAnimation { duration: 110 } }

        RowLayout {
            anchors.centerIn: parent
            spacing: ScreenTools.defaultFontPixelWidth * 0.55
            width: parent.width - ScreenTools.defaultFontPixelWidth * 1.4

            Text {
                text: btn.glyph
                font.pixelSize: Config.fontSizeBody
                color: (btn.isDanger || btn.isChecked) ? Colors.dialogText
                       : (btn.isActiveBtn ? Colors.textPrimary : Colors.textDisabled)
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                text: btn.label
                font.pixelSize: Config.fontSizeSmall
                font.weight:    Font.DemiBold
                color: (btn.isDanger || btn.isChecked) ? Colors.dialogText
                       : (btn.isActiveBtn ? Colors.textPrimary : Colors.textDisabled)
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                elide: Text.ElideRight
            }

            // Status badge — only shown on the Checklist button
            Rectangle {
                visible: btn.action === "checklist"
                         && root.hasVehicle
                         && typeof PreflightManager !== 'undefined'
                         && typeof PreflightChecklistModel !== 'undefined'
                width:  badgeTxt.implicitWidth + ScreenTools.defaultFontPixelWidth * 0.8
                height: ScreenTools.defaultFontPixelHeight * 1.1
                radius: height / 2
                Layout.alignment: Qt.AlignVCenter
                color: {
                    if (typeof PreflightChecklistModel === 'undefined') return "transparent"
                    var f = PreflightChecklistModel.blockingFailedCount
                    if (f > 0)  return Colors.dialogFocus
                    var p = typeof PreflightManager !== 'undefined' ? PreflightManager.pendingChecks : 0
                    if (p > 0)  return Colors.warning
                    return Colors.success
                }
                Text {
                    id: badgeTxt
                    anchors.centerIn: parent
                    font.pointSize: ScreenTools.defaultFontPointSize * 0.75
                    font.weight:    Font.Bold
                    color:          Colors.dialogText
                    text: {
                        if (typeof PreflightChecklistModel === 'undefined') return ""
                        var f = PreflightChecklistModel.blockingFailedCount
                        if (f > 0) return f.toString()
                        var p = typeof PreflightManager !== 'undefined' ? PreflightManager.pendingChecks : 0
                        if (p > 0) return p.toString()
                        return "OK"
                    }
                }
            }
        }

        MouseArea {
            id: _btnMA
            anchors.fill: parent
            hoverEnabled: true
            cursorShape:  Qt.PointingHandCursor
            enabled:      btn.isActiveBtn

            ToolTip.visible: containsMouse
            ToolTip.text:    btn.tooltip
            ToolTip.delay:   400

            onClicked: root.actionTriggered(btn.action)
        }
    }

    // ── Button column ────────────────────────────────────────────────────
    Column {
        id: btnCol
        spacing: Config.spacingSmall

        // 1. Preflight Checklist
        QuickButton {
            action:   "checklist"
            glyph:    "\u2713"
            label:    qsTr("Checklist")
            tooltip:  qsTr("Open Preflight Checklist")
            enabled:  root.hasVehicle
        }

        // 2. Flight Time detail popover
        QuickButton {
            action:   "flightTime"
            glyph:    "\u23F1"
            label:    qsTr("Flight time")
            tooltip:  qsTr("Battery & remaining flight time")
            enabled:  root.hasVehicle
        }

        // 3. Battery Config modal button
        QuickButton {
            action:   "batConfig"
            glyph:    "\uD83D\uDD0B"
            label:    qsTr("Bat Config")
            tooltip:  qsTr("Configure battery type & cell count for flight time calculation")
            enabled:  root.hasVehicle
        }

        // 4. FC Status panel
        QuickButton {
            action:   "fcStatus"
            glyph:    "\u2699\uFE0F"
            label:    qsTr("FC status")
            tooltip:  qsTr("Flight controller status summary")
            enabled:  root.hasVehicle
        }

        // 5. Event log panel
        QuickButton {
            action:   "events"
            glyph:    "\u2630"
            label:    qsTr("Events")
            tooltip:  qsTr("Vehicle message / event log")
            enabled:  root.hasVehicle
        }

        // ── Divider ────────────────────────────────────────────────────
        Rectangle {
            width:  ScreenTools.defaultFontPixelWidth * 15
            height: 1
            color:  Qt.rgba(Colors.border.r, Colors.border.g, Colors.border.b, 0.5)
        }

        // 6. Force Arm — red danger button.
        QuickButton {
            id:        forceArmBtn
            action:    "forceArm"
            glyph:     "\u26A0\uFE0F"
            label:     root._isArmed ? qsTr("Armed \u2713") : qsTr("Force Arm")
            tooltip:   qsTr("Bypass pre-arm checks and arm immediately (dangerous!)")
            isDanger:  !root._isArmed
            isChecked: root._isArmed
            enabled:   root.hasVehicle && !root._isArmed
        }

        // 7. Disarm — enabled only when armed.
        QuickButton {
            id:        disarmBtn
            action:    "disarm"
            glyph:     "\uD83D\uDED1"
            label:     qsTr("Disarm")
            tooltip:   qsTr("Disarm the vehicle")
            isDanger:  true
            isChecked: false
            enabled:   root.hasVehicle && root._isArmed
        }

        // 8. Mode toggle — MANUAL ↔ AUTO, label shows live FC mode.
        QuickButton {
            id:        modeBtn
            action:    "switchManual"
            glyph:     "\u2708\uFE0F"
            label: {
                var fm = root._activeVehicle ? root._activeVehicle.flightMode : ""
                if (!fm) return qsTr("Mode: --")
                return qsTr("Mode: %1").arg(fm)
            }
            tooltip:   qsTr("Toggle MANUAL / AUTO flight mode")
            isDanger:  false
            isChecked: {
                var fm = root._activeVehicle ? root._activeVehicle.flightMode : ""
                return fm === "MANUAL" || fm === "Manual" || fm === "STABILIZE" || fm === "Stabilize"
            }
            enabled:   root.hasVehicle
        }
    }

    // ── Mode-change toast notification ─────────────────────────────────────
    // Shown for 5 minutes (300 s) after a manual mode switch.
    property string _lastModeLabel: ""
    property int    _modeToastSecsLeft: 0

    Timer {
        id: modeToastTimer
        interval: 1000
        repeat:   true
        onTriggered: {
            root._modeToastSecsLeft -= 1
            if (root._modeToastSecsLeft <= 0) {
                modeToastTimer.stop()
                root._modeToastSecsLeft = 0
            }
        }
    }

    Rectangle {
        id:      modeToast
        visible: root._modeToastSecsLeft > 0
        width:   modeToastRow.implicitWidth + ScreenTools.defaultFontPixelWidth * 3
        height:  ScreenTools.defaultFontPixelHeight * 2.2
        radius:  height / 2
        color:   Qt.rgba(0.08, 0.55, 0.35, 0.93)
        border.color: Qt.rgba(0.2, 1.0, 0.6, 0.55)
        border.width: 1
        // Position below the button column, flush left
        x: 0
        y: btnCol.y + btnCol.height + ScreenTools.defaultFontPixelHeight * 0.5
        z: QGroundControl.zOrderWidgets + 5

        Behavior on opacity { NumberAnimation { duration: 200 } }
        opacity: root._modeToastSecsLeft > 0 ? 1.0 : 0.0

        Row {
            id:               modeToastRow
            anchors.centerIn: parent
            spacing:          ScreenTools.defaultFontPixelWidth * 0.6

            Text {
                text:           "\u2708\uFE0F"
                font.pixelSize: Config.fontSizeBody
                color:          "white"
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: {
                    var m = Math.floor(root._modeToastSecsLeft / 60)
                    var s = root._modeToastSecsLeft % 60
                    var ts = m + ":" + (s < 10 ? "0" + s : s)
                    return qsTr("Mode: ") + root._lastModeLabel + "  (" + ts + ")"
                }
                font.pixelSize: Config.fontSizeSmall
                font.weight:    Font.DemiBold
                color:          "white"
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ── Force-Arm confirm dialog ──────────────────────────────────────────
    Dialog {
        id:              forceArmDialog
        title:           qsTr("Force Arm \u2014 Are you sure?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal:           true
        closePolicy:     Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width:           340

        Column {
            width:   parent.width
            spacing: 8
            Text {
                width:    parent.width
                wrapMode: Text.WordWrap
                text:     qsTr("This will arm the vehicle immediately, bypassing ALL pre-arm safety checks.\n\nOnly use this if you are certain the vehicle is safe to arm.")
                font.pixelSize: Config.fontSizeSmall
                color:    Colors.textPrimary
            }
            Text {
                width:    parent.width
                wrapMode: Text.WordWrap
                text:     qsTr("\u26A0 Propellers will spin. Keep clear of the vehicle.")
                font.pixelSize: Config.fontSizeSmall
                font.weight:    Font.Bold
                color:    Qt.rgba(1.0, 0.4, 0.2, 1.0)
            }
        }

        onAccepted: {
            if (typeof ArmingGate !== 'undefined' && ArmingGate)
                ArmingGate.forceArm()
            var v = root._activeVehicle
            if (v && typeof v.forceArm === "function")
                v.forceArm()
            if (typeof TelemetryProvider !== 'undefined' && TelemetryProvider)
                TelemetryProvider.forceArm()
        }
    }

    // ── Disarm confirm dialog ──────────────────────────────────────────────
    Dialog {
        id:              disarmDialog
        title:           qsTr("Disarm vehicle?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal:           true
        closePolicy:     Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width:           300

        Text {
            width:    parent.width
            wrapMode: Text.WordWrap
            text:     qsTr("Are you sure you want to disarm the vehicle?\nMotors will stop immediately.")
            font.pixelSize: Config.fontSizeSmall
            color:    Colors.textPrimary
        }

        onAccepted: {
            var v = root._activeVehicle
            var isFlying = v ? (v.flying || v.armed) : false
            if (typeof VehicleProfileManager !== 'undefined' && VehicleProfileManager) {
                VehicleProfileManager.disarmVehicle(isFlying)
            } else if (v) {
                v.armed = false
            }
        }
    }

    // ── Battery Config dialog ─────────────────────────────────────────────
    Dialog {
        id:              batteryConfigDialog
        title:           qsTr("Battery Configuration")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal:           true
        closePolicy:     Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width:           360

        ColumnLayout {
            width:   parent.width
            spacing: Config.spacingMedium

            Text {
                text: qsTr("Set pack configuration & chemistry for flight time calculation:")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textPrimary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("Type:")
                    font.pixelSize: Config.fontSizeSmall
                    font.weight: Font.DemiBold
                    color: Colors.textSecondary
                    Layout.preferredWidth: 110
                }

                ComboBox {
                    id: batTypeCombo
                    Layout.fillWidth: true
                    model: ["LiPo", "LiHV", "LiIon", "LiFe"]
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("Series (S):")
                    font.pixelSize: Config.fontSizeSmall
                    font.weight: Font.DemiBold
                    color: Colors.textSecondary
                    Layout.preferredWidth: 110
                }

                SpinBox {
                    id: cellCountSpin
                    Layout.fillWidth: true
                    from: 1
                    to: 24
                    editable: true
                    value: 6
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("Parallel (P):")
                    font.pixelSize: Config.fontSizeSmall
                    font.weight: Font.DemiBold
                    color: Colors.textSecondary
                    Layout.preferredWidth: 110
                }

                SpinBox {
                    id: parallelSpin
                    Layout.fillWidth: true
                    from: 1
                    to: 16
                    editable: true
                    value: 1
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("Capacity (mAh):")
                    font.pixelSize: Config.fontSizeSmall
                    font.weight: Font.DemiBold
                    color: Colors.textSecondary
                    Layout.preferredWidth: 110
                }

                SpinBox {
                    id: capacitySpin
                    Layout.fillWidth: true
                    from: 100
                    to: 100000
                    stepSize: 500
                    editable: true
                    value: 5000
                }
            }
        }

        onAboutToShow: {
            if (typeof VehicleProfileManager !== 'undefined' && VehicleProfileManager) {
                var currentType = VehicleProfileManager.batteryType || "LiPo"
                var idx = batTypeCombo.model.indexOf(currentType)
                batTypeCombo.currentIndex = idx >= 0 ? idx : 0

                var series = VehicleProfileManager.batterySeriesCells
                cellCountSpin.value = series > 0 ? series : 6

                var parallel = VehicleProfileManager.batteryParallelCells
                parallelSpin.value = parallel > 0 ? parallel : 1

                var mah = VehicleProfileManager.batteryCellMah
                capacitySpin.value = mah > 0 ? mah : 5000
            }
        }

        onAccepted: {
            var selectedType = batTypeCombo.currentText
            var sCount = cellCountSpin.value
            var pCount = parallelSpin.value
            var mahVal = capacitySpin.value

            if (typeof VehicleProfileManager !== 'undefined' && VehicleProfileManager) {
                VehicleProfileManager.batteryType = selectedType
                VehicleProfileManager.batterySeriesCells = sCount
                VehicleProfileManager.batteryParallelCells = pCount
                VehicleProfileManager.batteryCellMah = mahVal
            }
            root._lastModeLabel = "Battery: " + sCount + "S " + pCount + "P " + mahVal + " mAh (" + selectedType + ")"
            root._modeToastSecsLeft = 10
            modeToastTimer.restart()
        }
    }

    // ── Route actionTriggered ──────────────────────────────────────────────
    Connections {
        target: root
        function onActionTriggered(action) {
            if (action === "forceArm") {
                forceArmDialog.open()
            } else if (action === "disarm") {
                disarmDialog.open()
            } else if (action === "batConfig") {
                batteryConfigDialog.open()
            } else if (action === "switchManual") {
                var vv = root._activeVehicle
                if (vv) {
                    var currentMode = vv.flightMode
                    var isManualLike = (currentMode === "MANUAL" || currentMode === "Manual" || currentMode === "STABILIZE" || currentMode === "Stabilize" || currentMode === "POSHOLD" || currentMode === "Position")
                    var targetMode = isManualLike ? "AUTO" : "MANUAL"
                    if (typeof VehicleProfileManager !== 'undefined' && VehicleProfileManager) {
                        VehicleProfileManager.setVehicleFlightMode(targetMode)
                    } else {
                        vv.flightMode = targetMode
                    }
                    root._lastModeLabel = targetMode
                    root._modeToastSecsLeft = 300
                    modeToastTimer.restart()
                }
            }
        }
    }
}