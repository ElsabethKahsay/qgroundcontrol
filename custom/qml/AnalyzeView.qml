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
import cpts 1.0

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

    property url pendingSource: ""
    property string pendingTitle: ""

    // This need to block click event leakage to underlying map.
    DeadMouseArea {
        anchors.fill: parent
    }

    GeoTagController {
        id: geoController
    }

    // Session start dialog — requires a mode selection before the preflight
    // checklist can load.
    SessionStartDialog {
        id: sessionStartDialog
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
                    console.log("AnalyzeView: buttonRepeater count=", buttonRepeater.count)
                    // Mark the first button as active and load its page on startup
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
                    checkable:          true
                    text:               modelData.title

                    // Active no-fly-zone count badge on the Airspace entry
                    Label {
                        anchors.top:        parent.top
                        anchors.right:      parent.right
                        anchors.margins:    2
                        visible:            modelData.title === "Airspace" && NoFlyZoneModel.activeCount > 0
                        text:               NoFlyZoneModel.activeCount
                        font.pixelSize:     9
                        font.bold:          true
                        color:              "#ffffff"
                        padding:            3
                        background: Rectangle {
                            radius:     width / 2
                            color:      "#b91c1c"
                            border.color: "#ffffff"
                            border.width: 1
                        }
                    }

                    onClicked: {
                        _clearAnalyzeButtonSelection()
                        checked = true
                        var urlString = modelData.url ? modelData.url.toString() : ""
                        console.log("MARKER-20260804 onClicked title=", modelData.title, "url=", urlString, "mode=", FlightSession.mode)
                        var isPreflight = urlString.indexOf("PreflightChecklistView.qml") !== -1
                        if (!isPreflight && modelData.title && modelData.title.indexOf("Preflight") !== -1) isPreflight = true
                        if (isPreflight && FlightSession.mode === FlightSession.None) {
                            console.log("AnalyzeView: storing pendingSource=", urlString)
                            pendingSource = modelData.url
                            pendingTitle = modelData.title
                            sessionStartDialog.open()
                        } else {
                            console.log("AnalyzeView: loading directly ->", urlString)
                            panelLoader.source  = urlString
                            panelLoader.title   = modelData.title
                        }
                    }
                }
            }
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
            console.log("AnalyzeView: panelLoader status=", status, "source=", source, "error=", errorString())
        }

        Connections {
            target:     panelLoader.item
            onPopout:   mainWindow.createrWindowedAnalyzePage(panelLoader.title, panelLoader.source)
        }
    }

    // Mode selected from the session dialog — load the preflight checklist.
    Connections {
        target: sessionStartDialog
        function onClosed() {
            console.log("AnalyzeView: sessionStartDialog closed; pendingSource=", pendingSource, "mode=", FlightSession.mode)
            if (pendingSource.toString() !== "" && FlightSession.mode !== FlightSession.None) {
                console.log("AnalyzeView: loading pendingSource ->", pendingSource)
                panelLoader.source = pendingSource
                panelLoader.title  = pendingTitle !== "" ? pendingTitle : qsTr("Preflight Checklist")
                pendingSource = ""
                pendingTitle = ""
                _selectAnalyzeButton(panelLoader.title, panelLoader.source)
            }
            pendingSource = ""
            pendingTitle = ""
        }
    }

    function _clearAnalyzeButtonSelection() {
        for (var i = 0; i < buttonRepeater.count; i++) {
            var item = buttonRepeater.itemAt(i)
            if (item) {
                item.checked = false
            }
        }
    }

    function _selectAnalyzeButton(title, url) {
        for (var i = 0; i < buttonRepeater.count; i++) {
            var item = buttonRepeater.itemAt(i)
            if (!item) continue
            if (item.text === title) {
                item.checked = true
                return
            }
            if (url && item.modelData && item.modelData.url && item.modelData.url.toString() === url.toString()) {
                item.checked = true
                return
            }
            if (title.indexOf("Preflight") !== -1 && item.text.indexOf("Preflight") !== -1) {
                item.checked = true
                return
            }
        }
    }
}
