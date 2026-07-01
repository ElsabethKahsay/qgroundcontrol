import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    color: "#1A0A2E"
    radius: 8
    border.color: "#9B59B6"
    border.width: 1

    property var clipboardHelper: null

    readonly property double decimalLat: parseFloat(latDecimalField.text) || 0
    readonly property double decimalLon: parseFloat(lonDecimalField.text) || 0

    readonly property int dmsLatDeg: latDegSpin.value
    readonly property int dmsLatMin: latMinSpin.value
    readonly property double dmsLatSec: latSecSpin.value / 1000.0
    readonly property string dmsLatDir: latDirCombo.currentText
    readonly property int dmsLonDeg: lonDegSpin.value
    readonly property int dmsLonMin: lonMinSpin.value
    readonly property double dmsLonSec: lonSecSpin.value / 1000.0
    readonly property string dmsLonDir: lonDirCombo.currentText

    readonly property double dmsToDecimalLat: computeDmsToDecimal(dmsLatDeg, dmsLatMin, dmsLatSec, dmsLatDir)
    readonly property double dmsToDecimalLon: computeDmsToDecimal(dmsLonDeg, dmsLonMin, dmsLonSec, dmsLonDir)

    function computeDmsToDecimal(deg, min, sec, dir) {
        var val = deg + min / 60.0 + sec / 3600.0
        if (dir === "S" || dir === "W")
            val = -val
        return val
    }

    readonly property bool valuesMatch: {
        var eps = 1e-6
        return Math.abs(decimalLat - dmsToDecimalLat) < eps && Math.abs(decimalLon - dmsToDecimalLon) < eps
    }

    function decimalToDmsLat(decimal) {
        var dir = decimal >= 0 ? "N" : "S"
        var abs = Math.abs(decimal)
        var deg = Math.floor(abs)
        var minFull = (abs - deg) * 60
        var min = Math.floor(minFull)
        var sec = (minFull - min) * 60
        return deg + "\u00B0 " + min + "' " + sec.toFixed(3) + "\" " + dir
    }

    function decimalToDmsLon(decimal) {
        var dir = decimal >= 0 ? "E" : "W"
        var abs = Math.abs(decimal)
        var deg = Math.floor(abs)
        var minFull = (abs - deg) * 60
        var min = Math.floor(minFull)
        var sec = (minFull - min) * 60
        return deg + "\u00B0 " + min + "' " + sec.toFixed(3) + "\" " + dir
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "DMS Converter"
                font.pixelSize: 18
                font.bold: true
                color: "#FFFFFF"
            }
            Item { Layout.fillWidth: true }

            Rectangle {
                visible: root.valuesMatch
                color: "#1B5E20"
                radius: 4
                Layout.preferredWidth: 120
                Layout.preferredHeight: 24
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 4
                    Text { text: "\u2713"; color: "#2ECC71"; font.bold: true; font.pixelSize: 14 }
                    Text { text: "Values match"; color: "#2ECC71"; font.pixelSize: 11; font.bold: true }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#2D1B4E"
                radius: 8
                border.color: "#9B59B6"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        text: "Decimal Degrees"
                        font.pixelSize: 14
                        font.bold: true
                        color: "#E91E63"
                    }

                    Text { text: "Latitude"; font.pixelSize: 12; color: "#F8BBD0" }
                    TextField {
                        id: latDecimalField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        text: "33.125055"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.family: "monospace"
                        background: Rectangle {
                            color: "#1A0A2E"
                            border.color: latDecimalField.activeFocus ? "#E91E63" : "#9B59B6"
                            border.width: 1
                            radius: 4
                        }
                        validator: DoubleValidator { bottom: -90.0; top: 90.0; decimals: 8 }
                    }

                    Text { text: "Longitude"; font.pixelSize: 12; color: "#F8BBD0" }
                    TextField {
                        id: lonDecimalField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        text: "118.852360"
                        color: "#FFFFFF"
                        font.pixelSize: 14
                        font.family: "monospace"
                        background: Rectangle {
                            color: "#1A0A2E"
                            border.color: lonDecimalField.activeFocus ? "#E91E63" : "#9B59B6"
                            border.width: 1
                            radius: 4
                        }
                        validator: DoubleValidator { bottom: -180.0; top: 180.0; decimals: 8 }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        color: "#1A0A2E"
                        radius: 4
                        border.color: "#9B59B6"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2

                            Text {
                                text: "Lat: " + decimalToDmsLat(decimalLat)
                                font.pixelSize: 12
                                color: "#FFFFFF"
                                font.family: "monospace"
                            }
                            Text {
                                text: "Lon: " + decimalToDmsLon(decimalLon)
                                font.pixelSize: 12
                                color: "#FFFFFF"
                                font.family: "monospace"
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (clipboardHelper)
                                    clipboardHelper.copyToClipboard(decimalToDmsLat(decimalLat) + " " + decimalToDmsLon(decimalLon))
                            }
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }

            Rectangle {
                width: 2
                Layout.fillHeight: true
                color: "#9B59B6"
                opacity: 0.5
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#2D1B4E"
                radius: 8
                border.color: "#9B59B6"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        text: "Degrees Minutes Seconds"
                        font.pixelSize: 14
                        font.bold: true
                        color: "#9B59B6"
                    }

                    Text { text: "Latitude"; font.pixelSize: 12; color: "#F8BBD0" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        SpinBox {
                            id: latDegSpin
                            from: 0; to: 90; value: 33
                            Layout.preferredWidth: 56
                        }
                        Text { text: "\u00B0"; color: "#F8BBD0" }

                        SpinBox {
                            id: latMinSpin
                            from: 0; to: 59; value: 7
                            Layout.preferredWidth: 56
                        }
                        Text { text: "'"; color: "#F8BBD0" }

                        SpinBox {
                            id: latSecSpin
                            from: 0; to: 59999; value: 30198; stepSize: 1
                            Layout.preferredWidth: 80
                            contentItem: TextInput {
                                text: (latSecSpin.value / 1000.0).toFixed(3)
                                color: "#FFFFFF"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 13
                            }
                        }
                        Text { text: "\""; color: "#F8BBD0" }

                        ComboBox {
                            id: latDirCombo
                            model: ["N", "S"]
                            currentIndex: 0
                            Layout.preferredWidth: 48
                            background: Rectangle { color: "#1A0A2E"; border.color: "#9B59B6"; border.width: 1; radius: 4 }
                            contentItem: Text {
                                text: latDirCombo.currentText
                                color: "#FFFFFF"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Text { text: "Longitude"; font.pixelSize: 12; color: "#F8BBD0" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        SpinBox {
                            id: lonDegSpin
                            from: 0; to: 180; value: 118
                            Layout.preferredWidth: 56
                        }
                        Text { text: "\u00B0"; color: "#F8BBD0" }

                        SpinBox {
                            id: lonMinSpin
                            from: 0; to: 59; value: 51
                            Layout.preferredWidth: 56
                        }
                        Text { text: "'"; color: "#F8BBD0" }

                        SpinBox {
                            id: lonSecSpin
                            from: 0; to: 59999; value: 8496; stepSize: 1
                            Layout.preferredWidth: 80
                            contentItem: TextInput {
                                text: (lonSecSpin.value / 1000.0).toFixed(3)
                                color: "#FFFFFF"
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 13
                            }
                        }
                        Text { text: "\""; color: "#F8BBD0" }

                        ComboBox {
                            id: lonDirCombo
                            model: ["E", "W"]
                            currentIndex: 0
                            Layout.preferredWidth: 48
                            background: Rectangle { color: "#1A0A2E"; border.color: "#9B59B6"; border.width: 1; radius: 4 }
                            contentItem: Text {
                                text: lonDirCombo.currentText
                                color: "#FFFFFF"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        color: "#1A0A2E"
                        radius: 4
                        border.color: "#9B59B6"
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2

                            Text {
                                text: "Lat: " + dmsToDecimalLat.toFixed(8)
                                font.pixelSize: 12
                                color: "#FFFFFF"
                                font.family: "monospace"
                            }
                            Text {
                                text: "Lon: " + dmsToDecimalLon.toFixed(8)
                                font.pixelSize: 12
                                color: "#FFFFFF"
                                font.family: "monospace"
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (clipboardHelper)
                                    clipboardHelper.copyToClipboard(dmsToDecimalLat.toFixed(8) + " " + dmsToDecimalLon.toFixed(8))
                            }
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }
        }
    }
}
