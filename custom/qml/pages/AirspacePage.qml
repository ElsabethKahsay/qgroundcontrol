import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation
import QtPositioning

import QGroundControl.Controls
import com.uav.preflight 1.0

// ── Airspace Compliance System ──────────────────────────────────────────────
// Persistent knowledge base of known restricted airspace (no-fly zones).
// Compliance is MANUAL: the operator reviews the zone list / map and records a
// decision.  There is NO automatic geometry/intersection checking and NO
// MAVLink geofence interaction — the Plan-view geofence is untouched.
//
// This page follows the QGC AnalyzePage pattern (like FlightHistoryPage) so it
// renders correctly inside the AnalyzeView panel loader.

AnalyzePage {
    id: root
    pageName: qsTr("Airspace Compliance")
    pageDescription: qsTr("Restricted-area knowledge base and manual compliance log")

    property bool _addZoneMode: false
    property var _lastMapCoord: null
    property int _selectedZoneId: -1
    property var _complianceRows: []

    readonly property int _currentFlightId: FlightSession.currentFlightId > 0 ? FlightSession.currentFlightId : -1

    // Map tile colors by zone reason
    function reasonColor(reason) {
        switch (reason) {
        case "Regulatory":
            return "#ef4444";
        case "Obstacle":
            return "#f97316";
        case "Restricted":
            return "#a855f7";
        case "Temporary":
            return "#3b82f6";
        default:
            return "#eab308";
        }
    }

    function reasonBorderColor(reason) {
        switch (reason) {
        case "Regulatory": return "#b91c1c";
        case "Obstacle": return "#c2410c";
        case "Restricted": return "#7e22ce";
        case "Temporary": return "#1d4ed8";
        default: return "#a16207";
        }
    }


    function reasonText(reason) {
        return reason && reason.length > 0 ? reason : "Other";
    }

    function zoneById(id) {
        for (var i = 0; i < NoFlyZoneModel.count; ++i) {
            var idx = NoFlyZoneModel.index(i, 0);
            if (NoFlyZoneModel.data(idx, NoFlyZoneModelRoles.ZoneIdRole) === id)
                return i;
        }
        return -1;
    }

    function formatCoord(v, dp) {
        return v.toFixed(dp || 5);
    }

    // Tap on the map: in add mode place a new zone, otherwise select the nearest
    // active zone (the zone is considered hit when tapped inside its radius).
    function _onMapClick(mapControl, pos) {
        if (!mapControl)
            return;
        var coord = mapControl.toCoordinate(Qt.point(pos.x, pos.y));
        if (_addZoneMode) {
            _addZoneMode = false;
            _lastMapCoord = coord;
            _selectedZoneId = -1;
            zoneDialog.openForCreate(coord.latitude, coord.longitude);
            return;
        }
        var bestId = -1, bestDist = -1;
        for (var i = 0; i < NoFlyZoneModel.count; ++i) {
            var m = NoFlyZoneModel.index(i, 0);
            if (!NoFlyZoneModel.data(m, NoFlyZoneModelRoles.ActiveRole))
                continue;
            var lat = NoFlyZoneModel.data(m, NoFlyZoneModelRoles.LatitudeRole);
            var lon = NoFlyZoneModel.data(m, NoFlyZoneModelRoles.LongitudeRole);
            var d = coord.distanceTo(QtPositioning.coordinate(lat, lon));
            var r = NoFlyZoneModel.data(m, NoFlyZoneModelRoles.RadiusMRole);
            if (d <= r && (bestDist < 0 || d < bestDist)) {
                bestDist = d;
                bestId = NoFlyZoneModel.data(m, NoFlyZoneModelRoles.ZoneIdRole);
            }
        }
        if (bestId > 0) {
            _selectedZoneId = bestId;
            zonePopup.open(bestId);
        }
    }

    // Center and zoom the map so all zones fit on screen.
    function _fitMapToZones(mapControl) {
        if (!mapControl || NoFlyZoneModel.count === 0)
            return;
        var latMin = 90, latMax = -90, lonMin = 180, lonMax = -180, maxRadius = 0;
        for (var i = 0; i < NoFlyZoneModel.count; ++i) {
            var m = NoFlyZoneModel.index(i, 0);
            var lat = NoFlyZoneModel.data(m, NoFlyZoneModelRoles.LatitudeRole);
            var lon = NoFlyZoneModel.data(m, NoFlyZoneModelRoles.LongitudeRole);
            var r = NoFlyZoneModel.data(m, NoFlyZoneModelRoles.RadiusMRole);
            latMin = Math.min(latMin, lat); latMax = Math.max(latMax, lat);
            lonMin = Math.min(lonMin, lon); lonMax = Math.max(lonMax, lon);
            maxRadius = Math.max(maxRadius, r);
        }
        var centerLat = (latMin + latMax) / 2;
        var centerLon = (lonMin + lonMax) / 2;
        var cosLat = Math.max(Math.cos(centerLat * Math.PI / 180), 0.15);
        var meters = Math.max((latMax - latMin) * 111320,
                              (lonMax - lonMin) * 111320 * cosLat,
                              maxRadius * 2);
        var mpp = meters / Math.max(mapControl.width * 0.6, 1);
        var zoom = Math.log2(156543.03392 * cosLat / Math.max(mpp, 1));
        mapControl.center = QtPositioning.coordinate(centerLat, centerLon);
        mapControl.zoomLevel = Math.min(Math.max(zoom, 2), Math.min(mapControl.maximumZoomLevel, 18));
    }

    pageComponent: Component {
        ColumnLayout {
            width: root.availableWidth
            height: root.availableHeight
            spacing: 0

            // ── Tabs ────────────────────────────────────────────────
            TabBar {
                id: tabBar
                Layout.fillWidth: true
                background: Rectangle {
                    color: "transparent"
                }

                TabButton {
                    text: qsTr("Zone List")
                    font.pixelSize: Config.fontSizeBody
                    font.bold: true
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: tabBar.currentIndex === 0 ? Colors.accentCyan : Colors.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle {
                            width: parent.width
                            height: 2
                            color: tabBar.currentIndex === 0 ? Colors.accentCyan : "transparent"
                            anchors.bottom: parent.bottom
                        }
                    }
                }
                TabButton {
                    text: qsTr("Map View")
                    font.pixelSize: Config.fontSizeBody
                    font.bold: true
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: tabBar.currentIndex === 1 ? Colors.accentCyan : Colors.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle {
                            width: parent.width
                            height: 2
                            color: tabBar.currentIndex === 1 ? Colors.accentCyan : "transparent"
                            anchors.bottom: parent.bottom
                        }
                    }
                }
                TabButton {
                    text: qsTr("Compliance Log")
                    font.pixelSize: Config.fontSizeBody
                    font.bold: true
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: tabBar.currentIndex === 2 ? Colors.accentCyan : Colors.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                    }
                    background: Rectangle {
                        color: "transparent"
                        Rectangle {
                            width: parent.width
                            height: 2
                            color: tabBar.currentIndex === 2 ? Colors.accentCyan : "transparent"
                            anchors.bottom: parent.bottom
                        }
                    }
                }
            }

            StackLayout {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: tabBar.currentIndex

                // ══════════════════════════════════════════════════════
                // TAB 1 — ZONE LIST
                // ══════════════════════════════════════════════════════
                Rectangle {
                    color: "transparent"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Config.spacingLarge
                        spacing: Config.spacingMedium

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Config.spacingSmall

                            Text {
                                text: qsTr("Restricted zones") + " (" + NoFlyZoneModel.count + ")"
                                font.pixelSize: Config.fontSizeH3
                                font.bold: true
                                color: Colors.textPrimary
                                Layout.fillWidth: true
                            }

                            AirButton {
                                text: qsTr("+ Add Zone")
                                onClicked: {
                                    root._selectedZoneId = -1;
                                    zoneDialog.openForCreate();
                                }
                            }
                        }

                        // ── Column headers ──────────────────────────
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: Colors.surfaceLight
                            radius: Config.radiusSmall
                            border.color: Colors.border
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Config.spacingMedium
                                anchors.rightMargin: Config.spacingMedium
                                spacing: Config.spacingSmall

                                Text {
                                    text: "ID"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.preferredWidth: 40
                                }
                                Text {
                                    text: "Name"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: "Radius"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.preferredWidth: 70
                                    horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: "Reason"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.preferredWidth: 100
                                }
                                Text {
                                    text: "Active"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.preferredWidth: 56
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }
                        }

                        // ── Zone rows + detail panel ────────────────
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: Config.spacingMedium

                            // Zone table
                            ListView {
                                id: zoneList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 4

                                model: NoFlyZoneModel

                                delegate: Rectangle {
                                    width: zoneList.width
                                    height: 34
                                    radius: Config.radiusSmall
                                    color: root._selectedZoneId === zoneId ? Colors.accentDim : Colors.surfaceLight
                                    border.color: root._selectedZoneId === zoneId ? Colors.accent : Colors.border
                                    border.width: 1

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: Config.spacingMedium
                                        anchors.rightMargin: Config.spacingMedium
                                        spacing: Config.spacingSmall

                                        Text {
                                            text: zoneId
                                            font.pixelSize: Config.fontSizeSmall
                                            color: Colors.textSecondary
                                            Layout.preferredWidth: 40
                                        }
                                        Text {
                                            text: name
                                            font.pixelSize: Config.fontSizeSmall
                                            color: Colors.textPrimary
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: radiusM.toFixed(0) + " m"
                                            font.pixelSize: Config.fontSizeSmall
                                            color: Colors.textSecondary
                                            Layout.preferredWidth: 70
                                            horizontalAlignment: Text.AlignRight
                                        }
                                        Text {
                                            text: root.reasonText(reason)
                                            font.pixelSize: Config.fontSizeSmall
                                            color: root.reasonColor(reason)
                                            Layout.preferredWidth: 100
                                        }
                                        Rectangle {
                                            Layout.preferredWidth: 12
                                            Layout.preferredHeight: 12
                                            radius: 6
                                            color: active ? Colors.success : Colors.textDisabled
                                            Layout.alignment: Qt.AlignHCenter
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root._selectedZoneId = zoneId
                                    }
                                }

                                Text {
                                    text: qsTr("No zones defined yet — use '+ Add Zone' to build the knowledge base.")
                                    font.pixelSize: Config.fontSizeBody
                                    color: Colors.textSecondary
                                    wrapMode: Text.WordWrap
                                    anchors.centerIn: parent
                                    visible: NoFlyZoneModel.count === 0
                                }
                            }

                            // Detail panel
                            Rectangle {
                                id: detailPanel
                                Layout.preferredWidth: 280
                                Layout.fillHeight: true
                                color: Colors.surfaceLight
                                radius: Config.radiusMedium
                                border.color: Colors.border
                                border.width: 1
                                visible: NoFlyZoneModel.count > 0

                                property var _row: {
                                    var i = root.zoneById(root._selectedZoneId);
                                    if (i < 0)
                                        return null;
                                    var m = NoFlyZoneModel.index(i, 0);
                                    return {
                                        "id": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.ZoneIdRole),
                                        "name": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.NameRole),
                                        "desc": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.DescriptionRole),
                                        "lat": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.LatitudeRole),
                                        "lon": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.LongitudeRole),
                                        "radius": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.RadiusMRole),
                                        "reason": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.ReasonRole),
                                        "active": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.ActiveRole),
                                        "created": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.CreatedAtRole),
                                        "updated": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.UpdatedAtRole),
                                        "creator": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.CreatedByNameRole)
                                    };
                                }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: Config.spacingMedium
                                    spacing: Config.spacingSmall

                                    Text {
                                        text: qsTr("Zone details")
                                        font.pixelSize: Config.fontSizeH3
                                        font.bold: true
                                        color: Colors.textPrimary
                                    }

                                    Text {
                                        text: detailPanel._row ? detailPanel._row.name : qsTr("Select a zone")
                                        font.pixelSize: Config.fontSizeBody
                                        font.bold: true
                                        color: Colors.textPrimary
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: detailPanel._row ? "Lat: " + root.formatCoord(detailPanel._row.lat) + "\nLon: " + root.formatCoord(detailPanel._row.lon) + "\nRadius: " + detailPanel._row.radius.toFixed(0) + " m" + "\nReason: " + detailPanel._row.reason + "\nStatus: " + (detailPanel._row.active ? "Active" : "Inactive") : ""
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textSecondary
                                        lineHeight: 1.5
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: detailPanel._row && detailPanel._row.desc.length > 0
                                        text: detailPanel._row && detailPanel._row.desc ? detailPanel._row.desc : ""
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textSecondary
                                        wrapMode: Text.WordWrap
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: detailPanel._row
                                        text: detailPanel._row ? "Added by: " + (detailPanel._row.creator && detailPanel._row.creator.length > 0 ? detailPanel._row.creator : "—") + "\nCreated: " + detailPanel._row.created + "\nUpdated: " + detailPanel._row.updated : ""
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textMuted
                                        lineHeight: 1.4
                                        wrapMode: Text.WordWrap
                                    }

                                    Item {
                                        Layout.fillHeight: true
                                    }

                                    AirButton {
                                        Layout.fillWidth: true
                                        text: qsTr("Edit")
                                        btnEnabled: detailPanel._row
                                        onClicked: zoneDialog.openForEdit(detailPanel._row)
                                    }

                                    AirButton {
                                        Layout.fillWidth: true
                                        text: qsTr("Toggle Active")
                                        baseColor: detailPanel._row && detailPanel._row.active ? Colors.warning : Colors.success
                                        btnEnabled: detailPanel._row
                                        onClicked: NoFlyZoneModel.toggleZoneActive(detailPanel._row.id)
                                    }

                                    AirButton {
                                        Layout.fillWidth: true
                                        text: qsTr("Delete")
                                        baseColor: Colors.error
                                        btnEnabled: detailPanel._row
                                        onClicked: {
                                            NoFlyZoneModel.removeZone(detailPanel._row.id);
                                            root._selectedZoneId = -1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ══════════════════════════════════════════════════════
                // TAB 2 — MAP VIEW (display only)
                // ══════════════════════════════════════════════════════
                Rectangle {
                    color: "transparent"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Config.spacingLarge
                        spacing: Config.spacingMedium

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Config.spacingSmall

                            Text {
                                text: qsTr("Active restricted zones")
                                font.pixelSize: Config.fontSizeH3
                                font.bold: true
                                color: Colors.textPrimary
                                Layout.fillWidth: true
                            }

                            Rectangle {
                                Layout.preferredHeight: 30
                                Layout.preferredWidth: addHereTxt.implicitWidth + Config.spacingMedium * 2
                                radius: Config.radiusSmall
                                color: root._addZoneMode ? Colors.warning : Colors.surfaceLight
                                border.color: root._addZoneMode ? Colors.warning : Colors.border
                                border.width: 1
                                Text {
                                    id: addHereTxt
                                    anchors.centerIn: parent
                                    text: root._addZoneMode ? "Click map to place zone…" : "+ Add zone here"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: root._addZoneMode ? Colors.background : Colors.textPrimary
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root._addZoneMode = !root._addZoneMode
                                }
                            }
                        }

                        // ── Map ─────────────────────────────────────
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Config.radiusMedium
                            clip: true
                            color: Colors.surfaceLight
                            border.color: Colors.border
                            border.width: 1

                            Map {
                                id: map
                                anchors.fill: parent
                                plugin: Plugin {
                                    name: "QGroundControl"
                                }

                                center: QtPositioning.coordinate(9.03, 38.74)
                                zoomLevel: 13

                                Component.onCompleted: {
                                    root._fitMapToZones(map)
                                }

                                // 1. Render Colored No-Fly Zone Circles
                                MapItemView {
                                    model: NoFlyZoneModel
                                    delegate: MapCircle {
                                        center: QtPositioning.coordinate(latitude, longitude)
                                        radius: radiusM
                                        color: root.reasonColor(reason)
                                        border.color: root.reasonBorderColor(reason)
                                        border.width: root._selectedZoneId === zoneId ? 4 : 3
                                        opacity: active ? (root._selectedZoneId === zoneId ? 0.95 : 0.75) : 0.0
                                        visible: active
                                    }
                                }

                                // 2. Render Zone Title & Radius Labels Centered on Each Circle
                                MapItemView {
                                    model: NoFlyZoneModel
                                    delegate: MapQuickItem {
                                        coordinate: QtPositioning.coordinate(latitude, longitude)
                                        anchorPoint.x: zoneTagBg.width / 2
                                        anchorPoint.y: zoneTagBg.height / 2
                                        visible: active

                                        sourceItem: Rectangle {
                                            id: zoneTagBg
                                            implicitWidth: zoneTagCol.implicitWidth + 14
                                            implicitHeight: zoneTagCol.implicitHeight + 8
                                            radius: 6
                                            color: "#e20f172a"
                                            border.color: root.reasonColor(reason)
                                            border.width: 1.5

                                            Column {
                                                id: zoneTagCol
                                                anchors.centerIn: parent
                                                spacing: 2
                                                Row {
                                                    spacing: 4
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    Rectangle {
                                                        width: 8; height: 8; radius: 4
                                                        color: root.reasonColor(reason)
                                                        anchors.verticalCenter: parent.verticalCenter
                                                    }
                                                    Text {
                                                        text: name
                                                        font.pixelSize: 11
                                                        font.bold: true
                                                        color: "#ffffff"
                                                    }
                                                }
                                                Text {
                                                    text: reason + " • " + (radiusM >= 1000 ? (radiusM/1000).toFixed(1) + " km" : radiusM.toFixed(0) + " m")
                                                    font.pixelSize: 9
                                                    color: "#cbd5e1"
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    root._selectedZoneId = zoneId
                                                    zonePopup.open(zoneId)
                                                }
                                            }
                                        }
                                    }
                                }

                                // Interactive Handling: drag to pan, wheel to zoom, tap to select/place.
                                // Qt 6.8's QtLocation Map has no built-in drag-panning, so a DragHandler
                                // translates the center from the pointer centroid. TapHandler uses the
                                // DragThreshold policy so drags fall through to the pan handler, and
                                // WheelHandler zooms. A full-size MouseArea would grab the press and
                                // prevent any of this.
                                DragHandler {
                                    id: mapPan
                                    target: map
                                    property point lastPos: Qt.point(0, 0)
                                    property variant cur: QtPositioning.coordinate(0, 0)
                                    onActiveChanged: {
                                        if (active) {
                                            cur = map.center
                                            lastPos = centroid.position
                                        }
                                    }
                                    onCentroidChanged: {
                                        if (!active) return
                                        var pos = centroid.position
                                        var dx = pos.x - lastPos.x
                                        var dy = pos.y - lastPos.y
                                        lastPos = pos
                                        if (dx === 0 && dy === 0) return
                                        var tl = map.toCoordinate(Qt.point(0, 0))
                                        var br = map.toCoordinate(Qt.point(map.width, map.height))
                                        var nlat = cur.latitude + (dy / map.height) * (tl.latitude - br.latitude)
                                        var nlon = cur.longitude - (dx / map.width) * (br.longitude - tl.longitude)
                                        cur = QtPositioning.coordinate(nlat, nlon)
                                        map.center = cur
                                    }
                                }

                                WheelHandler {
                                    acceptedDevices: PointerDevice.Mouse
                                    onWheel: (event) => {
                                        if (event.angleDelta.y > 0) {
                                            map.zoomLevel = Math.min(map.zoomLevel + 0.5, map.maximumZoomLevel)
                                        } else if (event.angleDelta.y < 0) {
                                            map.zoomLevel = Math.max(map.zoomLevel - 0.5, map.minimumZoomLevel)
                                        }
                                        event.accepted = true
                                    }
                                }

                                TapHandler {
                                    acceptedButtons: Qt.LeftButton
                                    gesturePolicy: TapHandler.DragThreshold
                                    onTapped: (eventPoint, button) => {
                                        root._onMapClick(map, Qt.point(eventPoint.position.x, eventPoint.position.y))
                                    }
                                }
                            }

                            // ── Map Floating Zoom & Navigation Controls ──
                            Column {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: Config.spacingMedium
                                spacing: Config.spacingSmall

                                // Fit All Zones Button
                                Rectangle {
                                    width: 36; height: 36; radius: Config.radiusSmall
                                    color: Colors.surfaceLight
                                    border.color: Colors.border; border.width: 1
                                    Text {
                                        anchors.centerIn: parent
                                        text: "🎯"
                                        font.pixelSize: 16
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root._fitMapToZones(map)
                                    }
                                }

                                // Zoom In Button (+)
                                Rectangle {
                                    width: 36; height: 36; radius: Config.radiusSmall
                                    color: Colors.surfaceLight
                                    border.color: Colors.border; border.width: 1
                                    Text {
                                        anchors.centerIn: parent
                                        text: "+"
                                        font.pixelSize: 20; font.bold: true
                                        color: Colors.textPrimary
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: map.zoomLevel = Math.min(map.zoomLevel + 1, map.maximumZoomLevel)
                                    }
                                }

                                // Zoom Out Button (-)
                                Rectangle {
                                    width: 36; height: 36; radius: Config.radiusSmall
                                    color: Colors.surfaceLight
                                    border.color: Colors.border; border.width: 1
                                    Text {
                                        anchors.centerIn: parent
                                        text: "−"
                                        font.pixelSize: 20; font.bold: true
                                        color: Colors.textPrimary
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: map.zoomLevel = Math.max(map.zoomLevel - 1, map.minimumZoomLevel)
                                    }
                                }
                            }

                            // ── Map Legend ──
                            Rectangle {
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.margins: Config.spacingMedium
                                radius: Config.radiusSmall
                                color: Colors.surfaceLight
                                border.color: Colors.border
                                border.width: 1

                                Row {
                                    anchors.fill: parent
                                    anchors.margins: Config.spacingSmall
                                    spacing: Config.spacingMedium

                                    Repeater {
                                        model: ["Regulatory", "Obstacle", "Restricted", "Temporary", "Other"]
                                        Row {
                                            spacing: 4
                                            Rectangle {
                                                width: 10
                                                height: 10
                                                radius: 5
                                                color: root.reasonColor(modelData)
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Text {
                                                text: modelData
                                                font.pixelSize: Config.fontSizeSmall
                                                color: Colors.textSecondary
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }
                                    }
                                }
                            }
                        }

                    }
                }

                // ══════════════════════════════════════════════════════════
                // TAB 3 — COMPLIANCE LOG
                // ══════════════════════════════════════════════════════════
                Rectangle {
                    color: "transparent"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Config.spacingLarge
                        spacing: Config.spacingMedium

                        // Status banner
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            radius: Config.radiusSmall
                            color: NoFlyZoneModel.complianceChecked ? Colors.successDim : Colors.warningDim
                            border.color: NoFlyZoneModel.complianceChecked ? Colors.success : Colors.warning
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: Config.spacingMedium
                                spacing: Config.spacingSmall

                                Text {
                                    text: NoFlyZoneModel.complianceChecked ? "✓" : "⚠"
                                    font.pixelSize: 18
                                    color: NoFlyZoneModel.complianceChecked ? Colors.success : Colors.warning
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: NoFlyZoneModel.complianceChecked ? qsTr("Airspace compliance checked for this mission") : qsTr("Mission is NOT yet marked 'Compliance Checked'")
                                    font.pixelSize: Config.fontSizeBody
                                    font.bold: true
                                    color: NoFlyZoneModel.complianceChecked ? Colors.success : Colors.warning
                                }
                                AirButton {
                                    text: qsTr("Run Compliance Check")
                                    onClicked: complianceDialog.open()
                                }
                            }
                        }

                        // ── History table ───────────────────────────
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            color: Colors.surfaceLight
                            radius: Config.radiusSmall
                            border.color: Colors.border
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Config.spacingMedium
                                anchors.rightMargin: Config.spacingMedium
                                spacing: Config.spacingSmall
                                Text {
                                    text: "Flight ID"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.preferredWidth: 70
                                }
                                Text {
                                    text: "Date"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.preferredWidth: 150
                                }
                                Text {
                                    text: "Operator"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.preferredWidth: 110
                                }
                                Text {
                                    text: "Result"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.preferredWidth: 80
                                }
                                Text {
                                    text: "Notes / Override"
                                    font.pixelSize: Config.fontSizeSmall
                                    font.bold: true
                                    color: Colors.textSecondary
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        ListView {
                            id: complianceList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 4

                            model: NoFlyZoneModel.complianceRecords

                            delegate: Rectangle {
                                width: complianceList.width
                                height: 34
                                radius: Config.radiusSmall
                                color: Colors.surfaceLight
                                border.color: Colors.border
                                border.width: 1

                                function resultColor(r) {
                                    if (r === "Clear")
                                        return Colors.success;
                                    if (r === "Caution")
                                        return Colors.warning;
                                    return Colors.error;
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Config.spacingMedium
                                    anchors.rightMargin: Config.spacingMedium
                                    spacing: Config.spacingSmall

                                    Text {
                                        text: modelData.flight_id > 0 ? modelData.flight_id : "—"
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textSecondary
                                        Layout.preferredWidth: 70
                                    }
                                    Text {
                                        text: modelData.checked_at
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textSecondary
                                        Layout.preferredWidth: 150
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: modelData.operator_name && modelData.operator_name.length > 0 ? modelData.operator_name : "—"
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textPrimary
                                        Layout.preferredWidth: 110
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: modelData.result
                                        font.pixelSize: Config.fontSizeSmall
                                        font.bold: true
                                        color: delegate.resultColor(modelData.result)
                                        Layout.preferredWidth: 80
                                    }
                                    Text {
                                        text: {
                                            var parts = [];
                                            if (modelData.notes && modelData.notes.length > 0)
                                                parts.push(modelData.notes);
                                            if (modelData.override_reason && modelData.override_reason.length > 0)
                                                parts.push("Override: " + modelData.override_reason);
                                            return parts.length > 0 ? parts.join("  ·  ") : "";
                                        }
                                        font.pixelSize: Config.fontSizeSmall
                                        color: Colors.textSecondary
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: recordDialog.open(modelData)
                                }
                            }

                            Text {
                                text: qsTr("No compliance records yet. Run a compliance check to create the first audit entry.")
                                font.pixelSize: Config.fontSizeBody
                                color: Colors.textSecondary
                                wrapMode: Text.WordWrap
                                anchors.centerIn: parent
                                visible: NoFlyZoneModel.complianceRecords.length === 0
                            }
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // ZONE ADD / EDIT FORM
        // ═══════════════════════════════════════════════════════════════════
    }

    Dialog {
        id: zoneDialog
        property int _editId: -1
        property bool _isEdit: false
        property real _initLat: 0.0
        property real _initLon: 0.0

        title: _isEdit ? qsTr("Edit Zone") : qsTr("Add Zone")
        modal: true
        anchors.centerIn: parent
        width: 420
        closePolicy: Popup.CloseOnEscape
        padding: Config.spacingLarge
        background: Rectangle {
            color: Colors.surface
            radius: Config.radiusMedium
            border.color: Colors.border
            border.width: 1
        }
        header: Label {
            text: zoneDialog.title
            font.bold: true
            color: Colors.textPrimary
            padding: Config.spacingMedium
            background: Rectangle {
                color: Colors.surfaceLight
            }
        }

        function openForCreate(lat, lon) {
            _isEdit = false;
            _editId = -1;
            nameField.text = "";
            descField.text = "";
            latField.text = lat !== undefined ? root.formatCoord(lat, 6) : "";
            lonField.text = lon !== undefined ? root.formatCoord(lon, 6) : "";
            radiusSpin.value = 500;
            reasonCombo.currentIndex = 0;
            activeSwitch.checked = true;
            open();
        }

        function openForEdit(row) {
            _isEdit = true;
            _editId = row.id;
            nameField.text = row.name;
            descField.text = row.desc;
            latField.text = root.formatCoord(row.lat, 6);
            lonField.text = root.formatCoord(row.lon, 6);
            radiusSpin.value = row.radius;
            var ri = reasonCombo.find(row.reason);
            reasonCombo.currentIndex = ri >= 0 ? ri : 0;
            activeSwitch.checked = row.active;
            open();
        }

        function useMapCenter() {
            var lat = root._lastMapCoord ? root._lastMapCoord.latitude : 0.0;
            var lon = root._lastMapCoord ? root._lastMapCoord.longitude : 0.0;
            latField.text = root.formatCoord(lat, 6);
            lonField.text = root.formatCoord(lon, 6);
        }

        readonly property bool _valid: nameField.text.trim().length > 0 && latField.text.length > 0 && lonField.text.length > 0 && !isNaN(parseFloat(latField.text)) && Math.abs(parseFloat(latField.text)) <= 90 && Math.abs(parseFloat(lonField.text)) <= 180 && parseFloat(radiusSpin.value) >= 50

        ColumnLayout {
            width: parent.width
            spacing: Config.spacingMedium

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Name *"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 90
                }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("e.g. Airport control zone")
                    font.pixelSize: Config.fontSizeSmall
                    background: Rectangle {
                        color: Colors.surfaceLight
                        radius: Config.radiusSmall
                        border.color: nameField.activeFocus ? Colors.accent : Colors.border
                        border.width: 1
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Description"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 90
                    Layout.alignment: Qt.AlignTop
                }
                TextArea {
                    id: descField
                    Layout.fillWidth: true
                    Layout.minimumHeight: 56
                    placeholderText: qsTr("Optional description…")
                    font.pixelSize: Config.fontSizeSmall
                    wrapMode: TextArea.WordWrap
                    background: Rectangle {
                        color: Colors.surfaceLight
                        radius: Config.radiusSmall
                        border.color: descField.activeFocus ? Colors.accent : Colors.border
                        border.width: 1
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Latitude *"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 90
                }
                TextField {
                    id: latField
                    Layout.fillWidth: true
                    placeholderText: "e.g. 47.39774"
                    font.pixelSize: Config.fontSizeSmall
                    validator: DoubleValidator {
                        bottom: -90
                        top: 90
                    }
                    background: Rectangle {
                        color: Colors.surfaceLight
                        radius: Config.radiusSmall
                        border.color: latField.activeFocus ? Colors.accent : Colors.border
                        border.width: 1
                    }
                }
                AirButton {
                    text: "Use map center"
                    implicitWidth: 118
                    onClicked: zoneDialog.useMapCenter()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Longitude *"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 90
                }
                TextField {
                    id: lonField
                    Layout.fillWidth: true
                    placeholderText: "e.g. 8.66115"
                    font.pixelSize: Config.fontSizeSmall
                    validator: DoubleValidator {
                        bottom: -180
                        top: 180
                    }
                    background: Rectangle {
                        color: Colors.surfaceLight
                        radius: Config.radiusSmall
                        border.color: lonField.activeFocus ? Colors.accent : Colors.border
                        border.width: 1
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Radius (m) *"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 90
                }
                SpinBox {
                    id: radiusSpin
                    Layout.fillWidth: true
                    from: 50
                    to: 50000
                    stepSize: 50
                    editable: true
                    value: 500
                    font.pixelSize: Config.fontSizeSmall
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Reason"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 90
                }
                ComboBox {
                    id: reasonCombo
                    Layout.fillWidth: true
                    model: ["Regulatory", "Obstacle", "Restricted", "Temporary", "Other"]
                    currentIndex: 0
                    font.pixelSize: Config.fontSizeSmall
                    background: Rectangle {
                        color: Colors.surfaceLight
                        radius: Config.radiusSmall
                        border.color: reasonCombo.activeFocus ? Colors.accent : Colors.border
                        border.width: 1
                    }
                    indicator: Text {
                        x: reasonCombo.width - width - Config.spacingSmall
                        y: (reasonCombo.height - height) / 2
                        text: "\u25BC"
                        color: Colors.textSecondary
                        font.pixelSize: 10
                    }
                    contentItem: Text {
                        text: reasonCombo.currentText
                        color: Colors.textPrimary
                        font.pixelSize: Config.fontSizeSmall
                        leftPadding: Config.spacingSmall
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Active"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 90
                }
                Switch {
                    id: activeSwitch
                    checked: true
                }
            }
        }

        footer: DialogButtonBox {
            alignment: Qt.AlignRight
            spacing: Config.spacingMedium
            background: Rectangle {
                color: Colors.surface
                border.color: Colors.divider
                border.width: 1
            }

            Button {
                text: qsTr("Cancel")
                font.pixelSize: Config.fontSizeBody
                contentItem: Text {
                    text: "Cancel"
                    color: Colors.textSecondary
                    font.pixelSize: Config.fontSizeBody
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: Colors.surfaceLight
                    radius: Config.radiusSmall
                    border.color: Colors.border
                    border.width: 1
                }
                onClicked: zoneDialog.close()
            }
            Button {
                text: qsTr("Save")
                font.pixelSize: Config.fontSizeBody
                enabled: zoneDialog._valid
                contentItem: Text {
                    text: "Save"
                    color: zoneDialog._valid ? Colors.background : Colors.textDisabled
                    font.pixelSize: Config.fontSizeBody
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: zoneDialog._valid ? Colors.accent : Colors.surfaceLight
                    radius: Config.radiusSmall
                    border.color: zoneDialog._valid ? Colors.accent : Colors.border
                    border.width: 1
                }
                onClicked: {
                    var ok;
                    if (zoneDialog._isEdit) {
                        ok = NoFlyZoneModel.updateZone(zoneDialog._editId, nameField.text.trim(), descField.text.trim(), parseFloat(latField.text), parseFloat(lonField.text), radiusSpin.value, reasonCombo.currentText, activeSwitch.checked);
                    } else {
                        var id = NoFlyZoneModel.createZone(nameField.text.trim(), descField.text.trim(), parseFloat(latField.text), parseFloat(lonField.text), radiusSpin.value, reasonCombo.currentText);
                        ok = id > 0;
                        if (ok)
                            root._selectedZoneId = id;
                    }
                    if (ok)
                        zoneDialog.close();
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // ZONE DETAIL POPUP (map tap)
    // ═══════════════════════════════════════════════════════════════════
    Popup {
        id: zonePopup
        property var _row: null
        width: 300
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        anchors.centerIn: parent

        background: Rectangle {
            color: Colors.surface
            radius: Config.radiusMedium
            border.color: Colors.border
            border.width: 1
        }

        function open(zoneId) {
            var i = root.zoneById(zoneId);
            if (i < 0)
                return;
            var m = NoFlyZoneModel.index(i, 0);
            _row = {
                "name": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.NameRole),
                "lat": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.LatitudeRole),
                "lon": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.LongitudeRole),
                "radius": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.RadiusMRole),
                "reason": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.ReasonRole),
                "desc": NoFlyZoneModel.data(m, NoFlyZoneModelRoles.DescriptionRole)
            };
            open();
        }

        Column {
            spacing: Config.spacingSmall
            width: parent.width

            Text {
                text: zonePopup._row ? zonePopup._row.name : ""
                font.pixelSize: Config.fontSizeH3
                font.bold: true
                color: Colors.textPrimary
                wrapMode: Text.WordWrap
                width: parent.width
            }
            Text {
                text: zonePopup._row ? "Lat " + root.formatCoord(zonePopup._row.lat) + "  ·  Lon " + root.formatCoord(zonePopup._row.lon) + "\nRadius: " + zonePopup._row.radius.toFixed(0) + " m  ·  Reason: " + zonePopup._row.reason : ""
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
                lineHeight: 1.4
                width: parent.width
            }
            Text {
                text: zonePopup._row ? zonePopup._row.desc : ""
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
                wrapMode: Text.WordWrap
                width: parent.width
                visible: text.length > 0
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // COMPLIANCE RECORD DETAIL POPUP
    // ═══════════════════════════════════════════════════════════════════
    Popup {
        id: recordDialog
        property var _row: null
        width: 380
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        anchors.centerIn: parent
        background: Rectangle {
            color: Colors.surface
            radius: Config.radiusMedium
            border.color: Colors.border
            border.width: 1
        }

        function open(row) {
            _row = row;
            open();
        }

        Column {
            spacing: Config.spacingSmall
            width: parent.width

            Text {
                text: "Compliance Record"
                font.pixelSize: Config.fontSizeH3
                font.bold: true
                color: Colors.textPrimary
            }
            Text {
                text: recordDialog._row ? "Flight: " + recordDialog._row.flight_id + "\nDate: " + recordDialog._row.checked_at + "\nOperator: " + recordDialog._row.operator_name + "\nResult: " + recordDialog._row.result : ""
                font.pixelSize: Config.fontSizeBody
                color: Colors.textPrimary
                lineHeight: 1.5
            }
            Text {
                text: recordDialog._row && recordDialog._row.notes ? "Notes:\n" + recordDialog._row.notes : ""
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
                wrapMode: Text.WordWrap
                visible: text.length > 0
                width: parent.width
            }
            Text {
                text: recordDialog._row && recordDialog._row.override_reason ? "Override reason:\n" + recordDialog._row.override_reason : ""
                font.pixelSize: Config.fontSizeSmall
                color: Colors.warning
                wrapMode: Text.WordWrap
                visible: text.length > 0
                width: parent.width
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // MANUAL COMPLIANCE FORM (Part 3)
    // ═══════════════════════════════════════════════════════════════════
    Dialog {
        id: complianceDialog
        title: qsTr("Manual Airspace Compliance Check")
        modal: true
        anchors.centerIn: parent
        width: 460
        closePolicy: Popup.NoAutoClose
        padding: Config.spacingLarge
        background: Rectangle {
            color: Colors.surface
            radius: Config.radiusMedium
            border.color: Colors.border
            border.width: 1
        }
        header: Label {
            text: complianceDialog.title
            font.bold: true
            color: Colors.textPrimary
            padding: Config.spacingMedium
            background: Rectangle {
                color: Colors.surfaceLight
            }
        }

        readonly property bool _reviewed: reviewCheck.checked
        readonly property bool _resultSelected: resultCombo.currentIndex > 0
        readonly property bool _overrideOk: resultCombo.currentText !== "Conflict" || overrideField.text.trim().length > 0
        readonly property bool _canSubmit: _reviewed && _resultSelected && _overrideOk

        ColumnLayout {
            width: parent.width
            spacing: Config.spacingMedium

            // Auto fields
            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall

                Text {
                    text: "Mission / Flight"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 120
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 30
                    radius: Config.radiusSmall
                    color: Colors.surfaceLight
                    border.color: Colors.border
                    border.width: 1
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Config.spacingSmall
                        anchors.verticalCenter: parent.verticalCenter
                        text: root._currentFlightId > 0 ? "Flight #" + root._currentFlightId : "No active flight record"
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textPrimary
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Operator"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 120
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 30
                    radius: Config.radiusSmall
                    color: Colors.surfaceLight
                    border.color: Colors.border
                    border.width: 1
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Config.spacingSmall
                        anchors.verticalCenter: parent.verticalCenter
                        text: OperatorManager.currentOperatorName.length > 0 ? OperatorManager.currentOperatorName : "No operator selected"
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textPrimary
                    }
                }
            }

            // Confirmation checkbox (required)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: Config.radiusSmall
                color: reviewCheck.checked ? Colors.successDim : Colors.surfaceLight
                border.color: reviewCheck.checked ? Colors.success : Colors.border
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Config.spacingSmall
                    spacing: Config.spacingSmall
                    CheckBox {
                        id: reviewCheck
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("I have reviewed the current Restricted Area list")
                        font.pixelSize: Config.fontSizeBody
                        color: Colors.textPrimary
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Result (required)
            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Result *"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 120
                }
                ComboBox {
                    id: resultCombo
                    Layout.fillWidth: true
                    model: ["Select result…", "Clear", "Caution", "Conflict"]
                    currentIndex: 0
                    font.pixelSize: Config.fontSizeSmall
                    background: Rectangle {
                        color: Colors.surfaceLight
                        radius: Config.radiusSmall
                        border.color: resultCombo.activeFocus ? Colors.accent : Colors.border
                        border.width: 1
                    }
                    indicator: Text {
                        x: resultCombo.width - width - Config.spacingSmall
                        y: (resultCombo.height - height) / 2
                        text: "\u25BC"
                        color: Colors.textSecondary
                        font.pixelSize: 10
                    }
                    contentItem: Text {
                        text: resultCombo.currentText
                        color: resultCombo.currentIndex === 0 ? Colors.textDisabled : Colors.textPrimary
                        font.pixelSize: Config.fontSizeSmall
                        leftPadding: Config.spacingSmall
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Notes (optional)
            RowLayout {
                Layout.fillWidth: true
                spacing: Config.spacingSmall
                Text {
                    text: "Notes"
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.preferredWidth: 120
                    Layout.alignment: Qt.AlignTop
                }
                TextArea {
                    id: notesField
                    Layout.fillWidth: true
                    Layout.minimumHeight: 52
                    placeholderText: qsTr("Optional free text…")
                    font.pixelSize: Config.fontSizeSmall
                    wrapMode: TextArea.WordWrap
                    background: Rectangle {
                        color: Colors.surfaceLight
                        radius: Config.radiusSmall
                        border.color: notesField.activeFocus ? Colors.accent : Colors.border
                        border.width: 1
                    }
                }
            }

            // Override reason (required only if Conflict + proceed)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Config.spacingSmall
                    Text {
                        text: "Override reason"
                        font.pixelSize: Config.fontSizeSmall
                        color: resultCombo.currentText === "Conflict" ? Colors.warning : Colors.textSecondary
                        Layout.preferredWidth: 120
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: resultCombo.currentText === "Conflict"
                        text: qsTr("Required — you are proceeding despite a conflict")
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.warning
                        wrapMode: Text.WordWrap
                    }
                }
                TextArea {
                    id: overrideField
                    Layout.fillWidth: true
                    Layout.minimumHeight: 44
                    enabled: resultCombo.currentText === "Conflict"
                    placeholderText: resultCombo.currentText === "Conflict" ? qsTr("Required if you still want to proceed…") : qsTr("Only needed for a Conflict result")
                    font.pixelSize: Config.fontSizeSmall
                    wrapMode: TextArea.WordWrap
                    color: resultCombo.currentText === "Conflict" ? Colors.textPrimary : Colors.textDisabled
                    background: Rectangle {
                        color: Colors.surfaceLight
                        radius: Config.radiusSmall
                        border.color: overrideField.activeFocus ? Colors.accent : Colors.border
                        border.width: 1
                    }
                }
            }
        }

        footer: DialogButtonBox {
            alignment: Qt.AlignRight
            spacing: Config.spacingMedium
            background: Rectangle {
                color: Colors.surface
                border.color: Colors.divider
                border.width: 1
            }

            Button {
                text: qsTr("Cancel")
                font.pixelSize: Config.fontSizeBody
                contentItem: Text {
                    text: "Cancel"
                    color: Colors.textSecondary
                    font.pixelSize: Config.fontSizeBody
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: Colors.surfaceLight
                    radius: Config.radiusSmall
                    border.color: Colors.border
                    border.width: 1
                }
                onClicked: complianceDialog.close()
            }
            Button {
                text: qsTr("Submit & Mark Checked")
                font.pixelSize: Config.fontSizeBody
                enabled: complianceDialog._canSubmit
                contentItem: Text {
                    text: "Submit & Mark Checked"
                    color: complianceDialog._canSubmit ? Colors.background : Colors.textDisabled
                    font.pixelSize: Config.fontSizeBody
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: complianceDialog._canSubmit ? Colors.accent : Colors.surfaceLight
                    radius: Config.radiusSmall
                    border.color: complianceDialog._canSubmit ? Colors.accent : Colors.border
                    border.width: 1
                }
                onClicked: {
                    var ok = NoFlyZoneModel.submitCompliance(resultCombo.currentText, notesField.text.trim(), overrideField.text.trim());
                    if (ok)
                        complianceDialog.close();
                }
            }
        }
    }
}
