import QtQuick
import QtQuick.Layouts
import com.uav.preflight 1.0

RowLayout {
    id: root

    property string label
    property string value
    property string statusText: ""
    property color valColor: Colors.textPrimary
    property int valSize: Config.fontSizeSmall
    property string valFamily: "sans-serif"
    property int quality: 0  // 0=Valid, 1=Stale, 2=Unavailable, 3=Calibrating

    spacing: Config.spacingSmall
    Layout.fillWidth: true
    Layout.minimumHeight: 22

    Text {
        text: root.label
        font.pixelSize: root.valSize * Config.fontScale
        color: Colors.textSecondary
        font.bold: true
        Layout.preferredWidth: 80
    }
    Text {
        text: root.quality >= 2 && root.statusText.length > 0 ? root.statusText : root.value
        font.pixelSize: root.valSize * Config.fontScale
        color: root.quality === 0 ? root.valColor
             : root.quality === 1 ? Colors.warning
             : Colors.textDisabled
        font.family: root.valFamily
        Layout.fillWidth: true
        elide: Text.ElideRight
        visible: root.quality < 2 || root.statusText.length > 0
    }
}
