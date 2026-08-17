import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.ScreenTools

import com.uav.preflight 1.0

Dialog {
    id: root

    signal cancelled()
    signal submitted()

    title: qsTr("Flight Record")
    modal: true
    anchors.centerIn: parent
    width: Math.min(520, parent.width * 0.92)
    closePolicy: Popup.NoAutoClose
    focus: true
    padding: Config.spacingLarge

    background: Rectangle {
        color: Colors.surface
        border.color: Colors.border
        border.width: 1
        radius: Config.radiusMedium
    }

    header: Rectangle {
        color: Colors.surfaceLight
        radius: Config.radiusMedium
        height: 48
        width: parent.width

        Text {
            anchors.centerIn: parent
            text: qsTr("\u2708\uFE0F  Start Flight Record")
            font.pixelSize: Config.fontSizeH3
            font.bold: true
            color: Colors.textPrimary
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Config.spacingMedium

        // Vehicle (read-only)
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: qsTr("Vehicle:")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textSecondary
                Layout.preferredWidth: 100
            }

            Rectangle {
                Layout.fillWidth: true
                height: 32
                color: Colors.surfaceLight
                radius: Config.radiusSmall
                border.color: Colors.border
                border.width: 1

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Config.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    text: VehicleRegistry.currentVehicleName.length > 0
                          ? VehicleRegistry.currentVehicleName
                          : qsTr("Connected vehicle")
                    font.pixelSize: Config.fontSizeBody
                    color: Colors.textPrimary
                }
            }
        }

        // Date & Time (read-only)
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: qsTr("Date & Time:")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textSecondary
                Layout.preferredWidth: 100
            }

            Rectangle {
                Layout.fillWidth: true
                height: 32
                color: Colors.surfaceLight
                radius: Config.radiusSmall
                border.color: Colors.border
                border.width: 1

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Config.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    text: Qt.formatDateTime(new Date(), "dd MMM yyyy  HH:mm")
                    font.pixelSize: Config.fontSizeBody
                    color: Colors.textPrimary
                }
            }
        }

        // Weather (read-only)
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: qsTr("Weather:")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textSecondary
                Layout.preferredWidth: 100
            }

            Rectangle {
                Layout.fillWidth: true
                height: 32
                color: Colors.surfaceLight
                radius: Config.radiusSmall
                border.color: Colors.border
                border.width: 1

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Config.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    text: WeatherProvider.currentSummary
                    font.pixelSize: Config.fontSizeBody
                    color: Colors.textPrimary
                    elide: Text.ElideRight
                }
            }
        }

        // Mission Purpose (required)
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: qsTr("Purpose:")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textSecondary
                Layout.preferredWidth: 100
            }

            ComboBox {
                id: purposeCombo
                Layout.fillWidth: true
                font.pixelSize: Config.fontSizeBody
                model: ["Select purpose...", "Test Flight", "Survey", "Inspection", "Mapping", "Training Flight", "Research", "Other"]
                currentIndex: 0

                background: Rectangle {
                    color: Colors.surfaceLight
                    border.color: purposeCombo.activeFocus ? Colors.accentCyan : Colors.border
                    border.width: 1
                    radius: Config.radiusSmall
                }

                contentItem: Text {
                    text: purposeCombo.currentText
                    color: purposeCombo.currentIndex === 0 ? Colors.textDisabled : Colors.textPrimary
                    font.pixelSize: Config.fontSizeBody
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Config.spacingSmall
                }

                indicator: Text {
                    x: purposeCombo.width - width - Config.spacingSmall
                    y: (purposeCombo.height - height) / 2
                    text: "\u25BC"
                    color: Colors.textSecondary
                    font.pixelSize: 10
                }
            }
        }

        // Location / Site (required)
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: qsTr("Location:")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textSecondary
                Layout.preferredWidth: 100
            }

            TextField {
                id: locationField
                Layout.fillWidth: true
                placeholderText: qsTr("Required — e.g. Site A, Hangar 3")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textPrimary
                background: Rectangle {
                    color: Colors.surfaceLight
                    border.color: locationField.activeFocus ? Colors.accentCyan : Colors.border
                    border.width: 1
                    radius: Config.radiusSmall
                }
            }
        }

        // Notes (optional)
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: qsTr("Notes:")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textSecondary
                Layout.preferredWidth: 100
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Config.spacingSmall
            }

            TextArea {
                id: notesField
                Layout.fillWidth: true
                Layout.minimumHeight: 72
                placeholderText: qsTr("Optional notes...")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textPrimary
                wrapMode: TextArea.WordWrap
                background: Rectangle {
                    color: Colors.surfaceLight
                    border.color: notesField.activeFocus ? Colors.accentCyan : Colors.border
                    border.width: 1
                    radius: Config.radiusSmall
                }
            }
        }
    }

    // ── Validation ─────────────────────────────────────────────
    readonly property bool _canSubmit: purposeCombo.currentIndex > 0
                                       && locationField.text.trim().length > 0

    // ── Footer buttons ──────────────────────────────────────────
    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        spacing: Config.spacingMedium

        background: Rectangle {
            color: Colors.surface
            border.color: Colors.divider
            border.width: 1
        }

        Button {
            id: cancelBtn
            text: qsTr("Cancel")
            font.pixelSize: Config.fontSizeBody

            contentItem: Text {
                text: cancelBtn.text
                color: Colors.textSecondary
                font.pixelSize: Config.fontSizeBody
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: Colors.surfaceLight
                radius: Config.radiusSmall
                border.color: Colors.border
                border.width: 1
            }

            onClicked: {
                FlightSession.cancelForm()
                root.cancelled()
                root.close()
            }
        }

        Button {
            id: submitBtn
            text: qsTr("Start Flight Record")
            font.pixelSize: Config.fontSizeBody
            enabled: root._canSubmit

            contentItem: Text {
                text: submitBtn.text
                color: submitBtn.enabled ? Colors.textPrimary : Colors.textDisabled
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: submitBtn.enabled ? Colors.teal : Colors.surfaceLight
                radius: Config.radiusSmall
                border.color: submitBtn.enabled ? Colors.accentCyan : Colors.border
                border.width: 1
            }

            onClicked: {
                FlightSession.startFlightSession(
                    purposeCombo.currentText,
                    locationField.text.trim(),
                    notesField.text.trim()
                )
                root.submitted()
                root.close()
            }
        }
    }
}
