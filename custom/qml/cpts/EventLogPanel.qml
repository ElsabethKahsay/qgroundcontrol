// Component: EventLogPanel
// Purpose: Scrollable vehicle STATUSTEXT / status-message log for the Fly view.
//   Hidden until the Events quick action toggles it.  Messages are appended on
//   Vehicle::textMessageReceived, newest first, with timestamps and an
//   All / Warning / Error severity filter.  History is kept for the session and
//   cleared when the active vehicle changes.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

import QGroundControl.ScreenTools

Rectangle {
    id: root

    /// Active Vehicle object (bind from the parent; may be null when disconnected).
    property var vehicle: null

    signal closeRequested()

    readonly property int kMaxEvents: 200

    // ── Filters ──
    property string activeFilter: "all"     // "all" | "warning" | "error"

    readonly property string _filterLabel: activeFilter === "error" ? qsTr("Errors")
                                          : activeFilter === "warning" ? qsTr("Warnings") : qsTr("All events")

    // ── Models ──
    ListModel { id: allEvents }
    ListModel { id: viewEvents }
    property int _lastCount: 0

    function _severityCat(sev) {
        if (sev <= 3) return "error"       // EMERGENCY..ERROR
        if (sev === 4) return "warning"    // WARNING
        return "info"                       // NOTICE..DEBUG
    }

    function _passes(sev) {
        if (activeFilter === "all") return true
        if (activeFilter === "warning") return sev <= 4
        if (activeFilter === "error") return sev <= 3
        return true
    }

    function _appendEvent(sev, text) {
        allEvents.insert(0, {
            "cat": _severityCat(sev),
            "sev": sev,
            "text": text,
            "ts": Qt.formatDateTime(new Date(), "hh:mm:ss.zzz")
        })
        while (allEvents.count > kMaxEvents) allEvents.remove(allEvents.count - 1)
        _rebuildView()
    }

    function _rebuildView() {
        viewEvents.clear()
        for (var i = 0; i < allEvents.count; i++) {
            var m = allEvents.get(i)
            if (_passes(m.sev)) {
                viewEvents.append({ "cat": m.cat, "text": m.text, "ts": m.ts, "sev": m.sev })
            }
        }
        _lastCount = viewEvents.count
        emptyLabel.visible = viewEvents.count === 0
    }

    // Vehicle message feed
    Connections {
        target: vehicle
        function onTextMessageReceived(sysid, componentid, severity, text, description) {
            root._appendEvent(severity, text)
        }
    }

    onVehicleChanged: {
        // New vehicle (or disconnect): start a fresh session log.
        if (allEvents.count > 0) allEvents.clear()
        _rebuildView()
    }

    function _setFilter(f) {
        activeFilter = f
        _rebuildView()
    }

    // ── Visual ──
    width: 340
    height: 340
    radius: Config.radiusLarge
    color: Qt.rgba(Colors.surface.r, Colors.surface.g, Colors.surface.b, 0.96)
    border.color: Colors.border
    border.width: 1

    ColumnLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.leftMargin: Config.spacingMedium
        anchors.rightMargin: Config.spacingMedium
        anchors.topMargin: Config.spacingSmall
        spacing: Config.spacingSmall

        // Header + close
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: qsTr("EVENT LOG")
                font.pixelSize: Config.fontSizeSmall
                font.weight: Font.DemiBold
                color: Colors.textSecondary
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: root._filterLabel
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: qsTr("\u2715")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
                Layout.alignment: Qt.AlignVCenter
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeRequested()
                }
            }
        }

        // Filter segmented control
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Repeater {
                model: [ { key: "all", label: qsTr("All") },
                         { key: "warning", label: qsTr("Warning") },
                         { key: "error", label: qsTr("Error") } ]

                delegate: Rectangle {
                    property bool isActive: root.activeFilter === modelData.key
                    width: flLabel.implicitWidth + ScreenTools.defaultFontPixelWidth * 1.6
                    height: ScreenTools.defaultFontPixelHeight * 1.7
                    radius: height / 2
                    color: isActive ? Colors.teal
                                    : (_ma.containsMouse ? Colors.surfaceLight : Colors.surface)
                    border.color: isActive ? Colors.accentCyan
                                    : (root.activeFilter === modelData.key ? Colors.accentCyan
                                    : Qt.rgba(Colors.accentCyan.r, Colors.accentCyan.g, Colors.accentCyan.b, 0.4))
                    border.width: 1
                    Layout.alignment: Qt.AlignVCenter

                    Behavior on color { ColorAnimation { duration: 120 } }

                    Text {
                        id: flLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: Config.fontSizeSmall
                        font.weight: Font.DemiBold
                        color: isActive ? Colors.dialogText : Colors.textPrimary
                    }

                    MouseArea {
                        id: _ma
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root._setFilter(modelData.key)
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                Text {
                    anchors.right: parent.right
                    text: viewEvents.count + (viewEvents.count === 1 ? qsTr(" message") : qsTr(" messages"))
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                }
            }
        }

        // Message list
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Config.radiusSmall
            color: Qt.rgba(Colors.surface2.r, Colors.surface2.g, Colors.surface2.b, 0.60)
            clip: true
            border.color: "transparent"
            border.width: 0

            Rectangle {
                anchors.fill: parent
                border.color: Qt.rgba(Colors.border.r, Colors.border.g, Colors.border.b, 0.5)
                border.width: 1
                radius: Config.radiusSmall
                color: "transparent"
            }

            Text {
                id: emptyLabel
                anchors.centerIn: parent
                visible: viewEvents.count === 0
                text: vehicle ? qsTr("No events yet") : qsTr("No vehicle connected")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textDisabled
            }

            ListView {
                id: msgList
                anchors.fill: parent
                anchors.margins: 4
                model: viewEvents
                clip: true
                spacing: 2
                ScrollBar.vertical: ScrollBar {
                    policy: msgList.contentHeight > msgList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                delegate: Item {
                    width: msgList.width
                    height: msgText.implicitHeight + 12

                    Column {
                        anchors.fill: parent
                        spacing: 1

                        Text {
                            text: model.ts + (model.cat === "error" ? "   [ERR]"
                                             : model.cat === "warning" ? "   [WRN]"
                                                                       : "   [INF]")
                            font.pixelSize: Config.fontSizeSmall * 0.8
                            color: model.cat === "error" ? Colors.error
                                   : model.cat === "warning" ? Colors.warning
                                                             : Colors.textDisabled
                        }
                        Text {
                            id: msgText
                            width: parent.width
                            text: model.text
                            font.pixelSize: Config.fontSizeSmall
                            color: model.cat === "error" ? Colors.error
                                   : model.cat === "warning" ? Colors.warning
                                                             : Colors.textPrimary
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}