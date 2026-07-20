import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0
Page {
    background: Rectangle { color: Colors.background }

    Column {
        anchors.fill: parent
        anchors.margins: Config.spacingLarge
        spacing: Config.spacingLarge

        Text {
            text: qsTr("Final Checks")
            font.pixelSize: Config.fontSizeH2
            color: Colors.primary
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // ── Weather card ──
        Rectangle {
            width: parent.width
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border

            Column {
                anchors.fill: parent
                anchors.margins: Config.spacingMedium
                spacing: Config.spacingSmall

                Row {
                    spacing: Config.spacingMedium
                    width: parent.width

                    Text { text: "Weather (" + VehicleTelemetry.gpsLatitude.toFixed(2) + ", " + VehicleTelemetry.gpsLongitude.toFixed(2) + ")"; font.pixelSize: Config.fontSizeH3; font.bold: true; color: Colors.textPrimary }

                    CustomButton {
                        text: WeatherProvider.loading ? "Loading..." : "Refresh"
                        baseColor: Colors.surface
                        implicitHeight: 24
                        font.pixelSize: Config.fontSizeSmall
                        enabled: !WeatherProvider.loading
                        onClicked: WeatherProvider.fetchWeather(VehicleTelemetry.gpsLatitude, VehicleTelemetry.gpsLongitude)
                    }
                }

                Text {
                    text: {
                        if (WeatherProvider.loading) return "Fetching weather data..."
                        if (WeatherProvider.lastError.length > 0) return "Error: " + WeatherProvider.lastError
                        if (WeatherProvider.temperature === 0) return "Tap Refresh to fetch live weather"
                        return WeatherProvider.temperature.toFixed(1) + "°C  ·  "
                            + WeatherProvider.weatherDescription + "  ·  "
                            + "Wind: " + WeatherProvider.windSpeed.toFixed(1) + " m/s  ·  "
                            + "Visibility: " + (WeatherProvider.visibility > 0 ? (WeatherProvider.visibility / 1000).toFixed(1) + " km" : "N/A")
                    }
                    font.pixelSize: Config.fontSizeBody
                    color: Colors.textSecondary
                    wrapMode: Text.WordWrap
                }
            }
        }

        ChecklistItem {
            text: qsTr("Airspace Clear")
            checked: VehicleTelemetry.airspaceClear
            status: VehicleTelemetry.airspaceClear ? "passed" : "failed"
            onToggled: VehicleTelemetry.airspaceClear = checked
        }

        ChecklistItem {
            text: "Wind Within Limits" + (WeatherProvider.windSpeed > 0 ? " (" + WeatherProvider.windSpeed.toFixed(1) + " m/s)" : "")
            checked: VehicleTelemetry.windOk
            status: VehicleTelemetry.windOk ? "passed" : "failed"
            onToggled: VehicleTelemetry.windOk = checked
        }

        ChecklistItem {
            text: qsTr("Home Point Set")
            checked: VehicleTelemetry.homePointSet
            status: VehicleTelemetry.homePointSet ? "passed" : "failed"
            onToggled: VehicleTelemetry.homePointSet = checked
        }

        // ── Operator Info ────────────────────────────
        Rectangle {
            width: parent.width
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border

            Column {
                anchors.fill: parent
                anchors.margins: Config.spacingMedium
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("Operator Info")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.textPrimary
                }

                Row {
                    spacing: Config.spacingMedium
                    width: parent.width
                    Text {
                        text: qsTr("Operator Name")
                        width: 140
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        text: VehicleTelemetry.pilotName
                        placeholderText: qsTr("Full name")
                        font.pixelSize: Config.fontSizeSmall
                        implicitWidth: 200
                        background: Rectangle { color: Colors.background; radius: Config.radiusSmall; border.color: Colors.border }
                        onTextChanged: {
                            VehicleTelemetry.pilotName = text
                            PreflightSettingsManager.pilotName = text
                        }
                    }
                }
                Row {
                    spacing: Config.spacingMedium
                    width: parent.width
                    Text {
                        text: qsTr("License / Cert #")
                        width: 140
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        text: VehicleTelemetry.pilotLicense
                        placeholderText: qsTr("FAA license or certificate")
                        font.pixelSize: Config.fontSizeSmall
                        implicitWidth: 200
                        background: Rectangle { color: Colors.background; radius: Config.radiusSmall; border.color: Colors.border }
                        onTextChanged: {
                            VehicleTelemetry.pilotLicense = text
                            PreflightSettingsManager.pilotLicense = text
                        }
                    }
                }
                Row {
                    spacing: Config.spacingMedium
                    width: parent.width
                    Text {
                        text: qsTr("Aircraft Reg")
                        width: 140
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TextField {
                        id: aircraftRegField
                        text: VehicleTelemetry.aircraftReg
                        placeholderText: qsTr("Tail number (N-XXXXX)")
                        font.pixelSize: Config.fontSizeSmall
                        implicitWidth: 200
                        background: Rectangle { color: Colors.background; radius: Config.radiusSmall; border.color: Colors.border }
                        onTextChanged: {
                            VehicleTelemetry.aircraftReg = text
                            PreflightSettingsManager.aircraftReg = text
                        }
                        function generateNNumber() {
                            var letters = "ABCDEFGHJKLMNPQRSTUVWXYZ"
                            var digits = "0123456789"
                            var n = "N"
                            for (var i = 0; i < 5; i++)
                                n += digits.charAt(Math.floor(Math.random() * digits.length))
                            n += letters.charAt(Math.floor(Math.random() * letters.length))
                            return n
                        }
                        Component.onCompleted: {
                            if (text === "")
                                text = generateNNumber()
                        }
                    }
                    CustomButton {
                        text: qsTr("\u21BA")
                        implicitWidth: 28
                        implicitHeight: 28
                        baseColor: Colors.surface
                        onClicked: aircraftRegField.text = aircraftRegField.generateNNumber()
                    }
                }
            }
        }

        // ── FAA Part 107 Compliance ───────────────────────────
        Rectangle {
            width: parent.width
            color: Colors.surfaceLight
            radius: Config.radiusMedium
            border.color: Colors.border
            visible: VehicleTelemetry.faaPart107Mode

            Column {
                anchors.fill: parent
                anchors.margins: Config.spacingMedium
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("FAA Part 107 Signature")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    text: VehicleTelemetry.digitalSignature.length > 0
                        ? "Signature: " + VehicleTelemetry.digitalSignature.substring(0, 16) + "\u2026"
                        : "No signature \u2014 export generates one"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    font.family: "monospace"
                }
            }
        }

        Row {
            spacing: Config.spacingMedium
            anchors.horizontalCenter: parent.horizontalCenter
            Text {
                text: qsTr("FAA Part 107 Mode")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textPrimary
                anchors.verticalCenter: parent.verticalCenter
            }
            Switch {
                checked: VehicleTelemetry.faaPart107Mode
                onCheckedChanged: VehicleTelemetry.faaPart107Mode = checked
            }
        }

        Text {
            text: VehicleTelemetry.hardwareTestPassed
                  ? "Hardware verification: PASS"
                  : "Hardware verification: required (Pre-Flight checklist)"
            font.pixelSize: Config.fontSizeSmall
            color: VehicleTelemetry.hardwareTestPassed ? Colors.success : Colors.warning
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: VehicleTelemetry.readyToLaunch ? "Ready to launch" : "Complete all final checks"
            font.pixelSize: Config.fontSizeBody
            color: VehicleTelemetry.readyToLaunch ? Colors.success : Colors.warning
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Row {
            spacing: Config.spacingMedium
            anchors.horizontalCenter: parent.horizontalCenter
            CustomButton {
                text: qsTr("← Back")
                width: (parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 3
                onClicked: Window.window.mainStackView.pop()
            }
            CustomButton {
                text: qsTr("Launch Ready →")
                width: (parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 3
                enabled: VehicleTelemetry.readyToLaunch
                onClicked: Window.window.mainStackView.push("LaunchReady.qml")
            }
            CustomButton {
                text: qsTr("Post-Flight")
                width: (parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 3
                baseColor: Colors.surface
                onClicked: Window.window.mainStackView.push("PostFlightSummary.qml")
            }
        }

        // Auto-fetch weather on page load
        Component.onCompleted: {
            if (VehicleTelemetry.gpsLatitude !== 0 || VehicleTelemetry.gpsLongitude !== 0)
                WeatherProvider.fetchWeather(VehicleTelemetry.gpsLatitude, VehicleTelemetry.gpsLongitude)
        }
    }
}
