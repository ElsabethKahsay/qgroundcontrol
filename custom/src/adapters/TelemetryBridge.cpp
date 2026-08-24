/// @file TelemetryBridge.cpp
/// @brief Implementation of the TelemetryBridge telemetry adapter.
///
/// This file wires up all Vehicle Fact signals, raw MAVLink message handlers,
/// and a periodic 200ms polling timer to keep cached telemetry values current.
/// MAVLink message decoding is handled in _handleMavlinkMessage() via a switch
/// on message.msgid. Connection quality is computed from heartbeat arrival timing
/// combined with the communication drop rate.

#include <QDateTime>
#include <QDebug>
#include <QLoggingCategory>

#include "TelemetryBridge.h"

#include "FactSystem/Fact.h"
#include "FactSystem/FactMetaData.h"
#include "FactSystem/ParameterManager.h"
#include "MissionManager/PlanManager.h"
#include "MultiVehicleManager.h"
#include "MissionManager/MissionManager.h"
#include "QGCApplication.h"
#include "Vehicle/Vehicle.h"
#include "Vehicle/FactGroups/VehicleBatteryFactGroup.h"
#include "QmlObjectListModel.h"
#include "Vehicle/FactGroups/VehicleEstimatorStatusFactGroup.h"
#include "Vehicle/FactGroups/VehicleVibrationFactGroup.h"
#include "Vehicle/FactGroups/VehicleWindFactGroup.h"

#include "core/ParameterWatchlist.h"
#include "utils/Config.h"

#include <cmath>

/// Logging category for TelemetryBridge debug/warning output.
Q_LOGGING_CATEGORY(telemetryBridgeLog, "telemetry.bridge")

TelemetryBridge::TelemetryBridge(QObject *parent)
    : QObject(parent)
{
    // Fire the polling timer every 200ms to pick up values that aren't pushed via signals.
    _updateTimer.setInterval(kTelemetryUpdateMs);
    connect(&_updateTimer, &QTimer::timeout, this, &TelemetryBridge::_onTelemetryUpdate);
}

void TelemetryBridge::setVehicle(Vehicle* vehicle)
{
    if (_vehicle == vehicle) return;

    // Disconnect all signals from the previous vehicle before switching.
    if (_vehicle) {
        disconnect(_vehicle, nullptr, this, nullptr);
    }

    _vehicle = vehicle;

    if (_vehicle) {
        _connectVehicleSignals();
        _updateTimer.start();
        emit isConnectedChanged();
    } else {
        // No vehicle: stop polling and reset connection quality to zero.
        _updateTimer.stop();
        _connectionQuality = 0;
        emit connectionQualityChanged();
        emit isConnectedChanged();
    }
}

bool TelemetryBridge::isConnected() const
{
    return _vehicle != nullptr;
}

bool TelemetryBridge::armed() const
{
    return _vehicle ? _vehicle->armed() : false;
}

QString TelemetryBridge::flightMode() const
{
    return _vehicle ? _vehicle->flightMode() : QString();
}

