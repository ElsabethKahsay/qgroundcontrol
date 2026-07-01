import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import com.uav.preflight 1.0

Rectangle {
    id: settingsPage
    color: qgcPal.window

    Flickable {
        anchors.fill: parent
        anchors.margins: ScreenTools.defaultFontPixelHeight
        contentHeight: settingsColumn.height + ScreenTools.defaultFontPixelHeight * 2
        clip: true

        ColumnLayout {
            id: settingsColumn
            width: parent.width
            spacing: ScreenTools.defaultFontPixelHeight

            Label {
                text: qsTr("UAV Preflight Settings")
                font.pointSize: ScreenTools.largeFontPointSize
                color: qgcPal.text
            }

            Rectangle {
                width: parent.width
                height: 1
                color: qgcPal.buttonBorder
            }

            GridLayout {
                columns: 2
                columnSpacing: ScreenTools.defaultFontPixelWidth * 2
                rowSpacing: ScreenTools.defaultFontPixelHeight * 0.5

                Label {
                    text: qsTr("Pilot Name")
                    color: qgcPal.text
                }
                QGCTextField {
                    Layout.fillWidth: true
                    text: PreflightSettingsManager.pilotName
                    onTextChanged: PreflightSettingsManager.pilotName = text
                }

                Label {
                    text: qsTr("Pilot License / ID")
                    color: qgcPal.text
                }
                QGCTextField {
                    Layout.fillWidth: true
                    text: PreflightSettingsManager.pilotLicense
                    onTextChanged: PreflightSettingsManager.pilotLicense = text
                }

                Label {
                    text: qsTr("Aircraft Registration")
                    color: qgcPal.text
                }
                QGCTextField {
                    Layout.fillWidth: true
                    text: PreflightSettingsManager.aircraftReg
                    onTextChanged: PreflightSettingsManager.aircraftReg = text
                }

                Label {
                    text: qsTr("FAA Part 107 Mode")
                    color: qgcPal.text
                }
                QGCCheckBox {
                    checked: PreflightSettingsManager.faaPart107Mode
                    onClicked: PreflightSettingsManager.faaPart107Mode = !PreflightSettingsManager.faaPart107Mode
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: qgcPal.buttonBorder
            }

            Label {
                text: qsTr("Video")
                font.pointSize: ScreenTools.mediumFontPointSize
                color: qgcPal.text
            }

            GridLayout {
                columns: 2
                columnSpacing: ScreenTools.defaultFontPixelWidth * 2
                rowSpacing: ScreenTools.defaultFontPixelHeight * 0.5

                Label {
                    text: qsTr("Require video feed for preflight pass")
                    color: qgcPal.text
                }
                QGCCheckBox {
                    checked: PreflightSettingsManager.videoRequiredForPass
                    onClicked: PreflightSettingsManager.videoRequiredForPass = !PreflightSettingsManager.videoRequiredForPass
                }

                Label {
                    text: qsTr("Auto-record on arm")
                    color: qgcPal.text
                }
                QGCCheckBox {
                    checked: PreflightSettingsManager.autoRecordOnArm
                    onClicked: PreflightSettingsManager.autoRecordOnArm = !PreflightSettingsManager.autoRecordOnArm
                }

                Label {
                    text: qsTr("Show telemetry overlay")
                    color: qgcPal.text
                }
                QGCCheckBox {
                    checked: PreflightSettingsManager.showTelemetryOverlay
                    onClicked: PreflightSettingsManager.showTelemetryOverlay = !PreflightSettingsManager.showTelemetryOverlay
                }

                Label {
                    text: qsTr("Video stream source override")
                    color: qgcPal.text
                }
                QGCTextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Leave empty to use QGC default")
                    text: PreflightSettingsManager.videoStreamUrlOverride
                    onTextChanged: PreflightSettingsManager.videoStreamUrlOverride = text
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: qgcPal.buttonBorder
            }

            Label {
                text: qsTr("Weather")
                font.pointSize: ScreenTools.mediumFontPointSize
                color: qgcPal.text
            }

            GridLayout {
                columns: 2
                columnSpacing: ScreenTools.defaultFontPixelWidth * 2
                rowSpacing: ScreenTools.defaultFontPixelHeight * 0.5

                Label { text: qsTr("Auto-fetch weather"); color: qgcPal.text }
                QGCCheckBox {
                    checked: PreflightSettingsManager.autoWeatherEnabled
                    onClicked: PreflightSettingsManager.autoWeatherEnabled = !PreflightSettingsManager.autoWeatherEnabled
                }

                Label { text: qsTr("Default ICAO"); color: qgcPal.text }
                QGCTextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("e.g. KLAX")
                    maximumLength: 4
                    text: PreflightSettingsManager.defaultIcao
                    onTextChanged: PreflightSettingsManager.defaultIcao = text
                }

                Label { text: qsTr("Wind sustained max (m/s)"); color: qgcPal.text }
                SpinBox {
                    from: 1; to: 30; stepSize: 1
                    value: PreflightSettingsManager.windThresholdSustained
                    onValueChanged: PreflightSettingsManager.windThresholdSustained = value
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Wind gust max (m/s)"); color: qgcPal.text }
                SpinBox {
                    from: 1; to: 35; stepSize: 1
                    value: PreflightSettingsManager.windThresholdGust
                    onValueChanged: PreflightSettingsManager.windThresholdGust = value
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Visibility min (km)"); color: qgcPal.text }
                SpinBox {
                    from: 1; to: 50; stepSize: 1
                    value: PreflightSettingsManager.visibilityThresholdKm
                    onValueChanged: PreflightSettingsManager.visibilityThresholdKm = value
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Ceiling min (m)"); color: qgcPal.text }
                SpinBox {
                    from: 0; to: 3000; stepSize: 10
                    value: PreflightSettingsManager.ceilingThresholdM
                    onValueChanged: PreflightSettingsManager.ceilingThresholdM = value
                    Layout.fillWidth: true
                }

                Label { text: qsTr("Update interval (min)"); color: qgcPal.text }
                SpinBox {
                    from: 1; to: 15; stepSize: 1
                    value: PreflightSettingsManager.weatherUpdateIntervalMin
                    onValueChanged: PreflightSettingsManager.weatherUpdateIntervalMin = value
                    Layout.fillWidth: true
                }
            }

            Label {
                text: qsTr("These settings are used to populate preflight checklist fields and log entries.")
                wrapMode: Text.WordWrap
                color: qgcPal.text
                font.pointSize: ScreenTools.smallFontPointSize
                Layout.fillWidth: true
            }
        }
    }
}
