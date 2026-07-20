// Component: ArmGateDialog
// Purpose: Modal dialog for reviewing preflight failures and acknowledging/overriding the
//   arming gate. Requires pilot name and reason before allowing override.
// Properties:
//   criticalFailCount (int) — number of critical (auto-failed) checks blocking arming
//   manualFailCount (int) — number of manual checks still pending confirmation
//   failedChecksModel (var) — model of failed checks to display in the list
// Signals:
//   reviewChecksRequested — emitted when user clicks "Review Checks" to navigate to checklist
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Dialog {
    id: root

    property int criticalFailCount: 0
    property int manualFailCount: 0
    property var failedChecksModel: PreflightManager.checks

    signal reviewChecksRequested()

    title: qsTr("Arm Gate — Preflight Review")
    modal: true
    anchors.centerIn: parent
    width: Math.min(520, parent.width * 0.92)
    closePolicy: Popup.CloseOnEscape
    focus: true
    padding: Config.spacingMedium

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

        RowLayout {
            anchors.fill: parent
            anchors.margins: Config.spacingMedium
            spacing: Config.spacingSmall

            Text {
                text: qsTr("\u26A0")
                font.pixelSize: 20
                color: Colors.error
            }
            Text {
                text: root.title
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: Colors.textPrimary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: ArmingGate.armingAllowed ? "Gate Open" : "Gate Closed"
                font.pixelSize: Config.fontSizeSmall
                font.bold: true
                color: ArmingGate.armingAllowed ? Colors.success : Colors.error
            }
        }
    }

    ColumnLayout {
        spacing: Config.spacingMedium

        Text {
            text: "Preflight checks detected " + root.criticalFailCount + " critical failure(s) "
                  + "and " + root.manualFailCount + " manual check(s) not confirmed."
            font.pixelSize: Config.fontSizeBody
            color: Colors.textPrimary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            color: Colors.surfaceLight
            radius: Config.radiusSmall
            border.color: Colors.border
            border.width: 1
            clip: true

            ListView {
                anchors.fill: parent
                anchors.margins: Config.spacingSmall
                spacing: 2
                model: failedChecksModel
                delegate: Item {
                    width: parent.width
                    height: 24
                    visible: {
                        var s = model.modelData ? model.modelData.status : 0
                        return s === 2 || s === 4
                    }
                    RowLayout {
                        width: parent.width
                        spacing: Config.spacingSmall
                        Text {
                            text: qsTr("\u2717")
                            font.pixelSize: 12
                            color: Colors.error
                        }
                        Text {
                            text: model.modelData ? model.modelData.label : ""
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.textPrimary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: model.modelData ? model.modelData.message : ""
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.textSecondary
                            elide: Text.ElideRight
                            Layout.preferredWidth: 120
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: Colors.checkWarnDim
            radius: Config.radiusSmall
            visible: root.criticalFailCount > 0

            RowLayout {
                anchors.fill: parent
                anchors.margins: Config.spacingSmall
                spacing: Config.spacingSmall

                Text {
                    text: qsTr("\u26A0 Override will bypass all safety gates")
                    font.pixelSize: Config.fontSizeSmall
                    font.bold: true
                    color: Colors.checkWarn
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: Config.spacingMedium
            rowSpacing: Config.spacingSmall
            Layout.fillWidth: true

            Text {
                text: qsTr("Pilot Name:")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textPrimary
                Layout.alignment: Qt.AlignRight
            }
            TextField {
                id: pilotNameField
                Layout.fillWidth: true
                placeholderText: qsTr("Enter pilot name")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textPrimary
                background: Rectangle {
                    color: Colors.surfaceLight
                    border.color: Colors.border
                    border.width: 1
                    radius: Config.radiusSmall
                }
            }

            Text {
                text: qsTr("Reason:")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textPrimary
                Layout.alignment: Qt.AlignRight
            }
            TextField {
                id: reasonField
                Layout.fillWidth: true
                placeholderText: qsTr("Override reason")
                font.pixelSize: Config.fontSizeBody
                color: Colors.textPrimary
                background: Rectangle {
                    color: Colors.surfaceLight
                    border.color: Colors.border
                    border.width: 1
                    radius: Config.radiusSmall
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingMedium

            Button {
                text: qsTr("Review Checks")
                font.pixelSize: Config.fontSizeSmall
                Layout.preferredHeight: 36
                Layout.preferredWidth: 120
                onClicked: {
                    root.reviewChecksRequested()
                    root.reject()
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Cancel")
                font.pixelSize: Config.fontSizeSmall
                Layout.preferredHeight: 36
                Layout.preferredWidth: 80
                onClicked: root.reject()
            }

            Button {
                id: overrideBtn
                text: qsTr("Acknowledge & Arm")
                font.pixelSize: Config.fontSizeSmall
                font.bold: true
                highlighted: true
                Layout.preferredHeight: 36
                Layout.preferredWidth: 160
                enabled: pilotNameField.text.trim().length > 0 && reasonField.text.trim().length > 0
                palette.highlight: Colors.error

                onClicked: {
                    var name = pilotNameField.text.trim()
                    var reason = reasonField.text.trim()
                    if (name.length === 0 || reason.length === 0) return
                    ArmingGate.acknowledgeOverride(name, reason)
                    root.accept()
                }
            }
        }
    }
}