/// Wires up all Vehicle Fact signals, FactGroup subscriptions, mission manager,
/// battery model, and raw MAVLink message routing. Called once per setVehicle().
void TelemetryBridge::_connectVehicleSignals()
{
    if (!_vehicle) return;

    // Forward arming and flight mode state changes directly.
    connect(_vehicle, &Vehicle::armedChanged, this, &TelemetryBridge::armedChanged);
    connect(_vehicle, &Vehicle::flightModeChanged, this, &TelemetryBridge::flightModeChanged);

    // GPS position: only emit when values change by more than ~1cm to avoid signal storms.
    connect(_vehicle, &Vehicle::coordinateChanged, this, [this](QGeoCoordinate coord) {
        bool changed = false;
        if (qAbs(coord.latitude() - _gpsLatitude) > 1e-7) { _gpsLatitude = coord.latitude(); changed = true; }
        if (qAbs(coord.longitude() - _gpsLongitude) > 1e-7) { _gpsLongitude = coord.longitude(); changed = true; }
        if (changed) emit gpsPositionChanged();
    });

    // Altitude relative to home (from Vehicle Fact).
    Fact* altFact = _vehicle->altitudeRelative();
    if (altFact) {
        connect(altFact, &Fact::valueChanged, this, [this](QVariant val) {
            _altitudeRelative = val.toDouble();
            emit altitudeChanged();
        });
        _altitudeRelative = altFact->rawValue().toDouble();
    }

    // Heading in degrees.
    Fact* headingFact = _vehicle->heading();
    if (headingFact) {
        connect(headingFact, &Fact::valueChanged, this, [this](QVariant val) {
            _heading = val.toDouble();
            emit headingChanged();
        });
        _heading = headingFact->rawValue().toDouble();
    }

    // Ground speed in m/s.
    Fact* gsFact = _vehicle->groundSpeed();
    if (gsFact) {
        connect(gsFact, &Fact::valueChanged, this, [this](QVariant val) {
            _groundSpeed = val.toDouble();
            emit groundSpeedChanged();
        });
        _groundSpeed = gsFact->rawValue().toDouble();
    }

    // Indicated airspeed in m/s.
    Fact* asFact = _vehicle->airSpeed();
    if (asFact) {
        connect(asFact, &Fact::valueChanged, this, [this](QVariant val) {
            _airspeed = val.toDouble();
            emit airspeedChanged();
        });
        _airspeed = asFact->rawValue().toDouble();
    }

    // Mark heartbeat as received on first connect.
    _heartbeatReceived = true;
    emit heartbeatReceivedChanged();

    // Load parameters immediately if already available, otherwise wait for ready signal.
    if (_vehicle->parameterManager()->parametersReady()) {
        _loadParameters();
    }
    connect(_vehicle->parameterManager(), &ParameterManager::parametersReadyChanged,
            this, &TelemetryBridge::_onParameterReadyChanged);

    // RC Channels via mavlinkMessageReceived

    // Vibration FactGroup: connect each axis + clipping count.
    auto* vibGroup = static_cast<VehicleVibrationFactGroup*>(_vehicle->vibrationFactGroup());
    if (vibGroup) {
        connect(vibGroup->xAxis(), &Fact::valueChanged, this, [this](QVariant val) {
            _vibrationX = val.toDouble();
            emit vibrationChanged();
        });
        connect(vibGroup->yAxis(), &Fact::valueChanged, this, [this](QVariant val) {
            _vibrationY = val.toDouble();
            emit vibrationChanged();
        });
        connect(vibGroup->zAxis(), &Fact::valueChanged, this, [this](QVariant val) {
            _vibrationZ = val.toDouble();
            emit vibrationChanged();
        });
        connect(vibGroup->clipCount1(), &Fact::valueChanged, this, [this](QVariant val) {
            _vibrationClipping = val.toUInt();
            emit vibrationChanged();
        });
    }

    // Estimator Status FactGroup: connect velocity, horizontal, vertical, and mag ratios.
    auto* estGroup = static_cast<VehicleEstimatorStatusFactGroup*>(_vehicle->estimatorStatusFactGroup());
    if (estGroup) {
        connect(estGroup->velRatio(), &Fact::valueChanged, this, [this](QVariant val) {
            _estimatorVelRatio = val.toDouble();
            emit estimatorStatusChanged();
        });
        connect(estGroup->horizPosRatio(), &Fact::valueChanged, this, [this](QVariant val) {
            _estimatorPosHorizRatio = val.toDouble();
            emit estimatorStatusChanged();
        });
        connect(estGroup->vertPosRatio(), &Fact::valueChanged, this, [this](QVariant val) {
            _estimatorPosVertRatio = val.toDouble();
            emit estimatorStatusChanged();
        });
        connect(estGroup->magRatio(), &Fact::valueChanged, this, [this](QVariant val) {
            _estimatorMagRatio = val.toDouble();
            emit estimatorStatusChanged();
        });
    }

    // Wind FactGroup: speed and direction.
    auto* windGroup = static_cast<VehicleWindFactGroup*>(_vehicle->windFactGroup());
    if (windGroup) {
        connect(windGroup->speed(), &Fact::valueChanged, this, [this](QVariant val) {
            _windSpeed = val.toDouble();
            emit windChanged();
        });
        connect(windGroup->direction(), &Fact::valueChanged, this, [this](QVariant val) {
            _windDirection = val.toDouble();
            emit windChanged();
        });
    }

    // Home position: only update when values change significantly to avoid redundant signals.
    connect(_vehicle, &Vehicle::homePositionChanged, this, [this](QGeoCoordinate home) {
        double lat = home.latitude();
        double lon = home.longitude();
        double alt = home.altitude();
        bool changed = false;
        if (qAbs(lat - _homeLatitude) > 1e-7) { _homeLatitude = lat; changed = true; }
        if (qAbs(lon - _homeLongitude) > 1e-7) { _homeLongitude = lon; changed = true; }
        if (qAbs(alt - _homeAltitude) > 0.1) { _homeAltitude = alt; changed = true; }
        if (changed) { _updateMissionInfo(); emit homePositionChanged(); }
    });

    // Primary battery from VehicleBatteryFactGroup (id=0). Also subscribes to the
    // battery list model for dynamically-added batteries (e.g. secondary battery).
    {
        QmlObjectListModel* battModel = _vehicle->batteries();
        _connectBatteryFacts(battModel);
        connect(battModel, &QmlObjectListModel::rowsInserted, this, [this]() {
            _connectBatteryFacts(_vehicle->batteries());
        });
    }

    // Subscribe to raw MAVLink messages for servo outputs, ESC telemetry, GPS2, etc.
    connect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &TelemetryBridge::_handleMavlinkMessage);

    // Mission Manager: update mission distances when items or current index change.
    if (auto* missionMgr = _vehicle->missionManager()) {
        connect(missionMgr, &MissionManager::newMissionItemsAvailable, this, [this](bool) {
            _updateMissionInfo();
        });
        connect(missionMgr, &MissionManager::currentIndexChanged, this, [this](int) {
            _updateMissionInfo();
        });
    }

    // Initialize connection quality to 100% on fresh connect.
    _connectionQuality = 100;
    _lastHeartbeatTime = QDateTime::currentDateTime();
    emit connectionQualityChanged();
}

