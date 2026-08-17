/****************************************************************************
 *
 * Custom FlyView override — half-video / half-map split layout.
 *
 * Layout:
 *   ┌───────────────────┬───────────────────┐
 *   │                   │         [Compass] │
 *   │                   │                   │
 *   │   VIDEO  (left)   │    MAP   (right)  │
 *   │                   │                   │
 *   │                   │                   │
 *   │  [Floating Telemetry Box]              │
 *   └───────────────────┴───────────────────┘
 *
 * Double-click either panel (or use divider button) to swap sides.
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

import com.uav.preflight 1.0

// 3D Viewer modules
import Viewer3D

Item {
    id: _root

    // These should only be used by MainRootWindow
    property var planController: _planController
    property var guidedController: _guidedController

    // Properties of UTM adapter
    property bool utmspSendActTrigger: false

    PlanMasterController {
        id: _planController
        flyView: true
        Component.onCompleted: start()
    }

    // The floating telemetry box is draggable and does not block a fixed
    // bottom edge, so we no longer need a strip height constant.

    property bool _mainWindowIsMap: !_videoIsMain
    property bool _isFullWindowItemDark: _mainWindowIsMap ? mapControl.isSatelliteMap : true
    property var _activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property var _missionController: _planController.missionController
    property var _geoFenceController: _planController.geoFenceController
    property var _rallyPointController: _planController.rallyPointController
    property real _margins: ScreenTools.defaultFontPixelWidth / 2
    property var _guidedController: guidedActionsController
    property var _guidedValueSlider: guidedValueSlider
    property var _widgetLayer: widgetLayer
    property real _toolsMargin: ScreenTools.defaultFontPixelWidth * 0.75
    property rect _centerViewport: Qt.rect(0, 0, width, height)
    property real _rightPanelWidth: ScreenTools.defaultFontPixelWidth * 30
    property var _mapControl: mapControl

    property real _fullItemZorder: 0
    property real _pipItemZorder: QGroundControl.zOrderWidgets

    // ── Split layout state ───────────────────────────────────────────────
    // false = video on left (default), map on right
    property bool _videoIsMain: true    // true means video is on the "left" panel
    property real _splitRatio: 0.4     // 40/60

    function _swapPanels() {
        _videoIsMain = !_videoIsMain;
        QGroundControl.saveBoolGlobalSetting("CustomFlyViewVideoIsMain", _videoIsMain);
    }

    Component.onCompleted: {
        _videoIsMain = QGroundControl.loadBoolGlobalSetting("CustomFlyViewVideoIsMain", true);
    }

    // The floating telemetry box is draggable and not full-width, so the
    // center viewport is simply the area below the toolbar.
    function _calcCenterViewPort() {
        _centerViewport = Qt.rect(
            0,
            toolbar.height,
            width,
            height - toolbar.height
        );
    }

    // The floating telemetry box floats above the map and does not
    // permanently block the bottom edge, so no bottom inset is needed.
    QGCToolInsets {
        id: _toolInsets
        leftEdgeBottomInset:  0
        bottomEdgeLeftInset:  0
    }

    FlyViewToolBar {
        id: toolbar
        visible: !QGroundControl.videoManager.fullScreen
        onHeightChanged: _calcCenterViewPort()
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  MAIN CONTENT AREA — below toolbar
    // ═══════════════════════════════════════════════════════════════════════
    Item {
        id: mainContent
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        // ── Left panel container ─────────────────────────────────────────
        Item {
            id: leftPanel
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: parent.width * _splitRatio
        }

        // ── Divider + swap button ────────────────────────────────────────
        Rectangle {
            id: panelDivider
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: leftPanel.right
            width: 3
            color: Colors.background
            z: QGroundControl.zOrderWidgets + 2

            Rectangle {
                id: swapBtn
                anchors.centerIn: parent
                width: ScreenTools.defaultFontPixelHeight * 2.6
                height: width
                radius: width / 2
                color: _swapMA.containsMouse ? Qt.rgba(Colors.accentCyan.r, Colors.accentCyan.g, Colors.accentCyan.b, 0.30) : Colors.surface
                border.color: Qt.rgba(Colors.accentCyan.r, Colors.accentCyan.g, Colors.accentCyan.b, 0.55)
                border.width: 1.5
                z: QGroundControl.zOrderWidgets + 3

                Behavior on color {
                    ColorAnimation {
                        duration: 150
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: qsTr("⇄")
                    font.pixelSize: ScreenTools.defaultFontPixelHeight * 1.2
                    color: Colors.accentCyan
                }

                MouseArea {
                    id: _swapMA
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: _swapPanels()

                    ToolTip.visible: containsMouse
                    ToolTip.text: qsTr("Swap Map / Video")
                    ToolTip.delay: 300
                }
            }
        }

        // ── Right panel container ────────────────────────────────────────
        Item {
            id: rightPanel
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: panelDivider.right
            anchors.right: parent.right
        }

        // ════════════════════════════════════════════════════════════════
        //  MAP — lives in the "map panel" (right by default)
        // ════════════════════════════════════════════════════════════════
        FlyViewMap {
            id: mapControl
            planMasterController: _planController

            // FIX 2: Was ScreenTools.defaultFontPixelHeight * 9.
            // That reserved space inside the map for a side instrument panel
            // that does not exist in our split layout, pushing map content
            // left inside its own panel. Zero = map fills its panel fully.
            rightPanelWidth: 0

            // FIX 1: Was pipMode: _videoIsMain.
            // When _videoIsMain was true (default), the map got pipMode:true,
            // causing it to render as a secondary/thumbnail view with reduced
            // controls. In a 50/50 split the map is always a full peer panel,
            // never a PIP thumbnail.
            pipMode: false

            toolInsets: customOverlay.totalToolInsets
            mapName: "FlightDisplayView"
            enabled: !viewer3DWindow.isOpen

            pipView: _dummyPipView

            parent: _videoIsMain ? rightPanel : leftPanel
            anchors.fill: parent
        }

        // ── Double-click overlay on MAP panel to swap ────────────────
        MouseArea {
            parent: mapControl
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            propagateComposedEvents: true
            z: 0   // below map interaction layer; only intercepts double-click

            onPressed: function(mouse) { mouse.accepted = false }
            onClicked: function(mouse) { mouse.accepted = false }
            onDoubleClicked: function(mouse) {
                _swapPanels()
                mouse.accepted = true
            }
        }

        // ════════════════════════════════════════════════════════════════
        //  VIDEO — lives in the "video panel" (left by default)
        //  NOTE: DO NOT modify FlyViewVideo internals — GStreamer lifecycle
        // ════════════════════════════════════════════════════════════════
        FlyViewVideo {
            id: videoControl
            pipView: _dummyPipView
            visible: QGroundControl.videoManager.hasVideo

            parent: _videoIsMain ? leftPanel : rightPanel
            anchors.fill: parent
        }

        // ── Double-click overlay on VIDEO panel to swap ──────────────
        MouseArea {
            parent: videoControl
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            propagateComposedEvents: true
            z: 0

            onPressed: function(mouse) { mouse.accepted = false }
            onClicked: function(mouse) { mouse.accepted = false }
            onDoubleClicked: function(mouse) {
                _swapPanels()
                mouse.accepted = true
            }
        }

        // ── "No Video" placeholder ───────────────────────────────────────
        Rectangle {
            visible: !QGroundControl.videoManager.hasVideo
            parent: _videoIsMain ? leftPanel : rightPanel
            anchors.fill: parent
            color: Colors.background

            Column {
                anchors.centerIn: parent
                spacing: ScreenTools.defaultFontPixelHeight * 0.6

                Text {
                    text: qsTr("NO SIGNAL")
                    font.pixelSize: ScreenTools.defaultFontPixelHeight * 2.4
                    color: Colors.textDisabled
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: qsTr("No Video Stream")
                    font.pointSize: ScreenTools.defaultFontPointSize * 1.05
                    color: Colors.textDisabled
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }

            MouseArea {
                anchors.fill: parent
                onDoubleClicked: _swapPanels()
            }
        }

        // ── Dummy PipView for compatibility ─────────────────────────────
        Item {
            id: _dummyPipView
            visible: false
            width: 0
            height: 0

            property var _pipContentItem: _dummyPipContent
            property var _windowContentItem: _dummyWindowContent

            Item {
                id: _dummyPipContent
            }
            Item {
                id: _dummyWindowContent
            }
        }

        // ════════════════════════════════════════════════════════════════
        //  WIDGET LAYER — toolbar strip + guided actions only.
        //  Instrument panel (bottom-right bloat) is hidden via corePlugin
        //  override; we draw our own compass in FlyViewCustomLayer.
        // ════════════════════════════════════════════════════════════════
        FlyViewWidgetLayer {
            id: widgetLayer
            anchors.fill: parent
            z: _fullItemZorder + 2
            parentToolInsets: _toolInsets
            mapControl: _mapControl
            visible: !QGroundControl.videoManager.fullScreen
            utmspActTrigger: utmspSendActTrigger
            isViewer3DOpen: viewer3DWindow.isOpen
        }

        // ── Custom overlay (telemetry strip bottom, compass top-right of
        //    map panel, preflight banner) ─────────────────────────────────
        FlyViewCustomLayer {
            id: customOverlay
            anchors.fill: parent
            z: _fullItemZorder + 3
            parentToolInsets: widgetLayer.totalToolInsets
            mapControl: _mapControl
            visible: !QGroundControl.videoManager.fullScreen

            // Panel geometry for the compass widget positioning.
            property rect mapPanelRect: _videoIsMain
                ? Qt.rect(rightPanel.x, 0, rightPanel.width, rightPanel.height)
                : Qt.rect(leftPanel.x,  0, leftPanel.width,  leftPanel.height)

            property rect videoPanelRect: _videoIsMain
                ? Qt.rect(leftPanel.x,  0, leftPanel.width,  leftPanel.height)
                : Qt.rect(rightPanel.x, 0, rightPanel.width, rightPanel.height)
        }

        FlyViewInsetViewer {
            id: widgetLayerInsetViewer
            anchors.fill: parent
            z: widgetLayer.z + 1
            insetsToView: widgetLayer.totalToolInsets
            visible: false
        }

        Connections {
            target: widgetLayer
            function onPreflightChecklistRequested() {
                customOverlay.openPreflightDialog()
            }
        }

        GuidedActionsController {
            id: guidedActionsController
            missionController: _missionController
            guidedValueSlider: _guidedValueSlider
        }

        GuidedValueSlider {
            id: guidedValueSlider
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            z: QGroundControl.zOrderTopMost
            visible: false
        }

        Viewer3D {
            id: viewer3DWindow
            anchors.fill: parent
        }
    }
}
