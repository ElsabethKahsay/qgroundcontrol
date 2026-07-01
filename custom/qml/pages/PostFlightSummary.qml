import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Page {
    id: root
    background: Rectangle { color: Colors.background }

    property bool flightActive: false
    property real flightStartTime: 0
    property real flightDuration: 0
    property real maxAltitude: 0
    property real maxGroundSpeed: 0
    property string notes: ""
    property string incidents: ""

    Timer {
        id: flightTimer
        interval: 1000
        repeat: true
        onTriggered: {
            if (flightActive) {
                var elapsed = (Date.now() - flightStartTime) / 1000
                flightDuration = elapsed
                var alt = VehicleTelemetry.altitude
                if (alt > maxAltitude) maxAltitude = alt
                var spd = VehicleTelemetry.groundSpeed
                if (spd > maxGroundSpeed) maxGroundSpeed = spd
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingMedium

        Text {
            text: "Post-Flight Summary"
            font.pixelSize: Config.fontSizeH2
            color: Colors.primary
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            text: VehicleTelemetry.selectedVehicle + " (" + VehicleTelemetry.selectedVehicleTypeLabel + ")"
            font.pixelSize: Config.fontSizeBody
            color: Colors.textSecondary
            Layout.alignment: Qt.AlignHCenter
        }

        // ── Flight timer controls ──
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Config.spacingMedium

            CustomButton {
                text: flightActive ? "End Flight" : "Start Flight"
                baseColor: flightActive ? Colors.error : Colors.success
                onClicked: {
                    if (flightActive) {
                        flightActive = false
                        flightTimer.stop()
                    } else {
                        flightStartTime = Date.now()
                        flightDuration = 0
                        maxAltitude = VehicleTelemetry.altitude
                        maxGroundSpeed = VehicleTelemetry.groundSpeed
                        flightActive = true
                        flightTimer.start()
                    }
                }
            }

            CustomButton {
                text: "Reset"
                baseColor: Colors.surface
                enabled: !flightActive && flightDuration > 0
                onClicked: {
                    flightDuration = 0
                    maxAltitude = 0
                    maxGroundSpeed = 0
                    notes = ""
                    incidents = ""
                }
            }
        }

        // ── Telemetry snapshot ──
        Rectangle {
            Layout.fillWidth: true
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border

            GridLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingMedium
                columns: 2
                columnSpacing: Config.spacingLarge
                rowSpacing: Config.spacingSmall

                Text { text: "Flight Time:"; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary }
                Text { text: formatDuration(flightDuration); font.pixelSize: Config.fontSizeBody; color: Colors.textPrimary; font.bold: true }

                Text { text: "Max Altitude:"; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary }
                Text { text: maxAltitude.toFixed(1) + " m"; font.pixelSize: Config.fontSizeBody; color: Colors.textPrimary; font.bold: true }

                Text { text: "Max Ground Speed:"; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary }
                Text { text: maxGroundSpeed.toFixed(1) + " m/s"; font.pixelSize: Config.fontSizeBody; color: Colors.textPrimary; font.bold: true }

                Text { text: "Battery Used:"; font.pixelSize: Config.fontSizeBody; color: Colors.textSecondary }
                Text { text: VehicleTelemetry.batteryVoltage.toFixed(1) + "V"; font.pixelSize: Config.fontSizeBody; color: Colors.textPrimary; font.bold: true }
            }
        }

        // ── Notes ──
        Text {
            text: "Flight Notes"
            font.pixelSize: Config.fontSizeH3
            font.bold: true
            color: Colors.textPrimary
        }

        TextArea {
            Layout.fillWidth: true
            Layout.minimumHeight: 60
            placeholderText: "Enter flight notes..."
            text: notes
            onTextChanged: notes = text
            background: Rectangle { color: Colors.surface; border.color: Colors.border; radius: Config.radiusSmall }
        }

        Text {
            text: "Incidents / Issues"
            font.pixelSize: Config.fontSizeH3
            font.bold: true
            color: Colors.textPrimary
        }

        TextArea {
            Layout.fillWidth: true
            Layout.minimumHeight: 60
            placeholderText: "Describe any issues encountered..."
            text: incidents
            onTextChanged: incidents = text
            background: Rectangle { color: Colors.surface; border.color: Colors.border; radius: Config.radiusSmall }
        }

        // ── Export ──
        Text {
            id: exportLabel
            text: VehicleTelemetry.exportStatus
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: text.length > 0
        }

        // ── Phase 8: Export scope options ──
        Rectangle {
            Layout.fillWidth: true
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border
            visible: VehicleProfileManager && VehicleProfileManager.currentDeviceUid !== ""

            ColumnLayout {
                anchors.margins: Config.spacingMedium
                anchors.fill: parent
                spacing: Config.spacingSmall

                Text {
                    text: "Export Preflight Data"
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    text: "This exports checklist results, overrides, and vehicle history — not the autopilot's raw flight log. Use QGC's Download Flight Log (Analyze tab) for .ulg/.bin logs."
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: Config.spacingSmall

                    CustomButton {
                        text: "Export Session CSV"
                        enabled: flightDuration > 0
                        implicitHeight: 28
                        font.pixelSize: Config.fontSizeSmall
                        onClicked: {
                            var token = Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss")
                            var sid = VehicleProfileManager ? VehicleProfileManager.currentFlightSessionId : -1
                            if (sid > 0) {
                                var p = ExportHelper.saveSessionCsv(sid, "preflight_session_" + token)
                                VehicleTelemetry.exportStatus = p.length > 0 ? "Saved: " + p : "Session export failed"
                            } else {
                                VehicleTelemetry.exportStatus = "No active session to export"
                            }
                        }
                    }

                    CustomButton {
                        text: "Export Session HTML"
                        enabled: flightDuration > 0
                        implicitHeight: 28
                        font.pixelSize: Config.fontSizeSmall
                        onClicked: {
                            var token = Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss")
                            var sid = VehicleProfileManager ? VehicleProfileManager.currentFlightSessionId : -1
                            var uid = VehicleProfileManager ? VehicleProfileManager.currentDeviceUid : ""
                            if (sid > 0 && uid.length > 0) {
                                var p = ExportHelper.saveSessionReport(sid, "preflight_report_" + token, uid)
                                VehicleTelemetry.exportStatus = p.length > 0 ? "Saved: " + p : "Report export failed"
                            } else {
                                VehicleTelemetry.exportStatus = "No active session to export"
                            }
                        }
                    }
                }

                RowLayout {
                    spacing: Config.spacingSmall

                    CustomButton {
                        text: "Export Vehicle CSV"
                        implicitHeight: 28
                        font.pixelSize: Config.fontSizeSmall
                        onClicked: {
                            var token = Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss")
                            var uid = VehicleProfileManager ? VehicleProfileManager.currentDeviceUid : ""
                            if (uid.length > 0) {
                                var p = ExportHelper.saveVehicleHistoryCsv(uid, "preflight_vehicle_" + token)
                                VehicleTelemetry.exportStatus = p.length > 0 ? "Saved: " + p : "Vehicle export failed"
                            } else {
                                VehicleTelemetry.exportStatus = "No vehicle connected"
                            }
                        }
                    }

                    CustomButton {
                        text: "Export Vehicle HTML"
                        implicitHeight: 28
                        font.pixelSize: Config.fontSizeSmall
                        onClicked: {
                            var token = Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss")
                            var uid = VehicleProfileManager ? VehicleProfileManager.currentDeviceUid : ""
                            if (uid.length > 0) {
                                var p = ExportHelper.saveVehicleReport(uid, "preflight_vehicle_" + token)
                                VehicleTelemetry.exportStatus = p.length > 0 ? "Saved: " + p : "Vehicle export failed"
                            } else {
                                VehicleTelemetry.exportStatus = "No vehicle connected"
                            }
                        }
                    }
                }

                CustomButton {
                    text: "Export Fleet CSV (stub)"
                    implicitHeight: 28
                    font.pixelSize: Config.fontSizeSmall
                    baseColor: Colors.surface
                    onClicked: {
                        var token = Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss")
                        var p = ExportHelper.saveFleetCsv("preflight_fleet_" + token)
                        VehicleTelemetry.exportStatus = p.length > 0 ? "Saved (stub): " + p : "Fleet export failed"
                    }
                }
            }
        }

        RowLayout {
            spacing: Config.spacingSmall
            Layout.alignment: Qt.AlignHCenter

            CustomButton {
                text: "Save Post-Flight Report"
                enabled: flightDuration > 0
                onClicked: saveReport()
            }

            CustomButton {
                text: "← Back"
                baseColor: Colors.surface
                onClicked: Window.window.mainStackView.pop()
            }
        }
    }

    function formatDuration(seconds) {
        var m = Math.floor(seconds / 60)
        var s = Math.floor(seconds % 60)
        return m + "m " + s + "s"
    }

    function saveReport() {
        var now = new Date()
        var token = Qt.formatDateTime(now, "yyyyMMdd_HHmmss")
        var baseName = "uav_postflight_" + token

        var record = {
            type: "post-flight",
            timestamp: Qt.formatDateTime(now, "yyyy-MM-ddTHH:mm:ssZ"),
            vehicleName: VehicleTelemetry.selectedVehicle,
            vehicleType: VehicleTelemetry.selectedVehicleTypeLabel,
            flightDurationSec: flightDuration,
            maxAltitude: maxAltitude,
            maxGroundSpeed: maxGroundSpeed,
            batteryVoltage: VehicleTelemetry.batteryVoltage,
            notes: notes,
            incidents: incidents,
            telemetry: {
                satellites: VehicleTelemetry.satellites,
                heading: VehicleTelemetry.heading,
                flightMode: TelemetryProvider ? TelemetryProvider.flightMode : "Unknown"
            }
        }

        var json = JSON.stringify(record, null, 2)

        var html = buildPostFlightHtml(record)
        var hp = ExportHelper.savePostFlightHtml(baseName, html)
        var jp = ExportHelper.saveComplianceJson(baseName, json)

        var paths = []
        if (hp.length > 0) paths.push(hp)
        if (jp.length > 0) paths.push(jp)

        VehicleTelemetry.exportStatus = paths.length > 0
            ? "Saved: " + paths.join(", ")
            : "Export failed"
    }

    function buildPostFlightHtml(rec) {
        var dur = formatDuration(rec.flightDurationSec)
        return "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            + "<title>Post-Flight Report</title>"
            + "<style>"
            + "body{font-family:system-ui,sans-serif;max-width:700px;margin:40px auto;padding:0 20px;color:#333;}"
            + "h1{font-size:22px;}"
            + ".meta{color:#666;font-size:13px;margin-bottom:20px;}"
            + "table{width:100%;border-collapse:collapse;margin:12px 0 20px;}"
            + "th,td{text-align:left;padding:6px 10px;border-bottom:1px solid #ddd;font-size:13px;}"
            + "th{background:#f5f5f5;font-weight:600;}"
            + "h2{font-size:16px;margin:20px 0 4px;border-bottom:1px solid #eee;padding-bottom:4px;}"
            + ".section{margin-bottom:20px;}"
            + ".notes{background:#f9f9f9;padding:12px;border-radius:6px;white-space:pre-wrap;font-size:13px;}"
            + ".footer{color:#999;font-size:11px;text-align:center;margin-top:40px;padding-top:12px;border-top:1px solid #eee;}"
            + "</style></head><body>"
            + "<h1>Post-Flight Report</h1>"
            + "<div class='meta'>" + rec.timestamp + " &middot; " + rec.vehicleName + " &middot; " + rec.vehicleType + "</div>"
            + "<div class='section'><h2>Flight Data</h2><table>"
            + "<tr><th>Metric</th><th>Value</th></tr>"
            + "<tr><td>Flight Duration</td><td>" + dur + "</td></tr>"
            + "<tr><td>Max Altitude</td><td>" + rec.maxAltitude.toFixed(1) + " m</td></tr>"
            + "<tr><td>Max Ground Speed</td><td>" + rec.maxGroundSpeed.toFixed(1) + " m/s</td></tr>"
            + "<tr><td>Battery Voltage</td><td>" + rec.batteryVoltage.toFixed(1) + " V</td></tr>"
            + "<tr><td>Satellites</td><td>" + rec.telemetry.satellites + "</td></tr>"
            + "<tr><td>Flight Mode</td><td>" + rec.telemetry.flightMode + "</td></tr>"
            + "</table></div>"
            + "<div class='section'><h2>Notes</h2><div class='notes'>" + (rec.notes || "—") + "</div></div>"
            + "<div class='section'><h2>Incidents</h2><div class='notes'>" + (rec.incidents || "None") + "</div></div>"
            + "<div class='footer'>Generated by UAV Preflight &mdash; " + rec.timestamp + "</div>"
            + "</body></html>"
    }
}
