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
    readonly property var _videoFeed: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("video.feed") : null
    readonly property var _homePosition: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("nav.home") : null
    readonly property var _attitude: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("nav.attitude") : null
    readonly property var _escFirmware: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("propulsion.esc.firmware") : null
    readonly property var _zoneCompliance: typeof PreflightManager !== "undefined" ? PreflightManager.checkById("airspace.zone_compliance") : null

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

    // The forecast at the flying site that the mission estimate uses: wind
    // at 10, 80 and 120 m, temperature corrected for the site's height, how
    // old it is and how sure, with the airport METAR as a check. "Prepare for
    // the site" fetches and keeps it while there is internet, for use at a
    // site without. The site is the vehicle's home, else its GPS position,
    // else the map centre (pan the map to the site before preparing).
    component SiteWeatherSection: Column {
        id: site
        spacing: 8

        property int _tick: 0
        readonly property bool _homeSet: typeof TelemetryProvider !== "undefined"
                                         && (Math.abs(TelemetryProvider.homeLatitude) > 0.01
                                             || Math.abs(TelemetryProvider.homeLongitude) > 0.01)
        readonly property bool _gpsSet: typeof TelemetryProvider !== "undefined" && TelemetryProvider.gpsSatellites > 0
        readonly property var _mapPos: QGroundControl.flightMapPosition
        readonly property double siteLat: _homeSet ? TelemetryProvider.homeLatitude
                                        : _gpsSet ? TelemetryProvider.gpsLatitude
                                        : (_mapPos && _mapPos.isValid ? _mapPos.latitude : NaN)
        readonly property double siteLon: _homeSet ? TelemetryProvider.homeLongitude
                                        : _gpsSet ? TelemetryProvider.gpsLongitude
                                        : (_mapPos && _mapPos.isValid ? _mapPos.longitude : NaN)
        readonly property double siteAmsl: _homeSet ? TelemetryProvider.homeAltitude
                                         : _gpsSet ? TelemetryProvider.globalAltitude : NaN
        readonly property string siteFrom: _homeSet ? qsTr("home") : _gpsSet ? qsTr("aircraft GPS") : qsTr("map centre")

        function _at(h) {
            var r = WeatherProvider.siteRevision + _tick   // re-read when the data or the clock moves
            return WeatherProvider.siteConditions(siteLat, siteLon, siteAmsl, h)
        }
        function _wind(c) {
            if (!c.windKnown) return "—"
            var s = c.windMs.toFixed(1) + " m/s"
            if (isFinite(c.gustMs) && c.gustMs > c.windMs + 0.05) s += " G" + c.gustMs.toFixed(1)
            if (isFinite(c.windFromDeg)) s += qsTr(" from %1°").arg(Math.round(c.windFromDeg))
            return s
        }
        function _windColor(c) { return c.gustRed ? Colors.error : Colors.textPrimary }
        // The highest height whose gust is above the limit, for the red line.
        readonly property var _redAt: c120.gustRed ? [120, c120] : c80.gustRed ? [80, c80] : c10.gustRed ? [10, c10] : null
        readonly property var c0: _at(0)
        readonly property var c10: _at(10)
        readonly property var c80: _at(80)
        readonly property var c120: _at(120)

        Timer { interval: 60000; running: true; repeat: true; onTriggered: site._tick++ }

        Label {
            text: qsTr("At the site")
            font.bold: true
            font.pixelSize: Config.fontSizeH3
        }
        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 16
            rowSpacing: 10

            InfoCell { label: qsTr("Wind at 10 m"); value: site._wind(site.c10); valueColor: site._windColor(site.c10) }
            InfoCell { label: qsTr("Wind at 80 m"); value: site._wind(site.c80); valueColor: site._windColor(site.c80) }
            InfoCell { label: qsTr("Wind at 120 m"); value: site._wind(site.c120); valueColor: site._windColor(site.c120) }
            InfoCell {
                label: qsTr("Temperature, humidity")
                value: (isFinite(site.c0.tempC) ? site.c0.tempC.toFixed(1) + " °C" : "—")
                       + (isFinite(site.c0.humidityPct) ? ", " + Math.round(site.c0.humidityPct) + " %" : "")
            }
            InfoCell {
                label: qsTr("Confidence")
                value: site.c10.confidence
                valueColor: site.c10.confidence === "High" ? Colors.success
                          : site.c10.confidence === "Medium" ? Colors.checkWarn
                          : site.c10.confidence === "Low" ? Colors.checkWarn : Colors.error
            }
            InfoCell {
                label: qsTr("METAR check")
                value: isFinite(site.c10.metarWindMs)
                       ? site.c10.metarWindMs.toFixed(1) + " m/s"
                         + (site.c10.metarGustMs > 0 ? " G" + site.c10.metarGustMs.toFixed(1) : "")
                       : "—"
            }
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            text: (site.c10.windSource.length > 0
                   ? qsTr("Wind from the %1").arg(site.c10.windSource)
                     + (site.c10.forecastHour.length > 0 ? qsTr(", hour of %1").arg(site.c10.forecastHour) : "")
                     + (site.c10.forecastAge.length > 0 ? qsTr(", %1 old").arg(site.c10.forecastAge) : "")
                     + ". "
                   : "")
                  + qsTr("Site: %1.").arg(site.siteFrom)
        }
        Label {
            width: parent.width
            visible: site._redAt !== null
            wrapMode: Text.Wrap
            font.bold: true
            color: Colors.error
            text: site._redAt === null ? ""
                  : qsTr("\u2716 Gusts %1 m/s at %2 m: above the %3 m/s gust limit")
                    .arg((isFinite(site._redAt[1].gustMs) ? site._redAt[1].gustMs : site._redAt[1].windMs).toFixed(1))
                    .arg(site._redAt[0])
                    .arg(site._redAt[1].gustLimitMs.toFixed(0))
        }
        Repeater {
            model: site.c10.reasons
            Label {
                width: site.width
                wrapMode: Text.Wrap
                font.pixelSize: Config.fontSizeSmall
                color: Colors.checkWarn
                text: "⚠ " + modelData
            }
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            text: site.c10.savedText
        }
        Button {
            text: WeatherProvider.isFetching ? qsTr("Fetching...") : qsTr("Prepare for the site")
            enabled: !WeatherProvider.isFetching && isFinite(site.siteLat)
            onClicked: WeatherProvider.prepareForSite(site.siteLat, site.siteLon,
                                                      typeof PreflightSettingsManager !== "undefined"
                                                      ? PreflightSettingsManager.defaultIcao : "")
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Config.fontSizeSmall
            color: Colors.textSecondary
            text: qsTr("No internet at the site? Prepare here first. Download the map with Application Settings → Offline Maps.")
        }
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

            // Title row: weather heading (left) + ESC firmware status (right)
            RowLayout {
                width: parent.width
                spacing: 8

                Label {
                    text: qsTr("Weather")
                    font.bold: true
                    font.pixelSize: Config.fontSizeH3
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    Layout.maximumWidth: root.width * 0.55
                    spacing: 0

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("ESC Firmware")
                        font.pixelSize: 11
                        color: Colors.textSecondary
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        Layout.fillWidth: true
                        text: {
                            if (!root._escFirmware) return "\u2014"
                            if (root._escFirmware.currentValueString.length > 0) return root._escFirmware.currentValueString
                            if (root._escFirmware.message.length > 0) return root._escFirmware.message
                            return "\u2014"
                        }
                        font.pixelSize: 9
                        font.bold: true
                        color: root._statusColor(root._escFirmware)
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignRight
                    }
                }
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

            // METAR observation or model forecast, and when.
            Label {
                width: parent.width
                visible: WeatherProvider.weatherSource.length > 0
                text: qsTr("Source: %1").arg(WeatherProvider.weatherSource)
                font.pixelSize: Config.fontSizeSmall
                font.italic: true
                color: Colors.textSecondary
                wrapMode: Text.Wrap
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
                color: WeatherProvider.windOk ? Colors.statusPassDim : Colors.statusFailDim
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

            // ── At the site: the weather the mission estimate uses ──
            SiteWeatherSection { width: parent.width }

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
                InfoCell {
                    icon: "\u{1F4F9}"   // 📹
                    label: qsTr("Video Feed")
                    value: _videoFeed ? _videoFeed.statusText : "No video source"
                    valueColor: _statusColor(_videoFeed)
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
                InfoCell {
                    icon: "\u{1F6A9}"   // 🚩
                    label: qsTr("Airspace")
                    value: _zoneCompliance ? _zoneCompliance.statusText : "—"
                    valueColor: _statusColor(_zoneCompliance)
                }
            }
        }
    }
}
