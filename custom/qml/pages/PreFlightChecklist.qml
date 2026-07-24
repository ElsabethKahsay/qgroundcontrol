import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0
import cpts 1.0 as CE

Page {
    id: root

    CE.ChecklistEngine {
        id: _engine
        viewMode: "standalone"
    }

    // ── Responsive layout ──
    readonly property bool isSingle: width < Config.breakpointSingle

    // ── Auto-fetch weather on page load (deferred) ──
    Component.onCompleted: {
        if (typeof WeatherProvider !== 'undefined' && TelemetryProvider) {
            var lat = TelemetryProvider.gpsLatitude
            var lon = TelemetryProvider.gpsLongitude
            if (lat !== 0 || lon !== 0)
                Qt.callLater(function() { WeatherProvider.fetchWeather(lat, lon) })
        }
    }

    property real _lastFetchLat: 0
    property real _lastFetchLon: 0
    property real _lastFetchTime: 0

    Connections {
        target: TelemetryProvider
        function onGpsLatitudeChanged() {
            var lat = TelemetryProvider.gpsLatitude
            var lon = TelemetryProvider.gpsLongitude
            if (lat === 0 && lon === 0) return
            var now = Date.now()
            if (now - _lastFetchTime < 30000) return
            var dLat = lat - _lastFetchLat
            var dLon = lon - _lastFetchLon
            if (dLat * dLat + dLon * dLon < 0.0001) return
            _lastFetchLat = lat
            _lastFetchLon = lon
            _lastFetchTime = now
            WeatherProvider.fetchWeather(lat, lon)
        }
    }

    // ── Collapsible group state ──
    property var _collapsed: ({})
    function _clone(obj) { var c = {}; for (var k in obj) c[k] = obj[k]; return c }
    function toggleGroup(idx) {
        _collapsed[idx] = !_collapsed[idx]
        _collapsed = _clone(_collapsed)
    }
    property int _highlightIndex: -1
    property string _highlightCheckId: ""

    background: Rectangle { color: Colors.background }

    // ── Dialogs ──
    Dialog {
        id: motorConfigDialog
        title: qsTr("ArduPilot Motor Setup")
        modal: true; standardButtons: Dialog.Yes | Dialog.No
        anchors.centerIn: parent
        width: Math.min(480, parent.width * 0.9)
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Colors.surface; border.color: Colors.border; border.width: 1; radius: Config.radiusMedium }
        padding: Config.spacingMedium
        header: Label { text: motorConfigDialog.title; font.bold: true; color: Colors.accent; padding: Config.spacingMedium }
        Text {
            wrapMode: Text.Wrap
            color: Colors.textPrimary
            font.pixelSize: 25
            text: TelemetryProvider.motorConfigWarning + "\n\n" +
                  "Setup will apply:\n" +
                  "\u2022 FRAME_CLASS = 1 (Quad)\n" +
                  "\u2022 FRAME_TYPE  = 1 (X)\n" +
                  "\u2022 SERVO1-4_FUNCTION = 33-36\n" +
                  "\u2022 BRD_SAFETYENABLE = 0\n\n" +
                  "After setup:\n" +
                  "1. Ensure vehicle is DISARMED\n" +
                  "2. Tap \"Motor Test\" in checklist\n" +
                  "3. Motors will spin ~2s then stop\n\n" +
                  "Proceed?"
        }
        onAccepted: TelemetryProvider.setupMotorConfig()
        onRejected: console.log("Motor setup skipped")
    }

    Dialog {
        id: overrideDialog
        title: qsTr("Override & Arm")
        modal: true; standardButtons: Dialog.Yes | Dialog.No
        anchors.centerIn: parent
        width: Math.min(400, parent.width * 0.9)
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Colors.surface; border.color: Colors.error; border.width: 1; radius: Config.radiusMedium }
        header: Label { text: overrideDialog.title; font.bold: true; color: Colors.error; padding: Config.spacingMedium }
        padding: Config.spacingMedium
        ColumnLayout {
            spacing: Config.spacingMedium
            Text { text: "There are " + _engine.criticalChecks + " critical failure(s) and " + _engine.pendingChecks + " pending check(s).  Override will bypass all safety gates and arm the vehicle."; color: Colors.textPrimary; font.pixelSize: 25; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "This should only be used in controlled testing environments."; color: Colors.error; font.pixelSize: 25; font.bold: true }
        }
        onAccepted: {
            ArmingGate.overrideGate("Operator override via dialog")
            TelemetryProvider.arm()
        }
        onRejected: console.log("Override cancelled")
    }

    // ── Flash timer for blocker highlight ──
    Timer {
        id: flashTimer
        interval: 1500; repeat: false
        onTriggered: { _highlightIndex = -1; _highlightCheckId = "" }
    }

    // ── Force Arm confirmation dialog ──
    Dialog {
        id: forceArmDialog
        title: qsTr("Force Arm")
        modal: true
        anchors.centerIn: parent
        width: Math.min(480, parent.width * 0.9)
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Colors.surface; border.color: Colors.errorDim; border.width: 1; radius: Config.radiusMedium }
        header: Label { text: forceArmDialog.title; font.bold: true; color: Colors.textPrimary; padding: Config.spacingMedium }
        padding: Config.spacingMedium

        ColumnLayout {
            spacing: Config.spacingMedium

            Text {
                text: qsTr("Force Arm overrides the following blocker(s):")
                color: Colors.textPrimary
                font.pixelSize: 25
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Repeater {
                model: PreflightManager.blockingChecks
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    color: Colors.surfaceLight
                    radius: Config.radiusSmall

                    RowLayout {
                        anchors.fill: parent; anchors.margins: Config.spacingSmall
                        spacing: Config.spacingSmall

                        Text { text: "\u2717"; color: Colors.error; font.bold: true }
                        Text {
                            text: modelData.label
                            color: Colors.textPrimary
                            font.pixelSize: 25
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingMedium

                Button {
                    text: qsTr("Cancel")
                    Layout.fillWidth: true
                    implicitHeight: 32
                    onClicked: forceArmDialog.close()
                }

                Button {
                    id: forceArmConfirmBtn
                    text: qsTr("Force Arm Anyway")
                    Layout.fillWidth: true
                    implicitHeight: 32
                    background: Rectangle { color: Colors.error; radius: Config.radiusSmall }
                    contentItem: Text {
                        text: forceArmConfirmBtn.text
                        font.bold: true
                        color: Colors.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        ArmingGate.forceArm()
                        TelemetryProvider.arm()
                        forceArmDialog.close()
                    }
                }
            }
        }
    }

    // ── Mode picker dialog ──
    Dialog {
        id: modePickerDialog
        title: qsTr("Select Gate Mode")
        modal: true
        anchors.centerIn: parent
        width: Math.min(320, parent.width * 0.8)
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Colors.surface; border.color: Colors.border; border.width: 1; radius: Config.radiusMedium }
        padding: Config.spacingMedium

        ColumnLayout {
            spacing: Config.spacingSmall

            Repeater {
                model: [
                    { mode: 0, name: "PASSIVE", desc: "Observe only \u2014 no arming restrictions" },
                    { mode: 2, name: "HYBRID",  desc: "Block arming, operator can override" },
                    { mode: 1, name: "ACTIVE",  desc: "Full enforcement \u2014 block on failures" }
                ]

                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: ArmingGate.mode === modelData.mode ? Colors.accentDim : "transparent"
                    radius: Config.radiusSmall
                    border.color: ArmingGate.mode === modelData.mode ? Colors.accent : Colors.borderLight
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent; anchors.margins: Config.spacingSmall
                        spacing: Config.spacingSmall

                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 1
                            Text {
                                text: modelData.name
                                font.bold: true
                                font.pixelSize: 25
                                color: Colors.textPrimary
                            }
                            Text {
                                text: modelData.desc
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textSecondary
                            }
                        }

                        Text {
                            text: qsTr("\u2713")
                            visible: ArmingGate.mode === modelData.mode
                            color: Colors.accent
                            font.bold: true
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            ArmingGate.mode = modelData.mode
                            modePickerDialog.close()
                        }
                    }
                }
            }
        }
    }

    // ── Scroll to first blocker and flash ──
    function scrollToFirstBlocker() {
        var checks = PreflightManager.checks
        if (!checks || checks.length === 0) return

        var blockerIdx = -1
        var blockerCat = -1
        for (var i = 0; i < checks.length; ++i) {
            var s = checks[i].status
            if (s === 2 || s === 4) {
                blockerIdx = i
                blockerCat = checks[i].checkCategory
                break
            }
        }
        if (blockerIdx < 0) return

        _highlightIndex = blockerIdx
        _highlightCheckId = checks[blockerIdx].checkId
        flashTimer.restart()

        var targetGroupIdx = -1
        for (var g = 0; g < _engine.catGroups.length; ++g) {
            if (_engine.catGroups[g].cats.indexOf(blockerCat) >= 0) {
                targetGroupIdx = g
                break
            }
        }

        if (targetGroupIdx >= 0 && root._collapsed[targetGroupIdx] !== false)
            root.toggleGroup(targetGroupIdx)

        var yPos = 0
        for (var j = 0; j < targetGroupIdx; ++j) {
            yPos += 36 + Config.spacingSmall + 8
            var catsJ = _engine.catGroups[j].cats
            var countJ = 0
            for (var c = 0; c < checks.length; ++c) {
                if (catsJ.indexOf(checks[c].checkCategory) >= 0 && checks[c].status !== 1)
                    countJ++
            }
            yPos += Math.ceil(countJ / 2) * 56
        }
        yPos += 36 + Config.spacingSmall
        var catsT = _engine.catGroups[targetGroupIdx].cats
        var countT = 0
        for (var c2 = 0; c2 < blockerIdx; ++c2) {
            if (catsT.indexOf(checks[c2].checkCategory) >= 0 && checks[c2].status !== 1)
                countT++
        }
        yPos += Math.floor(countT / 2) * 56

        checklistFlickable.contentY = Math.max(0, yPos - 100)
    }

    Component.onCompleted: {
        if (TelemetryProvider.motorConfigWarning.length > 0)
            motorConfigDialog.open()
    }

    // ──────────────────────────────────────────────────────────
    // MAIN LAYOUT: 35% telemetry | 65% checklist
    // ──────────────────────────────────────────────────────────
    RowLayout {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingLarge

        // ══════════════════════════════════════════════════════
        // LEFT: LIVE TELEMETRY DASHBOARD (35%)
        // ══════════════════════════════════════════════════════
        Rectangle {
            visible: !isSingle
            Layout.preferredWidth: parent.width * Config.kTelemetryPanelRatio
            Layout.fillHeight: true
            color: Colors.surface
            radius: Config.radiusMedium
            border.color: Colors.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingMedium
                spacing: Config.spacingMedium

                // Header
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: Colors.surfaceLight
                    radius: Config.radiusSmall
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: Config.spacingMedium; anchors.rightMargin: Config.spacingMedium
                        Text { text: "\uD83D\uDCCA Live Telemetry"; font.pixelSize: 25; font.bold: true; color: Colors.textPrimary; Layout.fillWidth: true }
                        Text { text: TelemetryProvider.isConnected ? "\u25CF Connected" : "\u25CB Disconnected"; font.pixelSize: 25; color: TelemetryProvider.isConnected ? Colors.success : Colors.error; font.bold: true }
                    }
                }

                // Scrollable telemetry content
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: telCol.implicitHeight + Config.spacingXLarge
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: telCol
                        width: parent.width
                        spacing: 20
                        anchors.margins: 10

                        // ── Telemetry cards ──
                        TelRow { label: "Power"; accent: Colors.pastelGreen
                            TelVal { text: TelemetryProvider.batteryVoltage.toFixed(2) + " V"; color: TelemetryProvider.batteryVoltage >= 15.0 ? Colors.success : TelemetryProvider.batteryVoltage >= 13.5 ? Colors.warning : Colors.error; bold: true }
                            TelSep {}
                            TelVal { text: TelemetryProvider.batteryCurrent.toFixed(1) + " A" }
                            TelSep {}
                            TelVal { text: TelemetryProvider.batteryPercent + "%" }
                            TelSep {}
                            TelVal { text: (TelemetryProvider.batteryVoltage / 4.2).toFixed(0) + "S"; bold: true }
                        }

                        TelRow { label: "GPS"; accent: Colors.pastelBlue
                            TelVal { text: TelemetryProvider.gpsFixTypeString; color: TelemetryProvider.gpsFixType >= 3 ? Colors.success : Colors.warning; bold: true }
                            TelSep {}
                            TelVal { text: TelemetryProvider.gpsSatellites + " sats"; color: TelemetryProvider.gpsSatellites >= 8 ? Colors.success : TelemetryProvider.gpsSatellites >= 5 ? Colors.warning : Colors.error }
                            TelSep {}
                            TelVal { text: "HDOP " + TelemetryProvider.gpsHdop.toFixed(1); color: TelemetryProvider.gpsHdop < 1.0 ? Colors.success : TelemetryProvider.gpsHdop < 2.0 ? Colors.warning : Colors.error }
                        }

                        TelRow { label: "Link"; accent: Colors.info
                            TelVal { text: VehicleTelemetry.connectionQuality + "%"; color: VehicleTelemetry.connectionQuality >= Config.connQualityGood ? Colors.success : VehicleTelemetry.connectionQuality > Config.connQualityDegraded ? Colors.warning : Colors.error; bold: true }
                            TelSep {}
                            TelVal { text: TelemetryProvider.isConnected ? TelemetryProvider.autopilotType : "Disconnected"; color: TelemetryProvider.isConnected ? Colors.success : Colors.error }
                        }

                        // ── Launch location entry ──
                        TelRow { label: "Location"; accent: Colors.pastelPink
                            Rectangle {
                                Layout.preferredWidth: 160; Layout.preferredHeight: 28
                                radius: Config.radiusSmall
                                border.color: Colors.border; border.width: 1
                                color: Colors.surfaceLight
                                TextInput {
                                    id: locationInput
                                    anchors.fill: parent; anchors.margins: 2
                                    font.pixelSize: 25; font.family: "monospace"
                                    horizontalAlignment: TextInput.AlignLeft; verticalAlignment: TextInput.AlignVCenter
                                    color: Colors.textPrimary
                                    text: VehicleProfileManager.currentLocationName
                                    placeholderText: qsTr("e.g. North Field")
                                    onEditingFinished: {
                                        VehicleProfileManager.setLocationName(text)
                                    }
                                }
                            }
                        }

                        TelRow { label: "Attitude"; accent: Colors.pastelPurple
                            TelVal { text: "R " + TelemetryProvider.roll.toFixed(1) + "\u00B0"; bold: true }
                            TelSep {}
                            TelVal { text: "P " + TelemetryProvider.pitch.toFixed(1) + "\u00B0"; bold: true }
                            TelSep {}
                            TelVal { text: "Y " + TelemetryProvider.heading.toFixed(1) + "\u00B0"; bold: true }
                        }

                        TelRow { label: "Compass"; accent: Colors.pastelPink
                            TelVal { text: TelemetryProvider.compassDataQuality === 0 ? "\u2713 OK" : TelemetryProvider.compassDataQuality === 3 ? "\u23F3" : "\u2717"; color: TelemetryProvider.compassDataQuality === 0 ? Colors.success : Colors.warning }
                            TelSep {}
                            TelVal { text: TelemetryProvider.magFieldStrength.toFixed(0) + " mG" }
                        }

                        TelRow { label: "EKF"; accent: Colors.warning
                            TelVal { text: TelemetryProvider.ekfStatus; color: TelemetryProvider.ekfStatus === "OK" ? Colors.success : Colors.warning; bold: true }
                            TelSep {}
                            TelVal { text: "V " + TelemetryProvider.ekfVariance.toFixed(2); color: TelemetryProvider.ekfVariance < 1.0 ? Colors.success : Colors.error }
                            TelSep {}
                            TelVal { text: "H " + TelemetryProvider.ekfHorizAccuracy.toFixed(1) + "m" }
                        }

                        TelRow { label: "RC"; accent: Colors.info
                            TelVal { text: TelemetryProvider.rcConnected ? "\u2713 Link" : "\u2717 No RC"; color: TelemetryProvider.rcConnected ? Colors.success : Colors.error; bold: true }
                            TelSep {}
                            TelVal { text: "RSSI " + TelemetryProvider.rcRssi + "%"; color: TelemetryProvider.rcRssi >= 50 ? Colors.success : TelemetryProvider.rcRssi >= 30 ? Colors.warning : Colors.error }
                        }

                        TelRow { label: "State"; accent: Colors.info
                            TelVal { text: TelemetryProvider.armed ? "Armed" : "Disarmed"; color: TelemetryProvider.armed ? Colors.warning : Colors.success; bold: true }
                            TelSep {}
                            TelVal { text: TelemetryProvider.flightMode }
                            TelSep {}
                            TelVal { text: TelemetryProvider.armed ? Math.floor(TelemetryProvider.flightTime / 60) + "m " + Math.floor(TelemetryProvider.flightTime % 60) + "s" : "0m 0s" }
                        }

                        TelRow { label: "Weather"; accent: Colors.warning
                            TelVal { text: WeatherProvider.loading ? "..." : (WeatherProvider.temperature.toFixed(1) + "\u00B0C"); bold: true }
                            TelSep {}
                            TelVal { text: WeatherProvider.loading ? "..." : WeatherProvider.weatherDescription; color: Colors.info }
                            TelSep {}
                            TelVal { text: WeatherProvider.loading ? "..." : (WeatherProvider.humidity.toFixed(0) + "% RH") }
                        }

                        TelRow { label: "Wind"; accent: Colors.pastelGreen
                            TelVal { text: WeatherProvider.loading ? "..." : (WeatherProvider.windSpeed.toFixed(1) + " m/s"); bold: true }
                            TelSep {}
                            TelVal { text: WeatherProvider.loading ? "..." : (WeatherProvider.windDirection.toFixed(0) + "\u00B0") }
                            TelSep {}
                            TelVal { text: "Gust " + (WeatherProvider.windGust > 0 ? WeatherProvider.windGust.toFixed(1) + " m/s" : "N/A") }
                        }

                        TelRow { label: "Visibility"; accent: Colors.pastelPurple
                            TelVal { text: WeatherProvider.loading ? "..." : (WeatherProvider.visibilityKm.toFixed(1) + " km"); bold: true }
                            TelSep {}
                            TelVal { text: WeatherProvider.loading ? "..." : (WeatherProvider.ceilingFt > 0 ? WeatherProvider.ceilingFt + " ft ceiling" : "No ceiling data") }
                        }

                        TelRow { label: "Speed"; accent: Colors.pastelBlue
                            TelVal { text: "Air " + TelemetryProvider.airspeed.toFixed(1); bold: true }
                            TelSep {}
                            TelVal { text: "Gnd " + TelemetryProvider.groundSpeed.toFixed(1); bold: true }
                        }

                        TelRow { label: "Health"; accent: Colors.success
                            preComps: [
                                TelHealth { label: "IMU"; ok: TelemetryProvider.imuDataQuality === 0 },
                                TelHealth { label: "Mag"; ok: TelemetryProvider.compassDataQuality === 0 },
                                TelHealth { label: "EKF"; ok: ekfOk() },
                                TelHealth { label: "PreArm"; ok: TelemetryProvider.preArmOk }
                            ]
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }

        // ══════════════════════════════════════════════════════
        // RIGHT: CHECKLIST WITH CATEGORY GROUPS (65%)
        // ══════════════════════════════════════════════════════
        Rectangle {
            Layout.preferredWidth: parent.width * Config.kChecklistPanelRatio
            Layout.fillHeight: true
            color: Colors.surface
            radius: Config.radiusMedium
            border.color: Colors.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // ── Header ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: Colors.surfaceLight
                    radius: Config.radiusMedium
                    border.color: Colors.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent; anchors.margins: Config.spacingMedium
                        spacing: Config.spacingMedium

                        Text { text: "\uD83D\uDD0D Checklist"; font.pixelSize: 25; font.bold: true; color: Colors.textPrimary }
                        Text { text: PreflightManager.passedChecks + "/" + PreflightManager.totalChecks; font.pixelSize: 25; color: Colors.textSecondary }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 60; height: 24; radius: Config.radiusSmall
                            color: VehicleTelemetry.connectionQuality >= Config.connQualityGood ? Colors.successDim : Colors.errorDim
                            border.color: VehicleTelemetry.connectionQuality >= Config.connQualityGood ? Colors.success : Colors.error; border.width: 1
                            Text { anchors.centerIn: parent; text: "\uD83D\uDCE1 " + VehicleTelemetry.connectionQuality + "%"; font.pixelSize: 25; font.bold: true; color: VehicleTelemetry.connectionQuality >= Config.connQualityGood ? Colors.success : Colors.error }
                        }
                    }
                }

                // \u2500\u2500 Top-level summary box (pink/purple theme) \u2500\u2500
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    Layout.leftMargin: Config.spacingSmall
                    Layout.rightMargin: Config.spacingSmall
                    Layout.topMargin: 4
                    radius: 10
                    visible: PreflightManager.totalChecks > 0

                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Colors.dialogSurface }
                        GradientStop { position: 0.5; color: Colors.dialogSurface }
                        GradientStop { position: 1.0; color: Colors.dialogSurface }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14; anchors.rightMargin: 10
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: PreflightManager.passedChecks + "/" + PreflightManager.totalChecks + " checks passed"
                                font.pixelSize: 25; font.bold: true; color: Colors.dialogHighlight
                            }
                            Text {
                                text: _engine.criticalChecks > 0 ? _engine.criticalChecks + " critical issue" + (_engine.criticalChecks !== 1 ? "s" : "") : "No critical issues"
                                font.pixelSize: 25
                                color: _engine.criticalChecks > 0 ? Colors.checkFailLight : Colors.checkPassLight
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            width: 32; height: 32; radius: 16
                            color: "transparent"
                            border.color: Colors.checkAccent; border.width: 2
                            Text {
                                anchors.centerIn: parent
                                text: PreflightManager.completionPercent + "%"
                                font.pixelSize: 25; font.bold: true; color: Colors.checkAccent
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                // ── Stale banner ──
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 32 : 0
                    color: Colors.checkWarnDim
                    visible: VehicleTelemetry.telemetryStale && !VehicleTelemetry.disconnectGuardActive
                    RowLayout {
                        anchors.fill: parent; anchors.margins: Config.spacingMedium
                        Text { text: "\u26A0 Stale data"; font.pixelSize: 25; color: Colors.checkWarn; font.bold: true }
                        Text { text: Math.floor((new Date() - VehicleTelemetry.lastTelemetryUpdate) / 1000) + "s ago"; font.pixelSize: 25; color: Colors.checkWarn }
                    }
                }

                // ── Scrollable groups ──
                Flickable {
                    id: checklistFlickable
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: grpCol.implicitHeight + Config.spacingMedium
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: grpCol
                        width: parent.width
                        spacing: Config.spacingSmall
                        anchors.margins: Config.spacingSmall

                        Repeater {
                            model: _engine.catGroups

                            delegate: ColumnLayout {
                                required property int index
                                required property var modelData
                                readonly property bool isOpen: !root._collapsed[index]
                                Layout.fillWidth: true
                                spacing: 0

                                // ── Group header (clickable) ──
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 36
                                    color: Colors.surfaceLight
                                    radius: Config.radiusSmall
                                    border.color: Colors.border
                                    border.width: 1

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.toggleGroup(index)
                                        cursorShape: Qt.PointingHandCursor
                                    }

                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: Config.spacingMedium
                                        spacing: Config.spacingSmall

                                        Item { Layout.fillWidth: true }
                                        Text { text: modelData.icon; font.pixelSize: 25 }
                                        Text { text: modelData.label; font.pixelSize: 25; font.bold: true; color: Colors.textPrimary; horizontalAlignment: Text.AlignHCenter }
                                        Item { Layout.fillWidth: true }

                                        Text { text: _engine.groupIcon(index); font.pixelSize: 25; color: _engine.groupColor(index); font.bold: true }
                                        Text { text: _engine.groupPassedCount(index); font.pixelSize: 25; color: Colors.textSecondary }

                                        Text {
                                            text: isOpen ? "\u25B2" : "\u25BC"
                                            font.pixelSize: 25; color: Colors.textSecondary
                                        }
                                    }
                                }

                                // ── Check tiles (collapsible) ──
                                Item {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: isOpen ? tileFlow.implicitHeight + Config.spacingSmall : 0
                                    clip: true

                                    Behavior on Layout.preferredHeight {
                                        NumberAnimation { duration: Config.animNormal; easing.type: Easing.InOutQuad }
                                    }

                                    Flow {
                                        id: tileFlow
                                        width: parent.width
                                        spacing: Config.spacingSmall
                                        visible: isOpen

                                        Repeater {
                                            model: PreflightManager.checks

                                            delegate: Rectangle {
                                                required property var modelData
                                                readonly property var chk: modelData
                                                readonly property int cStatus: chk ? chk.status : 0
                                                readonly property bool matchCat: chk && modelData.cats ? false : chk && _engine.catGroups[index].cats.indexOf(chk.checkCategory) >= 0
                                                readonly property bool isPassed: cStatus === 1
                                                readonly property bool isFailed: cStatus === 2
                                                readonly property bool isWarning: cStatus === 3
                                                readonly property bool isManual: (chk ? chk.type : 0) === 1
                                                readonly property bool isAction: (chk ? chk.type : 0) === 2
                                                readonly property bool isHighlighted: chk && chk.checkId === root._highlightCheckId
                                                readonly property bool isMotorCheck: chk && chk.checkId === "propulsion.motors.spin"

                                                visible: chk && _engine.catGroups[index] && _engine.catGroups[index].cats.indexOf(chk.checkCategory) >= 0
                                                width: isMotorCheck ? parent.width
                                                     : (isManual || isAction) ? parent.width
                                                     : (parent.width / 2 - Config.spacingSmall / 2)
                                                implicitHeight: isMotorCheck
                                                    ? (motorPanelLoader.item ? motorPanelLoader.item.implicitHeight : 56)
                                                    : 56
                                                color: "transparent"

                                                Loader {
                                                    id: motorPanelLoader
                                                    active: isMotorCheck
                                                    visible: isMotorCheck
                                                    anchors.fill: parent
                                                    source: "qrc:/qml/cpts/MotorCheckPanel.qml"
                                                    onLoaded: {
                                                        if (item)
                                                            item.checklistCheck = chk
                                                    }
                                                }

                                                Rectangle {
                                                    id: nonMotorTile
                                                    visible: !isMotorCheck
                                                    width: parent.width
                                                    implicitHeight: 52
                                                    radius: Config.radiusSmall
                                                    color: isHighlighted ? Qt.lighter(Colors.error, 1.8)
                                                         : isPassed ? Colors.successDim
                                                         : isFailed ? Colors.errorDim
                                                         : isWarning ? Colors.checkWarnDim
                                                         : Colors.surface
                                                    border.color: isHighlighted ? Colors.error
                                                                : isPassed ? Colors.success
                                                                : isFailed ? Colors.error
                                                                : isWarning ? Colors.checkWarn
                                                                : Colors.borderLight
                                                    border.width: isHighlighted ? 2 : 1

                                                    Behavior on color { ColorAnimation { duration: Config.animFast } }

                                                    RowLayout {
                                                        anchors.fill: parent; anchors.margins: Config.spacingMedium
                                                        spacing: Config.spacingSmall

                                                        Text {
                                                            text: isPassed ? "\u2713" : isFailed ? "\u2717" : isWarning ? "\u26A0" : "\u25CF"
                                                            font.pixelSize: 25
                                                            color: isPassed ? Colors.success : isFailed ? Colors.error : isWarning ? Colors.checkWarn : Colors.textDisabled
                                                            Layout.alignment: Qt.AlignVCenter
                                                        }

                                                        ColumnLayout {
                                                            Layout.fillWidth: true; spacing: 1
                                                            Text { text: chk.label; font.pixelSize: 25; font.bold: true; color: Colors.textPrimary; elide: Text.ElideRight }
                                                            Text { visible: chk.message.length > 0; text: chk.message; font.pixelSize: 25; color: Colors.textSecondary; elide: Text.ElideRight; maximumLineCount: 1 }
                                                        }

                                                        Button {
                                                            visible: (isManual || isAction) && !isPassed
                                                            text: isManual ? "Confirm" : "Run"
                                                            highlighted: true
                                                            font.pixelSize: 25
                                                            Layout.preferredHeight: 28
                                                            Layout.preferredWidth: 56
                                                            onClicked: {
                                                                if (isAction) {
                                                                    var c = PreflightManager.checkById(chk.checkId)
                                                                    if (c) { c.evaluate(); if (c.status === 1) return; c.confirm("Action completed") }
                                                                } else { chk.confirm("Operator confirmed") }
                                                            }
                                                        }

                                                        Rectangle {
                                                            visible: isPassed
                                                            color: Colors.success; radius: Config.radiusSmall
                                                            Layout.preferredHeight: 24; Layout.preferredWidth: 48
                                                            Text { anchors.centerIn: parent; text: "PASSED"; color: Colors.background; font.pixelSize: 25; font.bold: true }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                Item { Layout.preferredHeight: Config.spacingSmall }
                            }
                        }

                        // Placeholder
                        Text {
                            visible: PreflightManager.totalChecks === 0
                            text: qsTr("No checks loaded\nConnect to a vehicle")
                            font.pixelSize: 25; color: Colors.textDisabled
                            Layout.alignment: Qt.AlignHCenter; horizontalAlignment: Text.AlignHCenter
                            Layout.fillWidth: true; Layout.preferredHeight: 80
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // ══════════════════════════════════════════════
                // FOOTER (48px fixed)
                // Mode badge, gate status, Override & Force Arm
                // ══════════════════════════════════════════════
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Config.kFooterHeight
                    color: Colors.footerBg
                    border.color: Colors.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent; anchors.margins: Config.spacingMedium
                        spacing: Config.spacingMedium

                        // Left: gate icon + blocker count (tappable)
                        Item {
                            Layout.preferredWidth: 220
                            Layout.fillHeight: true

                            RowLayout {
                                anchors.fill: parent
                                spacing: Config.spacingSmall

                                Rectangle {
                                    width: 18; height: 18; radius: 9
                                    color: _engine.criticalChecks > 0 ? Colors.error : Colors.accent
                                    Text {
                                        anchors.centerIn: parent
                                        text: _engine.criticalChecks > 0 ? "\u2717" : "\u2713"
                                        font.pixelSize: 11; font.bold: true
                                        color: Colors.background
                                    }
                                }

                                Text {
                                    id: blockerText
                                    Layout.fillWidth: true
                                    text: _engine.criticalChecks > 0 ? "Closed \u2014 " + _engine.criticalChecks + " blocker(s)"
                                         : _engine.pendingChecks > 0 ? _engine.pendingChecks + " pending"
                                         : PreflightManager.totalChecks > 0 ? "Open \u2014 all checks passed"
                                         : "No checks"
                                    font.pixelSize: 25; font.bold: true
                                    color: _engine.criticalChecks > 0 ? Colors.error : Colors.accent
                                    elide: Text.ElideRight

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.scrollToFirstBlocker()
                                    }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // Center: Mode badge (non-interactive pill with chevron)
                        Rectangle {
                            id: modeBadge
                            Layout.preferredHeight: 24
                            Layout.preferredWidth: modeBadgeRow.implicitWidth + Config.spacingMedium * 2
                            radius: 12
                            color: Colors.accentDim
                            border.color: Colors.accent
                            border.width: 1

                            RowLayout {
                                id: modeBadgeRow
                                anchors.centerIn: parent
                                spacing: Config.spacingSmall

                                Text {
                                    text: ArmingGate.mode === 0 ? "PASSIVE"
                                         : ArmingGate.mode === 1 ? "ACTIVE"
                                         : "HYBRID"
                                    font.pixelSize: 25; font.bold: true
                                    color: Colors.accent
                                }
                                Text {
                                    text: qsTr("\u25BE")
                                    font.pixelSize: 25
                                    color: Colors.textSecondary
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: modePickerDialog.open()
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // Right: Override + Force Arm buttons
                        RowLayout {
                            spacing: Config.spacingSmall
                            Layout.alignment: Qt.AlignRight

                            // Override — HYBRID only
                            Button {
                                id: overrideBtn
                                visible: ArmingGate.mode === 2
                                text: qsTr("Override")
                                font.pixelSize: 25; font.bold: true
                                implicitHeight: 28; implicitWidth: 90
                                enabled: _engine.criticalChecks > 0 || _engine.pendingChecks > 0
                                background: Rectangle {
                                    color: "transparent"
                                    border.color: Colors.accent
                                    border.width: 1
                                    radius: Config.radiusSmall
                                }
                                contentItem: Text {
                                    text: overrideBtn.text
                                    font: overrideBtn.font
                                    color: Colors.accent
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                onClicked: overrideDialog.open()
                            }

                            // Force Arm — always visible when blockers exist
                            Button {
                                id: forceArmBtn
                                text: qsTr("Force Arm")
                                font.pixelSize: 25; font.bold: true
                                implicitHeight: 28; implicitWidth: 100
                                enabled: _engine.criticalChecks > 0
                                background: Rectangle {
                                    color: Colors.errorDim
                                    radius: Config.radiusSmall
                                }
                                contentItem: Text {
                                    text: forceArmBtn.text
                                    font: forceArmBtn.font
                                    color: Colors.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                onClicked: forceArmDialog.open()
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Disconnect guard overlay ──
    Rectangle {
        anchors.fill: parent
        color: Colors.background; opacity: 0.92
        visible: VehicleTelemetry.disconnectGuardActive
        z: 200

        ColumnLayout {
            anchors.centerIn: parent; spacing: Config.spacingLarge
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 72; Layout.preferredHeight: 72; radius: 36
                color: Colors.surface; border.color: Colors.error; border.width: 2
                Text { anchors.centerIn: parent; text: "\u26A0"; font.pixelSize: 25; color: Colors.error }
            }
            Text { text: "Connection Lost"; font.pixelSize: 25; font.bold: true; color: Colors.error; Layout.alignment: Qt.AlignHCenter }
            Text { text: VehicleTelemetry.disconnectGuardMessage; font.pixelSize: 25; color: Colors.textSecondary; Layout.alignment: Qt.AlignHCenter }
            Text { text: "Checks suspended \u2014 reconnecting..."; font.pixelSize: 25; color: Colors.textDisabled; Layout.alignment: Qt.AlignHCenter }
        }
    }

    // ── Drawer for narrow screens ──
    Drawer {
        id: telemetryDrawer
        edge: Qt.LeftEdge
        width: Math.min(parent.width * 0.85, 360)
        height: parent.height
        modal: isSingle
        visible: isSingle
        background: Rectangle { color: Colors.surface }
        ScrollView {
            anchors.fill: parent; clip: true
            ColumnLayout {
                width: parent.width; spacing: Config.spacingMedium
                anchors.margins: Config.spacingMedium
                Text { text: "\uD83D\uDCCA Telemetry"; font.pixelSize: 25; font.bold: true; color: Colors.accent; Layout.alignment: Qt.AlignHCenter }
                Text { text: "Connection: " + (TelemetryProvider.isConnected ? "\u2713 Connected" : "\u2717 Disconnected"); font.pixelSize: 25; color: Colors.textPrimary }
                Text { text: "Voltage: " + TelemetryProvider.batteryVoltage.toFixed(2) + " V"; font.pixelSize: 25; color: root.batteryColor(TelemetryProvider.batteryVoltage) }
                Text { text: "GPS: " + TelemetryProvider.gpsSatellites + " sats, " + TelemetryProvider.gpsFixTypeString; font.pixelSize: 25; color: root.gpsColor(TelemetryProvider.gpsSatellites) }
                Text { text: "Attitude: R:" + TelemetryProvider.roll.toFixed(1) + "\u00B0 P:" + TelemetryProvider.pitch.toFixed(1) + "\u00B0"; font.pixelSize: 25; color: Colors.textPrimary }
                Item { Layout.fillHeight: true }
            }
        }
    }

    // ── Shared helper component instances (avoid QML scope) ──
    function batteryColor(v) {
        return v >= 15.0 ? Colors.success : v >= 13.5 ? Colors.warning : Colors.error
    }
    function gpsColor(sats) {
        return sats >= 8 ? Colors.success : sats >= 5 ? Colors.warning : Colors.error
    }
    function ekfOk() {
        var s = TelemetryProvider.ekfStatus
        return s.length > 0 && s !== "Converging" && s !== "Unknown"
    }

    // ── Reusable telemetry components ──
    component TelRow: Rectangle {
        property string label
        property color accent: Colors.textSecondary
        property list<Item> preComps
        property list<Item> _vals: valRow.data

        Layout.fillWidth: true
        Layout.preferredHeight: 40
        color: Colors.surface
        radius: 4
        border.color: Colors.border; border.width: 1

        RowLayout {
            id: valRow
            anchors.fill: parent; anchors.leftMargin: Config.spacingSmall; anchors.rightMargin: Config.spacingSmall
            spacing: Config.spacingSmall

            Rectangle { width: 3; height: 16; radius: 1.5; color: accent; Layout.alignment: Qt.AlignVCenter }
            Text { text: label; font.pixelSize: 25; color: Colors.textSecondary; font.bold: true; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignHCenter }
            Item { Layout.fillWidth: true }
        }
    }

    component TelVal: Text {
        property bool bold: false
        font.pixelSize: 25
        font.family: "monospace"
        font.bold: bold
        color: Colors.textPrimary
    }

    component TelSep: Rectangle {
        width: 1; height: 14; color: Colors.border; Layout.alignment: Qt.AlignVCenter; visible: true
    }

    component TelHealth: Rectangle {
        property string label
        property bool ok: true

        Layout.preferredWidth: 48; Layout.preferredHeight: 48
        radius: Config.radiusSmall
        color: ok ? Colors.successDim : Colors.errorDim
        border.color: ok ? Colors.success : Colors.error; border.width: 1

        ColumnLayout {
            anchors.centerIn: parent; spacing: 2
            Text { text: ok ? "\u2713" : "\u2717"; font.pixelSize: 25; color: ok ? Colors.success : Colors.error; Layout.alignment: Qt.AlignHCenter }
            Text { text: label; font.pixelSize: 25; color: Colors.textPrimary; font.bold: true; Layout.alignment: Qt.AlignHCenter }
        }
    }
}
