import QtQuick
import QGroundControl

Loader {
    id: root

    property int criticalFailCount: 0
    property int manualFailCount: 0

    active: false
    source: "qrc:/qml/cpts/ArmGateDialog.qml"

    signal accepted
    signal closed
    signal reviewChecksRequested

    onLoaded: {
        item.criticalFailCount = root.criticalFailCount
        item.manualFailCount = root.manualFailCount
        item.accepted.connect(function() {
            root.accepted()
        })
        item.closed.connect(function() {
            root.closed()
            root.active = false
        })
        item.reviewChecksRequested.connect(function() {
            root.reviewChecksRequested()
            root.active = false
        })
        item.open()
    }
}
