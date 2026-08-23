/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Window
import QtQuick.Controls

import QGroundControl
import QGroundControl.Palette
import QGroundControl.Controls
import QGroundControl.Controllers
import QGroundControl.ScreenTools

import com.uav.preflight 1.0

Rectangle {
    id:     _root
    color:  qgcPal.window
    z:      QGroundControl.zOrderTopMost

    signal popout()

    readonly property real  _defaultTextHeight:     ScreenTools.defaultFontPixelHeight
    readonly property real  _defaultTextWidth:      ScreenTools.defaultFontPixelWidth
    readonly property real  _horizontalMargin:      _defaultTextWidth / 2
    readonly property real  _verticalMargin:        _defaultTextHeight / 2
    readonly property real  _buttonWidth:           _defaultTextWidth * 18

    // This need to block click event leakage to underlying map.
    DeadMouseArea {
        anchors.fill: parent
    }

    GeoTagController {
        id: geoController
    }

    QGCFlickable {
        id:                 buttonScroll
        width:              buttonColumn.width
        anchors.topMargin:  _defaultTextHeight / 2
        anchors.top:        parent.top
        anchors.bottom:     parent.bottom
        anchors.leftMargin: _horizontalMargin
        anchors.left:       parent.left
        contentHeight:      buttonColumn.height
        flickableDirection: Flickable.VerticalFlick
        clip:               true

        Column {
            id:         buttonColumn
            width:      _maxButtonWidth
            spacing:    _defaultTextHeight / 2

            property real _maxButtonWidth: 0

            Component.onCompleted: reflowWidths()

            // I don't know why this does not work
            Connections {
                target:         QGroundControl.settingsManager.appSettings.appFontPointSize
                onValueChanged: buttonColumn.reflowWidths()
            }

            function reflowWidths() {
                buttonColumn._maxButtonWidth = 0
                for (var i = 0; i < children.length; i++) {
                    buttonColumn._maxButtonWidth = Math.max(buttonColumn._maxButtonWidth, children[i].width)
                }
                for (var j = 0; j < children.length; j++) {
                    children[j].width = buttonColumn._maxButtonWidth
                }
            }

            Repeater {
                id:     buttonRepeater
                model:  QGroundControl.corePlugin ? QGroundControl.corePlugin.analyzePages : []

                Component.onCompleted:  {
                    console.log("AnalyzeView(src): buttonRepeater count=", buttonRepeater.count)
                    if (buttonRepeater.count > 0) {
                        itemAt(0).checked = true
                        var firstPage = QGroundControl.corePlugin.analyzePages[0]
                        if (firstPage && firstPage.url) {
                            panelLoader.source = firstPage.url.toString()
                            panelLoader.title  = firstPage.title
                        }
                    }
                }

                SubMenuButton {
                    id:                 subMenu
                    imageResource:      modelData.icon
                    autoExclusive:      true
                    text:               modelData.title

                    onClicked: {
                        console.log("AnalyzeView(src): onClicked title=", modelData.title, "url=", modelData.url, "mode=", FlightSession.mode)
                        var urlString = modelData.url ? modelData.url.toString() : ""
                        var isPreflight = urlString.indexOf("PreflightChecklistView.qml") !== -1
                        if (!isPreflight && modelData.title && modelData.title.indexOf("Preflight") !== -1) isPreflight = true
                        if (isPreflight && FlightSession.mode === FlightSession.None) {
                            console.log("AnalyzeView(src): storing pendingSource=", urlString)
                            pendingSource = urlString
                            pendingTitle = modelData.title
                            sessionStartDialog.open()
                        } else {
                            console.log("AnalyzeView(src): loading directly ->", urlString)
                            panelLoader.source  = urlString
                            panelLoader.title   = modelData.title
                            checked             = true
                        }
                    }
                }
            }
        }
    }

    // Pending source when waiting for a session mode selection
    property string pendingSource: ""
    property string pendingTitle: ""

    Dialog {
        id: sessionStartDialog
        title: qsTr("Start Session")
        modal: true
        standardButtons: Dialog.Cancel
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(520, parent.width * 0.6)

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12

            Text { text: qsTr("Select session mode to begin preflight:"); font.pixelSize: 14 }

            Row {
                spacing: 12
                Button { text: qsTr("Training"); onClicked: { FlightSession.setMode("TRAINING"); sessionStartDialog.close(); } }
                Button { text: qsTr("Testing"); onClicked: { FlightSession.setMode("TESTING"); sessionStartDialog.close(); } }
                Button { text: qsTr("Flight");   onClicked: { FlightSession.setMode("FLIGHT");   sessionStartDialog.close(); } }
            }
        }

        onClosed: {
            console.log("AnalyzeView(src): sessionStartDialog closed; pendingSource=", pendingSource, "mode=", FlightSession.mode)
            if (pendingSource !== "" && FlightSession.mode !== FlightSession.None) {
                console.log("AnalyzeView(src): loading pendingSource ->", pendingSource)
                panelLoader.source = pendingSource
                panelLoader.title  = pendingTitle !== "" ? pendingTitle : qsTr("Preflight Checklist")
                pendingSource = ""
                pendingTitle = ""
                // attempt to mark the button checked
                for (var i = 0; i < buttonRepeater.count; i++) {
                    if (buttonRepeater.itemAt(i).text === panelLoader.title) {
                        buttonRepeater.itemAt(i).checked = true
                        break
                    }
                }
            }
            pendingSource = ""
            pendingTitle = ""
        }
    }

    Rectangle {
        id:                     divider
        anchors.topMargin:      _verticalMargin
        anchors.bottomMargin:   _verticalMargin
        anchors.leftMargin:     _horizontalMargin
        anchors.left:           buttonScroll.right
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        width:                  1
        color:                  qgcPal.windowShade
    }

    Loader {
        id:                     panelLoader
        anchors.topMargin:      _verticalMargin
        anchors.bottomMargin:   _verticalMargin
        anchors.leftMargin:     _horizontalMargin
        anchors.rightMargin:    _horizontalMargin
        anchors.left:           divider.right
        anchors.right:          parent.right
        anchors.top:            parent.top
        anchors.bottom:         parent.bottom
        source:                 "qrc:/qml/QGroundControl/AnalyzeView/LogDownloadPage.qml"

        property string title:  qsTr("Log Download")

        onStatusChanged: {
            console.log("AnalyzeView(src): panelLoader status=", status, "source=", source, "error=", errorString())
        }

        Connections {
            target:     panelLoader.item
            onPopout:   mainWindow.createrWindowedAnalyzePage(panelLoader.title, panelLoader.source)
        }
    }
}
