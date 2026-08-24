pragma Singleton
import QtQuick

// Component: DistanceTracker
// Purpose: Great-circle distance & bearing from the drone to a user-supplied
//          target location.  Recomputes automatically whenever the primary
//          GPS position updates (TelemetryProvider.gpsPositionChanged).
// Units:   distanceM in metres, bearingStr in degrees true (0° = North).
QtObject {
    id: root

    property bool   hasTarget:   false
    property double targetLat:   0
    property double targetLon:   0
    property double distanceM:   0
    property string distanceStr: "—"
    property string bearingStr:  "—"

    function setTarget(lat, lon) {
        if (isNaN(lat) || isNaN(lon)) {
            // Empty / partially-typed or invalid input — hide the readout.
            hasTarget = false
            distanceStr = "—"
            bearingStr = "—"
            return
        }
        targetLat = lat
        targetLon = lon
        hasTarget = true
        root.update(TelemetryProvider.gpsLatitude, TelemetryProvider.gpsLongitude)
    }

    // Haversine distance + initial bearing from the drone to the target.
    function update(droneLat, droneLon) {
        if (!hasTarget || isNaN(droneLat) || isNaN(droneLon))
            return
        var R  = 6371000
        var φ1 = droneLat  * Math.PI / 180
        var φ2 = root.targetLat * Math.PI / 180
        var Δφ = (root.targetLat - droneLat) * Math.PI / 180
        var Δλ = (root.targetLon - droneLon) * Math.PI / 180
        var a  = Math.sin(Δφ / 2) * Math.sin(Δφ / 2)
               + Math.cos(φ1) * Math.cos(φ2) * Math.sin(Δλ / 2) * Math.sin(Δλ / 2)
        distanceM  = R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a))
        distanceStr = distanceM < 1000
            ? distanceM.toFixed(0) + " m"
            : (distanceM / 1000).toFixed(2) + " km"
        var y    = Math.sin(Δλ) * Math.cos(φ2)
        var x    = Math.cos(φ1) * Math.sin(φ2) - Math.sin(φ1) * Math.cos(φ2) * Math.cos(Δλ)
        var brng = (Math.atan2(y, x) * 180 / Math.PI + 360) % 360
        bearingStr = brng.toFixed(0) + "°"
    }

    // QtObject has no default child property, so hold the Connections explicitly.
    property Connections telemetryConn: Connections {
        target: TelemetryProvider
        function onGpsPositionChanged() {
            root.update(TelemetryProvider.gpsLatitude, TelemetryProvider.gpsLongitude)
        }
    }
}
