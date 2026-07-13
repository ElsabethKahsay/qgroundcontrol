import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0
Button {
    id: control
    property color baseColor: Colors.secondary
    property color textColor: "#ffffff"
    property bool rounded: false

    contentItem: Text {
        text: control.text
        font.pixelSize: Config.fontSizeBody
        font.bold: true
        font.letterSpacing: 0.5
        color: control.enabled ? textColor : "#94a3b8"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 40
        radius: rounded ? height / 2 : Config.radiusMedium
        color: {
            if (!control.enabled) return Colors.surfaceLight
            if (control.down) return Qt.darker(baseColor, 1.3)
            return baseColor
        }
        border.width: 0
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 2
            radius: 1
            color: Qt.rgba(0, 0, 0, 0.15)
        }
    }
}
