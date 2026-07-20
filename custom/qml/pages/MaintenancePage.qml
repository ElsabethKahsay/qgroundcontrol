import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0

Page {
    background: Rectangle { color: Colors.background }

    Column {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingLarge

        Row {
            width: parent.width
            spacing: Config.spacingMedium
            Text {
                text: qsTr("Maintenance Tracker")
                font.pixelSize: Config.fontSizeH2
                color: Colors.primary
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { height: 1; width: 1; Layout.fillWidth: true }
            CustomButton {
                text: qsTr("← Back")
                baseColor: Colors.surface
                implicitHeight: 28
                onClicked: Window.window.mainStackView.pop()
            }
        }

        // ── Vehicle profile & battery history ─────────────────────
        Rectangle {
            width: parent.width
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border
            visible: VehicleProfileManager.currentDeviceUid !== ""

            Column {
                anchors.margins: Config.spacingMedium
                anchors.fill: parent
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("Current Vehicle")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    text: {
                        var h = VehicleProfileManager.currentVehicleHistoryJson
                        if (!h) return "No history"
                        try {
                            var o = JSON.parse(h)
                            var parts = []
                            if (o.autopilotType) parts.push(o.autopilotType)
                            if (o.airframeType) parts.push(o.airframeType)
                            if (o.friendlyName) parts.push('"' + o.friendlyName + '"')
                            return parts.length > 0 ? parts.join(" · ") : "Unknown"
                        } catch(e) { return "Unknown" }
                    }
                    font.pixelSize: Config.fontSizeBody
                    color: Colors.textSecondary
                }

                // Flight stats row
                Row {
                    spacing: Config.spacingLarge
                    visible: {
                        var h = VehicleProfileManager.currentVehicleHistoryJson
                        if (!h) return false
                        try {
                            var o = JSON.parse(h)
                            return o.totalFlightCount !== undefined
                        } catch(e) { return false }
                    }

                    Column {
                        Text { text: "Flights"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                        Text {
                            text: {
                                try { return JSON.parse(VehicleProfileManager.currentVehicleHistoryJson).totalFlightCount || 0 }
                                catch(e) { return 0 }
                            }
                            font.pixelSize: Config.fontSizeH3; font.bold: true; color: Colors.primary
                        }
                    }
                    Column {
                        Text { text: "Flight Hours"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                        Text {
                            text: {
                                try {
                                    var h = JSON.parse(VehicleProfileManager.currentVehicleHistoryJson).totalFlightHours || 0
                                    return h.toFixed(1)
                                } catch(e) { return "0.0" }
                            }
                            font.pixelSize: Config.fontSizeH3; font.bold: true; color: Colors.primary
                        }
                    }
                    Column {
                        Text { text: "Identity"; font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                        Text {
                            text: {
                                try {
                                    var src = JSON.parse(VehicleProfileManager.currentVehicleHistoryJson).identitySource || ""
                                    if (src === "hardware_uid") return "Hardware UID"
                                    return src
                                } catch(e) { return "" }
                            }
                            font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary
                        }
                    }
                }

                // Battery health trend
                Rectangle {
                    width: parent.width
                    color: Colors.background
                    radius: Config.radiusSmall
                    border.color: Colors.border
                    visible: {
                        try {
                            var h = JSON.parse(VehicleProfileManager.currentVehicleHistoryJson)
                            return h.batteryHealthTrend && h.batteryHealthTrend.capacityRetainedPct !== undefined
                        } catch(e) { return false }
                    }

                    Column {
                        anchors.margins: Config.spacingSmall
                        anchors.fill: parent
                        spacing: 2

                        Text {
                            text: qsTr("Battery Health")
                            font.pixelSize: Config.fontSizeH4; font.bold: true; color: Colors.textPrimary
                        }

                        Text {
                            text: {
                                try {
                                    var t = JSON.parse(VehicleProfileManager.currentVehicleHistoryJson).batteryHealthTrend
                                    return "Capacity retained: " + t.capacityRetainedPct.toFixed(1) + "%"
                                        + "  ·  Sag: " + t.averageVoltageSagV.toFixed(3) + "V"
                                        + "  ·  Cycles: " + t.totalCycles
                                } catch(e) { return "" }
                            }
                            font.pixelSize: Config.fontSizeSmall
                            color: {
                                try {
                                    var pct = JSON.parse(VehicleProfileManager.currentVehicleHistoryJson).batteryHealthTrend.capacityRetainedPct
                                    if (pct < 80) return Colors.danger
                                    if (pct < 90) return "#e6a817"
                                    return Colors.success
                                } catch(e) { return Colors.textSecondary }
                            }
                        }
                    }
                }
            }
        }

        // ── Phase 8: Export section ─────────────────────────────
        Rectangle {
            width: parent.width
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border
            visible: VehicleProfileManager.currentDeviceUid !== ""

            ColumnLayout {
                anchors.margins: Config.spacingMedium
                anchors.fill: parent
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("Export Vehicle Data")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    text: qsTr("Exports preflight checklist history, overrides, and battery data. To download the autopilot's raw flight log (.ulg/.bin), use the Analyze tab in the main QGC window.")
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: Config.spacingSmall

                    CustomButton {
                        text: qsTr("Export Vehicle CSV")
                        implicitHeight: 28
                        font.pixelSize: Config.fontSizeSmall
                        onClicked: {
                            var token = Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss")
                            var uid = VehicleProfileManager.currentDeviceUid
                            var p = ExportHelper.saveVehicleHistoryCsv(uid, "maintenance_export_" + token)
                            exportStatus.text = p.length > 0 ? "Saved: " + p : "Export failed"
                        }
                    }

                    CustomButton {
                        text: qsTr("Export Vehicle HTML")
                        implicitHeight: 28
                        font.pixelSize: Config.fontSizeSmall
                        onClicked: {
                            var token = Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss")
                            var uid = VehicleProfileManager.currentDeviceUid
                            var p = ExportHelper.saveVehicleReport(uid, "maintenance_export_" + token)
                            exportStatus.text = p.length > 0 ? "Saved: " + p : "Export failed"
                        }
                    }

                    CustomButton {
                        text: qsTr("Export Fleet CSV (stub)")
                        implicitHeight: 28
                        font.pixelSize: Config.fontSizeSmall
                        baseColor: Colors.surface
                        onClicked: {
                            var token = Qt.formatDateTime(new Date(), "yyyyMMdd_HHmmss")
                            var p = ExportHelper.saveFleetCsv("maintenance_fleet_" + token)
                            exportStatus.text = p.length > 0 ? "Saved (stub): " + p : "Export failed"
                        }
                    }
                }

                Text {
                    id: exportStatus
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    visible: text.length > 0
                }
            }
        }

        // ── Add component form ──────────────────────────────────
        Rectangle {
            width: parent.width
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border

            Row {
                anchors.margins: Config.spacingMedium
                anchors.fill: parent
                spacing: Config.spacingSmall

                TextField {
                    id: newName
                    placeholderText: qsTr("Name (e.g. Motor 1)")
                    implicitWidth: 160
                    font.pixelSize: Config.fontSizeSmall
                    background: Rectangle { color: Colors.background; radius: Config.radiusSmall; border.color: Colors.border }
                }
                ComboBox {
                    id: newType
                    model: ["motor", "propeller", "battery", "esc", "airframe", "other"]
                    implicitWidth: 120
                    currentIndex: 0
                }
                TextField {
                    id: newMaxHours
                    placeholderText: qsTr("Max hours")
                    implicitWidth: 80
                    font.pixelSize: Config.fontSizeSmall
                    background: Rectangle { color: Colors.background; radius: Config.radiusSmall; border.color: Colors.border }
                    validator: DoubleValidator { bottom: 0 }
                }
                TextField {
                    id: newMaxCycles
                    placeholderText: qsTr("Max cycles")
                    implicitWidth: 80
                    font.pixelSize: Config.fontSizeSmall
                    background: Rectangle { color: Colors.background; radius: Config.radiusSmall; border.color: Colors.border }
                    validator: IntValidator { bottom: 0 }
                }
                CustomButton {
                    text: qsTr("Add")
                    implicitHeight: 28
                    font.pixelSize: Config.fontSizeSmall
                    enabled: newName.text.length > 0
                    onClicked: {
                        MaintenanceTracker.addComponent(
                            newName.text,
                            newType.currentText,
                            parseFloat(newMaxHours.text) || 0,
                            parseInt(newMaxCycles.text) || 0
                        )
                        newName.text = ""
                        newMaxHours.text = ""
                        newMaxCycles.text = ""
                    }
                }
            }
        }

        // ── Component grid ──────────────────────────────────────
        Text {
            text: "Components (" + MaintenanceTracker.components.length + ")"
            font.pixelSize: Config.fontSizeH3
            font.bold: true
            color: Colors.textPrimary
        }

        ListView {
            width: parent.width
            height: parent.height - y - Config.spacingLarge
            clip: true
            spacing: Config.spacingMedium

            model: MaintenanceTracker.components

            delegate: Rectangle {
                width: parent.width
                color: Colors.surfaceLight
                radius: Config.radiusMedium
                border.color: Colors.border
                height: 80

                Row {
                    anchors.fill: parent
                    anchors.margins: Config.spacingMedium
                    spacing: Config.spacingMedium

                    Column {
                        width: 160
                        spacing: 2
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            text: modelData.name || ""
                            font.pixelSize: Config.fontSizeBody
                            font.bold: true
                            color: Colors.textPrimary
                        }
                        Text {
                            text: (modelData.type || "") + "  ·  ID: " + (modelData.id || 0)
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.textSecondary
                        }
                    }

                    // Hours progress
                    Column {
                        width: (parent.width - 320) / 2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        visible: (modelData.maxHours || 0) > 0

                        Row {
                            spacing: Config.spacingSmall
                            Text {
                                text: "Hours: " + (modelData.currentHours || 0).toFixed(1)
                                      + " / " + (modelData.maxHours || 0).toFixed(1)
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textPrimary
                            }
                            Text {
                                text: {
                                    var pct = modelData.maxHours > 0
                                        ? Math.round(modelData.currentHours / modelData.maxHours * 100)
                                        : 0
                                    return pct + "%"
                                }
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                color: {
                                    var pct = modelData.maxHours > 0
                                        ? modelData.currentHours / modelData.maxHours * 100
                                        : 0
                                    if (pct >= Config.kMaintCriticalPercent) return Colors.danger
                                    if (pct >= Config.kMaintWarningPercent) return "#e6a817"
                                    return Colors.success
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 8
                            radius: 4
                            color: Colors.border
                            Rectangle {
                                width: parent.width * Math.min(
                                    modelData.maxHours > 0 ? modelData.currentHours / modelData.maxHours : 0, 1)
                                height: parent.height
                                radius: 4
                                color: {
                                    var pct = modelData.maxHours > 0
                                        ? modelData.currentHours / modelData.maxHours * 100
                                        : 0
                                    if (pct >= Config.kMaintCriticalPercent) return Colors.danger
                                    if (pct >= Config.kMaintWarningPercent) return "#e6a817"
                                    return Colors.success
                                }
                            }
                        }
                    }

                    // Cycles progress
                    Column {
                        width: (parent.width - 320) / 2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        visible: (modelData.maxCycles || 0) > 0

                        Row {
                            spacing: Config.spacingSmall
                            Text {
                                text: "Cycles: " + (modelData.currentCycles || 0)
                                      + " / " + (modelData.maxCycles || 0)
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textPrimary
                            }
                            Text {
                                text: {
                                    var pct = modelData.maxCycles > 0
                                        ? Math.round(modelData.currentCycles / modelData.maxCycles * 100)
                                        : 0
                                    return pct + "%"
                                }
                                font.pixelSize: Config.fontSizeSmall
                                font.bold: true
                                color: {
                                    var pct = modelData.maxCycles > 0
                                        ? modelData.currentCycles / modelData.maxCycles * 100
                                        : 0
                                    if (pct >= Config.kMaintCriticalPercent) return Colors.danger
                                    if (pct >= Config.kMaintWarningPercent) return "#e6a817"
                                    return Colors.success
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 8
                            radius: 4
                            color: Colors.border
                            Rectangle {
                                width: parent.width * Math.min(
                                    modelData.maxCycles > 0 ? modelData.currentCycles / modelData.maxCycles : 0, 1)
                                height: parent.height
                                radius: 4
                                color: {
                                    var pct = modelData.maxCycles > 0
                                        ? modelData.currentCycles / modelData.maxCycles * 100
                                        : 0
                                    if (pct >= Config.kMaintCriticalPercent) return Colors.danger
                                    if (pct >= Config.kMaintWarningPercent) return "#e6a817"
                                    return Colors.success
                                }
                            }
                        }
                    }

                    CustomButton {
                        text: qsTr("Reset")
                        implicitHeight: 24
                        font.pixelSize: Config.fontSizeSmall
                        baseColor: Colors.surface
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: MaintenanceTracker.resetComponent(modelData.id, "")
                    }

                    CustomButton {
                        text: "✕"
                        implicitHeight: 24
                        implicitWidth: 24
                        font.pixelSize: Config.fontSizeSmall
                        baseColor: Colors.surface
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: MaintenanceTracker.removeComponent(modelData.id)
                    }
                }
            }

            Text {
                text: qsTr("No components tracked — add one above")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textSecondary
                visible: parent.count === 0
                anchors.centerIn: parent
            }
        }
    }
}
