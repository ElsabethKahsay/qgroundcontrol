// Component: StatBox
// Purpose: Compact label + value column used in the flight stats banner.
// Properties:
//   label (string) — small grey caption above the value
//   value (string) — large value text (white by default)
//   valueColor (color) — color of the value text (default Colors.textPrimary)
import QtQuick
import QtQuick.Layouts
import com.uav.preflight 1.0

Column {
    id: root
    spacing: 2

    property string label
    property string value
    property color valueColor: Colors.textPrimary

    Text {
        text: root.label
        font.pixelSize: Config.fontSizeSmall
        color: Colors.textSecondary
    }
    Text {
        text: root.value
        font.pixelSize: Config.fontSizeH3 * Config.fontScale
        font.bold: true
        color: root.valueColor
    }
}