/// Iterates the battery list model, identifies primary (id=0) and secondary (id=1)
/// batteries, and connects their Fact signals to the bridge's cached member variables.
void TelemetryBridge::_connectBatteryFacts(QObject* batteryModel)
{
    auto* model = qobject_cast<QmlObjectListModel*>(batteryModel);
    if (!model) return;

    for (int i = 0; i < model->count(); ++i) {
        auto* batt = qobject_cast<VehicleBatteryFactGroup*>(model->get(i));
        if (!batt) continue;
        uint8_t id = static_cast<uint8_t>(batt->id()->rawValue().toUInt());

        if (id == 0 || id == 1) {
            // Voltage: route to primary (id=0) or secondary (id=1) cache.
            connect(batt->voltage(), &Fact::valueChanged, this, [this, id](QVariant val) {
                double v = val.toDouble();
                if (id == 0 || id == 1) {
                    if (id == 0) {
                        _batteryVoltage = v;
                        emit batteryVoltageChanged();
                    } else {
                        _battery2Voltage = v;
                        if (!_battery2Present) {
                            _battery2Present = true;
                            emit battery2PresentChanged();
                        }
                        emit battery2VoltageChanged();
                    }
                }
            });
            // Current draw (amps).
            connect(batt->current(), &Fact::valueChanged, this, [this, id](QVariant val) {
                double c = val.toDouble();
                if (id == 0) {
                    _batteryCurrent = c;
                    emit batteryCurrentChanged();
                } else {
                    _battery2Current = c;
                    emit battery2CurrentChanged();
                }
            });
            // Remaining percentage (0-100).
            connect(batt->percentRemaining(), &Fact::valueChanged, this, [this, id](QVariant val) {
                int pct = val.toInt();
                if (id == 0) {
                    _batteryPercent = pct;
                    emit batteryPercentChanged();
                } else {
                    _battery2Percent = pct;
                    emit battery2PercentChanged();
                }
            });
            // Temperature (primary battery only).
            connect(batt->temperature(), &Fact::valueChanged, this, [this, id](QVariant val) {
                if (id == 0) {
                    _batteryTemperature = val.toDouble();
                    emit batteryTemperatureChanged();
                }
            });

            // Seed initial values from the current Fact raw values.
            if (id == 0) {
                _batteryVoltage = batt->voltage()->rawValue().toDouble();
                _batteryCurrent = batt->current()->rawValue().toDouble();
                _batteryPercent = batt->percentRemaining()->rawValue().toInt();
                _batteryTemperature = batt->temperature()->rawValue().toDouble();
            } else {
                _battery2Voltage = batt->voltage()->rawValue().toDouble();
                _battery2Current = batt->current()->rawValue().toDouble();
                _battery2Percent = batt->percentRemaining()->rawValue().toInt();
                _battery2Present = true;
            }
        }
    }
}

void TelemetryBridge::_onVehicleConnectedChanged(bool connected)
{
    if (!connected) {
        _heartbeatReceived = false;
        emit heartbeatReceivedChanged();
    }
}

void TelemetryBridge::_onParameterReadyChanged(bool ready)
{
    if (ready && _vehicle) {
        _fwMotorChannel = -1; // re-detect the fixed-wing motor channel with fresh parameters
        _loadParameters();
    }
}

/// Auto-detects the servo output channel that drives the fixed-wing motor.
///
/// Mirrors HardwareTestController: SERVOn_FUNCTION 70 (Throttle) or 73 (Motor)
/// identifies the throttle output on ArduPilot; fall back to the RC throttle
/// channel (RCMAP_THROTTLE / RC_MAP_THROTTLE, default 3) when params are
/// unavailable. The result is cached until parameters reload.
int TelemetryBridge::_fwMotorOutputChannel()
{
    if (_fwMotorChannel > 0)
        return _fwMotorChannel;

    int detected = 3;
    if (_vehicle && _vehicle->parameterManager() && _vehicle->parameterManager()->parametersReady()) {
        ParameterManager *paramMgr = _vehicle->parameterManager();
        const int compId = _vehicle->defaultComponentId();

        const int functions[] = {70, 73};
        for (int fn : functions) {
            for (int ch = 1; ch <= 16; ++ch) {
                Fact *fact = paramMgr->getParameter(compId, QStringLiteral("SERVO%1_FUNCTION").arg(ch));
                if (fact && qRound(fact->rawValue().toFloat()) == fn) {
                    detected = ch;
                    ch = 16;
                    break;
                }
            }
            if (detected != 3) break;
        }

        if (detected == 3) {
            const QStringList names = {QStringLiteral("RCMAP_THROTTLE"), QStringLiteral("RC_MAP_THROTTLE")};
            for (const QString &name : names) {
                Fact *fact = paramMgr->getParameter(compId, name);
                if (fact) {
                    int ch = fact->rawValue().toInt();
                    if (ch >= 1 && ch <= 16) {
                        detected = ch;
                        break;
                    }
                }
            }
        }
    }

    _fwMotorChannel = qBound(1, detected, 16);
    return _fwMotorChannel;
}

