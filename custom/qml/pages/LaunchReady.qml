import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Page {
    id: root
    background: Rectangle { color: Colors.background }

    Column {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingLarge

        Text {
            text: qsTr("Launch Ready")
            font.pixelSize: Config.fontSizeH2
            color: Colors.primary
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: "Vehicle: " + VehicleTelemetry.selectedVehicle + " (" + VehicleTelemetry.selectedVehicleTypeLabel + ")"
            font.pixelSize: Config.fontSizeBody
            color: Colors.textSecondary
            wrapMode: Text.WordWrap
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: VehicleTelemetry.batteryVoltage.toFixed(1) + "V  ·  "
                + VehicleTelemetry.satellites + " sats  ·  "
                + VehicleTelemetry.groundSpeed.toFixed(1) + " m/s"
            font.pixelSize: Config.fontSizeBody
            color: Colors.textSecondary
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // ── Status summary ──
        Rectangle {
            width: parent.width
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border
            Column {
                anchors.fill: parent
                anchors.margins: Config.spacingMedium
                spacing: Config.spacingSmall

                Text { text: "MAVLink: " + (VehicleTelemetry.mavlinkConnected ? TelemetryProvider.autopilotType : "Not connected"); font.pixelSize: Config.fontSizeSmall; color: VehicleTelemetry.mavlinkConnected ? Colors.success : Colors.error }
                Text { text: "Checklist Result: " + (ChecklistEngine.allMandatoryPassed ? "PASS" : "FAIL"); font.pixelSize: Config.fontSizeBody; color: ChecklistEngine.allMandatoryPassed ? Colors.success : Colors.error }
                Text { text: "Hardware Test: " + (VehicleTelemetry.hardwareTestPassed ? "PASS" : "INCOMPLETE"); font.pixelSize: Config.fontSizeSmall; color: VehicleTelemetry.hardwareTestPassed ? Colors.success : Colors.error }
                Text { text: "Payload Secured: " + (VehicleTelemetry.payloadSecured ? "Yes" : "No"); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                Text { text: "Final Checks: " + (VehicleTelemetry.allFinalChecksPassed ? "Complete" : "Incomplete"); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                Text { text: "Airspace Compliance: " + (NoFlyZoneModel.complianceChecked ? "Checked ✓" : "NOT CHECKED"); font.pixelSize: Config.fontSizeSmall; color: NoFlyZoneModel.complianceChecked ? Colors.success : Colors.warning }
            }
        }

        // ── Weather (live) ──
        Rectangle {
            width: parent.width
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border
            Column {
                anchors.fill: parent
                anchors.margins: Config.spacingMedium
                spacing: Config.spacingSmall
                Text { text: "Weather"; font.pixelSize: Config.fontSizeH3; font.bold: true; color: Colors.textPrimary }
                Text { text: WeatherProvider.loading ? "Loading..." : (WeatherProvider.lastError.length > 0 ? WeatherProvider.lastError : WeatherProvider.temperature.toFixed(1) + "°C  ·  " + WeatherProvider.weatherDescription + "  ·  Wind " + WeatherProvider.windSpeed.toFixed(1) + " m/s"); font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary; wrapMode: Text.WordWrap }
            }
        }

        // ── Export section ──
        Rectangle {
            width: parent.width
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingMedium
                spacing: Config.spacingMedium

                Text {
                    text: qsTr("Export Pre-Flight Report")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    text: "Reports saved to: " + ExportHelper.defaultExportDir()
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    spacing: Config.spacingSmall

                    CustomButton {
                        text: qsTr("&#128196; Export HTML")
                        Layout.fillWidth: true
                        onClicked: doExport("html")
                    }
                    CustomButton {
                        text: qsTr("&#128196; Export JSON")
                        Layout.fillWidth: true
                        baseColor: Colors.surface
                        onClicked: doExport("json")
                    }
                    CustomButton {
                        text: qsTr("&#128196; Export PDF")
                        Layout.fillWidth: true
                        baseColor: Colors.surface
                        onClicked: doExport("pdf")
                    }
                }

                Text {
                    id: exportLabel
                    text: VehicleTelemetry.exportStatus
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    wrapMode: Text.WordWrap
                    visible: text.length > 0
                }
            }
        }

        // ── Launch block ──
        Rectangle {
            width: parent.width
            visible: !VehicleTelemetry.readyToLaunch || !NoFlyZoneModel.complianceChecked
            color: Colors.error
            radius: Config.radiusSmall
            opacity: 0.25
            implicitHeight: blockLabel.implicitHeight + Config.spacingMedium * 2

            Text {
                id: blockLabel
                anchors.centerIn: parent
                width: parent.width - Config.spacingMedium * 2
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: !VehicleTelemetry.mavlinkConnected
                      ? "Launch blocked: connect to autopilot first"
                      : (!NoFlyZoneModel.complianceChecked
                         ? "Launch blocked: complete the manual airspace compliance check (Airspace page)"
                         : (!VehicleTelemetry.hardwareTestPassed
                            ? "Launch blocked: complete hardware actuator verification"
                            : "Launch blocked: complete checklist, payload, and final checks"))
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textPrimary
            }
        }

        CustomButton {
            text: qsTr("Airspace / Compliance Check")
            anchors.horizontalCenter: parent.horizontalCenter
            baseColor: Colors.surface
            onClicked: Window.window.mainStackView.push("AirspacePage.qml")
        }

        CustomButton {
            text: qsTr("Authorize Launch")
            anchors.horizontalCenter: parent.horizontalCenter
            enabled: VehicleTelemetry.readyToLaunch && NoFlyZoneModel.complianceChecked
            onClicked: {
                doExport("html")
                VehicleTelemetry.exportStatus = "Launch authorized — report exported"
            }
        }

        CustomButton {
            text: qsTr("← Back")
            anchors.horizontalCenter: parent.horizontalCenter
            baseColor: Colors.surface
            onClicked: Window.window.mainStackView.pop()
        }

        CustomButton {
            text: qsTr("Post-Flight Summary →")
            anchors.horizontalCenter: parent.horizontalCenter
            baseColor: Colors.surface
            onClicked: Window.window.mainStackView.push("PostFlightSummary.qml")
        }
    }

    // ── Export helper ──
    function doExport(fmt) {
        var record = VehicleTelemetry.buildComplianceRecord()
        var now = new Date()
        var token = Qt.formatDateTime(now, "yyyyMMdd_HHmmss")
        var baseName = "uav_preflight_" + token

        var json = JSON.stringify(record, null, 2)
        var paths = []

        if (fmt === "html" || fmt === "all") {
            var hp = ExportHelper.saveComplianceHtml(baseName, json)
            if (hp.length > 0) paths.push(hp)
        }
        if (fmt === "json" || fmt === "all") {
            var jp = ExportHelper.saveComplianceJson(baseName, json)
            if (jp.length > 0) paths.push(jp)
        }
        if (fmt === "pdf" || fmt === "all") {
            var pp = ExportHelper.saveCompliancePdf(baseName, json)
            if (pp.length > 0) paths.push(pp)
        }

        // Save to SQLite
        var operatorId = VehicleTelemetry.pilotName.length > 0
            ? VehicleTelemetry.pilotName
            : "Unknown Operator"
        var vehicleId = "SYSID_1"
        var snapshot = JSON.stringify(record.telemetry)
        var dbOk = Database.saveComplianceLog(token, vehicleId, record.vehicleType, operatorId, json, snapshot)

        if (paths.length > 0) {
            VehicleTelemetry.exportStatus = "Saved: " + paths.join(", ") + (dbOk ? " (DB OK)" : " (DB failed)")
        } else {
            VehicleTelemetry.exportStatus = "Export failed — see logs"
        }
    }
}
