// Component: VehicleTelemetry (Singleton)
// Purpose: Application-wide state singleton that bridges C++ TelemetryProvider signals to QML
//   properties. Manages vehicle selection, checklist state, stale-data detection, disconnect
//   guard, FAA Part 107 compliance records, and readiness-to-launch computation.
// Properties:
//   connectionQuality (int) — MAVLink connection quality (0–100)
//   vehicleType (string) — detected vehicle type string
//   heartbeatReceived (bool) — whether a heartbeat has been received
//   batteryVoltage (real) — current battery voltage
//   satellites (int) — number of GPS satellites
//   altitude (real) — relative altitude
//   groundSpeed (real) — ground speed
//   heading (real) — compass heading
//   rcRssi (int) — RC RSSI value
//   isConnected (bool) — link connection status
//   roll, pitch (real) — vehicle attitude
//   batteryCurrent (real) — battery current
//   armed (bool) — vehicle armed state
//   gpsFixTypeString (string) — GPS fix type label
//   servoOutputs (string) — servo output values string
//   motorOutputs (var) — motor output array
//   motorCount (int) — detected motor count
//   ekfVelVariance, ekfPosHorizVariance, ekfPosVertVariance, ekfCompassVariance,
//     ekfTerrainVariance, ekfAirspeedVariance (real) — EKF variance values
//   ekfStatus (string) — EKF status string
//   visibilityKm (real) — weather visibility
//   ceilingFt (int) — cloud ceiling
//   metarString (string) — raw METAR string
//   notams (var) — NOTAMs array
//   preArmOk (bool) — pre-arm check result
//   windSpeed, windDirection (real) — wind data
//   terrainHeight (real) — terrain height
//   flightTime (real) — total flight time
//   preArmMessage (string) — pre-arm failure message
//   preArmSeverity (string) — pre-arm severity level
//   gimbalDetected (bool), gimbalPitch/roll/yaw (real), gimbalMode (int),
//     gimbalCalibrating (bool) — gimbal state
//   batteryDataQuality (int), batteryStatusString (string) — battery quality
//   gpsDataQuality (int), gpsStatusString (string) — GPS quality
//   imuDataQuality (int), compassDataQuality (int), rcDataQuality (int) — sensor quality
//   hardwareSetupRequired (bool) — whether HW setup is needed
//   selectedVehicleType (string), selectedVehicle (string) — user selection
//   exportStatus (string) — compliance export status
//   pilotName (string), pilotLicense (string), aircraftReg (string) — Part 107 operator data
//   faaPart107Mode (bool) — FAA Part 107 compliance mode
//   digitalSignature (string), signatureTimestamp (string) — compliance signature
//     gpsReady, batteryGood, airspeedValid, transitionReady (bool) — computed readiness
//   allChecklistItemsChecked (bool) — all mandatory checks passed
//   payloadSecured (bool) — payload confirmation
//   airspaceClear (bool), windOk (bool), homePointSet (bool) — final checks
//   allFinalChecksPassed (bool) — all final checks passed
//   hardwareTestPassed (bool) — all hardware tests passed
//   mavlinkConnected (bool) — MAVLink connection established
//   readyToLaunch (bool) — all conditions satisfied for arming
//   staleTimeoutMs (int) — stale data timeout (5000ms)
//   lastTelemetryUpdate (var, Date) — timestamp of last telemetry update
//   telemetryStale (bool) — whether telemetry data is stale
//   disconnectGuardActive (bool) — whether disconnect guard is active
//   disconnectGuardMessage (string) — disconnect guard message
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors

pragma Singleton
import QtQuick