/// Loads watchlist parameters from the ParameterManager into the local cache
/// and connects Fact::valueChanged signals so subsequent changes are tracked.
/// Also exposes each parameter as a dynamic Qt property for QML binding.
void TelemetryBridge::_loadParameters()
{
    if (!_vehicle) return;

    ParameterManager* paramMgr = _vehicle->parameterManager();
    int compId = _vehicle->defaultComponentId();

    _parameterCache.clear();

    // Disconnect any previously established per-parameter Fact connections.
    if (!_paramConnections.isEmpty()) {
        for (auto &c : _paramConnections)
            disconnect(c);
        _paramConnections.clear();
    }
    int loadedCount = 0;

    // Iterate the global watchlist and load matching parameters from the vehicle.
    QStringList availableParams = paramMgr->parameterNames(compId);
    const QSet<QString> watchlistNames = ParameterWatchlist::names();

    for (const QString& name : watchlistNames) {
        if (!availableParams.contains(name))
            continue;
        Fact* fact = paramMgr->getParameter(compId, name);
        if (!fact)
            continue;
        double val = fact->rawValue().toDouble();
        _parameterCache[name] = static_cast<float>(val);

        // Expose the parameter as a dynamic Q_PROPERTY so QML can bind to it by name.
        setProperty(name.toLatin1().constData(), val);

        // Subscribe to future changes on this parameter.
        QString paramName = name;
        _paramConnections.append(
            connect(fact, &Fact::valueChanged, this, [this, paramName]() {
                Vehicle *v = _vehicle;
                if (!v) return;
                auto *pm = v->parameterManager();
                if (!pm) return;
                int cid = v->defaultComponentId();
                Fact *f = pm->getParameter(cid, paramName);
                if (!f) return;
                float val = f->rawValue().toFloat();
                _parameterCache[paramName] = val;
                setProperty(paramName.toLatin1().constData(), static_cast<double>(val));
                emit parameterUpdated(paramName, val);
            }));
        ++loadedCount;
    }

    _parametersReady = true;
    emit parametersReadyChanged(true);
    qCDebug(telemetryBridgeLog) << "Parameters loaded:" << loadedCount << "/" << watchlistNames.size();
}

void TelemetryBridge::_onTelemetryUpdate()
{
    _updateFromVehicle();
}

/// Periodic poll callback. Currently only ensures parameters are loaded once
/// the ParameterManager becomes ready (covers the case where parameters weren't
/// available at connect time).
void TelemetryBridge::_updateFromVehicle()
{
    if (!_vehicle) return;

    if (_vehicle->parameterManager()->parametersReady() && !_parametersReady) {
        _loadParameters();
    }
}

