// Component: WeatherInfoPanel
// Purpose: Sticky info panel for the right 30% of the Preflight Checklist page.
//   Displays live conditions from WeatherProvider plus link-health and
//   position/mission check statuses in a scrollable 2-column grid.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QGroundControl
import com.uav.preflight 1.0

Rectangle {
    id: root

    color: Colors.surface2

    readonly property var _telemetryDrop: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("com.telemetry.drop_rate") : null
    readonly property var _mavlinkProtocol: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("comm.mavlink.protocol") : null
    readonly property var _heartbeat: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("comm.heartbeat") : null
    readonly property var _homePosition: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("nav.home") : null
    readonly property var _attitude: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("nav.attitude") : null

    function _statusColor(check) {
        if (!check) return Colors.textSecondary
        if (check.status === 1) return Colors.success
        if (check.status === 2) return Colors.error
        if (check.status === 3) return Colors.checkWarn
        return Colors.textSecondary
    }

    // Left border to visually separate from the checklist column
    Rectangle {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 1
        color: Colors.border
    }

    // ── 2-column grid cell: icon + label over value ────────────────
    component InfoCell: Column {
        property string icon: ""
        property string label: ""
        property string value: "—"
        property color valueColor: Colors.textPrimary
        Layout.fillWidth: true
        Layout.preferredWidth: 130
        spacing: 2

        Label {
            text: (icon.length > 0 ? icon + "  " : "") + label
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            elide: Text.ElideRight
            width: parent.width
        }
        Label {
            text: value.length > 0 ? value : "—"
            font.pixelSize: Config.fontSizeBody
            font.bold: true
            color: valueColor
            wrapMode: Text.Wrap
            width: parent.width
        }
    }

    component SectionDivider: Rectangle {
        width: parent.width
        height: 1
        color: Colors.border
    }

    // ── Scrollable content ─────────────────────────────────────────
    Flickable {
        anchors { fill: parent; margins: 16 }
        contentWidth: width
        contentHeight: panelCol.height + Config.spacingMedium
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: panelCol
            width: parent.width
            spacing: 16

            // Title
            Label {
                text: qsTr("Weather")
                font.bold: true
                font.pixelSize: Config.fontSizeH3
            }

            // Last updated
            Label {
                text: WeatherProvider.lastUpdated.length > 0
                      ? "Updated: " + WeatherProvider.lastUpdated
                      : "Not yet fetched"
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
            }

            // Fetch / refresh button
            Button {
                text: WeatherProvider.isFetching ? qsTr("Fetching...") : qsTr("Refresh")
                enabled: !WeatherProvider.isFetching
                onClicked: {
                    var pos = QGroundControl.flightMapPosition
                    var lat = (pos && pos.isValid) ? pos.latitude : 0
                    var lon = (pos && pos.isValid) ? pos.longitude : 0
                    if ((lat === 0 && lon === 0) && typeof VehicleTelemetry !== "undefined") {
                        lat = VehicleTelemetry.gpsLatitude
                        lon = VehicleTelemetry.gpsLongitude
                    }
                    WeatherProvider.fetchWeather(lat, lon)
                }
            }

            // ── Live telemetry: altitude & GPS fix ────────────────────
            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: 16
                rowSpacing: 10

                InfoCell {
                    icon: "\u2B06"   // ⬆
                    label: qsTr("Altitude MSL")
                    value: TelemetryProvider.gpsSatellites > 0
                           ? TelemetryProvider.globalAltitude.toFixed(1) + " m"
                           : qsTr("Waiting for GPS\u2026")
                }
                InfoCell {
                    icon: "\u{1F4CD}"   // 📍
                    label: qsTr("GPS Position")
                    value: TelemetryProvider.gpsSatellites > 0
                           ? TelemetryProvider.gpsLatitude.toFixed(5) + ", "
                             + TelemetryProvider.gpsLongitude.toFixed(5)
                           : qsTr("No fix")
                }
            }

            SectionDivider {}

            // ── Conditions (2-column grid) ──────────────────────────
            Label {
                text: qsTr("Conditions")
                font.bold: true
                font.pixelSize: Config.fontSizeH3
            }
            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: 16
                rowSpacing: 10

                InfoCell {
                    icon: "\u{1F321}"   // 🌡
                    label: qsTr("Temperature")
                    value: WeatherProvider.temperature > -999
                           ? WeatherProvider.temperature.toFixed(1) + " \u00B0C"
                           : ""
                }
                InfoCell {
                    icon: "\u{1F4A8}"   // 💨
                    label: qsTr("Wind")
                    value: WeatherProvider.windSummary
                }
                InfoCell {
                    icon: "\u{1F441}"   // 👁
                    label: qsTr("Visibility")
                    value: WeatherProvider.visibilityKm > 0
                           ? WeatherProvider.visibilityKm.toFixed(1) + " km"
                           : ""
                }
                InfoCell {
                    icon: "\u2601"      // ☁
                    label: qsTr("Ceiling")
                    value: WeatherProvider.ceiling
                }
                InfoCell {
                    icon: "\u{1F327}"   // 🌧
                    label: qsTr("Precipitation")
                    value: WeatherProvider.precipitation.length > 0
                           ? WeatherProvider.precipitation.join(", ")
                           : "None"
                }
                InfoCell {
                    icon: "\u{1F4E1}"   // 📡
                    label: qsTr("Station")
                    value: WeatherProvider.stationId
                }
            }

            SectionDivider {}

            // Raw METAR string
            Column {
                width: parent.width
                spacing: 4

                Label {
                    text: qsTr("METAR")
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    font.bold: true
                }
                Label {
                    text: WeatherProvider.rawMetar.length > 0
                          ? WeatherProvider.rawMetar
                          : "—"
                    wrapMode: Text.Wrap
                    width: parent.width
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                }
            }

            // Wind go/no-go indicator
            Rectangle {
                width: parent.width
                height: 48
                radius: Config.radiusLarge
                color: WeatherProvider.windOk ? "#1A3A2A" : "#3A1A1A"
                border.color: WeatherProvider.windOk ? Colors.statePass : Colors.stateFail
                Label {
                    anchors.centerIn: parent
                    text: WeatherProvider.windOk
                          ? "\u2713  Wind within limits"
                          : "\u2717  Wind exceeds limits"
                    color: WeatherProvider.windOk ? Colors.statePass : Colors.stateFail
                    font.bold: true
                }
            }

            SectionDivider {}

            // ── Link health (2-column grid) ─────────────────────────
            Label {
                text: qsTr("Link Health")
                font.bold: true
                font.pixelSize: Config.fontSizeH3
            }
            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: 16
                rowSpacing: 10

                InfoCell {
                    icon: "\u{1F4E9}"   // 📩
                    label: qsTr("Telemetry Drop")
                    value: _telemetryDrop ? _telemetryDrop.statusText : "—"
                    valueColor: _statusColor(_telemetryDrop)
                }
                InfoCell {
                    icon: "\u{1F6F0}"   // 🛰
                    label: qsTr("MAVLink Protocol")
                    value: _mavlinkProtocol ? _mavlinkProtocol.statusText : "—"
                    valueColor: _statusColor(_mavlinkProtocol)
                }
                InfoCell {
                    icon: "\u{1F493}"   // 💓
                    label: qsTr("Heartbeat")
                    value: _heartbeat ? _heartbeat.statusText : "—"
                    valueColor: _statusColor(_heartbeat)
                }
            }

            SectionDivider {}

            // ── Position & mission (2-column grid) ──────────────────
            Label {
                text: qsTr("Position")
                font.bold: true
                font.pixelSize: Config.fontSizeH3
            }
            GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: 16
                rowSpacing: 10

                InfoCell {
                    icon: "\u{1F3E0}"   // 🏠
                    label: qsTr("Home Position")
                    value: _homePosition ? _homePosition.statusText : "—"
                    valueColor: _statusColor(_homePosition)
                }
                InfoCell {
                    icon: "\u{1F4C8}"   // 📈
                    label: qsTr("Attitude")
                    value: _attitude ? _attitude.statusText : "—"
                    valueColor: _statusColor(_attitude)
                }
            }
        }
    }
}
