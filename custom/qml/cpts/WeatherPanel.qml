import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

Item {
    id: root
    implicitHeight: col.implicitHeight + 20

    property color cardBg: Colors.surfaceLight
    property color cardBorder: Colors.border

    Rectangle {
        anchors.fill: parent
        color: cardBg
        radius: Config.radiusMedium
        border.color: cardBorder

        Column {
            id: col
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            // ── Header ──────────────────────────────────────────────
            RowLayout {
                width: parent.width
                spacing: 8

                Text {
                    text: qsTr("Weather")
                    font.pixelSize: Config.fontSizeH3
                    font.bold: true
                    color: Colors.textPrimary
                }

                Text {
                    text: WeatherProvider.loading ? "Loading..." : ""
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    Layout.alignment: Qt.AlignVCenter
                    visible: WeatherProvider.loading
                }

                Item { Layout.fillWidth: true }

                CustomButton {
                    text: qsTr("\u21bb")
                    implicitWidth: 28
                    implicitHeight: 28
                    baseColor: Colors.surface
                    font.pixelSize: Config.fontSizeBody
                    enabled: !WeatherProvider.loading
                    onClicked: {
                        if (VehicleTelemetry.gpsLatitude !== 0 || VehicleTelemetry.gpsLongitude !== 0)
                            WeatherProvider.fetchWeather(VehicleTelemetry.gpsLatitude, VehicleTelemetry.gpsLongitude)
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Refresh weather")
                }
            }

            // ── Error ───────────────────────────────────────────────
            Text {
                text: WeatherProvider.lastError
                font.pixelSize: Config.fontSizeSmall
                color: Colors.danger
                visible: WeatherProvider.lastError.length > 0
                wrapMode: Text.WordWrap
                width: parent.width
            }

            // ── Wind arrow + main stats ─────────────────────────────
            Row {
                width: parent.width
                height: 80
                spacing: 12

                // Wind arrow (Canvas)
                Canvas {
                    id: arrowCanvas
                    width: 72
                    height: 72
                    anchors.verticalCenter: parent.verticalCenter

                    property real windDir: WeatherProvider.windDirection
                    property real windSpeed: WeatherProvider.windSpeed

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var cx = width / 2
                        var cy = height / 2
                        var r = Math.min(cx, cy) - 4
                        var angle = windDir * Math.PI / 180.0

                        // Circle
                        ctx.strokeStyle = Colors.border
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.arc(cx, cy, r, 0, Math.PI * 2)
                        ctx.stroke()

                        // Tick marks N, E, S, W
                        var tickLen = 6
                        ctx.strokeStyle = Colors.textSecondary
                        ctx.lineWidth = 1
                        for (var i = 0; i < 4; i++) {
                            var a = i * Math.PI / 2 - Math.PI / 2
                            var x1 = cx + (r - tickLen) * Math.cos(a)
                            var y1 = cy + (r - tickLen) * Math.sin(a)
                            var x2 = cx + r * Math.cos(a)
                            var y2 = cy + r * Math.sin(a)
                            ctx.beginPath()
                            ctx.moveTo(x1, y1)
                            ctx.lineTo(x2, y2)
                            ctx.stroke()
                        }

                        // Arrow (wind comes FROM direction, arrow points FROM)
                        var arrowLen = r * 0.75
                        var tipX = cx - arrowLen * Math.sin(angle)
                        var tipY = cy + arrowLen * Math.cos(angle)
                        var tailX = cx + arrowLen * 0.35 * Math.sin(angle)
                        var tailY = cy - arrowLen * 0.35 * Math.cos(angle)

                        // Shaft
                        ctx.strokeStyle = Colors.primary
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(tailX, tailY)
                        ctx.lineTo(tipX, tipY)
                        ctx.stroke()

                        // Arrowhead
                        var headSize = 8
                        var headAngle = 0.4
                        var hx1 = tipX + headSize * Math.sin(angle + headAngle)
                        var hy1 = tipY - headSize * Math.cos(angle + headAngle)
                        var hx2 = tipX + headSize * Math.sin(angle - headAngle)
                        var hy2 = tipY - headSize * Math.cos(angle - headAngle)

                        ctx.fillStyle = Colors.primary
                        ctx.beginPath()
                        ctx.moveTo(tipX, tipY)
                        ctx.lineTo(hx1, hy1)
                        ctx.lineTo(hx2, hy2)
                        ctx.closePath()
                        ctx.fill()
                    }

                    Connections {
                        target: WeatherProvider
                        function onWeatherUpdated() { arrowCanvas.requestPaint() }
                    }

                    Component.onCompleted: arrowCanvas.requestPaint()

                    Text {
                        anchors.centerIn: parent
                        text: Math.round(arrowCanvas.windSpeed) + ""
                        font.pixelSize: 10
                        font.bold: true
                        color: Colors.textPrimary
                    }
                }

                // Stats column
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3

                    Text {
                        text: WeatherProvider.temperature > -999
                            ? WeatherProvider.temperature.toFixed(1) + " \u00b0C  ·  " + WeatherProvider.weatherDescription
                            : "Tap Refresh"
                        font.pixelSize: Config.fontSizeBody
                        color: Colors.textPrimary
                    }

                    Text {
                        text: "Wind: " + (WeatherProvider.windSpeed * 3.6).toFixed(1) + " km/h"
                              + " from " + directionLabel(WeatherProvider.windDirection)
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                    }

                    Text {
                        text: "Visibility: " + (WeatherProvider.visibilityKm > 0
                            ? WeatherProvider.visibilityKm.toFixed(1) + " km"
                            : "N/A")
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                    }

                    Text {
                        text: "Ceiling: " + (WeatherProvider.ceilingFt > 0
                            ? WeatherProvider.ceilingFt + " ft"
                            : "N/A")
                        font.pixelSize: Config.fontSizeSmall
                        color: Colors.textSecondary
                    }
                }
            }

            // ── METAR raw string ────────────────────────────────────
            Text {
                text: WeatherProvider.metarString
                font.pixelSize: Config.fontSizeSmall
                font.family: "monospace"
                color: Colors.textSecondary
                visible: WeatherProvider.metarString.length > 0
                wrapMode: Text.WordWrap
                width: parent.width
                ToolTip.visible: mouseArea.containsMouse
                ToolTip.text: qsTr("Latest METAR for station")
                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }

            // ── ICAO entry + METAR fetch ────────────────────────────
            Row {
                spacing: 6
                width: parent.width

                TextField {
                    id: icaoInput
                    placeholderText: qsTr("ICAO (e.g. KLAX)")
                    font.pixelSize: Config.fontSizeSmall
                    maximumLength: 4
                    implicitWidth: 100
                    height: 24
                    background: Rectangle {
                        color: Colors.background
                        radius: Config.radiusSmall
                        border.color: Colors.border
                    }
                    onAccepted: WeatherProvider.fetchMetar(text)
                }

                CustomButton {
                    text: qsTr("METAR")
                    implicitHeight: 24
                    font.pixelSize: Config.fontSizeSmall
                    baseColor: Colors.surface
                    enabled: icaoInput.text.length >= 3 && !WeatherProvider.loading
                    onClicked: WeatherProvider.fetchMetar(icaoInput.text)
                }

                CustomButton {
                    text: qsTr("NOTAM")
                    implicitHeight: 24
                    font.pixelSize: Config.fontSizeSmall
                    baseColor: Colors.surface
                    enabled: icaoInput.text.length >= 3 && !WeatherProvider.loading
                    onClicked: WeatherProvider.fetchNotam(icaoInput.text)
                }
            }

            // ── NOTAM list ──────────────────────────────────────────
            Repeater {
                model: WeatherProvider.notams

                delegate: Rectangle {
                    width: parent.width
                    color: Colors.surface
                    radius: Config.radiusSmall
                    border.color: Colors.border
                    height: 18 + textItem.implicitHeight
                    visible: index < 5

                    Column {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Text {
                            text: (modelData.type || "NOTAM") + "  " + (modelData.id || "")
                            font.pixelSize: Config.fontSizeSmall
                            font.bold: true
                            color: Colors.warning
                        }
                        Text {
                            id: textItem
                            text: modelData.text || ""
                            font.pixelSize: Config.fontSizeSmall
                            color: Colors.textSecondary
                            wrapMode: Text.WordWrap
                            width: parent.width
                            elide: Text.ElideRight
                            maximumLineCount: 3
                        }
                    }
                }
            }

            Text {
                text: qsTr("No active NOTAMs")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
                visible: WeatherProvider.notams.length === 0 && !WeatherProvider.loading
            }
        }
    }

    function directionLabel(deg) {
        var dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
        var idx = Math.round(deg / 45) % 8
        return dirs[idx]
    }
}