/// Central MAVLink message dispatcher. Decodes each supported message type and
/// updates the corresponding cached member variable + emits the change signal.
/// Only fires an emit when the value actually changes (with a small deadband)
/// to avoid unnecessary QML re-evaluation.
void TelemetryBridge::_handleMavlinkMessage(const mavlink_message_t& message)
{
    switch (message.msgid) {
    case MAVLINK_MSG_ID_GPS_RAW_INT: {
        mavlink_gps_raw_int_t gps;
        mavlink_msg_gps_raw_int_decode(&message, &gps);

        // Speed accuracy: centimeters → meters. UINT16_MAX = invalid/unknown.
        double acc = static_cast<double>(gps.vel_acc) / 100.0;
        if (gps.vel_acc != UINT16_MAX && qAbs(acc - _gpsSpeedAccuracy) > 0.01) {
            _gpsSpeedAccuracy = acc;
            emit gpsSpeedAccuracyChanged();
        }

        // Altitude MSL: millimeters → meters. INT32_MAX = invalid.
        double alt = gps.alt / 1000.0;
        if (gps.alt != INT32_MAX && qAbs(alt - _gpsAltitude) > 0.1) {
            _gpsAltitude = alt;
            emit gpsAltitudeChanged();
        }

        _gpsFixType = gps.fix_type;
        _gpsSatellites = gps.satellites_visible;

        // HDOP: centi-units → dimensionless. UINT16_MAX = invalid.
        double hdop = gps.eph / 100.0;
        if (gps.eph != UINT16_MAX && qAbs(hdop - _gpsHdop) > 0.01) {
            _gpsHdop = hdop;
            emit gpsHdopChanged();
        }
        emit gpsFixTypeChanged();
        emit gpsSatellitesChanged();
        break;
    }
    case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: {
        mavlink_global_position_int_t gp;
        mavlink_msg_global_position_int_decode(&message, &gp);

        // MSL altitude (mm → m) from EKF-blended solution.
        double altMsl = gp.alt / 1000.0;
        if (gp.alt != INT32_MAX && qAbs(altMsl - _globalAltitude) > 0.1) {
            _globalAltitude = altMsl;
            emit globalAltitudeChanged();
        }

        // Relative altitude (mm → m) from same source, used as fallback
        // when the Vehicle Fact for altitudeRelative is not available.
        double relAlt = gp.relative_alt / 1000.0;
        if (gp.relative_alt != INT32_MAX && qAbs(relAlt - _altitudeRelative) > 0.1) {
            _altitudeRelative = relAlt;
            emit altitudeChanged();
        }

        // Vertical speed (cm/s → m/s) from the same EKF solution.
        double vs = gp.vz / 100.0;
        if (gp.vz != INT32_MAX && qAbs(vs - _verticalSpeed) > 0.01) {
            _verticalSpeed = vs;
            emit verticalSpeedChanged();
        }
        break;
    }
    case MAVLINK_MSG_ID_RC_CHANNELS: {
        mavlink_rc_channels_t rc;
        mavlink_msg_rc_channels_decode(&message, &rc);

        // Failsafe detection: no channels or RSSI of 0 or 255 (invalid) means RC loss.
        bool wasFailsafe = _rcFailsafe;
        _rcFailsafe = (rc.chancount == 0 || rc.rssi == 0 || rc.rssi == UINT8_MAX);
        if (rc.rssi != UINT8_MAX) {
            _rcRssi = rc.rssi;
            emit rcRssiChanged();
        }
        if (wasFailsafe != _rcFailsafe) {
            emit rcFailsafeChanged();
        }

        // Record the timestamp of this RC update (local clock, microseconds).
        qint64 nowUsec = QDateTime::currentMSecsSinceEpoch() * 1000;
        if (nowUsec != _rcLastUpdateUsec) {
            _rcLastUpdateUsec = nowUsec;
            emit rcLastUpdateChanged();
        }
        break;
    }
    case MAVLINK_MSG_ID_SYS_STATUS: {
        mavlink_sys_status_t sys;
        mavlink_msg_sys_status_decode(&message, &sys);

        // Sensor health bitmask from the autopilot.
        uint32_t health = sys.onboard_control_sensors_health;
        if (health != _sensorHealth) {
            _sensorHealth = health;
            emit sensorHealthChanged();
        }

        // AHRS health: bit 11 (MAV_SYS_STATUS_AHRS) of the health bitmask.
        bool ahrsOk = (health & MAV_SYS_STATUS_AHRS) != 0;
        if (ahrsOk != _ahrsHealth) {
            _ahrsHealth = ahrsOk;
            emit ahrsHealthChanged();
        }

        // Pre-arm check result: bit 14 (MAV_SYS_STATUS_PREARM_CHECK).
        bool preArmOk = (health & MAV_SYS_STATUS_PREARM_CHECK) != 0;
        if (preArmOk != _preArmOk) {
            _preArmOk = preArmOk;
            emit preArmOkChanged();
        }

        // Communication drop rate: hundredths of a percent → percentage.
        double drop = static_cast<double>(sys.drop_rate_comm) / 100.0;
        if (qAbs(drop - _commDropRate) > 0.01) {
            _commDropRate = drop;
            emit commDropRateChanged();
        }

        // System voltage from the power module (mV → V).
        if (sys.voltage_battery != UINT16_MAX) {
            double vb = sys.voltage_battery / 1000.0;
            if (qAbs(vb - _sysVoltageBattery) > 0.01) {
                _sysVoltageBattery = vb;
                emit sysVoltageBatteryChanged();
            }
        }
        break;
    }
    case MAVLINK_MSG_ID_ATTITUDE: {
        mavlink_attitude_t att;
        mavlink_msg_attitude_decode(&message, &att);
        _roll = att.roll;
        _pitch = att.pitch;
        _yaw = att.yaw;
        emit attitudeChanged();
        break;
    }
    case MAVLINK_MSG_ID_SERVO_OUTPUT_RAW: {
        mavlink_servo_output_raw_t servo;
        mavlink_msg_servo_output_raw_decode(&message, &servo);

        // Only process port 0 (channels 1-16). Port 1 would be channels 17-32.
        if (servo.port == 0) {
            uint16_t vals[16] = {
                servo.servo1_raw, servo.servo2_raw, servo.servo3_raw, servo.servo4_raw,
                servo.servo5_raw, servo.servo6_raw, servo.servo7_raw, servo.servo8_raw,
                servo.servo9_raw, servo.servo10_raw, servo.servo11_raw, servo.servo12_raw,
                servo.servo13_raw, servo.servo14_raw, servo.servo15_raw, servo.servo16_raw
            };
            QVariantList outputs;
            outputs.reserve(16);

            // A fixed-wing airframe has a single motor regardless of which
            // servo output drives it (e.g. CH3). Report just that motor.
            if (_vehicle && (_vehicle->fixedWing() || _vehicle->vehicleType() == MAV_TYPE_FIXED_WING)) {
                const int ch = _fwMotorOutputChannel();
                int motorCount = 1;
                if (motorCount != _motorCount) {
                    _motorCount = motorCount;
                    emit motorCountChanged();
                }
                outputs.append(ch >= 1 && ch <= 16 ? vals[ch - 1] : 0);
                if (_motorOutputs != outputs) {
                    _motorOutputs = outputs;
                    emit servoOutputsChanged();
                }
                break;
            }

            // Multirotor: motor count = index of the highest non-zero channel + 1.
            int count = 0;
            for (int i = 0; i < 16; ++i) {
                outputs.append(vals[i]);
                if (vals[i] > 0) count = i + 1;
            }
            if (count != _motorCount) {
                _motorCount = count;
                emit motorCountChanged();
            }
            if (_motorOutputs != outputs) {
                _motorOutputs = outputs;
                emit servoOutputsChanged();
            }
        }
        break;
    }
    case MAVLINK_MSG_ID_ESTIMATOR_STATUS: {
        mavlink_estimator_status_t est;
        mavlink_msg_estimator_status_decode(&message, &est);
        if (_estimatorFlags != est.flags) {
            _estimatorFlags = est.flags;
            emit estimatorStatusChanged();
        }
        break;
    }
    case MAVLINK_MSG_ID_EKF_STATUS_REPORT: {
        // ArduPilot EKF_STATUS_REPORT provides similar data as ESTIMATOR_STATUS
        // but with different field names. Both populate the same bridge properties.
        mavlink_ekf_status_report_t ekf;
        mavlink_msg_ekf_status_report_decode(&message, &ekf);
        if (ekf.flags != _estimatorFlags) {
            _estimatorFlags = ekf.flags;
            emit estimatorStatusChanged();
        }

        // Each ratio is a consistency check: 0 = perfect, >1 = innovation exceeds threshold.
        double velRatio = ekf.velocity_variance;
        double posHorizRatio = ekf.pos_horiz_variance;
        double posVertRatio = ekf.pos_vert_variance;
        double compassRatio = ekf.compass_variance;
        double terrainRatio = ekf.terrain_alt_variance;

        if (qAbs(velRatio - _estimatorVelRatio) > 0.0001) {
            _estimatorVelRatio = velRatio;
            emit estimatorStatusChanged();
        }
        if (qAbs(posHorizRatio - _estimatorPosHorizRatio) > 0.0001) {
            _estimatorPosHorizRatio = posHorizRatio;
            emit estimatorStatusChanged();
        }
        if (qAbs(posVertRatio - _estimatorPosVertRatio) > 0.0001) {
            _estimatorPosVertRatio = posVertRatio;
            emit estimatorStatusChanged();
        }
        if (qAbs(compassRatio - _estimatorMagRatio) > 0.0001) {
            _estimatorMagRatio = compassRatio;
            emit estimatorStatusChanged();
        }
        if (qAbs(terrainRatio - _ekfTerrainVariance) > 0.0001) {
            _ekfTerrainVariance = terrainRatio;
            emit ekfVarianceChanged();
        }
        break;
    }
    case MAVLINK_MSG_ID_ESC_TELEMETRY_1_TO_4: {
        mavlink_esc_telemetry_1_to_4_t esc;
        mavlink_msg_esc_telemetry_1_to_4_decode(&message, &esc);

        // Decode the first 4 ESCs (indices 0-3). Temperature is in degrees C,
        // voltage in centivolts (÷100), current in centiamps (÷100).
        QVariantList temps, volts, currents, rpms;
        temps.reserve(4); volts.reserve(4); currents.reserve(4); rpms.reserve(4);
        for (int i = 0; i < 4; ++i) {
            temps.append(static_cast<double>(esc.temperature[i]));
            volts.append(esc.voltage[i] / 100.0);
            currents.append(esc.current[i] / 100.0);
            rpms.append(static_cast<double>(esc.rpm[i]));
        }
        _escTemperatures = temps;
        _escVoltages = volts;
        _escCurrents = currents;
        _escRpm = rpms;
        emit escTelemetryChanged();
        break;
    }
    case MAVLINK_MSG_ID_ESC_TELEMETRY_5_TO_8: {
        mavlink_esc_telemetry_5_to_8_t esc;
        mavlink_msg_esc_telemetry_5_to_8_decode(&message, &esc);

        // Append or update ESCs 5-8 (indices 4-7) into the existing lists.
        // Depends on ESC_TELEMETRY_1_TO_4 having already populated indices 0-3.
        if (_escTemperatures.size() >= 4) {
            for (int i = 0; i < 4; ++i) {
                int idx = i + 4;
                if (idx < _escTemperatures.size()) {
                    _escTemperatures[idx] = static_cast<double>(esc.temperature[i]);
                    _escVoltages[idx] = esc.voltage[i] / 100.0;
                    _escCurrents[idx] = esc.current[i] / 100.0;
                    _escRpm[idx] = static_cast<double>(esc.rpm[i]);
                } else {
                    _escTemperatures.append(static_cast<double>(esc.temperature[i]));
                    _escVoltages.append(esc.voltage[i] / 100.0);
                    _escCurrents.append(esc.current[i] / 100.0);
                    _escRpm.append(static_cast<double>(esc.rpm[i]));
                }
            }
            emit escTelemetryChanged();
        }
        break;
    }
    case MAVLINK_MSG_ID_ESC_TELEMETRY_9_TO_12: {
        mavlink_esc_telemetry_9_to_12_t esc;
        mavlink_msg_esc_telemetry_9_to_12_decode(&message, &esc);

        // Append or update ESCs 9-12 (indices 8-11). Requires 1_TO_4 + 5_TO_8 first.
        if (_escTemperatures.size() >= 8) {
            for (int i = 0; i < 4; ++i) {
                int idx = i + 8;
                if (idx < _escTemperatures.size()) {
                    _escTemperatures[idx] = static_cast<double>(esc.temperature[i]);
                    _escVoltages[idx] = esc.voltage[i] / 100.0;
                    _escCurrents[idx] = esc.current[i] / 100.0;
                    _escRpm[idx] = static_cast<double>(esc.rpm[i]);
                } else {
                    _escTemperatures.append(static_cast<double>(esc.temperature[i]));
                    _escVoltages.append(esc.voltage[i] / 100.0);
                    _escCurrents.append(esc.current[i] / 100.0);
                    _escRpm.append(static_cast<double>(esc.rpm[i]));
                }
            }
            emit escTelemetryChanged();
        }
        break;
    }
    case MAVLINK_MSG_ID_GPS2_RAW: {
        mavlink_gps2_raw_t g2;
        mavlink_msg_gps2_raw_decode(&message, &g2);

        _gps2FixType = g2.fix_type;
        _gps2Satellites = g2.satellites_visible;
        _gps2Latitude = g2.lat / 1e7;    // degrees * 1e7 → degrees
        _gps2Longitude = g2.lon / 1e7;
        _gps2Eph = g2.eph;                // horizontal position accuracy (cm)
        _gps2Yaw = g2.yaw;                // dual-antenna yaw (centidegrees)
        emit gps2Changed();
        break;
    }
    case MAVLINK_MSG_ID_SCALED_PRESSURE: {
        mavlink_scaled_pressure_t sp;
        mavlink_msg_scaled_pressure_decode(&message, &sp);
        _baroPressure = sp.press_abs;            // absolute pressure in hPa
        _baroTemperature = sp.temperature / 100.0; // centidegrees → degrees C
        emit baroChanged();
        break;
    }
    case MAVLINK_MSG_ID_OPTICAL_FLOW: {
        mavlink_optical_flow_t of;
        mavlink_msg_optical_flow_decode(&message, &of);
        if (_opticalFlowQuality != static_cast<int>(of.quality)) {
            _opticalFlowQuality = of.quality;
            emit opticalFlowQualityChanged();
        }
        break;
    }
    case MAVLINK_MSG_ID_RADIO_STATUS: {
        mavlink_radio_status_t rs;
        mavlink_msg_radio_status_decode(&message, &rs);

        // UINT8_MAX = invalid RSSI → treat as 0.
        _radioRssi = rs.rssi == UINT8_MAX ? 0 : rs.rssi;
        _radioTxBuf = rs.txbuf;
        _radioRxErrors = rs.rxerrors;
        emit radioStatusChanged();
        break;
    }
    case MAVLINK_MSG_ID_TERRAIN_REPORT: {
        mavlink_terrain_report_t tr;
        mavlink_msg_terrain_report_decode(&message, &tr);
        _terrainHeight = static_cast<double>(tr.current_height);
        emit terrainReportChanged();
        break;
    }
    case MAVLINK_MSG_ID_HEARTBEAT: {
        mavlink_heartbeat_t hb;
        mavlink_msg_heartbeat_decode(&message, &hb);

        // Track the MAVLink protocol version.
        uint8_t ver = hb.mavlink_version;
        if (ver != _mavlinkVersion) {
            _mavlinkVersion = ver;
            emit mavlinkVersionChanged();
        }

        // Detect companion computer: any component ID other than 1 (autopilot) or 0.
        bool wasCompanion = _companionDetected;
        if (message.compid != 1 && message.compid != 0)
            _companionDetected = true;
        if (wasCompanion != _companionDetected)
            emit companionDetectedChanged();

        // Connection quality algorithm:
        //   1. Start at 100% on each heartbeat.
        //   2. If the heartbeat arrived late (>1200ms vs expected ~1000ms), degrade
        //      proportionally: 1 point lost per 50ms of excess delay.
        //   3. Factor in the comm drop rate as a hard ceiling.
        //   4. Clamp to [0, 100].
        {
            QDateTime now = QDateTime::currentDateTime();
            int newQuality = 100;
            if (_lastHeartbeatTime.isValid()) {
                int elapsedMs = _lastHeartbeatTime.msecsTo(now);
                if (elapsedMs > 1200) {
                    newQuality = qMax(0, 100 - ((elapsedMs - 1000) / 50));
                }
            }
            if (_commDropRate > 0.0) {
                newQuality = qMin(newQuality, static_cast<int>(100.0 - _commDropRate));
            }
            newQuality = qBound(0, newQuality, 100);
            if (newQuality != _connectionQuality) {
                _connectionQuality = newQuality;
                emit connectionQualityChanged();
            }
            _lastHeartbeatTime = now;
        }
        break;
    }
    case MAVLINK_MSG_ID_ESC_INFO: {
        mavlink_esc_info_t ei;
        mavlink_msg_esc_info_decode(&message, &ei);

        // Only update when count or connection type changes to avoid redundant signals.
        if (ei.count != _escInfoCount || ei.connection_type != _escInfoConnectionType) {
            _escInfoCount = ei.count;
            _escInfoConnectionType = ei.connection_type;
            QVariantList failures, errors;
            failures.reserve(4); errors.reserve(4);
            for (int i = 0; i < 4; ++i) {
                failures.append(static_cast<uint16_t>(ei.failure_flags[i]));
                errors.append(static_cast<uint32_t>(ei.error_count[i]));
            }
            _escInfoFailureFlags = failures;
            _escInfoErrorCount = errors;
            emit escInfoChanged();
        }
        break;
    }
    default:
        break;
    }
}

