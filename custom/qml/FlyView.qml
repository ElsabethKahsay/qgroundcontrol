/****************************************************************************
 *
 * Custom FlyView override — half-video / half-map split layout.
 *
 * Layout:
 *   ┌───────────────────┬───────────────────┐
 *   │  [Telemetry Strip]│                   │
 *   │                   │                   │
 *   │   VIDEO  (left)   │    MAP   (right)  │
 *   │                   │                   │
 *   │                   │       [Compass]   │
 *   └───────────────────┴───────────────────┘
 *   ↑ swap button on divider
 *
 * Swapping inverts which side each panel lives on.
 * GStreamer is NOT touched — FlyViewVideo manages its own pipeline.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 UAV Preflight Contributors
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QtLocation
import QtPositioning
import QtQuick.Window
import QtQml.Models

import QGroundControl
import QGroundControl.Controllers
import QGroundControl.Controls
import QGroundControl.FactSystem
import QGroundControl.FlightDisplay
import QGroundControl.FlightMap
import QGroundControl.Palette
import QGroundControl.ScreenTools
import QGroundControl.Vehicle

// 3D Viewer modules
import Viewer3D

Item {
    id: _root

    // These should only be used by MainRootWindow
    property var planController:    _planController
    property var guidedController:  _guidedController

    // Properties of UTM adapter
    property bool utmspSendActTrigger: false

    PlanMasterController {
        id:                     _planController
        flyView:                true
        Component.onCompleted:  start()
    }

    property bool   _mainWindowIsMap:       !_videoIsMain
    property bool   _isFullWindowItemDark:  _mainWindowIsMap ? mapControl.isSatelliteMap : true
    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property var    _missionController:     _planController.missionController
    property var    _geoFenceController:    _planController.geoFenceController
    property var    _rallyPointController:  _planController.rallyPointController
    property real   _margins:               ScreenTools.defaultFontPixelWidth / 2
    property var    _guidedController:      guidedActionsController
    property var    _guidedValueSlider:     guidedValueSlider
    property var    _widgetLayer:           widgetLayer
    property real   _toolsMargin:           ScreenTools.defaultFontPixelWidth * 0.75
    property rect   _centerViewport:        Qt.rect(0, 0, width, height)
    property real   _rightPanelWidth:       ScreenTools.defaultFontPixelWidth * 30
    property var    _mapControl:            mapControl

    property real   _fullItemZorder:    0
    property real   _pipItemZorder:     QGroundControl.zOrderWidgets

    // ── Split layout state ───────────────────────────────────────────────
    // false = video on left (default), map on right
    property bool   _videoIsMain:   true    // true means video is on the "left" panel
    property real   _splitRatio:    0.5     // 50/50

    function _swapPanels() {
        _videoIsMain = !_videoIsMain
        QGroundControl.saveBoolGlobalSetting("CustomFlyViewVideoIsMain", _videoIsMain)
    }

    Component.onCompleted: {
        _videoIsMain = QGroundControl.loadBoolGlobalSetting("CustomFlyViewVideoIsMain", true)
    }

    function _calcCenterViewPort() {
        var newToolInset = Qt.rect(0, 0, width, height)
        toolstrip.adjustToolInset(newToolInset)
    }

    function dropMainStatusIndicatorTool() {
        toolbar.dropMainStatusIndicatorTool();
    }

    QGCToolInsets {
        id:                     _toolInsets
        leftEdgeBottomInset:    0
        bottomEdgeLeftInset:    0
    }

    FlyViewToolBar {
        id:         toolbar
        visible:    !QGroundControl.videoManager.fullScreen
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  MAIN CONTENT AREA — below toolbar
    // ═══════════════════════════════════════════════════════════════════════
    Item {
        id:                 mainContent
        anchors.top:        toolbar.bottom
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        anchors.right:      parent.right

        // ── Left panel container ─────────────────────────────────────────
        Item {
            id:             leftPanel
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            anchors.left:   parent.left
            width:          parent.width * _splitRatio
        }

        // ── Divider + swap button ────────────────────────────────────────
        Rectangle {
            id:             panelDivider
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            anchors.left:   leftPanel.right
            width:          3
            color:          Qt.rgba(0.10, 0.10, 0.20, 0.95)
            z:              QGroundControl.zOrderWidgets + 2

            Rectangle {
                id:                 swapBtn
                anchors.centerIn:   parent
                width:              ScreenTools.defaultFontPixelHeight * 2.6
                height:             width
                radius:             width / 2
                color:              _swapMA.containsMouse
                                        ? Qt.rgba(0, 0.83, 1, 0.30)
                                        : Qt.rgba(0.07, 0.07, 0.16, 0.92)
                border.color:       Qt.rgba(0, 0.83, 1, 0.55)
                border.width:       1.5
                z:                  QGroundControl.zOrderWidgets + 3

                Behavior on color { ColorAnimation { duration: 150 } }

                Text {
                    anchors.centerIn: parent
                    text:             "⇄"
                    font.pixelSize:   ScreenTools.defaultFontPixelHeight * 1.2
                    color:            "#00D4FF"
                }

                MouseArea {
                    id:             _swapMA
                    anchors.fill:   parent
                    hoverEnabled:   true
                    cursorShape:    Qt.PointingHandCursor
                    onClicked:      _swapPanels()

                    ToolTip.visible: containsMouse
                    ToolTip.text:    qsTr("Swap Map / Video")
                    ToolTip.delay:   300
                }
            }
        }

        // ── Right panel container ────────────────────────────────────────
        Item {
            id:             rightPanel
            anchors.top:    parent.top
            anchors.bottom: parent.bottom
            anchors.left:   panelDivider.right
            anchors.right:  parent.right
        }

        // ════════════════════════════════════════════════════════════════
        //  MAP — lives in the "map panel" (right by default)
        // ════════════════════════════════════════════════════════════════
        FlyViewMap {
            id:                     mapControl
            planMasterController:   _planController
            rightPanelWidth:        ScreenTools.defaultFontPixelHeight * 9
            pipMode:                _videoIsMain   // when video is main (left), map is secondary (right)
            toolInsets:             customOverlay.totalToolInsets
            mapName:                "FlightDisplayView"
            enabled:                !viewer3DWindow.isOpen

            pipView:                _dummyPipView

            parent:                 _videoIsMain ? rightPanel : leftPanel
            anchors.fill:           parent
        }

        // ════════════════════════════════════════════════════════════════
        //  VIDEO — lives in the "video panel" (left by default)
        //  ⚠ DO NOT modify FlyViewVideo internals — GStreamer lifecycle
        // ════════════════════════════════════════════════════════════════
        FlyViewVideo {
            id:             videoControl
            pipView:        _dummyPipView
            visible:        QGroundControl.videoManager.hasVideo

            parent:         _videoIsMain ? leftPanel : rightPanel
            anchors.fill:   parent
        }

        // ── "No Video" placeholder ───────────────────────────────────────
        Rectangle {
            visible:        !QGroundControl.videoManager.hasVideo
            parent:         _videoIsMain ? leftPanel : rightPanel
            anchors.fill:   parent
            color:          Qt.rgba(0.05, 0.05, 0.09, 1.0)

            Column {
                anchors.centerIn: parent
                spacing:          ScreenTools.defaultFontPixelHeight * 0.6

                Text {
                    text:                     "📹"
                    font.pixelSize:           ScreenTools.defaultFontPixelHeight * 2.4
                    color:                    "#374151"
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text:                     qsTr("No Video Stream")
                    font.pointSize:           ScreenTools.defaultFontPointSize * 1.05
                    color:                    "#4B5563"
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }

        // ── Dummy PipView for compatibility ─────────────────────────────
        Item {
            id:             _dummyPipView
            visible:        false
            width:          0
            height:         0

            property var    _pipContentItem:    _dummyPipContent
            property var    _windowContentItem: _dummyWindowContent

            Item { id: _dummyPipContent }
            Item { id: _dummyWindowContent }
        }

        // ════════════════════════════════════════════════════════════════
        //  WIDGET LAYER — toolbar strip + guided actions only.
        //  Instrument panel (bottom-right bloat) is hidden via corePlugin
        //  override; we draw our own compass in FlyViewCustomLayer.
        // ════════════════════════════════════════════════════════════════
        FlyViewWidgetLayer {
            id:                     widgetLayer
            anchors.fill:           parent
            z:                      _fullItemZorder + 2
            parentToolInsets:       _toolInsets
            mapControl:             _mapControl
            visible:                !QGroundControl.videoManager.fullScreen
            utmspActTrigger:        utmspSendActTrigger
            isViewer3DOpen:         viewer3DWindow.isOpen
        }

        // ── Custom overlay (telemetry strip top-left, compass, preflight) ─
        FlyViewCustomLayer {
            id:                 customOverlay
            anchors.fill:       parent
            z:                  _fullItemZorder + 3
            parentToolInsets:   widgetLayer.totalToolInsets
            mapControl:         _mapControl
            visible:            !QGroundControl.videoManager.fullScreen

            // Pass panel geometry so the overlay knows where each half is
            property real videoHalfWidth: leftPanel.width
            property bool videoOnLeft:    _videoIsMain
        }

        FlyViewInsetViewer {
            id:                     widgetLayerInsetViewer
            anchors.fill:           parent
            z:                      widgetLayer.z + 1
            insetsToView:           widgetLayer.totalToolInsets
            visible:                false
        }

        GuidedActionsController {
            id:                 guidedActionsController
            missionController:  _missionController
            guidedValueSlider:     _guidedValueSlider
        }

        GuidedValueSlider {
            id:                 guidedValueSlider
            anchors.right:      parent.right
            anchors.top:        parent.top
            anchors.bottom:     parent.bottom
            z:                  QGroundControl.zOrderTopMost
            visible:            false
        }

        Viewer3D {
            id:             viewer3DWindow
            anchors.fill:   parent
        }
    }
}
