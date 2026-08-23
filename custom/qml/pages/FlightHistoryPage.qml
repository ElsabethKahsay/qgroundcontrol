import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl.Controls
import com.uav.preflight 1.0
import cpts 1.0

AnalyzePage {
    id: root
    pageName: qsTr("Flight History")
    pageDescription: qsTr("Completed flight records with full audit detail")

    property var historyModel: FlightHistoryModel {}
    property int _selectedFlightId: -1
    property var _detail: ({})

    property var _flight: ({})
    property var _preChecks: []
    property var _postChecks: []
    property var _events: []
    property var _motorTests: []
    property var _surfaceTests: []
    property var _compliance: []
    property var _handovers: []

    on_DetailChanged: {
        _flight      = _detail && _detail.flight       ? _detail.flight       : ({})
        _preChecks   = _detail && _detail.checks_pre   ? _detail.checks_pre   : []
        _postChecks  = _detail && _detail.checks_post  ? _detail.checks_post  : []
        _events      = _detail && _detail.events       ? _detail.events       : []
        _motorTests  = _detail && _detail.motor_tests  ? _detail.motor_tests  : []
        _surfaceTests= _detail && _detail.surface_tests? _detail.surface_tests: []
        _compliance  = _detail && _detail.compliance   ? _detail.compliance   : []
        _handovers   = _detail && _detail.handovers    ? _detail.handovers    : []
    }

    // ── Helpers ──────────────────────────────────────────────────────
    function fmtTime(ts) {
        return ts && ts.indexOf(" ") >= 0 ? ts.split(" ")[1] : (ts || "")
    }
    function summaryLine(checks) {
        var stats = categoryStats(checks)
        var parts = []
        for (var i = 0; i < stats.length; ++i) {
            var s = stats[i]
            var icon = s.fail > 0 ? "✗" : (s.warn > 0 ? "⚠" : "✓")
            parts.push(s.category + " " + icon + " " + s.pass + "/" + (s.pass + s.fail + s.warn))
        }
        return parts.join("   ")
    }
    function fmtDuration(sec) {
        sec = Math.max(0, sec || 0)
        var h = Math.floor(sec / 3600)
        var m = Math.floor((sec % 3600) / 60)
        var s = sec % 60
        if (h > 0) return h + "h " + m + "m"
        if (m > 0) return m + "m " + s + "s"
        return s + "s"
    }
    function fmtDate(ts) {
        return ts && ts.indexOf(" ") >= 0 ? ts.split(" ")[0] : (ts || "")
    }
    // Group checks by category → [{category, pass, fail, warn}]
    function categoryStats(checks) {
        var out = []
        var seen = {}
        for (var i = 0; i < checks.length; ++i) {
            var c = checks[i]
            var cat = c.category && c.category.length > 0 ? c.category : "General"
            if (!seen[cat]) {
                seen[cat] = out.length
                out.push({category: cat, pass: 0, fail: 0, warn: 0, rows: []})
            }
            var stat = out[seen[cat]]
            var s = String(c.status).toUpperCase()
            if (s.indexOf("PASS") === 0) stat.pass++
            else if (s.indexOf("FAIL") === 0) stat.fail++
            else stat.warn++
            stat.rows.push(c)
        }
        return out
    }
    function checkStatusColor(s) {
        var up = String(s).toUpperCase()
        if (up.indexOf("PASS") === 0) return Colors.statePass
        if (up.indexOf("FAIL") === 0) return Colors.stateFail
        if (up.indexOf("WARN") === 0) return Colors.checkWarn
        return Colors.textSecondary
    }
    function eventColor(evt) {
        if (evt === "ARM") return Colors.statePass
        if (evt === "DISARM") return Colors.info
        if (evt === "MODE_CHANGE") return Colors.accent
        if (evt === "BATTERY_WARN") return Colors.checkWarn
        if (evt === "BATTERY_CRIT" || evt === "CHECK_DEGRADED") return Colors.stateFail
        return Colors.textSecondary
    }
    function modeBadgeColor(m) {
        if (m === "FLIGHT") return "#1A3A2A"
        if (m === "TRAINING") return "#1A2A4A"
        return "#2A2A1A"
    }
    function modeTextColor(m) {
        if (m === "FLIGHT") return Colors.statePass
        if (m === "TRAINING") return Colors.accent
        return Colors.checkWarn
    }

    function rebuildVehicleFilter() {
        vehicleFilterModel.clear()
        vehicleFilterModel.append({ id: -1, name: qsTr("All Vehicles") })
        var arr = JSON.parse(Database.getAllVehiclesJson())
        for (var i = 0; i < arr.length; ++i) {
            vehicleFilterModel.append({ id: arr[i].sysid,
                                        name: (arr[i].friendlyName ||
                                               (qsTr("Vehicle ") + arr[i].sysid)) })
        }
        if (typeof vehiclePicker !== "undefined" && vehiclePicker !== null) {
            var vid = root.historyModel.filterVehicleId
            for (var j = 0; j < vehicleFilterModel.count; ++j) {
                if (vehicleFilterModel.get(j).id === vid) {
                    vehiclePicker.currentIndex = j
                    return
                }
            }
            vehiclePicker.currentIndex = 0
        }
    }

    function loadDetail(flightId) {
        _selectedFlightId = flightId
        _detail = historyModel.getFlightDetail(flightId)
    }

    function exportFeedback(path) {
        if (path && path.length > 0) {
            _toastMsg = qsTr("Export written to:\n%1").arg(path)
            _toastColor = Colors.statePass
        } else {
            _toastMsg = qsTr("Export failed")
            _toastColor = Colors.stateFail
        }
        statusToast.opacity = 1.0
        statusTimer.restart()
    }

    property string _toastMsg: ""
    property color _toastColor: Colors.statePass

    Component.onCompleted: {
        rebuildVehicleFilter()
        var vid = VehicleRegistry.currentVehicleId
        if (vid > 0)
            historyModel.setFilterVehicleId(vid)
        else
            historyModel.reload()
    }

    ListModel {
        id: vehicleFilterModel
    }

    Timer {
        id: statusTimer
        interval: 6000
        onTriggered: statusToast.opacity = 0.0
    }

    pageComponent: Component {
        ColumnLayout {
            width: root.availableWidth
            height: root.availableHeight
            spacing: 0

            // ── Stats banner ─────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                implicitHeight: 60
                color: Colors.surface2

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Config.spacingLarge
                    anchors.rightMargin: Config.spacingLarge
                    spacing: Config.spacingXLarge

                    StatBox { label: qsTr("Total Flights"); value: root.historyModel.stats.totalFlights }
                    StatBox { label: qsTr("Total Hours");   value: root.historyModel.stats.totalHoursStr }
                    StatBox { label: qsTr("Avg Pass Rate"); value: root.historyModel.stats.avgPassRate + "%" }
                    StatBox {
                        label: qsTr("Anomaly Rate")
                        value: root.historyModel.stats.anomalyRate + "%"
                        valueColor: root.historyModel.stats.anomalyRate > 10
                                    ? Colors.checkWarn : Colors.statePass
                    }
                    StatBox { label: qsTr("Vehicles");  value: root.historyModel.stats.vehicleCount }
                    StatBox { label: qsTr("Operators"); value: root.historyModel.stats.operatorCount }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: qsTr("stats reflect current filter")
                        color: Colors.textSecondary
                        font.pixelSize: Config.fontSizeSmall
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // ── Filter bar (sticky, above the list) ──────────────────
            Rectangle {
                id: filterBar
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                implicitHeight: 56
                color: Colors.surface2

                RowLayout {
                    anchors { fill: parent; margins: Config.spacingMedium }
                    spacing: 8

                    Label {
                        text: qsTr("From")
                        color: Colors.textSecondary
                        font.pixelSize: Config.fontSizeSmall
                    }
                    TextField {
                        id: fromField
                        Layout.preferredWidth: 110
                        placeholderText: "YYYY-MM-DD"
                        color: Colors.textPrimary
                        background: Rectangle {
                            color: Colors.surfaceLight
                            radius: Config.radiusSmall
                            border.color: Colors.divider
                        }
                        onTextChanged: root.historyModel.setFilterFromDate(text.trim())
                    }
                    Label {
                        text: qsTr("To")
                        color: Colors.textSecondary
                        font.pixelSize: Config.fontSizeSmall
                    }
                    TextField {
                        id: toField
                        Layout.preferredWidth: 110
                        placeholderText: "YYYY-MM-DD"
                        color: Colors.textPrimary
                        background: Rectangle {
                            color: Colors.surfaceLight
                            radius: Config.radiusSmall
                            border.color: Colors.divider
                        }
                        onTextChanged: root.historyModel.setFilterToDate(text.trim())
                    }

                    ComboBox {
                        id: vehiclePicker
                        Layout.preferredWidth: 150
                        model: vehicleFilterModel
                        textRole: "name"
                        onCurrentIndexChanged: {
                            if (currentIndex >= 0) {
                                var item = vehicleFilterModel.get(currentIndex)
                                var targetId = (item && item.id !== undefined) ? item.id : -1
                                if (root.historyModel.filterVehicleId !== targetId) {
                                    root.historyModel.setFilterVehicleId(targetId)
                                }
                            }
                        }

                        // Listen to changes on the filter to update picker index
                        Connections {
                            target: root.historyModel
                            function onFilterVehicleIdChanged() {
                                var vid = root.historyModel.filterVehicleId
                                for (var i = 0; i < vehicleFilterModel.count; ++i) {
                                    if (vehicleFilterModel.get(i).id === vid) {
                                        vehiclePicker.currentIndex = i
                                        return
                                    }
                                }
                                vehiclePicker.currentIndex = 0
                            }
                        }

                        Component.onCompleted: {
                            var vid = root.historyModel.filterVehicleId
                            for (var i = 0; i < vehicleFilterModel.count; ++i) {
                                if (vehicleFilterModel.get(i).id === vid) {
                                    currentIndex = i
                                    return
                                }
                            }
                            currentIndex = 0
                        }
                    }

                    ComboBox {
                        id: modePicker
                        Layout.preferredWidth: 120
                        model: [qsTr("All Modes"), "FLIGHT", "TRAINING", "TESTING"]
                        onCurrentIndexChanged:
                            root.historyModel.setFilterMode(currentIndex === 0 ? "" : currentText)
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Search purpose, location, operator...")
                        color: Colors.textPrimary
                        background: Rectangle {
                            color: Colors.surfaceLight
                            radius: Config.radiusSmall
                            border.color: Colors.divider
                        }
                        onTextChanged: root.historyModel.setFilterSearch(text.trim())
                    }

                    ComboBox {
                        id: sortPicker
                        Layout.preferredWidth: 150
                        model: [qsTr("Newest First"), qsTr("Oldest First"),
                                qsTr("Longest First"), qsTr("By Operator"),
                                qsTr("By Vehicle"), qsTr("By Pass Rate")]
                        property var sorts: ["date_desc", "date_asc", "duration_desc",
                                             "operator", "vehicle", "pass_rate_desc"]
                        onCurrentIndexChanged:
                            root.historyModel.setSortBy(sorts[currentIndex])
                    }

                    Button {
                        text: qsTr("↻")
                        flat: true
                        ToolTip.text: qsTr("Refresh")
                        ToolTip.visible: hovered
                        onClicked: root.historyModel.reload()
                    }

                    Button {
                        text: qsTr("⬇ Export CSV")
                        flat: true
                        onClicked: exportMenu.open()
                    }

                    Menu {
                        id: exportMenu
                        MenuItem {
                            text: qsTr("Export current view (with filters)")
                            onTriggered: {
                                var path = Database.exportFlightsCsv(
                                    fromField.text.trim(), toField.text.trim(),
                                    vehiclePicker.currentIndex >= 0
                                        ? vehicleFilterModel.get(vehiclePicker.currentIndex).id
                                        : -1,
                                    root.historyModel.filterMode)
                                root.exportFeedback(path)
                            }
                        }
                        MenuItem {
                            text: qsTr("Export detailed log for selected flight")
                            enabled: root._selectedFlightId > 0
                            onTriggered: {
                                var path = Database.exportFlightDetailCsv(root._selectedFlightId)
                                root.exportFeedback(path)
                            }
                        }
                        MenuItem {
                            text: qsTr("Export all flights (no filter)")
                            onTriggered: {
                                var path = Database.exportFlightsCsv("", "", -1, "")
                                root.exportFeedback(path)
                            }
                        }
                    }

                    Text {
                        id: statusToast
                        font.pixelSize: Config.fontSizeSmall
                        font.bold: true
                        color: root._toastColor
                        opacity: 0.0
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.maximumWidth: 420
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ── Main: list (35%) + detail (65%) ──────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // ── Left: flight list ────────────────────────────────
                Rectangle {
                    Layout.preferredWidth: parent.width * 0.35
                    Layout.fillHeight: true
                    color: Colors.surface

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Config.spacingMedium
                        spacing: Config.spacingSmall

                        Text {
                            text: qsTr("Flights")
                            font.pixelSize: Config.fontSizeH3
                            font.bold: true
                            color: Colors.textPrimary
                        }

                        // Result count + pagination
                        Row {
                            spacing: 16
                            Label {
                                text: root.historyModel.totalCount + " " + qsTr("flights")
                                      + (root.historyModel.totalCount !== root.historyModel.rowCount()
                                         ? " (" + qsTr("showing") + " " + root.historyModel.rowCount() + ")"
                                         : "")
                                color: Colors.textSecondary
                                font.pixelSize: Config.fontSizeSmall
                            }

                            Row {
                                visible: root.historyModel.totalCount > root.historyModel.pageSize
                                spacing: 4
                                Button {
                                    text: qsTr("‹")
                                    flat: true
                                    onClicked: root.historyModel.prevPage()
                                    enabled: root.historyModel.currentPage > 0
                                }
                                Label {
                                    text: qsTr("Page") + " " + (root.historyModel.currentPage + 1)
                                          + " " + qsTr("of") + " " + Math.ceil(root.historyModel.totalCount / root.historyModel.pageSize)
                                    color: Colors.textSecondary
                                    font.pixelSize: Config.fontSizeSmall
                                }
                                Button {
                                    text: qsTr("›")
                                    flat: true
                                    onClicked: root.historyModel.nextPage()
                                    enabled: (root.historyModel.currentPage + 1) * root.historyModel.pageSize
                                              < root.historyModel.totalCount
                                }
                            }

                            Item { width: 100 }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Colors.divider
                        }

                        ListView {
                            id: flightList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: root.historyModel
                            clip: true
                            spacing: 4

                            delegate: Rectangle {
                                required property int index
                                required property int flightId
                                required property string date
                                required property string operatorName
                                required property string vehicleName
                                required property string mode
                                required property string purpose
                                required property string location
                                required property string durationStr
                                required property int prePassRate
                                required property int anomalyCount

                                width: flightList.width
                                height: 72
                                radius: Config.radiusSmall
                                color: index % 2 === 0 ? Colors.surface2 : Colors.surface
                                border.color: (root._selectedFlightId === flightId) ? Colors.accent : Colors.divider
                                border.width: 1

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8

                                    // Index number
                                    Text {
                                        text: (root.historyModel.currentPage * root.historyModel.pageSize + index + 1) + "."
                                        color: Colors.textSecondary
                                        font.pixelSize: Config.fontSizeSmall
                                        width: 32
                                    }

                                    // Mode badge
                                    Rectangle {
                                        width: 70
                                        height: 22
                                        radius: 4
                                        color: root.modeBadgeColor(mode)
                                        Text {
                                            anchors.centerIn: parent
                                            text: mode
                                            color: root.modeTextColor(mode)
                                            font.pixelSize: Config.fontSizeSmall
                                            font.bold: true
                                        }
                                    }

                                    // Core info
                                    Column {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            text: vehicleName + "  ·  " + operatorName
                                            font.pixelSize: Config.fontSizeBody
                                            font.bold: true
                                            color: Colors.textPrimary
                                            elide: Text.ElideRight
                                            width: parent.width
                                        }
                                        Text {
                                            text: (purpose || qsTr("No purpose"))
                                                  + (location && location.length > 0 ? "  ·  " + location : "")
                                            color: Colors.textSecondary
                                            font.pixelSize: Config.fontSizeSmall
                                            elide: Text.ElideRight
                                            width: parent.width
                                        }
                                    }

                                    // Stats
                                    Column {
                                        spacing: 2
                                        Text {
                                            text: durationStr
                                            font.pixelSize: Config.fontSizeBody
                                            color: Colors.textPrimary
                                            anchors.right: parent.right
                                        }
                                        Text {
                                            text: prePassRate + "% pre"
                                            color: prePassRate >= 90 ? Colors.statePass : Colors.checkWarn
                                            font.pixelSize: Config.fontSizeSmall
                                            anchors.right: parent.right
                                        }
                                    }

                                    // Anomaly indicator
                                    Rectangle {
                                        visible: anomalyCount > 0
                                        width: 24
                                        height: 24
                                        radius: 12
                                        color: Colors.stateFail
                                        Text {
                                            anchors.centerIn: parent
                                            text: anomalyCount
                                            color: "white"
                                            font.pixelSize: Config.fontSizeSmall
                                            font.bold: true
                                        }
                                        ToolTip.text: anomalyCount + " " + qsTr("in-flight anomaly(s)")
                                        ToolTip.visible: hover.hovered
                                        HoverHandler {
                                            id: hover
                                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                        }
                                    }

                                    // Date
                                    Text {
                                        text: root.fmtDate(date)
                                        color: Colors.textSecondary
                                        font.pixelSize: Config.fontSizeSmall
                                        width: 90
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.loadDetail(flightId)
                                }
                            }
                        }
                    }

                    // ── Empty state ──────────────────────────────────
                    Item {
                        anchors.fill: parent
                        visible: root.historyModel.totalCount === 0

                        Column {
                            anchors.centerIn: parent
                            spacing: 12

                            Text {
                                text: qsTr("📋")
                                font.pixelSize: 48
                                anchors.horizontalCenter: parent.horizontalCenter
                                color: Colors.textSecondary
                            }
                            Text {
                                text: (root.historyModel.filterFromDate.length > 0
                                       || root.historyModel.filterVehicleId > 0)
                                      ? qsTr("No flights match the current filter")
                                      : qsTr("No flights recorded yet")
                                color: Colors.textSecondary
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Button {
                                visible: root.historyModel.filterFromDate.length > 0
                                         || root.historyModel.filterVehicleId > 0
                                text: qsTr("Clear Filters")
                                anchors.horizontalCenter: parent.horizontalCenter
                                onClicked: {
                                    fromField.text = ""
                                    toField.text = ""
                                    vehiclePicker.currentIndex = 0
                                }
                            }
                        }
                    }
                }

                // ── Right: detail panel ──────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Colors.surfaceLight

                    Item {
                        anchors.fill: parent
                        anchors.margins: Config.spacingLarge

                        Text {
                            anchors.centerIn: parent
                            visible: root._selectedFlightId < 0
                            text: qsTr("Select a flight from the list")
                            font.pixelSize: Config.fontSizeBody
                            color: Colors.textSecondary
                        }

                        ScrollView {
                            id: detailScrollView
                            visible: root._selectedFlightId >= 0
                            anchors.fill: parent
                            clip: true
                            contentWidth: availableWidth

                            ColumnLayout {
                                width: detailScrollView.availableWidth
                                spacing: Config.spacingMedium

                                // ── Header ────────────────────────────
                                Column {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text {
                                        text: qsTr("Flight #%1  ·  %2  ·  %3")
                                                  .arg(root._flight.id).arg(root._flight.vehicle_name || "")
                                                  .arg(root.fmtDate(root._flight.started_at))
                                        font.pixelSize: Config.fontSizeH1
                                        font.bold: true
                                        color: Colors.textPrimary
                                    }
                                    Text {
                                        text: [(root._flight.operator_name || ""),
                                               (root._flight.purpose || ""),
                                               (root._flight.location || "")]
                                              .filter(function (v) { return v.length > 0 })
                                              .join("  ·  ")
                                        font.pixelSize: Config.fontSizeBody
                                        color: Colors.textSecondary
                                    }
                                    Text {
                                        text: qsTr("Duration: %1  ·  Max Alt: %2 m  ·  Min Batt: %3 V")
                                                  .arg(root.fmtDuration(root._flight.duration_sec))
                                                  .arg(root._flight.max_altitude_m >= 0 ? root._flight.max_altitude_m.toFixed(0) : "—")
                                                  .arg(root._flight.min_battery_v >= 0 ? root._flight.min_battery_v.toFixed(1) : "—")
                                        font.pixelSize: Config.fontSizeBody
                                        color: Colors.textMuted
                                    }
                                    Text {
                                        text: qsTr("Flight window: %1 → %2")
                                                  .arg(root.fmtTime(root._flight.started_at))
                                                  .arg(root.fmtTime(root._flight.ended_at || root._flight.disarmed_at))
                                        font.pixelSize: Config.fontSizeBody
                                        color: Colors.textMuted
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 1
                                    color: Colors.divider
                                }

                                // ── Pre-flight checks ─────────────────
                                CollapsibleSection {
                                    headerText: qsTr("PRE-FLIGHT CHECKS")
                                    summaryText: root.summaryLine(root._preChecks)
                                    expanded: false
                                    onToggled: expanded = !expanded

                                    Column {
                                        width: parent.width
                                        spacing: 6
                                        Repeater {
                                            model: root.categoryStats(root._preChecks)
                                            delegate: Item {
                                                required property var modelData
                                                width: parent.width
                                                height: 26
                                                implicitHeight: 26
                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 12
                                                    Text {
                                                        text: modelData.category
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: true
                                                        color: Colors.textPrimary
                                                        Layout.preferredWidth: 220
                                                    }
                                                    Text {
                                                        text: "<font color='%1'>✓ %2</font>".arg(Colors.statePass).arg(modelData.pass)
                                                              + "   <font color='%1'>✗ %3</font>".arg(Colors.stateFail).arg(modelData.fail)
                                                              + "   <font color='%1'>⚠ %4</font>".arg(Colors.checkWarn).arg(modelData.warn)
                                                        font.pixelSize: Config.fontSizeBody
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }
                                        // Detailed rows
                                        Repeater {
                                            model: root._preChecks
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: parent.width
                                                height: 28
                                                implicitHeight: 28
                                                color: "transparent"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 12
                                                    Text {
                                                        text: modelData.check_id
                                                        font.pixelSize: Config.fontSizeBody
                                                        color: Colors.textSecondary
                                                        Layout.preferredWidth: 220
                                                        elide: Text.ElideRight
                                                    }
                                                    Text {
                                                        text: modelData.status
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: true
                                                        color: root.checkStatusColor(modelData.status)
                                                        Layout.preferredWidth: 90
                                                    }
                                                    Text {
                                                        text: (modelData.message || "")
                                                        font.pixelSize: Config.fontSizeBody
                                                        color: Colors.textMuted
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // ── Flight events ─────────────────────
                                CollapsibleSection {
                                    headerText: qsTr("FLIGHT EVENTS")
                                    summaryText: root._events.length + " " + qsTr("events")
                                    expanded: false
                                    onToggled: expanded = !expanded

                                    Column {
                                        width: parent.width
                                        spacing: 6
                                        Repeater {
                                            model: root._events
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: parent.width
                                                height: 28
                                                implicitHeight: 28
                                                color: "transparent"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 12
                                                    Text {
                                                        text: root.fmtTime(modelData.timestamp)
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.family: "monospace"
                                                        color: Colors.textSecondary
                                                        Layout.preferredWidth: 90
                                                    }
                                                    Text {
                                                        text: modelData.event_type
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: true
                                                        color: root.eventColor(modelData.event_type)
                                                        Layout.preferredWidth: 160
                                                    }
                                                    Text {
                                                        text: [modelData.flight_mode && modelData.flight_mode.length > 0
                                                               ? "→ " + modelData.flight_mode : "",
                                                               modelData.battery_v > 0
                                                               ? "Batt: " + modelData.battery_v.toFixed(1) + "V" : "",
                                                               modelData.altitude_m > 0
                                                               ? "Alt: " + modelData.altitude_m.toFixed(0) + "m" : "",
                                                               modelData.gps_sats > 0
                                                               ? "GPS: " + modelData.gps_sats : ""]
                                                              .filter(function (v) { return v.length > 0 })
                                                              .join("  ·  ")
                                                        font.pixelSize: Config.fontSizeBody
                                                        color: Colors.textPrimary
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }
                                     }
                                 }

                                // ── Post-flight checks ────────────────
                                CollapsibleSection {
                                    headerText: qsTr("POST-FLIGHT CHECKS")
                                    summaryText: root.summaryLine(root._postChecks)
                                    expanded: false
                                    onToggled: expanded = !expanded

                                    Column {
                                        width: parent.width
                                        spacing: 6
                                        Repeater {
                                            model: root._postChecks
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: parent.width
                                                height: 28
                                                implicitHeight: 28
                                                color: "transparent"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 12
                                                    Text {
                                                        text: modelData.check_id
                                                        font.pixelSize: Config.fontSizeBody
                                                        color: Colors.textSecondary
                                                        Layout.preferredWidth: 220
                                                        elide: Text.ElideRight
                                                    }
                                                    Text {
                                                        text: modelData.status
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: true
                                                        color: root.checkStatusColor(modelData.status)
                                                        Layout.preferredWidth: 90
                                                    }
                                                    Text {
                                                        text: (modelData.message || "")
                                                        font.pixelSize: Config.fontSizeBody
                                                        color: Colors.textMuted
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // ── Motor tests ───────────────────────
                                CollapsibleSection {
                                    headerText: qsTr("MOTOR TESTS")
                                    summaryText: root._motorTests.length + " " + qsTr("tests")
                                    expanded: false
                                    onToggled: expanded = !expanded

                                    Column {
                                        width: parent.width
                                        spacing: 0
                                        Repeater {
                                            model: root._motorTests
                                            delegate: Rectangle {
                                                required property var modelData
                                                property int rowIndex: index
                                                width: parent.width
                                                height: 40
                                                implicitHeight: 40
                                                color: rowIndex % 2 === 0 ? Colors.surface2 : "transparent"
                                                radius: Config.radiusSmall
                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: Config.spacingMedium
                                                    anchors.rightMargin: Config.spacingMedium
                                                    spacing: 16

                                                    // Motor label badge
                                                    Rectangle {
                                                        width: 48
                                                        height: 26
                                                        radius: Config.radiusSmall
                                                        color: Colors.surfaceLight
                                                        Text {
                                                            anchors.centerIn: parent
                                                            text: "M" + modelData.motor_index
                                                            font.pixelSize: Config.fontSizeBody
                                                            font.bold: true
                                                            color: Colors.textPrimary
                                                        }
                                                    }

                                                    // Layout for short result + PWM/throttle details
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 16
                                                        visible: modelData.result.length <= 8

                                                        // Result badge
                                                        Rectangle {
                                                            width: 72
                                                            height: 26
                                                            radius: Config.radiusSmall
                                                            color: Qt.rgba(root.checkStatusColor(modelData.result).r,
                                                                           root.checkStatusColor(modelData.result).g,
                                                                           root.checkStatusColor(modelData.result).b,
                                                                           0.2)
                                                            Text {
                                                                anchors.centerIn: parent
                                                                text: modelData.result
                                                                font.pixelSize: Config.fontSizeBody
                                                                font.bold: true
                                                                color: root.checkStatusColor(modelData.result)
                                                            }
                                                        }

                                                        // PWM / throttle details
                                                        Text {
                                                            text: (modelData.actual_pwm > 0
                                                                  ? modelData.actual_pwm + "µs" : "—")
                                                                  + (modelData.throttle_pct > 0
                                                                     ? "  @  " + modelData.throttle_pct + "%" : "")
                                                                  + (modelData.pwm_delta !== 0
                                                                     ? "  Δ" + modelData.pwm_delta : "")
                                                            font.pixelSize: Config.fontSizeBody
                                                            color: Colors.textSecondary
                                                            Layout.fillWidth: true
                                                        }
                                                    }

                                                    // Layout for long status/error message
                                                    Text {
                                                        visible: modelData.result.length > 8
                                                        text: modelData.result
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: true
                                                        color: root.checkStatusColor(modelData.result)
                                                        Layout.fillWidth: true
                                                        elide: Text.ElideRight
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // ── Surface tests ─────────────────────
                                CollapsibleSection {
                                    headerText: qsTr("SURFACE TESTS")
                                    summaryText: root._surfaceTests.length + " " + qsTr("tests")
                                    expanded: false
                                    onToggled: expanded = !expanded

                                    Column {
                                        width: parent.width
                                        spacing: 6
                                        Repeater {
                                            model: root._surfaceTests
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: parent.width
                                                height: 28
                                                implicitHeight: 28
                                                color: "transparent"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 12
                                                    Text {
                                                        text: modelData.surface_id
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: true
                                                        color: Colors.textPrimary
                                                        Layout.preferredWidth: 180
                                                    }
                                                    Text {
                                                        text: modelData.result
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: true
                                                        color: root.checkStatusColor(modelData.result)
                                                        Layout.preferredWidth: 90
                                                    }
                                                    Text {
                                                        text: (modelData.min_pwm_actual || modelData.max_pwm_actual)
                                                              ? (modelData.min_pwm_actual + "–" + modelData.max_pwm_actual + "µs") : ""
                                                        font.pixelSize: Config.fontSizeBody
                                                        color: Colors.textSecondary
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // ── Zone compliance ───────────────────
                                CollapsibleSection {
                                    headerText: qsTr("ZONE COMPLIANCE")
                                    summaryText: root._compliance.length + " " + qsTr("zone checks")
                                    expanded: false
                                    onToggled: expanded = !expanded

                                    Column {
                                        width: parent.width
                                        spacing: 6
                                        Repeater {
                                            model: root._compliance
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: parent.width
                                                height: 28
                                                implicitHeight: 28
                                                color: "transparent"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 12
                                                    Text {
                                                        text: root.fmtDate(modelData.checked_at) + " " + root.fmtTime(modelData.checked_at)
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.family: "monospace"
                                                        color: Colors.textSecondary
                                                        Layout.preferredWidth: 160
                                                    }
                                                    Text {
                                                        text: modelData.result
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: true
                                                        color: modelData.result === "Clear" ? Colors.statePass : Colors.checkWarn
                                                        Layout.preferredWidth: 90
                                                    }
                                                    Text {
                                                        text: (modelData.notes || "")
                                                        font.pixelSize: Config.fontSizeBody
                                                        color: Colors.textMuted
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // ── Handovers (training only) ─────────
                                CollapsibleSection {
                                    headerText: qsTr("TRAINER HANDOVERS")
                                    summaryText: root._handovers.length + " " + qsTr("handover(s)")
                                    expanded: true
                                    visible: root._handovers.length > 0
                                    onToggled: expanded = !expanded

                                    Column {
                                        width: parent.width
                                        spacing: 6
                                        Repeater {
                                            model: root._handovers
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: parent.width
                                                height: 28
                                                implicitHeight: 28
                                                color: "transparent"
                                                RowLayout {
                                                    anchors.fill: parent
                                                    spacing: 12
                                                    Text {
                                                        text: root.fmtTime(modelData.timestamp)
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.family: "monospace"
                                                        color: Colors.textSecondary
                                                        Layout.preferredWidth: 90
                                                    }
                                                    Text {
                                                        text: modelData.event_type
                                                        font.pixelSize: Config.fontSizeBody
                                                        font.bold: true
                                                        color: Colors.accent
                                                        Layout.preferredWidth: 160
                                                    }
                                                    Text {
                                                        text: (modelData.triggered_by || "")
                                                        font.pixelSize: Config.fontSizeBody
                                                        color: Colors.textMuted
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}