/// Recalculates mission item count, home-to-first-waypoint distance, and
/// total mission distance (Haversine) across all consecutive waypoints.
/// Called whenever the mission items list or current index changes.
void TelemetryBridge::_updateMissionInfo()
{
    if (!_vehicle) return;
    auto* mgr = _vehicle->missionManager();
    if (!mgr) return;
    const auto &items = mgr->missionItems();
    int count = items.size();
    if (count != _missionCount) {
        _missionCount = count;
        emit missionCountChanged();
    }

    // Distance from home position to the first waypoint.
    const QGeoCoordinate home(_homeLatitude, _homeLongitude);
    if (count > 0 && home.isValid() && !qFuzzyIsNull(_homeLatitude) && !qFuzzyIsNull(_homeLongitude)) {
        const MissionItem* first = items.first();
        double dist = home.distanceTo(first->coordinate());
        if (qAbs(dist - _missionFirstWpDistance) > 0.5) {
            _missionFirstWpDistance = dist;
            emit missionFirstWpDistanceChanged();
        }
    }

    // Sum of Haversine distances between consecutive waypoints (ignoring invalid coords).
    double totalDist = 0.0;
    QGeoCoordinate prev;
    bool firstValid = false;
    for (const auto *item : items) {
        QGeoCoordinate coord = item->coordinate();
        if (!coord.isValid())
            continue;
        if (firstValid)
            totalDist += prev.distanceTo(coord);
        prev = coord;
        firstValid = true;
    }
    if (qAbs(totalDist - _missionTotalDistance) > 0.5) {
        _missionTotalDistance = totalDist;
        emit missionTotalDistanceChanged();
    }
}

