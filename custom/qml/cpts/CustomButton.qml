// Component: CustomButton
// Purpose: Styled button component based on QML Button with a configurable background color.
//   Shows a darker shade when pressed.
// Properties:
//   baseColor (color) — background color of the button (default: Colors.primary)
import QtQuick
import QtQuick.Controls
import com.uav.preflight 1.0
Button {
    id: control
    property color baseColor: Colors.primary
    contentItem: Text {
        text: control.text
        font.pixelSize: Config.fontSizeBody
        color: Colors.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 50
        radius: Config.radiusMedium
        color: control.down ? Qt.darker(baseColor, 1.2) : baseColor
    }
}
