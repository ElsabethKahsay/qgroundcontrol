import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.uav.preflight 1.0

// ── Battery Time Estimator ───────────────────────────────────────────────────
// Configures the battery pack (chemistry, S/P/mAh) for the connected vehicle and
// displays the live flight-time estimate.  All values persist to the vehicle
// profile via VehicleProfileManager (and from there to the database).
//
// The live calculation itself runs in VehicleProfileManager::_updateLiveEstimate()
// every 2 s from TelemetryBridge telemetry:
//   capacity_Ah   = N_parallel × cell_mAh / 1000
//   remaining_Ah  = capacity_Ah × (SOC / 100)
//   usable_Ah     = remaining_Ah × (1 - 0.20)        // 20% reserve always
//   time_min      = usable_Ah / I_smooth × 60, gated on armed && I_smooth >= 3 A
// and is delivered to the FlyView chip through VehicleProfileManager.liveSOC /
// .liveTimeMins, so the estimator panel and the chip always agree.
Rectangle {
    id: root

    radius: Config.radiusMedium
    color: Colors.surface
    border.color: Colors.border
    border.width: 1
    implicitHeight: estCol.implicitHeight + Config.spacingMedium * 2

    // Emitted when the operator dismisses the panel from the FlyView overlay.
    signal closeRequested()

    // Live data delivered by VehicleProfileManager (updated every 2 s).
    readonly property int    liveSOC:  VehicleProfileManager ? VehicleProfileManager.liveSOC : -1
    readonly property double liveMin:  VehicleProfileManager ? VehicleProfileManager.liveTimeMins : -1

    // Cell voltage per chemistry: LiPo=3.7/4.2, LiIon=3.6/4.1, LiHV=3.8/4.35.
    readonly property var __nomV: [3.7, 3.6, 3.8]
    readonly property var __maxV: [4.2, 4.1, 4.35]

    ColumnLayout {
        id: estCol
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Config.spacingMedium }
        spacing: Config.spacingMedium

        // ── Header ──
        RowLayout {
            Layout.fillWidth: true
            spacing: Config.spacingSmall

            Text {
                text: "\u23F1 Battery Time Estimator"
                font.pixelSize: Config.fontSizeBody
                font.bold: true
                color: Colors.textPrimary
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("Reserve: 20%")
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
            }
            Rectangle {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                radius: 11
                color: closeMa.containsMouse ? Colors.surfaceLight : "transparent"
                border.color: Colors.border
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "\u00D7"
                    font.bold: true
                    font.pixelSize: Config.fontSizeBody
                    color: Colors.textSecondary
                }
                MouseArea {
                    id: closeMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeRequested()
                }
            }
        }

        // ── Battery type selector ──
        RowLayout {
            width: parent.width
            spacing: Config.spacingSmall

            Text { text: qsTr("Type:"); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary; Layout.preferredWidth: 92 }
            ComboBox {
                id: battTypeBox
                model: ["LiPo", "LiIon", "LiHV"]
                font.pixelSize: Config.fontSizeSmall
                onCurrentIndexChanged: estimator.calculate()
            }
            Text {
                text: qsTr("%1V nom / %2V max").arg(root.__nomV[battTypeBox.currentIndex].toFixed(1))
                              .arg(root.__maxV[battTypeBox.currentIndex].toFixed(2))
                font.pixelSize: Config.fontSizeSmall
                color: Colors.textSecondary
            }
        }

        // ── Cell config: S / P / mAh ──
        RowLayout {
            width: parent.width
            spacing: Config.spacingSmall

            Text { text: qsTr("Cells:"); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary; Layout.preferredWidth: 92 }
            SpinBox { id: seriesBox;   from:1; to:14; value:6; onValueChanged: estimator.calculate() }
            Text { text: qsTr("S \u00D7"); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
            SpinBox { id: parallelBox; from:1; to:10; value:1; onValueChanged: estimator.calculate() }
            Text { text: qsTr("P \u00D7"); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
            SpinBox { id: cellMahBox;  from:500; to:30000; value:5000; stepSize:500; editable:true; onValueChanged: estimator.calculate() }
            Text { text: qsTr("mAh"); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
        }

        // ── Full-capacity display (read-only) ──
        Text {
            Layout.fillWidth: true
            font.pixelSize: Config.fontSizeSmall
            font.bold: true
            color: Colors.textPrimary
            text: {
                var nominalV  = seriesBox.value  * root.__nomV[battTypeBox.currentIndex]
                var maxV      = seriesBox.value  * root.__maxV[battTypeBox.currentIndex]
                var capacityAh = parallelBox.value * cellMahBox.value / 1000.0
                return qsTr("%1S \u00D7 %2P \u00D7 %3 mAh = %4 Ah \u00B7 %5V nom \u00B7 %6V max")
                    .arg(seriesBox.value).arg(parallelBox.value).arg(cellMahBox.value)
                    .arg(capacityAh.toFixed(2)).arg(nominalV.toFixed(1)).arg(maxV.toFixed(2))
            }
        }

        // ── Live estimate card ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: liveCol.implicitHeight + Config.spacingMedium * 2
            radius: Config.radiusSmall
            color: Colors.surfaceLight
            border.color: root.liveMin < 0 ? Colors.border
                        : root.liveMin < 5  ? Colors.stateFail
                        : root.liveMin < 10 ? Colors.checkWarn
                                            : Colors.statePass
            border.width: 1.5

            ColumnLayout {
                id: liveCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Config.spacingMedium }
                spacing: Config.spacingSmall

                Text { text: qsTr("LIVE ESTIMATE"); font.pixelSize: Config.fontSizeSmall; color: Colors.textSecondary }
                Text {
                    text: {
                        if (root.liveSOC < 0)   return "? \u00B7 \u2014"          // FC not configured
                        if (root.liveMin < 0)   return root.liveSOC + "% \u00B7 \u2014"  // disarmed / low current
                        return root.liveSOC + "% \u00B7 ~" + root.liveMin.toFixed(0) + " min"
                    }
                    font.bold: true
                    font.pixelSize: Config.fontSizeH2
                    color: root.liveMin < 0 ? Colors.textSecondary
                         : root.liveMin < 5  ? Colors.stateFail
                         : root.liveMin < 10 ? Colors.checkWarn
                                             : Colors.statePass
                }
                Text {
                    font.pixelSize: Config.fontSizeSmall
                    color: Colors.textSecondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    text: root.liveSOC < 0 ? qsTr("Battery SOC not reported by flight controller.")
                         : root.liveMin < 0 ? qsTr("Arm and draw \u2265 3.0 A to estimate flight time.")
                                           : qsTr("Usable capacity (20% reserve) at current draw.")
                }
            }
        }

        // ── Live decision taps to the FlyView chip ──
        Text {
            Layout.fillWidth: true
            font.pixelSize: Config.fontSizeSmall - 1
            font.italic: true
            color: Colors.textDisabled
            wrapMode: Text.WordWrap
            text: qsTr("FlyView chip uses the same SOC / duration as this panel (refreshes every 2 s).")
        }
    }

    // ── Persist model ──
    QtObject {
        id: proxy

        readonly property var model: ["LiPo", "LiIon", "LiHV"]

        // Map a type string to its index in the ComboBox model (defaults to LiPo).
        function indexOfType(type) {
            var idx = proxy.model.indexOf(type)
            return idx >= 0 ? idx : 0
        }
    }

    // ── Estimator binding ──
    QtObject {
        id: estimator

        function calculate() {
            if (typeof VehicleProfileManager === "undefined" || !VehicleProfileManager) return
            VehicleProfileManager.batteryType = battTypeBox.currentText
            VehicleProfileManager.batterySeriesCells = seriesBox.value
            VehicleProfileManager.batteryParallelCells = parallelBox.value
            VehicleProfileManager.batteryCellMah = cellMahBox.value
        }

        // Sync the controls from the persisted profile (idempotent: setters early-out).
        function loadFromProfile() {
            if (typeof VehicleProfileManager === "undefined" || !VehicleProfileManager) return
            var tIdx = proxy.indexOfType(VehicleProfileManager.batteryType)
            if (battTypeBox.currentIndex !== tIdx) battTypeBox.currentIndex = tIdx
            if (seriesBox.value !== VehicleProfileManager.batterySeriesCells) seriesBox.value   = VehicleProfileManager.batterySeriesCells
            if (parallelBox.value !== VehicleProfileManager.batteryParallelCells) parallelBox.value = VehicleProfileManager.batteryParallelCells
            if (cellMahBox.value !== VehicleProfileManager.batteryCellMah) cellMahBox.value  = VehicleProfileManager.batteryCellMah
        }
    }

    // Reload persisted config whenever a (new) vehicle becomes current or profile changes.
    Connections {
        target: typeof VehicleProfileManager !== "undefined" ? VehicleProfileManager : null
        function onCurrentVehicleChanged() { estimator.loadFromProfile() }
        function onBatteryConfigChanged()  { estimator.loadFromProfile() }
        function onLiveDataChanged()       { /* binding updates automatically */ }
    }

    Component.onCompleted: {
        estimator.loadFromProfile()
        estimator.calculate()
    }
}
