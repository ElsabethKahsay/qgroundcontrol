// Component: TelemetryBar
// Purpose: Arc gauge for displaying a telemetry value with numeric readout in the center.
//   Configurable min/max range, unit label, arc colors, and sweep angles.
// Properties:
//   value (real) — current gauge value (clamped between minValue and maxValue)
//   minValue (real) — minimum of the gauge range (default: 0)
//   maxValue (real) — maximum of the gauge range (default: 100)
//   unit (string) — unit string displayed below the value (e.g. "V", "m/s")
//   label (string) — subsystem label below the unit (default: "GAUGE")
//   precision (int) — number of decimal places shown (default: 1)
//   arcColor (color) — color of the value arc (default: Colors.primary)
//   backgroundColor (color) — color of the background arc (default: Colors.surface)
//   arcWidth (real) — thickness of the arc stroke (default: 14)
//   startAngle (real) — arc start angle in degrees, 0 = 3 o'clock (default: -140)
//   sweepAngle (real) — total arc sweep in degrees (default: 280)
import QtQuick
import QtQuick.Shapes
import com.uav.preflight 1.0
Item {
    id: root

    // ---- Customisable Properties ----
    property real value: 0
    property real minValue: 0
    property real maxValue: 100
    property string unit: ""
    property string label: qsTr("GAUGE")
    property int precision: 1           // decimal places shown

    property color arcColor: Colors.primary
    property color backgroundColor: Colors.surface
    property real arcWidth: 14
    property real startAngle: -140      // degrees, 0 = 3 o'clock
    property real sweepAngle: 280       // total arc length

    implicitWidth: 200
    implicitHeight: 200

    // --- Background Arc ---
    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            id: bgPath
            strokeColor: backgroundColor
            strokeWidth: arcWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: (Math.min(root.width, root.height) - arcWidth) / 2
                radiusY: radiusX
                startAngle: root.startAngle
                sweepAngle: root.sweepAngle
            }
        }
    }

    // --- Value Arc ---
    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            id: valuePath
            strokeColor: arcColor
            strokeWidth: arcWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                id: valueArc
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: (Math.min(root.width, root.height) - arcWidth) / 2
                radiusY: radiusX
                startAngle: root.startAngle
                sweepAngle: {
                    let ratio = (root.value - root.minValue) / (root.maxValue - root.minValue)
                    ratio = Math.max(0, Math.min(1, ratio))
                    return root.sweepAngle * ratio
                }
            }
        }
    }

    // --- Numeric Display in the Middle ---
    Column {
        anchors.centerIn: parent
        spacing: 2

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.value.toFixed(root.precision)
            font.pixelSize: Config.fontSizeH1
            color: Colors.textPrimary
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.unit
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            visible: root.unit !== ""
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.label
            font.pixelSize: Config.fontSizeSmall
            color: Colors.secondary
        }
    }
}