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

            RowLayout {
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

                // Stop the pending export-status timer on teardown (R13).
                Component.onDestruction: exportTimer.stop()
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Colors.divider
            }

            // ── Two-pane layout: vehicle list (left) + detail panel (right) ──
            RowLayout {
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