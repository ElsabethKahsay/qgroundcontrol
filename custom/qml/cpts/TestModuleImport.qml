import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Rectangle {
    anchors.fill: parent
    color: Colors.primary

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Text {
            text: "Module Import Test"
            font.pixelSize: 24
            font.bold: true
            color: "#FFFFFF"
        }

        Text {
            text: "Colors.surface: " + Colors.surface
            font.pixelSize: 16
            color: "#FFFFFF"
        }

        Text {
            text: "Config.fontSizeBody: " + Config.fontSizeBody
            font.pixelSize: 16
            color: "#FFFFFF"
        }

        Rectangle {
            Layout.fillWidth: true
            height: 80
            color: Colors.surface
            radius: 12
            border.color: Colors.border
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "Colors.surface rectangle"
                font.pixelSize: 14
                color: Colors.textPrimary
            }
        }

        Text {
            text: "CatModel0 exists: " + (typeof CatModel0 !== "undefined")
            font.pixelSize: 14
            color: "#FFFFFF"
        }

        Text {
            text: "CatModel0.count: " + (typeof CatModel0 !== "undefined" ? CatModel0.count : "N/A")
            font.pixelSize: 14
            color: "#FFFFFF"
        }

        Text {
            text: "PreflightManager exists: " + (typeof PreflightManager !== "undefined")
            font.pixelSize: 14
            color: "#FFFFFF"
        }

        Text {
            text: "Total checks: " + (typeof PreflightManager !== "undefined" ? PreflightManager.totalChecks : "N/A")
            font.pixelSize: 14
            color: "#FFFFFF"
        }
    }
}
