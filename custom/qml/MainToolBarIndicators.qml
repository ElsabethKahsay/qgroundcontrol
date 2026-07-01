import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.ScreenTools
import QGroundControl.Palette

Row {
    id:                 indicatorRow
    anchors.top:        parent.top
    anchors.bottom:     parent.bottom
    anchors.margins:    _toolIndicatorMargins
    spacing:            ScreenTools.defaultFontPixelWidth * 1.5

    property var  _activeVehicle:           QGroundControl.multiVehicleManager.activeVehicle
    property real _toolIndicatorMargins:    ScreenTools.defaultFontPixelHeight * 0.66

    function dropMessageIndicatorTool() {
        toolIndicatorsRepeater.dropMessageIndicatorTool();
    }

    QGCPalette { id: qgcPal }

    // Standard app-provided toolbar indicators
    Repeater {
        id:     appRepeater
        model:  QGroundControl.corePlugin.toolBarIndicators
        Loader {
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            source:             modelData
            visible:            item ? item.showIndicator : false
        }
    }

    // Preflight status indicator
    Loader {
        id: preflightLoader
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        source: "qrc:/qml/cpts/PreflightToolbarIndicator.qml"
        visible: item ? item.showIndicator : false
    }

    // ── Preflight Checklist toolbar button ─────────────────────────────
    Rectangle {
        id: preflightChecklistBtn
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: ScreenTools.defaultFontPixelWidth * 10
        visible: true
        color: _btnMA.containsPress ? "#065F7C"
             : _btnMA.containsMouse ? Qt.rgba(0,0,0,0.08)
             : "transparent"
        radius: 4

        Behavior on color { ColorAnimation { duration: 100 } }

        RowLayout {
            anchors.centerIn: parent
            spacing: ScreenTools.defaultFontPixelWidth * 0.4

            Text {
                text: _activeVehicle ? "\u2714" : "\u2610"
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.9
                color: _activeVehicle ? "#0891B2" : qgcPal.colorGrey
            }

            Text {
                text: qsTr("Checklist")
                font.pointSize: ScreenTools.defaultFontPointSize * 0.85
                font.weight: Font.Medium
                color: _activeVehicle ? qgcPal.text : qgcPal.colorGrey
            }
        }

        MouseArea {
            id: _btnMA
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: _activeVehicle ? Qt.PointingHandCursor : Qt.ArrowCursor
            enabled: !!_activeVehicle

            ToolTip.visible: containsMouse
            ToolTip.text:    _activeVehicle ? qsTr("Open Preflight Test Page")
                                           : qsTr("Connect a vehicle first")
            ToolTip.delay:   400

            onClicked: {
                // Show the preflight checklist via a popup dialog
                var obj = Qt.createQmlObject('import QtQuick.Controls; import QGroundControl.Controls; QGCPopupDialog { title: "Preflight Checklist"; buttons: StandardButton.Close; Loader { source: "qrc:/qml/cpts/PreflightChecklistView.qml"; width: ScreenTools.defaultFontPixelWidth * 80; height: ScreenTools.defaultFontPixelHeight * 40 } }', mainWindow)
                obj.open()
            }
        }
    }

    Repeater {
        id:     toolIndicatorsRepeater
        model:  _activeVehicle ? _activeVehicle.toolIndicators : []

        function dropMessageIndicatorTool() {
            for (var i=0; i<count; i++) {
                var thisTool = itemAt(i);
                if (thisTool.item.dropMessageIndicator) {
                    thisTool.item.dropMessageIndicator();
                }
            }
        }

        Loader {
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            source:             modelData
            visible:            item ? item.showIndicator : false
        }
    }

    Repeater {
        model: _activeVehicle ? _activeVehicle.modeIndicators : []
        Loader {
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            source:             modelData
            visible:            item ? item.showIndicator : false
        }
    }
}
