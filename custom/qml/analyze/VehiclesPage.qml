import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl.Controls
import com.uav.preflight 1.0

AnalyzePage {
    id:                 root
    pageName:           qsTr("Vehicles")
    pageDescription:    qsTr("Previously connected vehicles")

    property int _selectedIndex: -1
    property string _statusText: ""

    // 20% larger text across the whole page
    property real fontScale: 1.2

    function fs(base) { return Math.round(base * fontScale) }

    // Compact "Xh Ym / m / s" string from cumulative flight-time seconds.
    // Total flight seconds → "Xh Ym" (falls back to seconds below a minute)
    function fmtFlightTime(sec) {
        var s = Number(sec || 0)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        if (h > 0) return h + "h " + m + "m"
        if (m > 0) return m + "m"
        return s + "s"
    }

    // ISO-8601 "2026-09-20T12:34:56Z" → "2026-09-20 12:34" (drops seconds/tz).
    // DB stores ISO timestamps — swap the "T" and cut to minute precision
    function fmtLastSeen(value) {
        if (!value) return "never"
        var d = value.replace("T", " ").slice(0, 16)
        return d
    }

    pageComponent: Component {
        ColumnLayout {
            width:  root.availableWidth
            height: root.availableHeight
            spacing: 8

            // VehicleListModel is the DB-backed single source; reload applies the date filter.
            // Reloads the C++ list model; empty date strings mean "no bound" in the SQL filter
            function refresh() {
                VehicleListModel.reload(_fromDateField.text.trim(), _toDateField.text.trim())
            }

            Component.onCompleted: refresh()

            // "Vehicles": the registry below. "Power types": copter power
            // systems (frame + props + cells) and what they learn from each
            // flight's charger number (POWER_TYPES_PLAN.md).
            TabBar {
                id: _tabs
                objectName: "vehiclesTabs"
                Layout.fillWidth: true
                TabButton { text: qsTr("Vehicles") }
                TabButton { objectName: "powerTypesTab"; text: qsTr("Power types") }
            }

            RowLayout {
                visible: _tabs.currentIndex === 0
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: VehicleListModel.count + " vehicle(s)"
                    font.pixelSize: fs(14)
                    color: Colors.textSecondary
                }

                Item { Layout.fillWidth: true }

                TextField {
                    id: _fromDateField
                    Layout.preferredWidth: 130
                    placeholderText: "From (YYYY-MM-DD)"
                    color: Colors.textPrimary
                    background: Rectangle { color: Colors.surface; radius: 4; border.color: Colors.divider }
                }

                TextField {
                    id: _toDateField
                    Layout.preferredWidth: 130
                    placeholderText: "To (YYYY-MM-DD)"
                    color: Colors.textPrimary
                    background: Rectangle { color: Colors.surface; radius: 4; border.color: Colors.divider }
                }

                Button {
                    text: qsTr("Apply")
                    onClicked: refresh()
                    flat: true
                }

                Button {
                    text: qsTr("↻")
                    onClicked: refresh()
                    flat: true
                }

                Button {
                    text: qsTr("Export CSV")
                    onClicked: {
                        // C++ Database writes the CSV and returns the saved path (empty on failure)
                        var path = Database.exportVehiclesCsv(_fromDateField.text.trim(),
                                                              _toDateField.text.trim())
                        _statusText = path.length > 0 ? qsTr("Saved: %1").arg(path) : qsTr("Export failed")
                        exportStatus.visible = true
                        exportTimer.restart()
                    }
                }
            }

            Text {
                id: exportStatus
                visible: false
                Layout.fillWidth: true
                text: _statusText
                wrapMode: Text.Wrap
                font.pixelSize: fs(11)
                color: Colors.success

                Timer {
                    id: exportTimer
                    // Auto-hide the CSV save confirmation after 6 s.
                    interval: 6000
                    onTriggered: exportStatus.visible = false
                }

                // Stop the pending export-status timer on teardown.
                Component.onDestruction: exportTimer.stop()
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Colors.divider
            }

            // ── Two-pane layout: vehicle list (left) + detail panel (right) ──
            RowLayout {
                visible: _tabs.currentIndex === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: root.availableWidth * 0.45
                    Layout.fillHeight: true
                    color: Colors.surface
                    radius: 8
                    border.color: Colors.divider
                    border.width: 1

                    ListView {
                        id: listView
                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        spacing: 4
                        model: VehicleListModel

                        delegate: Rectangle {
                            required property int index
                            required property string friendlyName
                            required property string vehicleTypeName
                            required property string autopilotType
                            required property string firmwareVersion
                            required property string fingerprintSource
                            required property int sysid
                            required property int totalFlightCount
                            required property int totalFlightTimeSec
                            required property string lastSeen
                            required property string fingerprint

                            width: listView.width
                            height: 72
                            radius: 6
                            color: (index === root._selectedIndex) ? Colors.surfaceLight : Colors.bgPrimary
                            border.color: (index === root._selectedIndex) ? Colors.skywinAccent : Colors.divider
                            border.width: (index === root._selectedIndex) ? 1 : 1

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root._selectedIndex = (root._selectedIndex === index) ? -1 : index
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Text {
                                            text: friendlyName || "unnamed"
                                            font.pixelSize: fs(14)
                                            font.bold: true
                                            color: Colors.textPrimary
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: "sysid " + sysid
                                            font.pixelSize: fs(10)
                                            color: Colors.textMuted
                                        }

                                        Text {
                                            // SYSID_TYPE_FALLBACK = fingerprint without a hardware UID.
                                            // SYSID_TYPE_FALLBACK = no hardware UID reported (weaker identity)
                                            text: fingerprintSource === "SYSID_TYPE_FALLBACK" ? "⚠ weak ID" : "✓ hardware ID"
                                            font.pixelSize: fs(10)
                                            color: fingerprintSource === "SYSID_TYPE_FALLBACK" ? Colors.warning : Colors.success
                                        }
                                    }

                                    Text {
                                        text: [vehicleTypeName || "Unknown Device",
                                               autopilotType,
                                               firmwareVersion ? "v" + firmwareVersion : ""].join(" · ")
                                        font.pixelSize: fs(12)
                                        color: Colors.textSecondary
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: "Flights: " + totalFlightCount + " · Time: " + root.fmtFlightTime(totalFlightTimeSec)
                                              + "  ·  Last seen: " + root.fmtLastSeen(lastSeen)
                                        font.pixelSize: fs(11)
                                        color: Colors.textMuted
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Detail panel ────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Colors.surface
                    radius: 8
                    border.color: Colors.divider
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Text {
                            text: qsTr("Vehicle Details")
                            font.pixelSize: fs(15)
                            font.bold: true
                            color: Colors.textPrimary
                        }

                        Text {
                            text: root._selectedIndex >= 0
                                  ? qsTr("Selected vehicle")
                                  : qsTr("Select a vehicle from the list to view its full details")
                            font.pixelSize: fs(12)
                            color: Colors.textSecondary
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            visible: root._selectedIndex >= 0
                            color: Colors.divider
                        }

                        // Detail block loads on demand once a row is selected
                        Loader {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            sourceComponent: root._selectedIndex >= 0 ? detailBlock : undefined
                            visible: root._selectedIndex >= 0
                        }
                    }
                }
            }

            // ── Power types tab ───────────────────────────────────────────
            // Left: the types (and the fleet's take-off/landing minutes).
            // Right: the selected type, what it has learned, the connected
            // copter's BATT_AMP_PERVLT suggestion, and its flights, where the
            // charger number is entered and each flight is used or dismissed.
            RowLayout {
                id: _ptPane
                visible: _tabs.currentIndex === 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                readonly property var _vpm: (typeof VehicleProfileManager !== "undefined") ? VehicleProfileManager : null
                property var types: []
                property int selId: -1          // -1 = none, 0 = new type
                property var sel: ({})
                property var summary: ({})
                property var flights: []
                property var ampSuggestion: ({})
                property string message: ""

                function reload() {
                    if (!_vpm) return
                    types = _vpm.listPowerTypes()
                    sel = ({})
                    for (var i = 0; i < types.length; ++i)
                        if (types[i].id === selId) sel = types[i]
                    if (selId > 0 && sel.id === undefined) selId = -1
                    summary = selId > 0 ? _vpm.powerTypeSummary(selId) : ({})
                    var rows = selId > 0 ? _vpm.powerTypeFlights(selId) : []
                    flights = rows.slice().reverse()      // newest first
                    ampSuggestion = (selId > 0 && selId === _vpm.currentPowerTypeId) ? _vpm.ampPerVoltSuggestion() : ({})
                    takeoffField.text = _vpm.takeoffMin
                    landField.text = _vpm.landMin
                }
                function select(id) {
                    selId = id
                    message = ""
                    reload()
                    var t = sel
                    ptFrameField.text    = id > 0 ? t.frame : (_vpm ? _vpm.currentFrameName() : "")
                    ptDiameterField.text = id > 0 ? t.propDiameterIn : ""
                    ptPitchField.text    = id > 0 ? t.propPitchIn : ""
                    ptCellsField.text    = id > 0 ? t.cells : ""
                    ptNoteField.text     = id > 0 ? t.motorNote : ""
                }
                function save() {
                    var d = Number(ptDiameterField.text.replace(",", "."))
                    var pt = Number(ptPitchField.text.replace(",", "."))
                    var c = parseInt(ptCellsField.text)
                    var id = _vpm ? _vpm.savePowerType(selId > 0 ? selId : 0, ptFrameField.text, d, pt, c, ptNoteField.text) : -1
                    if (id > 0) {
                        message = qsTr("Saved.")
                        select(id)
                    } else {
                        message = qsTr("Not saved: frame, prop diameter and pitch (inches) and cells (1–24) are needed, "
                                       + "and no other type may have the same frame, props and cells.")
                    }
                }
                function statusText(s) {
                    if (!s || s.status === undefined) return ""
                    var names = { "none": qsTr("No model yet"), "tooAlike": qsTr("Flights too alike"),
                                  "payloadNotModelled": qsTr("Payload not modelled"),
                                  "payloadProportional": qsTr("Payload assumed proportional"),
                                  "full": qsTr("Fully learned") }
                    return names[s.status] || s.status
                }
                function num(v, d) { return (v === undefined || v === null || v < 0) ? "--" : Number(v).toFixed(d) }

                Component.onCompleted: reload()
                Connections {
                    target: _ptPane._vpm
                    function onPowerTypeChanged() { _ptPane.reload() }
                }

                // ── Left: the list ────────────────────────────────────────
                Rectangle {
                    Layout.preferredWidth: root.availableWidth * 0.34
                    Layout.fillHeight: true
                    color: Colors.surface
                    radius: 8
                    border.color: Colors.divider
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Power types")
                                font.pixelSize: fs(14)
                                font.bold: true
                                color: Colors.textPrimary
                            }
                            Button {
                                objectName: "ptNewButton"
                                text: qsTr("New")
                                flat: true
                                onClicked: _ptPane.select(0)
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: fs(11)
                            color: Colors.textSecondary
                            text: _ptPane._vpm && _ptPane._vpm.currentDeviceUid !== ""
                                  ? qsTr("Connected copter's type: %1").arg(_ptPane._vpm.currentPowerTypeName || qsTr("not linked"))
                                  : qsTr("No copter connected")
                        }

                        ListView {
                            id: _ptList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 4
                            model: _ptPane.types
                            delegate: Rectangle {
                                width: _ptList.width
                                height: 52
                                radius: 6
                                color: modelData.id === _ptPane.selId ? Colors.surfaceLight : Colors.bgPrimary
                                border.color: modelData.id === _ptPane.selId ? Colors.skywinAccent : Colors.divider
                                border.width: 1
                                MouseArea { anchors.fill: parent; onClicked: _ptPane.select(modelData.id) }
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 2
                                    Text {
                                        text: modelData.name
                                        font.pixelSize: fs(13)
                                        font.bold: true
                                        color: Colors.textPrimary
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: qsTr("%1 aircraft · %2 flights").arg(modelData.vehicles).arg(modelData.flights)
                                        font.pixelSize: fs(11)
                                        color: Colors.textMuted
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Colors.divider }

                        Text {
                            text: qsTr("Mission check: take-off and landing (min, as hover)")
                            font.pixelSize: fs(11)
                            color: Colors.textSecondary
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: takeoffField; objectName: "takeoffField"; Layout.preferredWidth: 60; color: Colors.textPrimary; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                            TextField { id: landField; objectName: "landField"; Layout.preferredWidth: 60; color: Colors.textPrimary; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                            Button {
                                objectName: "fleetSaveButton"
                                text: qsTr("Save")
                                flat: true
                                onClicked: {
                                    // Read both first: each save reloads the page, which
                                    // refills both fields from the stored values.
                                    var takeoff = Number(takeoffField.text.replace(",", "."))
                                    var land = Number(landField.text.replace(",", "."))
                                    var ok = _ptPane._vpm.setTakeoffMin(takeoff)
                                    ok = _ptPane._vpm.setLandMin(land) && ok
                                    _ptPane.message = ok ? qsTr("Take-off and landing minutes saved.")
                                                         : qsTr("Take-off and landing must be 0–10 minutes.")
                                }
                            }
                        }
                    }
                }

                // ── Right: the selected type ──────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Colors.surface
                    radius: 8
                    border.color: Colors.divider
                    border.width: 1

                    Text {
                        visible: _ptPane.selId < 0
                        anchors.centerIn: parent
                        width: parent.width - 32
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Select a power type, or press New. A power type is one way of building a copter's "
                                   + "power system: frame, props (diameter × pitch, inches) and battery cells.")
                        font.pixelSize: fs(12)
                        color: Colors.textSecondary
                    }

                    Flickable {
                        visible: _ptPane.selId >= 0
                        anchors.fill: parent
                        anchors.margins: 12
                        contentHeight: _ptDetail.implicitHeight
                        clip: true

                        ColumnLayout {
                            id: _ptDetail
                            width: parent.width
                            spacing: 8

                            Text {
                                text: _ptPane.selId > 0 ? (_ptPane.sel.name || "") : qsTr("New power type")
                                font.pixelSize: fs(15)
                                font.bold: true
                                color: Colors.textPrimary
                            }

                            GridLayout {
                                columns: 4
                                columnSpacing: 8
                                rowSpacing: 4
                                Layout.fillWidth: true
                                Text { text: qsTr("Frame"); font.pixelSize: fs(11); color: Colors.textMuted }
                                TextField { id: ptFrameField; objectName: "ptFrameField"; Layout.fillWidth: true; placeholderText: qsTr("e.g. Quad X"); color: Colors.textPrimary }
                                Text { text: qsTr("Cells (S)"); font.pixelSize: fs(11); color: Colors.textMuted }
                                TextField { id: ptCellsField; objectName: "ptCellsField"; Layout.preferredWidth: 60; placeholderText: "6"; color: Colors.textPrimary; inputMethodHints: Qt.ImhDigitsOnly }
                                Text { text: qsTr("Prop diameter (in)"); font.pixelSize: fs(11); color: Colors.textMuted }
                                TextField { id: ptDiameterField; objectName: "ptDiameterField"; Layout.fillWidth: true; placeholderText: "15"; color: Colors.textPrimary; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                                Text { text: qsTr("Prop pitch (in)"); font.pixelSize: fs(11); color: Colors.textMuted }
                                TextField { id: ptPitchField; objectName: "ptPitchField"; Layout.preferredWidth: 60; placeholderText: "5.5"; color: Colors.textPrimary; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                                Text { text: qsTr("Motor (note)"); font.pixelSize: fs(11); color: Colors.textMuted }
                                TextField { id: ptNoteField; objectName: "ptNoteField"; Layout.fillWidth: true; Layout.columnSpan: 3; placeholderText: qsTr("optional"); color: Colors.textPrimary }
                            }

                            RowLayout {
                                spacing: 6
                                Button { objectName: "ptSaveButton"; text: qsTr("Save"); highlighted: true; onClicked: _ptPane.save() }
                                Button {
                                    objectName: "ptLinkButton"
                                    text: qsTr("Link connected copter")
                                    visible: _ptPane.selId > 0
                                    enabled: _ptPane._vpm && _ptPane._vpm.currentDeviceUid !== "" && _ptPane._vpm.currentPowerTypeId !== _ptPane.selId
                                    onClicked: _ptPane.message = _ptPane._vpm.linkCurrentVehiclePowerType(_ptPane.selId)
                                                                 ? qsTr("Connected copter linked.") : qsTr("Could not link the connected copter.")
                                }
                                Button {
                                    objectName: "ptDeleteButton"
                                    text: qsTr("Delete")
                                    visible: _ptPane.selId > 0
                                    enabled: _ptPane.sel.vehicles === 0 && _ptPane.sel.flights === 0
                                    onClicked: if (_ptPane._vpm.deletePowerType(_ptPane.selId)) _ptPane.select(-1)
                                }
                            }

                            Text {
                                visible: _ptPane.message !== ""
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: _ptPane.message
                                font.pixelSize: fs(11)
                                color: Colors.textSecondary
                            }

                            // What the type has learned.
                            Rectangle { visible: _ptPane.selId > 0; Layout.fillWidth: true; height: 1; color: Colors.divider }
                            Text {
                                visible: _ptPane.selId > 0
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                font.pixelSize: fs(12)
                                color: _ptPane.summary.validated ? Colors.statusPass : Colors.textPrimary
                                text: {
                                    var s = _ptPane.summary
                                    if (!s || s.status === undefined) return ""
                                    var head = _ptPane.statusText(s) + (s.validated ? qsTr(" · Validated") : "")
                                                + (s.source === "synthetic" ? qsTr(" · from the FC's synthetic current, not yet learned") : "")
                                    if (s.source === "none")
                                        return head
                                    return head + "\n" + qsTr("Hover %1 A + %2 A/kg · Cruise %3 A + %4 A/kg")
                                            .arg(Number(s.hover0).toFixed(1)).arg(Number(s.hoverK).toFixed(1))
                                            .arg(Number(s.cruise0).toFixed(1)).arg(Number(s.cruiseK).toFixed(1))
                                }
                            }
                            Text {
                                visible: _ptPane.selId > 0 && _ptPane.summary.status !== undefined
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                font.pixelSize: fs(11)
                                color: Colors.textSecondary
                                // The charger ÷ FC factor only means something when the
                                // FC current follows throttle (a current sensor); on a
                                // copter's synthetic current the GCS counts the charge.
                                text: qsTr("%1 flights used, %2 waiting.").arg(_ptPane.summary.usedFlights).arg(_ptPane.summary.pendingFlights)
                                      + (_ptPane.summary.fcCurrentFollowsThrottle === false
                                         ? qsTr(" The flight controller's current does not follow throttle: the GCS counts the charge in flight.")
                                         : qsTr(" Charger ÷ FC mAh: %1.").arg(Number(_ptPane.summary.chargerFactor).toFixed(2)))
                                      + (_ptPane.summary.advice ? " " + _ptPane.summary.advice + "." : "")
                            }

                            // BATT_AMP_PERVLT (connected copter on this type).
                            Text {
                                objectName: "ampPerVoltText"
                                visible: _ptPane.ampSuggestion.available === true
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                font.pixelSize: fs(11)
                                color: Colors.warning
                                text: qsTr("BATT_AMP_PERVLT on the connected copter: %1 → suggested %2 (flight #%3: charger %4 mAh, FC %5 mAh). "
                                           + "Set it yourself in the parameter editor; the GCS never writes it. "
                                           + "If you already changed it after that flight, ignore this.")
                                        .arg(Number(_ptPane.ampSuggestion.ampPerVolt).toFixed(1))
                                        .arg(Number(_ptPane.ampSuggestion.suggested).toFixed(1))
                                        .arg(_ptPane.ampSuggestion.sessionId)
                                        .arg(Number(_ptPane.ampSuggestion.chargerMah).toFixed(0))
                                        .arg(Number(_ptPane.ampSuggestion.fcMah).toFixed(0))
                            }

                            // The type's flights, newest first.
                            Rectangle { visible: _ptPane.selId > 0; Layout.fillWidth: true; height: 1; color: Colors.divider }
                            Text {
                                visible: _ptPane.selId > 0
                                text: _ptPane.flights.length > 0
                                      ? qsTr("Flights (charge the pack straight after landing, enter the mAh the charger put back)")
                                      : qsTr("No flights recorded with this type yet.")
                                font.pixelSize: fs(12)
                                font.bold: true
                                color: Colors.textPrimary
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }

                            Repeater {
                                model: _ptPane.selId > 0 ? _ptPane.flights : []
                                delegate: Rectangle {
                                    id: _flightRow
                                    readonly property var f: modelData
                                    readonly property string st: f.learningState
                                    Layout.fillWidth: true
                                    implicitHeight: _flightCol.implicitHeight + 12
                                    radius: 6
                                    color: Colors.bgPrimary
                                    border.color: f.outlier === true ? Colors.warning : Colors.divider
                                    border.width: 1

                                    ColumnLayout {
                                        id: _flightCol
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: parent.top
                                        anchors.margins: 6
                                        spacing: 3

                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            font.pixelSize: fs(12)
                                            color: Colors.textPrimary
                                            text: qsTr("#%1 · %2 · %3 kg · hover %4 min · cruise %5 min · FC %6 mAh")
                                                    .arg(f.sessionId).arg(root.fmtLastSeen(f.startedAt))
                                                    .arg(Number(f.payloadKg).toFixed(1))
                                                    .arg(_ptPane.num(f.hoverMinEmpty + f.hoverMinLoaded, 1))
                                                    .arg(_ptPane.num(f.cruiseMinEmpty + f.cruiseMinLoaded, 1))
                                                    .arg(_ptPane.num(f.fcMah, 0))
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            font.pixelSize: fs(11)
                                            color: st === "excluded" ? Colors.textMuted : (f.outlier === true ? Colors.warning : Colors.textSecondary)
                                            text: (st === "excluded" ? qsTr("Not used for learning: the pack was not full at arming, or telemetry was missing.")
                                                   : st === "used" ? qsTr("Used")
                                                   : st === "dismissed" ? qsTr("Dismissed")
                                                   : qsTr("Waiting: enter the charger number, then Use or Dismiss."))
                                                  + (f.predictedMah !== undefined ? qsTr(" · model predicts %1 mAh").arg(Number(f.predictedMah).toFixed(0)) : "")
                                                  + (f.outlier === true ? qsTr(" · more than 20 % off the model: wrong pack charged, or a heavier payload?") : "")
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Text { text: qsTr("Charger (mAh)"); font.pixelSize: fs(11); color: Colors.textMuted }
                                            TextField {
                                                id: _chargerField
                                                objectName: "chargerField_" + f.sessionId
                                                Layout.preferredWidth: 90
                                                text: f.chargerMah > 0 ? Number(f.chargerMah).toFixed(0) : ""
                                                placeholderText: qsTr("e.g. 5100")
                                                color: Colors.textPrimary
                                                inputMethodHints: Qt.ImhDigitsOnly
                                            }
                                            Button {
                                                objectName: "chargerSave_" + f.sessionId
                                                text: qsTr("Save")
                                                flat: true
                                                onClicked: if (!_ptPane._vpm.setFlightChargerMah(f.sessionId, Number(_chargerField.text)))
                                                               _ptPane.message = qsTr("Charger number not saved: 1–200000 mAh.")
                                            }
                                            Item { Layout.fillWidth: true }
                                            Button {
                                                objectName: "useButton_" + f.sessionId
                                                text: qsTr("Use")
                                                visible: st !== "excluded"
                                                enabled: st !== "used" && f.chargerMah > 0
                                                onClicked: _ptPane._vpm.setFlightLearningState(f.sessionId, "used")
                                            }
                                            Button {
                                                objectName: "dismissButton_" + f.sessionId
                                                text: qsTr("Dismiss")
                                                visible: st !== "excluded"
                                                enabled: st !== "dismissed"
                                                onClicked: _ptPane._vpm.setFlightLearningState(f.sessionId, "dismissed")
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

    Component {
        id: detailBlock

        Item {
            // vehicleData() (C++) is the snapshotted record for the selected row
            property var v: VehicleListModel.vehicleData(root._selectedIndex)

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                // Inline display-name edit
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    TextField {
                        id: nameField
                        Layout.fillWidth: true
                        text: v.friendlyName || ""
                        color: Colors.textPrimary
                        placeholderText: qsTr("Display name")
                        background: Rectangle { color: Colors.surfaceLight; radius: 4; border.color: Colors.divider }
                    }

                    Button {
                        text: qsTr("Save")
                        // Display names persist via Database (owner); reload refreshes the rows.
                        onClicked: {
                            if (v.fingerprint && nameField.text.length > 0) {
                                // Rename keyed on fingerprint (stable ID) and persisted via Database (C++)
                                Database.updateVehicleName(v.fingerprint, nameField.text)
                                VehicleListModel.reload()
                            }
                        }
                        flat: true
                    }
                }

                Item { Layout.preferredHeight: 4 }

                // Weak-identification warning
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    visible: v.fingerprintSource === "SYSID_TYPE_FALLBACK"
                    color: Colors.warningDim
                    radius: 4

                    Label {
                        anchors.fill: parent
                        anchors.margins: 6
                        text: qsTr("⚠ Weak identification — no hardware UID reported. Records identified by connection details only.")
                        font.pixelSize: fs(10)
                        color: Colors.warning
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 4

                    Repeater {
                        model: root._detailRows(v)
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                text: modelData.label
                                font.pixelSize: fs(11)
                                color: Colors.textMuted
                                Layout.preferredWidth: 110
                            }
                            Text {
                                text: modelData.value
                                font.pixelSize: fs(12)
                                color: Colors.textPrimary
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }

    // Field/label rows for the detail grid — a flat map of the persisted vehicle record
    function _detailRows(v) {
        if (!v) return []
        var rows = []
        rows.push({label: "Type", value: v.vehicleTypeName || "Unknown"})
        rows.push({label: "Airframe", value: v.airframeType || "—"})
        rows.push({label: "Autopilot", value: v.autopilotType || "—"})
        rows.push({label: "Firmware", value: v.firmwareVersion || "—"})
        rows.push({label: "Board", value: v.boardVersion || "—"})
        rows.push({label: "Hardware UID", value: v.hardwareUid || "—"})
        rows.push({label: "SysID", value: v.sysid})
        rows.push({label: "COMPID", value: v.compid})
        rows.push({label: "Motors", value: v.motorCount})
        rows.push({label: "Frame class", value: v.frameClass})
        rows.push({label: "First seen", value: root.fmtLastSeen(v.firstSeen)})
        rows.push({label: "Last seen", value: root.fmtLastSeen(v.lastSeen)})
        rows.push({label: "Flights", value: v.totalFlightCount})
        rows.push({label: "Flight time", value: root.fmtFlightTime(v.totalFlightTimeSec)})
        return rows
    }
}