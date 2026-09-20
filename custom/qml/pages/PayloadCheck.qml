// PayloadCheck: confirms the payload is secured before the flow may continue.
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
            text: qsTr("Payload Check")
            font.pixelSize: Config.fontSizeH2
            color: Colors.secondary
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: qsTr("Payload: Camera Gimbal\nWeight: 1.2 kg")
            color: Colors.textSecondary
            font.pixelSize: Config.fontSizeBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            anchors.horizontalCenter: parent.horizontalCenter
        }

        VideoPreview {
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Safety gate: advancing stays blocked until the payload is confirmed secured.
        ChecklistItem {
            text: qsTr("Payload Secured")
            checked: VehicleTelemetry.payloadSecured
            status: VehicleTelemetry.payloadSecured ? "passed" : "pending"
            onToggled: VehicleTelemetry.payloadSecured = checked
        }

        Row {
            spacing: Config.spacingMedium
            anchors.horizontalCenter: parent.horizontalCenter
            CustomButton {
                text: qsTr("← Back")
                width: (parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 2
                onClicked: Config.navigatePop()
            }
            CustomButton {
                text: qsTr("Next →")
                width: (parent.width - Config.spacingMedium - Config.spacingLarge * 2) / 2
                // Progression to FinalChecks is gated on the payload-secured confirmation.
                enabled: VehicleTelemetry.payloadSecured
                onClicked: Config.navigatePush("FinalChecks.qml")
            }
        }
    }
}