/// Check whether a named parameter exists in the local watchlist cache.
bool TelemetryBridge::hasParameter(const QString& name) const
{
    return _parameterCache.contains(name);
}

/// Read a parameter from the cache, returning defaultValue if the name is not present.
float TelemetryBridge::parameterValue(const QString& name, float defaultValue) const
{
    auto it = _parameterCache.find(name);
    if (it != _parameterCache.end()) {
        return it.value();
    }
    return defaultValue;
}

/// Send an arm command to the vehicle if the arming gate allows it.
/// Queries ArmingGate.isArmingAllowed() before sending. If no gate is set,
/// the command is sent unconditionally. If the gate is closed and no override
/// is active, the command is silently blocked and a warning is logged.
void TelemetryBridge::arm()
{
    if (!_vehicle) {
        qCWarning(telemetryBridgeLog) << "arm(): no vehicle connected";
        return;
    }

    // Check arming gate if one is registered.
    if (_armingGate) {
        bool allowed = _armingGate->property("armingAllowed").toBool();
        bool overrideActive = _armingGate->property("overrideActive").toBool();
        if (!allowed && !overrideActive) {
            QString reason = _armingGate->property("denialReason").toString();
            qCWarning(telemetryBridgeLog) << "arm(): BLOCKED by ArmingGate —" << reason;
            return;
        }
    }

    qCWarning(telemetryBridgeLog) << "arm(): sending MAV_CMD_COMPONENT_ARM_DISARM";
    _vehicle->sendMavCommand(_vehicle->defaultComponentId(),
                             MAV_CMD_COMPONENT_ARM_DISARM,
                             true,    // showError
                             1.0f);   // param1 = 1 (arm)
}

/// Store a reference to the ArmingGate so arm() can query gate state.
/// Uses QObject* to avoid circular header dependency; accesses properties dynamically.
void TelemetryBridge::setArmingGate(QObject* gate)
{
    _armingGate = gate;
}

/// Set a parameter value in the local cache and expose it as a dynamic Qt property.
/// This does NOT send the value to the vehicle — use QgcParameterAdapter::setParam()
/// or Vehicle::sendMavCommand for that.
void TelemetryBridge::setParameterValue(const QString& name, float value)
{
    _parameterCache[name] = value;
    setProperty(name.toLatin1().constData(), static_cast<double>(value));
}
