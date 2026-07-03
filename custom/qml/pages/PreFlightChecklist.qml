import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Page {
    id: root

    // ── Responsive layout ──
    readonly property bool isSingle: width < Config.breakpointSingle

    // ── Auto-fetch weather on page load ──
    Component.onCompleted: {
        if (typeof WeatherProvider !== 'undefined' && TelemetryProvider) {
            var lat = TelemetryProvider.gpsLatitude
            var lon = TelemetryProvider.gpsLongitude
            if (lat !== 0 || lon !== 0) {
                WeatherProvider.fetchWeather(lat, lon)
            }
        }
    }

    // ── Collapsible group state ──
    property var _collapsed: ({})
    function _clone(obj) { var c = {}; for (var k in obj) c[k] = obj[k]; return c }
    function toggleGroup(idx) {
        _collapsed[idx] = !_collapsed[idx]
        _collapsed = _clone(_collapsed)
    }
    function groupIcon(idx) {
        var arr = _catGroups
        if (idx < 0 || idx >= arr.length) return ""
        var g = arr[idx]
        var allPassed = true
        var anyFail = false
        var anyPending = false
        var checks = PreflightManager.checks
        for (var i = 0; i < (checks ? checks.length : 0); ++i) {
            var c = checks[i]
            for (var j = 0; j < g.cats.length; ++j) {
                if (c.checkCategory === g.cats[j]) {
                    if (c.status === 0 || c.status === 3) { allPassed = false; anyPending = true }
                    if (c.status === 2 || c.status === 4) { allPassed = false; anyFail = true }
                }
            }
        }
        if (allPassed) return "\u2713"
        if (anyFail) return "\u2717"
        return "\u25CF"
    }
    function groupColor(idx) {
        var arr = _catGroups
        if (idx < 0 || idx >= arr.length) return Colors.textSecondary
        var g = arr[idx]
        var anyFail = false
        var anyPending = false
        var checks = PreflightManager.checks
        for (var i = 0; i < (checks ? checks.length : 0); ++i) {
            var c = checks[i]
            for (var j = 0; j < g.cats.length; ++j) {
                if (c.checkCategory === g.cats[j]) {
                    if (c.status === 2 || c.status === 4) anyFail = true
                    if (c.status === 0 || c.status === 3) anyPending = true
                }
            }
        }
        if (anyFail) return Colors.error
        if (anyPending) return Colors.warning
        return Colors.success
    }
    function groupPassedCount(idx) {
        var arr = _catGroups
        if (idx < 0 || idx >= arr.length) return "0/0"
        var g = arr[idx]
        var passed = 0, total = 0
        var checks = PreflightManager.checks
        for (var i = 0; i < (checks ? checks.length : 0); ++i) {
            var c = checks[i]
            for (var j = 0; j < g.cats.length; ++j) {
                if (c.checkCategory === g.cats[j]) {
                    total++
                    if (c.status === 1) passed++
                }
            }
        }
        return passed + "/" + total
    }

    readonly property var _catGroups: [
        { label: "Power \u0026 Propulsion",     icon: "\u26A1", cats: [0, 1], accent: Colors.pastelGreen },
        { label: "GPS/Navigation \u0026 Sensors", icon: "\uD83D\uDEE0", cats: [2],    accent: Colors.pastelBlue },
        { label: "Communication \u0026 Control", icon: "\uD83D\uDCF6", cats: [3],    accent: Colors.pastelPurple },
        { label: "Airframe \u0026 Physical",    icon: "\uD83D\uDEE9", cats: [4],    accent: Colors.pastelPink },
        { label: "Safety \u0026 Failsafes",     icon: "\uD83D\uDEE1", cats: [5],    accent: Colors.warning },
        { label: "Environment \u0026 Mission",  icon: "\uD83C\uDF2C", cats: [6, 7], accent: Colors.info }
    ]

    readonly property int _nPending: {
        var n = 0
        var a = PreflightManager.checks
        for (var i = 0; i < (a ? a.length : 0); ++i) {
            if (a[i].status === 0 || a[i].status === 3) n++
        }
        return n
    }

    readonly property int _nCritical: {
        var n = 0
        var a = PreflightManager.checks
        for (var i = 0; i < (a ? a.length : 0); ++i) {
            if (a[i].status === 2 || a[i].status === 4) n++
        }
        return n
    }

    background: Rectangle { color: Colors.background }

    // ── Dialogs ──
    Dialog {
        id: motorConfigDialog
        title: "ArduPilot Motor Setup"
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
            font.pixelSize: Config.fontSizeBody
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
        title: "Override & Arm"
        modal: true; standardButtons: Dialog.Yes | Dialog.No
        anchors.centerIn: parent
        width: Math.min(400, parent.width * 0.9)
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Colors.surface; border.color: Colors.error; border.width: 1; radius: Config.radiusMedium }
        header: Label { text: overrideDialog.title; font.bold: true; color: Colors.error; padding: Config.spacingMedium }
        padding: Config.spacingMedium
        ColumnLayout {
            spacing: Config.spacingMedium
            Text { text: "There are " + _nCritical + " critical failure(s) and " + _nPending + " pending check(s).  Override will bypass all safety gates and arm the vehicle."; color: Colors.textPrimary; font.pixelSize: Config.fontSizeBody; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "This should only be used in controlled testing environments."; color: Colors.error; font.pixelSize: Config.fontSizeSmall; font.bold: true }
        }
        onAccepted: {
            ArmingGate.overrideGate("Operator override via dialog")
            TelemetryProvider.arm()
        }
        onRejected: console.log("Override cancelled")
    }

    // ── Critical Issues Dialog ──
    Dialog {
        id: criticalIssuesDialog2
        title: "Critical Issues"
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width * 0.9)
        height: Math.min(420, parent.height * 0.8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Colors.surface; border.color: "#9333ea"; border.width: 2; radius: 12 }
        padding: 0

        header: Rectangle {
            height: 48; color: "#2D1B4E"; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: parent.color }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
                Text { text: "\u26A0 Critical / Blocking Issues"; font.pixelSize: 15; font.bold: true; color: "#F8BBD0"; Layout.fillWidth: true }
                Text { text: _nCritical + " issue" + (_nCritical !== 1 ? "s" : ""); font.pixelSize: 12; color: "#CE93D8" }
            }
        }

        contentItem: Flickable {
            clip: true
            contentHeight: issueCol2.implicitHeight + 16
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: issueCol2
                width: parent.width; spacing: 6

                Repeater {
                    model: PreflightManager.blockingChecks

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property var chk: modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: 12; Layout.rightMargin: 12
                        Layout.topMargin: index === 0 ? 8 : 0
                        height: 60; radius: 8
                        color: Colors.errorDim
                        border.color: Colors.error; border.width: 1

                        RowLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 10

                            Rectangle {
                                width: 28; height: 28; radius: 14; color: Colors.error
                                Text { anchors.centerIn: parent; text: (index + 1).toString(); font.pixelSize: 11; font.bold: true; color: "#fff" }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 1
                                Text { text: chk ? chk.label : ""; font.pixelSize: 13; font.bold: true; color: Colors.textPrimary; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: chk ? chk.message : ""; font.pixelSize: 11; color: Colors.textSecondary; elide: Text.ElideRight; maximumLineCount: 1; Layout.fillWidth: true }
                            }

                            Rectangle {
                                width: 72; height: 26; radius: 6; color: "#9333ea"
                                Text { anchors.centerIn: parent; text: "Go to Check"; font.pixelSize: 9; font.bold: true; color: "#fff" }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: criticalIssuesDialog2.close()
                                }
                            }
                        }
                    }
                }

                Text {
                    visible: _nCritical === 0
                    text: "\u2713 No critical issues"
                    font.pixelSize: 14; color: Colors.success
                    Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 20
                }
            }
        }

        footer: Rectangle {
            height: 44; color: Colors.surfaceLight; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: parent.color }
            RowLayout {
                anchors.fill: parent; anchors.margins: 12
                Item { Layout.fillWidth: true }
                Rectangle {
                    width: 80; height: 28; radius: 6; color: "#9333ea"
                    Text { anchors.centerIn: parent; text: "Close"; font.pixelSize: 12; font.bold: true; color: "#fff" }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: criticalIssuesDialog2.close() }
                }
            }
        }
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
                        Text { text: "\uD83D\uDCCA Live Telemetry"; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary; Layout.fillWidth: true }
                        Text { text: TelemetryProvider.isConnected ? "\u25CF Connected" : "\u25CB Disconnected"; font.pixelSize: Config.fontSizeSmall; color: TelemetryProvider.isConnected ? Colors.success : Colors.error; font.bold: true }
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

                        // ── Payload weight entry ──
                        TelRow { label: "Payload"; accent: Colors.pastelPink
                            Rectangle {
                                Layout.preferredWidth: 60; Layout.preferredHeight: 28
                                radius: Config.radiusSmall
                                border.color: Colors.border; border.width: 1
                                color: Colors.surfaceLight
                                TextInput {
                                    id: payloadInput
                                    anchors.fill: parent; anchors.margins: 2
                                    font.pixelSize: Config.fontSizeBody; font.family: "monospace"
                                    horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter
                                    color: Colors.textPrimary
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    text: VehicleProfileManager.currentPayloadWeightKg.toFixed(1)
                                    onEditingFinished: {
                                        var v = parseFloat(text)
                                        if (!isNaN(v) && v >= 0) {
                                            VehicleProfileManager.setPayloadWeightKg(v)
                                        } else {
                                            text = VehicleProfileManager.currentPayloadWeightKg.toFixed(1)
                                        }
                                    }
                                }
                            }
                            TelVal { text: "kg"; bold: false }
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

                        Text { text: "\uD83D\uDD0D Checklist"; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary }
                        Text { text: PreflightManager.passedChecks + "/" + PreflightManager.totalChecks; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            width: 60; height: 24; radius: Config.radiusSmall
                            color: VehicleTelemetry.connectionQuality >= Config.connQualityGood ? Colors.successDim : Colors.errorDim
                            border.color: VehicleTelemetry.connectionQuality >= Config.connQualityGood ? Colors.success : Colors.error; border.width: 1
                            Text { anchors.centerIn: parent; text: "\uD83D\uDCE1 " + VehicleTelemetry.connectionQuality + "%"; font.pixelSize: Config.fontSizeSmall; font.bold: true; color: VehicleTelemetry.connectionQuality >= Config.connQualityGood ? Colors.success : Colors.error }
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
                        GradientStop { position: 0.0; color: "#2D1B4E" }
                        GradientStop { position: 0.5; color: "#4A1942" }
                        GradientStop { position: 1.0; color: "#6B1D5E" }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14; anchors.rightMargin: 10
                        spacing: 12

                        ColumnLayout {
                            spacing: 1
                            Text {
                                text: PreflightManager.passedChecks + "/" + PreflightManager.totalChecks + " checks passed"
                                font.pixelSize: 14; font.bold: true; color: "#F8BBD0"
                            }
                            Text {
                                text: _nCritical > 0 ? _nCritical + " critical issue" + (_nCritical !== 1 ? "s" : "") : "No critical issues"
                                font.pixelSize: 11
                                color: _nCritical > 0 ? "#EF9A9A" : "#A5D6A7"
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            width: 32; height: 32; radius: 16
                            color: "transparent"
                            border.color: "#CE93D8"; border.width: 2
                            Text {
                                anchors.centerIn: parent
                                text: PreflightManager.completionPercent + "%"
                                font.pixelSize: 9; font.bold: true; color: "#CE93D8"
                            }
                        }

                        Rectangle {
                            width: 56; height: 26; radius: 6
                            color: _nCritical > 0 ? "#E91E63" : "#9333ea"
                            Text { anchors.centerIn: parent; text: "Show"; font.pixelSize: 11; font.bold: true; color: "#fff" }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: criticalIssuesDialog2.open()
                            }
                        }
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
                        Text { text: "\u26A0 Stale data"; font.pixelSize: Config.fontSizeSmall; color: Colors.checkWarn; font.bold: true }
                        Text { text: Math.floor((new Date() - VehicleTelemetry.lastTelemetryUpdate) / 1000) + "s ago"; font.pixelSize: Config.fontSizeSmall; color: Colors.checkWarn }
                    }
                }

                // ── Scrollable groups ──
                Flickable {
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
                            model: _catGroups

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
                                        Text { text: modelData.icon; font.pixelSize: 14 }
                                        Text { text: modelData.label; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary; horizontalAlignment: Text.AlignHCenter }
                                        Item { Layout.fillWidth: true }

                                        Text { text: root.groupIcon(index); font.pixelSize: 14; color: root.groupColor(index); font.bold: true }
                                        Text { text: root.groupPassedCount(index); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }

                                        Text {
                                            text: isOpen ? "\u25B2" : "\u25BC"
                                            font.pixelSize: 10; color: Colors.textSecondary
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
                                                readonly property bool matchCat: chk && modelData.cats ? false : chk && _catGroups[index].cats.indexOf(chk.checkCategory) >= 0
                                                readonly property bool isPassed: cStatus === 1
                                                readonly property bool isFailed: cStatus === 2
                                                readonly property bool isWarning: cStatus === 3
                                                readonly property bool isManual: (chk ? chk.type : 0) === 1
                                                readonly property bool isAction: (chk ? chk.type : 0) === 2

                                                visible: chk && _catGroups[index] && _catGroups[index].cats.indexOf(chk.checkCategory) >= 0
                                                width: (isManual || isAction) ? parent.width : (parent.width / 2 - Config.spacingSmall / 2)
                                                implicitHeight: 52
                                                radius: Config.radiusSmall
                                                color: isPassed ? Colors.successDim
                                                     : isFailed ? Colors.errorDim
                                                     : isWarning ? Colors.checkWarnDim
                                                     : Colors.surface
                                                border.color: isPassed ? Colors.success
                                                            : isFailed ? Colors.error
                                                            : isWarning ? Colors.checkWarn
                                                            : Colors.borderLight
                                                border.width: isPassed || isFailed || isWarning ? 1 : 1

                                                Behavior on color { ColorAnimation { duration: Config.animFast } }

                                                RowLayout {
                                                    anchors.fill: parent; anchors.margins: Config.spacingMedium
                                                    spacing: Config.spacingSmall

                                                    Text {
                                                        text: isPassed ? "\u2713" : isFailed ? "\u2717" : isWarning ? "\u26A0" : "\u25CF"
                                                        font.pixelSize: 16
                                                        color: isPassed ? Colors.success : isFailed ? Colors.error : isWarning ? Colors.checkWarn : Colors.textDisabled
                                                        Layout.alignment: Qt.AlignVCenter
                                                    }

                                                    ColumnLayout {
                                                        Layout.fillWidth: true; spacing: 1
                                                        Text { text: chk.label; font.pixelSize: Config.fontSizeBody; font.bold: true; color: Colors.textPrimary; elide: Text.ElideRight }
                                                        Text { visible: chk.message.length > 0; text: chk.message; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary; elide: Text.ElideRight; maximumLineCount: 1 }
                                                    }

                                                    Button {
                                                        visible: (isManual || isAction) && !isPassed
                                                        text: isManual ? "Confirm" : "Run"
                                                        highlighted: true
                                                        font.pixelSize: Config.fontSizeSmall
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
                                                        Text { anchors.centerIn: parent; text: "PASSED"; color: Colors.background; font.pixelSize: Config.fontSizeSmall; font.bold: true }
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
                            text: "No checks loaded\nConnect to a vehicle"
                            font.pixelSize: Config.fontSizeBody; color: Colors.textDisabled
                            Layout.alignment: Qt.AlignHCenter; horizontalAlignment: Text.AlignHCenter
                            Layout.fillWidth: true; Layout.preferredHeight: 80
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // ══════════════════════════════════════════════
                // FOOTER (48px fixed)
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

                        // Left: overall status
                        Text {
                            Layout.preferredWidth: 180
                            text: _nCritical > 0 ? _nCritical + " CRITICAL FAILURES"
                                 : _nPending > 0 ? _nPending + " ITEMS PENDING"
                                 : PreflightManager.totalChecks > 0 ? "ALL CHECKS PASSED" : "NO CHECKS"
                            font.pixelSize: Config.fontSizeSmall; font.bold: true
                            color: _nCritical > 0 ? Colors.error : _nPending > 0 ? Colors.warning : Colors.success
                            elide: Text.ElideRight
                        }

                        // Center: Arming gate mode
                        RowLayout {
                            Layout.alignment: Qt.AlignCenter
                            spacing: Config.spacingSmall

                            Text { text: "GATE:"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary; font.bold: true }
                            Text {
                                text: ArmingGate.mode === 0 ? "PASSIVE"
                                     : ArmingGate.mode === 1 ? "ACTIVE"
                                     : "HYBRID"
                                font.pixelSize: Config.fontSizeSmall; font.bold: true
                                color: ArmingGate.mode === 0 ? Colors.warning : ArmingGate.mode === 1 ? Colors.success : Colors.info
                            }

                            Rectangle {
                                width: 36; height: 20; radius: 10
                                color: ArmingGate.mode === 0 ? Colors.warningDim
                                     : ArmingGate.mode === 1 ? Colors.successDim
                                     : Colors.info
                                opacity: 0.6

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        var next = (ArmingGate.mode + 1) % 3
                                        ArmingGate.mode = next
                                    }
                                    cursorShape: Qt.PointingHandCursor
                                }

                                Rectangle {
                                    x: ArmingGate.mode === 0 ? 2 : ArmingGate.mode === 1 ? parent.width - width - 2 : parent.width / 2 - width / 2
                                    y: 2; width: 16; height: 16; radius: 8
                                    color: Colors.textPrimary

                                    Behavior on x { NumberAnimation { duration: Config.animFast; easing.type: Easing.InOutQuad } }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // Right: Override button + timestamp
                        RowLayout {
                            spacing: Config.spacingMedium
                            Layout.alignment: Qt.AlignRight

                            Button {
                                id: overrideBtn
                                text: "Override && Arm"
                                font.pixelSize: Config.fontSizeSmall; font.bold: true
                                implicitHeight: 32; implicitWidth: 120
                                enabled: _nCritical > 0 || _nPending > 0
                                highlighted: true
                                palette.highlight: Colors.error

                                onClicked: overrideDialog.open()
                            }

                            Text {
                                text: "Iteration 1 v1.0"
                                font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary
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
                Text { anchors.centerIn: parent; text: "\u26A0"; font.pixelSize: 36; color: Colors.error }
            }
            Text { text: "Connection Lost"; font.pixelSize: Config.fontSizeH1; font.bold: true; color: Colors.error; Layout.alignment: Qt.AlignHCenter }
            Text { text: VehicleTelemetry.disconnectGuardMessage; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary; Layout.alignment: Qt.AlignHCenter }
            Text { text: "Checks suspended \u2014 reconnecting..."; font.pixelSize: Config.fontSizeSmall; color: Colors.textDisabled; Layout.alignment: Qt.AlignHCenter }
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
                Text { text: "\uD83D\uDCCA Telemetry"; font.pixelSize: Config.fontSizeH2; font.bold: true; color: Colors.accent; Layout.alignment: Qt.AlignHCenter }
                Text { text: "Connection: " + (TelemetryProvider.isConnected ? "\u2713 Connected" : "\u2717 Disconnected"); font.pixelSize: Config.fontSizeSmall; color: Colors.textPrimary }
                Text { text: "Voltage: " + TelemetryProvider.batteryVoltage.toFixed(2) + " V"; font.pixelSize: Config.fontSizeSmall; color: root.batteryColor(TelemetryProvider.batteryVoltage) }
                Text { text: "GPS: " + TelemetryProvider.gpsSatellites + " sats, " + TelemetryProvider.gpsFixTypeString; font.pixelSize: Config.fontSizeSmall; color: root.gpsColor(TelemetryProvider.gpsSatellites) }
                Text { text: "Attitude: R:" + TelemetryProvider.roll.toFixed(1) + "\u00B0 P:" + TelemetryProvider.pitch.toFixed(1) + "\u00B0"; font.pixelSize: Config.fontSizeSmall; color: Colors.textPrimary }
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
        Layout.preferredHeight: 42
        color: Colors.surface
        radius: Config.radiusSmall
        border.color: Colors.borderLight; border.width: 1

        RowLayout {
            id: valRow
            anchors.fill: parent; anchors.leftMargin: Config.spacingMedium; anchors.rightMargin: Config.spacingSmall
            spacing: Config.spacingSmall

            Rectangle { width: 3; height: 18; radius: 1.5; color: accent; Layout.alignment: Qt.AlignVCenter }
            Text { text: label; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary; font.bold: true; Layout.preferredWidth: 100; horizontalAlignment: Text.AlignHCenter }
            Item { Layout.fillWidth: true }
        }
    }

    component TelVal: Text {
        property bool bold: false
        font.pixelSize: Config.fontSizeBody
        font.family: "monospace"
        font.bold: bold
        color: Colors.textPrimary
    }

    component TelSep: Rectangle {
        width: 1; height: 16; color: Colors.borderLight; Layout.alignment: Qt.AlignVCenter; visible: true
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
            Text { text: ok ? "\u2713" : "\u2717"; font.pixelSize: 16; color: ok ? Colors.success : Colors.error; Layout.alignment: Qt.AlignHCenter }
            Text { text: label; font.pixelSize: Config.fontSizeSmall; color: Colors.textPrimary; font.bold: true; Layout.alignment: Qt.AlignHCenter }
        }
    }
}