QtObject {
    // ── Telemetry (bridged from C++ TelemetryProvider) ───────────────
    property int connectionQuality: TelemetryProvider ? TelemetryProvider.connectionQuality : 0
    property string vehicleType: TelemetryProvider ? TelemetryProvider.vehicleType : ""
    property bool heartbeatReceived: TelemetryProvider ? TelemetryProvider.heartbeatReceived : false
    property real batteryVoltage: TelemetryProvider ? TelemetryProvider.batteryVoltage : 0.0
    property int satellites: TelemetryProvider ? TelemetryProvider.gpsSatellites : 0
    property real altitude: TelemetryProvider ? TelemetryProvider.altitudeRelative : 0.0
    property real groundSpeed: TelemetryProvider ? TelemetryProvider.groundSpeed : 0.0
    property real heading: TelemetryProvider ? TelemetryProvider.heading : 0.0
    property int rcRssi: TelemetryProvider ? TelemetryProvider.rcRssi : 0
    property bool isConnected: TelemetryProvider ? TelemetryProvider.isConnected : false
    property real roll: TelemetryProvider ? TelemetryProvider.roll : 0.0
    property real pitch: TelemetryProvider ? TelemetryProvider.pitch : 0.0
    property real batteryCurrent: TelemetryProvider ? TelemetryProvider.batteryCurrent : 0.0
    property real batteryCurrentAmps: TelemetryProvider ? TelemetryProvider.batteryCurrentAmps : 0.0
    property real batteryTemperature: TelemetryProvider ? TelemetryProvider.batteryTemperature : NaN
    property string batteryChargeState: TelemetryProvider ? TelemetryProvider.batteryChargeState : "OK"
    property real batteryTimeRemainingSeconds: TelemetryProvider ? TelemetryProvider.batteryTimeRemainingSeconds : -1
    property var batteryCellVoltages: TelemetryProvider ? TelemetryProvider.batteryCellVoltages : []
    property bool armed: TelemetryProvider ? TelemetryProvider.armed : false
    property string gpsFixTypeString: TelemetryProvider ? TelemetryProvider.gpsFixTypeString : "No GPS"
    property string servoOutputs: TelemetryProvider ? TelemetryProvider.servoOutputsString : ""
    property var motorOutputs: TelemetryProvider ? TelemetryProvider.motorOutputs : []
    property int motorCount: TelemetryProvider ? TelemetryProvider.motorCount : 0
    property real ekfVelVariance: TelemetryProvider ? TelemetryProvider.ekfVelVariance : 0.0
    property real ekfPosHorizVariance: TelemetryProvider ? TelemetryProvider.ekfPosHorizVariance : 0.0
    property real ekfPosVertVariance: TelemetryProvider ? TelemetryProvider.ekfPosVertVariance : 0.0
    property real ekfCompassVariance: TelemetryProvider ? TelemetryProvider.ekfCompassVariance : 0.0
    property real ekfTerrainVariance: TelemetryProvider ? TelemetryProvider.ekfTerrainVariance : 0.0
    property real ekfAirspeedVariance: TelemetryProvider ? TelemetryProvider.ekfAirspeedVariance : 0.0
    property string ekfStatus: TelemetryProvider ? TelemetryProvider.ekfStatus : "Unknown"
    property real visibilityKm: WeatherProvider ? WeatherProvider.visibilityKm : 0.0
    property int ceilingFt: WeatherProvider ? WeatherProvider.ceilingFt : 0
    property string metarString: WeatherProvider ? WeatherProvider.metarString : ""
    property var notams: WeatherProvider ? WeatherProvider.notams : []
    property bool preArmOk: TelemetryProvider ? TelemetryProvider.preArmOk : false
    property real windSpeed: TelemetryProvider ? TelemetryProvider.windSpeed : 0.0
    property real windDirection: TelemetryProvider ? TelemetryProvider.windDirection : 0.0
    property real terrainHeight: TelemetryProvider ? TelemetryProvider.terrainHeight : 0.0
    property real flightTime: TelemetryProvider ? TelemetryProvider.flightTime : 0.0
    property string preArmMessage: TelemetryProvider ? TelemetryProvider.preArmMessage : ""
    property string lastLogTimestamp: TelemetryProvider ? TelemetryProvider.lastLogTimestamp : ""
    property string preArmSeverity: TelemetryProvider ? TelemetryProvider.preArmSeverity : "info"
    property bool gimbalDetected: TelemetryProvider ? TelemetryProvider.gimbalDetected : false
    property real gimbalPitch: TelemetryProvider ? TelemetryProvider.gimbalPitch : 0.0
    property real gimbalRoll: TelemetryProvider ? TelemetryProvider.gimbalRoll : 0.0
    property real gimbalYaw: TelemetryProvider ? TelemetryProvider.gimbalYaw : 0.0
    property int gimbalMode: TelemetryProvider ? TelemetryProvider.gimbalMode : 1
    property bool gimbalCalibrating: TelemetryProvider ? TelemetryProvider.gimbalCalibrating : false

    // ── Sensor quality (bridged from C++ TelemetryProvider) ──────────
    property int batteryDataQuality: TelemetryProvider ? TelemetryProvider.batteryDataQuality : 2
    property string batteryStatusString: TelemetryProvider ? TelemetryProvider.batteryStatusString : ""
    property int gpsDataQuality: TelemetryProvider ? TelemetryProvider.gpsDataQuality : 2
    property string gpsStatusString: TelemetryProvider ? TelemetryProvider.gpsStatusString : ""
    property int imuDataQuality: TelemetryProvider ? TelemetryProvider.imuDataQuality : 3
    property int compassDataQuality: TelemetryProvider ? TelemetryProvider.compassDataQuality : 3
    property int rcDataQuality: TelemetryProvider ? TelemetryProvider.rcDataQuality : 2
    property bool hardwareSetupRequired: TelemetryProvider ? TelemetryProvider.hardwareSetupRequired : false

    // ── Selection state ──────────────────────────────────────────────
    property string selectedVehicleType: ""
    property string selectedVehicle: ""
    property string exportStatus: ""

    // ── Compliance / FAA Part 107 ────────────────────────────────────
    property string pilotName: ""
    property string pilotLicense: ""
    property string aircraftReg: ""
    property bool faaPart107Mode: false
    property string digitalSignature: ""
    property string signatureTimestamp: ""

    readonly property string selectedVehicleTypeLabel: {
        if (selectedVehicleType === "Quad") return "Quadcopter"
        if (selectedVehicleType === "FixedWing") return "Fixed-Wing"
        return "VTOL"
    }

    property var availableVehicles: [
        { name: "Quadrotor X1", type: "Quad" },
        { name: "FixedWing Falcon", type: "FixedWing" },
        { name: "Hybrid VTOL V2", type: "VTOL" }
    ]

    // ── Available vehicles ────────────────────────────────────────────

    function selectVehicle(name, type) {
        selectedVehicle = name
        selectedVehicleType = type
        if (typeof ChecklistModel !== "undefined" && ChecklistModel !== null)
            ChecklistModel.loadTemplateForType(type)
    }

    // ── Computed properties ──────────────────────────────────────────
    readonly property bool gpsReady: satellites >= 8
    readonly property bool batteryGood: batteryVoltage >= 15
    readonly property bool airspeedValid: selectedVehicleType === "FixedWing" || selectedVehicleType === "VTOL" ? groundSpeed >= 20 : true
    readonly property bool transitionReady: selectedVehicleType === "VTOL" ? (batteryGood && gpsReady && airspeedValid) : true

    readonly property bool allChecklistItemsChecked:
        typeof ChecklistEngine !== "undefined" && ChecklistEngine !== null
            ? ChecklistEngine.allMandatoryPassed
            : false

    // ── Payload & Final Checks ───────────────────────────────────────
    property bool payloadSecured: false

    property bool airspaceClear: true
    property bool windOk: true
    property bool homePointSet: true
    readonly property bool allFinalChecksPassed: airspaceClear && windOk && homePointSet

    readonly property bool hardwareTestPassed:
        typeof HardwareTestController !== "undefined" && HardwareTestController !== null
            ? HardwareTestController.allPassed
            : true

    readonly property bool mavlinkConnected:
        typeof TelemetryProvider !== "undefined" && TelemetryProvider !== null
            ? TelemetryProvider.isConnected
            : false

    readonly property bool readyToLaunch:
        mavlinkConnected
        && allChecklistItemsChecked
        && payloadSecured
        && allFinalChecksPassed
        && hardwareTestPassed

    // ── Compliance record builder ────────────────────────────────────
    function buildComplianceRecord() {
        var items = []
        if (typeof PreflightManager !== "undefined" && PreflightManager !== null && PreflightManager.totalChecks > 0) {
            var checks = PreflightManager.checks
            for (var i = 0; i < checks.length; i++) {
                var c = checks[i]
                items.push({
                    name: c.id,
                    type: c.checkType === 0 ? "auto" : "manual",
                    status: c.status,
                    required: true
                })
            }
        }

        var ts = Qt.formatDateTime(new Date(), "yyyy-MM-ddTHH:mm:ssZ")

        var record = {
            timestamp: ts,
            vehicleName: selectedVehicle,
            vehicleType: selectedVehicleTypeLabel,
            telemetry: {
                batteryVoltage: batteryVoltage,
                batteryPercent: TelemetryProvider ? TelemetryProvider.batteryPercent : 0,
                satellites: satellites,
                altitude: altitude,
                groundSpeed: groundSpeed,
                heading: heading,
                rcRssi: rcRssi,
                gpsLatitude: TelemetryProvider ? TelemetryProvider.gpsLatitude : 0,
                gpsLongitude: TelemetryProvider ? TelemetryProvider.gpsLongitude : 0,
                imuHealthy: TelemetryProvider ? TelemetryProvider.imuHealthy : false,
                compassHealthy: TelemetryProvider ? TelemetryProvider.compassHealthy : false,
                flightMode: TelemetryProvider ? TelemetryProvider.flightMode : "Unknown",
                temperature: WeatherProvider ? WeatherProvider.temperature : 0,
                windSpeed: WeatherProvider ? WeatherProvider.windSpeed : 0,
                windDirection: WeatherProvider ? WeatherProvider.windDirection : 0,
                weatherDescription: WeatherProvider ? WeatherProvider.weatherDescription : "",
                visibility: WeatherProvider ? WeatherProvider.visibility : 0
            },
            payloadSecured: payloadSecured,
            finalChecks: {
                airspaceClear: airspaceClear,
                windOk: windOk,
                homePointSet: homePointSet,
                allFinalChecksPassed: allFinalChecksPassed
            },
            checklist: items,
            connectionStatus: TelemetryProvider ? TelemetryProvider.connectionStatus : "N/A",
            simulationMode: false,
            mavlinkUrl: TelemetryProvider ? TelemetryProvider.connectionUrl : "",
            autopilotType: TelemetryProvider ? TelemetryProvider.autopilotType : "Unknown",
            readyToLaunch: readyToLaunch,
            result: readyToLaunch ? "pass" : "fail"
        }

        // Add Part 107 compliance info
        if (faaPart107Mode) {
            record.faaPart107 = {
                pilotName: pilotName,
                pilotLicense: pilotLicense,
                aircraftReg: aircraftReg,
                faaPart107Mode: true
            }
        }

        // Generate digital signature (SHA-256 of JSON + timestamp)
        var jsonStr = JSON.stringify(record)
        if (typeof ExportHelper !== "undefined" && ExportHelper !== null) {
            digitalSignature = ExportHelper.generateHash(jsonStr)
            signatureTimestamp = ts
            record.digitalSignature = digitalSignature
            record.signatureTimestamp = signatureTimestamp
        }

        return record
    }

    // ── Stale-monitor timer ──────────────────────────────────────────
    property Timer _staleTimer: Timer {
        interval: 1000
        repeat: true
        running: isConnected
        onTriggered: {
            if (telemetryStale && !disconnectGuardActive)
                activateDisconnectGuard()
            else if (!telemetryStale && disconnectGuardActive)
                deactivateDisconnectGuard()
        }
    }

    // ── Update timestamp on any telemetry change ─────────────────────
    function _touchTelemetry() {
        lastTelemetryUpdate = new Date()
    }

    // ── Initialization ───────────────────────────────────────────────
    Component.onCompleted: {
        if (typeof ChecklistModel !== "undefined" && ChecklistModel !== null) {
            ChecklistModel.loadTemplateForType(selectedVehicleType)
        }
        // Load persisted operator settings
        if (typeof PreflightSettingsManager !== "undefined" && PreflightSettingsManager !== null) {
            pilotName = PreflightSettingsManager.pilotName
            pilotLicense = PreflightSettingsManager.pilotLicense
            aircraftReg = PreflightSettingsManager.aircraftReg
            faaPart107Mode = PreflightSettingsManager.faaPart107Mode
        }
    }

    // ── Stale-data detection ──────────────────────────────────────────
    readonly property int staleTimeoutMs: 5000
    property var lastTelemetryUpdate: new Date(0)
    property bool telemetryStale: {
        if (!isConnected) return false
        var elapsed = new Date() - lastTelemetryUpdate
        return elapsed > staleTimeoutMs
    }

    // ── Disconnect guard ──────────────────────────────────────────────
    property bool disconnectGuardActive: false
    property string disconnectGuardMessage: ""

    function activateDisconnectGuard() {
        disconnectGuardActive = true
        disconnectGuardMessage = "Connection lost — checks suspended"
    }

    function deactivateDisconnectGuard() {
        disconnectGuardActive = false
        disconnectGuardMessage = ""
    }

    // ── Live telemetry bridge (C++ TelemetryProvider → QML properties)
    // The Connections block reacts to C++ signal emissions and updates
    // the QML-side properties, keeping the TelemetryBar and other
    // consumers automatically in sync.
    property Connections _telemetryBridge: Connections {
        target: typeof TelemetryProvider !== "undefined" ? TelemetryProvider : null

        function onConnectionQualityChanged() {
            connectionQuality = TelemetryProvider.connectionQuality
            _touchTelemetry()
        }
        function onHeartbeatReceivedChanged() {
            heartbeatReceived = TelemetryProvider.heartbeatReceived
            _touchTelemetry()
        }
        function onVehicleTypeChanged() {
            vehicleType = TelemetryProvider.vehicleType
            var vt = TelemetryProvider.vehicleType
            if (vt === "Quad" || vt === "FixedWing" || vt === "VTOL") {
                selectedVehicleType = vt
                if (typeof ChecklistModel !== "undefined" && ChecklistModel !== null) {
                    ChecklistModel.loadTemplateForType(vt)
                    if (typeof ChecklistEngine !== "undefined" && ChecklistEngine !== null)
                        ChecklistEngine.reevaluateAll()
                }
            }
        }
        function onBatteryVoltageChanged() {
            batteryVoltage = TelemetryProvider.batteryVoltage
            _touchTelemetry()
        }
        function onTelemetryUpdated() {
            _touchTelemetry()
        }
        function onGpsSatellitesChanged() {
            satellites = TelemetryProvider.gpsSatellites
        }
        function onAltitudeChanged() {
            altitude = TelemetryProvider.altitudeRelative
        }
        function onGroundSpeedChanged() {
            groundSpeed = TelemetryProvider.groundSpeed
        }
        function onHeadingChanged() {
            heading = TelemetryProvider.heading
        }
        function onRcRssiChanged() {
            rcRssi = TelemetryProvider.rcRssi
        }
        function onIsConnectedChanged() {
            isConnected = TelemetryProvider.isConnected
            lastTelemetryUpdate = new Date()
            if (isConnected)
                deactivateDisconnectGuard()
            else
                activateDisconnectGuard()
        }
        function onAttitudeChanged() {
            roll = TelemetryProvider.roll
            pitch = TelemetryProvider.pitch
        }
        function onBatteryCurrentChanged() {
            batteryCurrent = TelemetryProvider.batteryCurrent
        }
        function onBatteryCurrentAmpsChanged() {
            batteryCurrentAmps = TelemetryProvider.batteryCurrentAmps
        }
        function onBatteryTemperatureChanged() {
            batteryTemperature = TelemetryProvider.batteryTemperature
        }
        function onBatteryChargeStateChanged() {
            batteryChargeState = TelemetryProvider.batteryChargeState
        }
        function onBatteryTimeRemainingSecondsChanged() {
            batteryTimeRemainingSeconds = TelemetryProvider.batteryTimeRemainingSeconds
        }
        function onBatteryCellVoltagesChanged() {
            batteryCellVoltages = TelemetryProvider.batteryCellVoltages
        }
        function onArmedChanged() {
            armed = TelemetryProvider.armed
        }
        function onGpsFixTypeChanged() {
            gpsFixTypeString = TelemetryProvider.gpsFixTypeString
            satellites = TelemetryProvider.gpsSatellites
        }
        function onServoOutputsChanged() {
            servoOutputs = TelemetryProvider.servoOutputsString
            motorOutputs = TelemetryProvider.motorOutputs
        }
        function onMotorCountChanged() {
            motorCount = TelemetryProvider.motorCount
        }
        function onEkfVarianceChanged() {
            ekfVelVariance = TelemetryProvider.ekfVelVariance
            ekfPosHorizVariance = TelemetryProvider.ekfPosHorizVariance
            ekfPosVertVariance = TelemetryProvider.ekfPosVertVariance
            ekfCompassVariance = TelemetryProvider.ekfCompassVariance
            ekfTerrainVariance = TelemetryProvider.ekfTerrainVariance
            ekfAirspeedVariance = TelemetryProvider.ekfAirspeedVariance
        }
        function onEkfStatusChanged() {
            ekfStatus = TelemetryProvider.ekfStatus
        }
        function onPreArmOkChanged() {
            preArmOk = TelemetryProvider.preArmOk
        }
        function onWindChanged() {
            windSpeed = TelemetryProvider.windSpeed
            windDirection = TelemetryProvider.windDirection
        }
        function onTerrainHeightChanged() {
            terrainHeight = TelemetryProvider.terrainHeight
        }
        function onFlightTimeChanged() {
            flightTime = TelemetryProvider.flightTime
        }
        function onPreArmMessageChanged() {
            preArmMessage = TelemetryProvider.preArmMessage
        }
        function onLastLogTimestampChanged() {
            lastLogTimestamp = TelemetryProvider.lastLogTimestamp
        }
        function onPreArmSeverityChanged() {
            preArmSeverity = TelemetryProvider.preArmSeverity
        }
        function onGimbalDetectedChanged() {
            gimbalDetected = TelemetryProvider.gimbalDetected
        }
        function onGimbalAttitudeChanged() {
            gimbalPitch = TelemetryProvider.gimbalPitch
            gimbalRoll = TelemetryProvider.gimbalRoll
            gimbalYaw = TelemetryProvider.gimbalYaw
        }
        function onGimbalModeChanged() {
            gimbalMode = TelemetryProvider.gimbalMode
        }
        function onGimbalCalibratingChanged() {
            gimbalCalibrating = TelemetryProvider.gimbalCalibrating
        }
        function onBatteryDataQualityChanged() {
            batteryDataQuality = TelemetryProvider.batteryDataQuality
            batteryStatusString = TelemetryProvider.batteryStatusString
        }
        function onBatteryStatusStringChanged() {
            batteryStatusString = TelemetryProvider.batteryStatusString
        }
        function onGpsDataQualityChanged() {
            gpsDataQuality = TelemetryProvider.gpsDataQuality
            gpsStatusString = TelemetryProvider.gpsStatusString
        }
        function onGpsStatusStringChanged() {
            gpsStatusString = TelemetryProvider.gpsStatusString
        }
        function onImuDataQualityChanged() {
            imuDataQuality = TelemetryProvider.imuDataQuality
        }
        function onCompassDataQualityChanged() {
            compassDataQuality = TelemetryProvider.compassDataQuality
        }
        function onRcDataQualityChanged() {
            rcDataQuality = TelemetryProvider.rcDataQuality
        }
        function onHardwareSetupRequiredChanged() {
            hardwareSetupRequired = TelemetryProvider.hardwareSetupRequired
        }
    }
}
