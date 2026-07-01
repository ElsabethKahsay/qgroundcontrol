import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0
Page {
    background: Rectangle { color: Colors.background }

    Column {
        anchors.centerIn: parent
        spacing: Config.spacingLarge

        Text {
            text: "UAV Pre‑Flight Check"
            font.pixelSize: Config.fontSizeH1
            color: Colors.primary
            anchors.horizontalCenter: parent.horizontalCenter
        }

        CustomButton {
            text: "Start Pre‑Flight"
            anchors.horizontalCenter: parent.horizontalCenter
            width: 250
            onClicked: Window.window.mainStackView.push("VehicleSelect.qml")
        }

        CustomButton {
            text: "Manage Templates"
            anchors.horizontalCenter: parent.horizontalCenter
            width: 250
            baseColor: Colors.surface
            onClicked: Window.window.mainStackView.push("TemplateManager.qml")
        }
    }
}