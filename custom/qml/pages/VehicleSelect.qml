// SPDX-License-Identifier: GPL-3.0-or-later
// Vehicle selection and MAVLink connection (PX4 / ArduPilot SITL or serial).
// Priority: USB autopilot (physical) > USB radio > SITL > saved > UDP fallback

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Page {
    id: root
    background: Rectangle { color: Colors.background }

    property int selectedPresetIndex: 0

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        Column {
            width: parent.width
            anchors.margins: Config.spacingLarge
            leftPadding: Config.spacingLarge
            rightPadding: Config.spacingLarge
            topPadding: Config.spacingLarge
            bottomPadding: Config.spacingLarge
            spacing: Config.spacingMedium

            Text {
                text: qsTr("MAVLink Connection")
                font.pixelSize: Config.fontSizeH2
                color: Colors.secondary
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                width: parent.width - 2 * Config.spacingLarge
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: TelemetryProvider.isConnected
                      ? "+ " + TelemetryProvider.connectionStatus
                      : "! " + (TelemetryProvider.connectionError || TelemetryProvider.connectionStatus)
                font.pixelSize: Config.fontSizeSmall
                color: TelemetryProvider.isConnected ? Colors.success : Colors.warning
            }

            Text {
                text: TelemetryProvider.autopilotType
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
                anchors.horizontalCenter: parent.horizontalCenter
                visible: TelemetryProvider.isConnected
            }

            // ── USB Device Detection Panel ─────────────────────────────────
            Rectangle {
                width: parent.width - 2 * Config.spacingLarge
                height: usbCol.implicitHeight + Config.spacingMedium * 2
                color: AutopilotDetector.usbAutopilotDetected ? Qt.rgba(0.1, 0.8, 0.3, 0.08) : Colors.surface
                radius: Config.radiusMedium
                border.color: AutopilotDetector.usbAutopilotDetected ? Colors.success : Colors.border
                border.width: AutopilotDetector.usbAutopilotDetected ? 2 : 1

                Column {
                    id: usbCol
                    anchors.fill: parent
                    anchors.margins: Config.spacingMedium
                    spacing: Config.spacingSmall

                    Row {
                        spacing: Config.spacingSmall
                        Text {
                            text: AutopilotDetector.usbAutopilotDetected ? "USB" : "..."
                            font.pixelSize: Config.fontSizeH2
                            verticalAlignment: Text.AlignVCenter
                        }
                        Column {
                            spacing: 2
                            Text {
                                text: AutopilotDetector.usbAutopilotDetected
                                    ? "USB Autopilot Detected"
                                    : "Scanning USB ports..."
                                font.pixelSize: Config.fontSizeBody
                                font.bold: true
                                color: AutopilotDetector.usbAutopilotDetected ? Colors.success : Colors.textSecondary
                            }
                            Text {
                                visible: AutopilotDetector.usbAutopilotDetected
                                text: AutopilotDetector.detectedDeviceName + " — " + AutopilotDetector.detectedDevicePath
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textSecondary
                            }
                            Text {
                                visible: AutopilotDetector.connectionSource.length > 0
                                text: "Source: " + AutopilotDetector.connectionSource
                                font.pixelSize: Config.fontSizeSmall
                                color: Colors.textDisabled
                            }
                        }
                    }

                    // Show all detected USB devices
                    Repeater {
                        model: AutopilotDetector.detectedDevices
                        delegate: Rectangle {
                            width: usbCol.width
                            height: devRow.implicitHeight + Config.spacingSmall
                            color: index === 0 ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                            radius: Config.radiusSmall

                            Row {
                                id: devRow
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: Config.spacingSmall
                                spacing: Config.spacingSmall

                                Text {
                                    text: index === 0 ? "*" : "."
                                    font.pixelSize: Config.fontSizeSmall
                                    color: index === 0 ? Colors.primary : Colors.textDisabled
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Text {
                                    text: modelData.friendlyName
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: index === 0
                                    color: Colors.textPrimary
                                }

                                Text {
                                    text: modelData.devicePath
                                    font.pixelSize: Config.fontSizeSmall
                                    color: Colors.textDisabled
                                }

                                CustomButton {
                                    text: qsTr("Connect")
                                    visible: !TelemetryProvider.isConnected || index > 0
                                    baseColor: Colors.surface
                                    height: 28
                                    onClicked: {
                                        urlField.text = modelData.connectionUrl
                                        TelemetryProvider.saveConnectionUrl(modelData.connectionUrl)
                                        TelemetryProvider.connectToVehicle(modelData.connectionUrl)
                                    }
                                }
                            }
                        }
                    }

                    // Rescan button
                    CustomButton {
                        text: qsTr("↻ Rescan USB")
                        width: 120
                        height: 28
                        baseColor: Colors.surface
                        onClicked: AutopilotDetector.rescan()
                    }
                }
            }

            // ── Manual Connection ──────────────────────────────────────────
            Rectangle {
                width: parent.width - 2 * Config.spacingLarge
                height: 1
                color: Colors.border
            }

            Text {
                text: qsTr("Manual Connection")
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: Colors.textSecondary
                anchors.horizontalCenter: parent.horizontalCenter
            }

            ComboBox {
                id: presetCombo
                width: parent.width - 2 * Config.spacingLarge
                model: TelemetryProvider.connectionPresetLabels
                currentIndex: selectedPresetIndex
                onActivated: selectedPresetIndex = currentIndex
            }

            TextField {
                id: urlField
                width: parent.width - 2 * Config.spacingLarge
                text: TelemetryProvider.defaultConnectionUrl
                placeholderText: qsTr("udp://:14550")
                color: Colors.textPrimary
                placeholderTextColor: Colors.textDisabled
                background: Rectangle {
                    color: Colors.surface
                    radius: Config.radiusSmall
                    border.color: Colors.border
                }
            }

            Row {
                spacing: Config.spacingMedium
                width: parent.width - 2 * Config.spacingLarge

                CustomButton {
                    text: TelemetryProvider.isConnected ? "Reconnect" : "Connect"
                    width: (parent.width - Config.spacingMedium) / 2
                    onClicked: {
                        const url = urlField.text.trim().length > 0
                                ? urlField.text.trim()
                                : TelemetryProvider.connectionPresetUrls[presetCombo.currentIndex]
                        TelemetryProvider.saveConnectionUrl(url)
                        TelemetryProvider.connectToVehicle(url)
                    }
                }

                CustomButton {
                    text: qsTr("Disconnect")
                    width: (parent.width - Config.spacingMedium) / 2
                    baseColor: Colors.surface
                    enabled: TelemetryProvider.isConnected
                    onClicked: TelemetryProvider.disconnectFromVehicle()
                }
            }

            CustomButton {
                text: qsTr("Use preset URL")
                width: parent.width - 2 * Config.spacingLarge
                baseColor: Colors.surface
                onClicked: {
                    urlField.text = TelemetryProvider.connectionPresetUrls[presetCombo.currentIndex]
                }
            }

            Rectangle {
                width: parent.width - 2 * Config.spacingLarge
                height: 1
                color: Colors.border
            }

            Text {
                text: qsTr("Vehicle Detection")
                font.pixelSize: Config.fontSizeH2
                color: Colors.secondary
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // Auto-detected vehicle type
            Rectangle {
                width: parent.width - 2 * Config.spacingLarge
                height: 80
                color: Colors.surface
                radius: Config.radiusMedium
                border.color: Colors.border

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Config.spacingSmall

                    Text {
                        text: VehicleTelemetry.vehicleType.length > 0
                            ? VehicleTelemetry.vehicleType + " detected"
                            : VehicleTelemetry.heartbeatReceived
                                ? "Heartbeat received - unknown vehicle type"
                                : "Waiting for autopilot heartbeat..."
                        color: VehicleTelemetry.vehicleType.length > 0 ? Colors.success
                             : VehicleTelemetry.heartbeatReceived ? Colors.warning
                             : Colors.textSecondary
                        font.pixelSize: Config.fontSizeBody
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        visible: VehicleTelemetry.vehicleType.length > 0
                        text: "Vehicle: " + VehicleTelemetry.selectedVehicle
                        color: Colors.textSecondary
                        font.pixelSize: Config.fontSizeSmall
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // Manual override button — always visible, lets user proceed to checklist even when auto-detect fires
            CustomButton {
                visible: true
                text: VehicleTelemetry.heartbeatReceived
                    ? "Proceed to Checklist (" + (VehicleTelemetry.vehicleType.length > 0 ? VehicleTelemetry.vehicleType + " detected" : "unknown type") + ")"
                    : "Select Vehicle Manually"
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Config.spacingLarge
                baseColor: Colors.surface
                enabled: TelemetryProvider.isConnected
                onClicked: {
                    Window.window.mainStackView.push("PreFlightChecklist.qml")
                }
            }

            // Auto-select checklist template once vehicle type is detected (user clicks button to proceed)
            Connections {
                target: TelemetryProvider
                function onVehicleTypeChanged() {
                    var vt = TelemetryProvider.vehicleType
                    if (vt === "Quad" || vt === "FixedWing" || vt === "VTOL") {
                        VehicleTelemetry.selectedVehicleType = vt
                        VehicleTelemetry.selectedVehicle = TelemetryProvider.vehicleType
                        ChecklistModel.loadTemplateForType(vt)
                        ChecklistEngine.reevaluateAll()
                    }
                }
            }

            Text {
                visible: !TelemetryProvider.isConnected
                width: parent.width - 2 * Config.spacingLarge
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: AutopilotDetector.usbAutopilotDetected
                    ? "USB autopilot detected — connect above or plug in a different device."
                    : "No USB autopilot found. Connect via USB-C or start ArduPilot SITL."
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
            }

            CustomButton {
                text: qsTr("Manage Vehicles")
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Config.spacingLarge
                baseColor: Colors.surface
                onClicked: Window.window.mainStackView.push("VehicleManagement.qml")
            }

            CustomButton {
                text: qsTr("← Back")
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Config.spacingLarge
                baseColor: Colors.surface
                onClicked: Window.window.mainStackView.pop()
            }
        }
    }

    Component.onCompleted: {
        const urls = TelemetryProvider.connectionPresetUrls
        const current = TelemetryProvider.defaultConnectionUrl
        for (let i = 0; i < urls.length; ++i) {
            if (urls[i] === current) {
                presetCombo.currentIndex = i
                break
            }
        }

        // If AutopilotDetector found a USB device, pre-fill the URL field
        if (AutopilotDetector.usbAutopilotDetected) {
            urlField.text = AutopilotDetector.detectedConnectionUrl
        } else {
            urlField.text = current
        }
    }
}