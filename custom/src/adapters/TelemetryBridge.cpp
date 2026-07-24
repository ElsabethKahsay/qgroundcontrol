#include "TelemetryBridge.h"

#include <QDateTime>
#include <QDebug>
#include <QLoggingCategory>

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

#include <cmath>

Q_LOGGING_CATEGORY(telemetryBridgeLog, "telemetry.bridge")

TelemetryBridge::TelemetryBridge(QObject *parent)
    : QObject(parent)
{
    _updateTimer.setInterval(200);
    connect(&_updateTimer, &QTimer::timeout, this, &TelemetryBridge::_onTelemetryUpdate);
}

void TelemetryBridge::setVehicle(Vehicle* vehicle)
{
    if (_vehicle == vehicle) return;

    if (_vehicle) {
        disconnect(_vehicle, nullptr, this, nullptr);
    }

    _vehicle = vehicle;

    if (_vehicle) {
        _connectVehicleSignals();
        _updateTimer.start();
        emit isConnectedChanged();
    } else {
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

void TelemetryBridge::_connectVehicleSignals()
{
    if (!_vehicle) return;

    connect(_vehicle, &Vehicle::armedChanged, this, &TelemetryBridge::armedChanged);
    connect(_vehicle, &Vehicle::flightModeChanged, this, &TelemetryBridge::flightModeChanged);
    connect(_vehicle, &Vehicle::coordinateChanged, this, [this](QGeoCoordinate coord) {
        bool changed = false;
        if (qAbs(coord.latitude() - _gpsLatitude) > 1e-7) { _gpsLatitude = coord.latitude(); changed = true; }
        if (qAbs(coord.longitude() - _gpsLongitude) > 1e-7) { _gpsLongitude = coord.longitude(); changed = true; }
        if (changed) emit gpsPositionChanged();
    });

    Fact* altFact = _vehicle->altitudeRelative();
    if (altFact) {
        connect(altFact, &Fact::valueChanged, this, [this](QVariant val) {
            _altitudeRelative = val.toDouble();
            emit altitudeChanged();
        });
        _altitudeRelative = altFact->rawValue().toDouble();
    }

    Fact* headingFact = _vehicle->heading();
    if (headingFact) {
        connect(headingFact, &Fact::valueChanged, this, [this](QVariant val) {
            _heading = val.toDouble();
            emit headingChanged();
        });
        _heading = headingFact->rawValue().toDouble();
    }

    Fact* gsFact = _vehicle->groundSpeed();
    if (gsFact) {
        connect(gsFact, &Fact::valueChanged, this, [this](QVariant val) {
            _groundSpeed = val.toDouble();
            emit groundSpeedChanged();
        });
        _groundSpeed = gsFact->rawValue().toDouble();
    }

    Fact* asFact = _vehicle->airSpeed();
    if (asFact) {
        connect(asFact, &Fact::valueChanged, this, [this](QVariant val) {
            _airspeed = val.toDouble();
            emit airspeedChanged();
        });
        _airspeed = asFact->rawValue().toDouble();
    }

    _heartbeatReceived = true;
    emit heartbeatReceivedChanged();

    if (_vehicle->parameterManager()->parametersReady()) {
        _loadParameters();
    }
    connect(_vehicle->parameterManager(), &ParameterManager::parametersReadyChanged,
            this, &TelemetryBridge::_onParameterReadyChanged);

    // RC Channels via mavlinkMessageReceived

    // Vibration FactGroup
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

    // Estimator Status FactGroup
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

    // Wind FactGroup
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

    // Home position
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

    // Primary battery from VehicleBatteryFactGroup (id=0)
    {
        QmlObjectListModel* battModel = _vehicle->batteries();
        _connectBatteryFacts(battModel);
        connect(battModel, &QmlObjectListModel::rowsInserted, this, [this]() {
            _connectBatteryFacts(_vehicle->batteries());
        });
    }

    // MAVLink messages for SERVO_OUTPUT_RAW, ESC_TELEMETRY_1_TO_4, etc.
    connect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &TelemetryBridge::_handleMavlinkMessage);

    // Mission Manager
    if (auto* missionMgr = _vehicle->missionManager()) {
        connect(missionMgr, &MissionManager::newMissionItemsAvailable, this, [this](bool) {
            _updateMissionInfo();
        });
        connect(missionMgr, &MissionManager::currentIndexChanged, this, [this](int) {
            _updateMissionInfo();
        });
    }

    _connectionQuality = 100;
    _lastHeartbeatTime = QDateTime::currentDateTime();
    emit connectionQualityChanged();
}

void TelemetryBridge::_connectBatteryFacts(QObject* batteryModel)
{
    auto* model = qobject_cast<QmlObjectListModel*>(batteryModel);
    if (!model) return;

    for (int i = 0; i < model->count(); ++i) {
        auto* batt = qobject_cast<VehicleBatteryFactGroup*>(model->get(i));
        if (!batt) continue;
        uint8_t id = static_cast<uint8_t>(batt->id()->rawValue().toUInt());

        if (id == 0 || id == 1) {
            // First battery in model or explicitly id=0 → primary
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
            connect(batt->temperature(), &Fact::valueChanged, this, [this, id](QVariant val) {
                if (id == 0) {
                    _batteryTemperature = val.toDouble();
                    emit batteryTemperatureChanged();
                }
            });

            // Read initial values
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
        _loadParameters();
    }
}

void TelemetryBridge::_loadParameters()
{
    if (!_vehicle) return;

    ParameterManager* paramMgr = _vehicle->parameterManager();
    int compId = _vehicle->defaultComponentId();

    _parameterCache.clear();
    if (!_paramConnections.isEmpty()) {
        for (auto &c : _paramConnections)
            disconnect(c);
        _paramConnections.clear();
    }
    int loadedCount = 0;

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
        setProperty(name.toLatin1().constData(), val);
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

void TelemetryBridge::_updateFromVehicle()
{
    if (!_vehicle) return;

    if (_vehicle->parameterManager()->parametersReady() && !_parametersReady) {
        _loadParameters();
    }
}

void TelemetryBridge::_handleMavlinkMessage(const mavlink_message_t& message)
{
    switch (message.msgid) {
    case MAVLINK_MSG_ID_GPS_RAW_INT: {
        mavlink_gps_raw_int_t gps;
        mavlink_msg_gps_raw_int_decode(&message, &gps);
        double acc = static_cast<double>(gps.vel_acc) / 100.0;
        if (gps.vel_acc != UINT16_MAX && qAbs(acc - _gpsSpeedAccuracy) > 0.01) {
            _gpsSpeedAccuracy = acc;
            emit gpsSpeedAccuracyChanged();
        }
        double alt = gps.alt / 1000.0;
        if (gps.alt != INT32_MAX && qAbs(alt - _gpsAltitude) > 0.1) {
            _gpsAltitude = alt;
            emit gpsAltitudeChanged();
        }
        _gpsFixType = gps.fix_type;
        _gpsSatellites = gps.satellites_visible;
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
        // MSL altitude (cm → m) from EKF blend
        double altMsl = gp.alt / 1000.0;
        if (gp.alt != INT32_MAX && qAbs(altMsl - _globalAltitude) > 0.1) {
            _globalAltitude = altMsl;
            emit globalAltitudeChanged();
        }
        // Update relative altitude (mm → m) from same source if Fact not available
        double relAlt = gp.relative_alt / 1000.0;
        if (gp.relative_alt != INT32_MAX && qAbs(relAlt - _altitudeRelative) > 0.1) {
            _altitudeRelative = relAlt;
            emit altitudeChanged();
        }
        break;
    }
    case MAVLINK_MSG_ID_RC_CHANNELS: {
        mavlink_rc_channels_t rc;
        mavlink_msg_rc_channels_decode(&message, &rc);
        bool wasFailsafe = _rcFailsafe;
        _rcFailsafe = (rc.chancount == 0 || rc.rssi == 0 || rc.rssi == UINT8_MAX);
        if (rc.rssi != UINT8_MAX) {
            _rcRssi = rc.rssi;
            emit rcRssiChanged();
        }
        if (wasFailsafe != _rcFailsafe) {
            emit rcFailsafeChanged();
        }
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
        uint32_t health = sys.onboard_control_sensors_health;
        if (health != _sensorHealth) {
            _sensorHealth = health;
            emit sensorHealthChanged();
        }
        // AHRS health from MAV_SYS_STATUS_AHRS bit
        bool ahrsOk = (health & MAV_SYS_STATUS_AHRS) != 0;
        if (ahrsOk != _ahrsHealth) {
            _ahrsHealth = ahrsOk;
            emit ahrsHealthChanged();
        }
        // Pre-arm check result
        bool preArmOk = (health & MAV_SYS_STATUS_PREARM_CHECK) != 0;
        if (preArmOk != _preArmOk) {
            _preArmOk = preArmOk;
            emit preArmOkChanged();
        }
        double drop = static_cast<double>(sys.drop_rate_comm) / 100.0;
        if (qAbs(drop - _commDropRate) > 0.01) {
            _commDropRate = drop;
            emit commDropRateChanged();
        }
        // voltage_battery from SYS_STATUS (mV → V) for power module health check
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
        if (servo.port == 0) {
            uint16_t vals[16] = {
                servo.servo1_raw, servo.servo2_raw, servo.servo3_raw, servo.servo4_raw,
                servo.servo5_raw, servo.servo6_raw, servo.servo7_raw, servo.servo8_raw,
                servo.servo9_raw, servo.servo10_raw, servo.servo11_raw, servo.servo12_raw,
                servo.servo13_raw, servo.servo14_raw, servo.servo15_raw, servo.servo16_raw
            };
            QVariantList outputs;
            outputs.reserve(16);
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
        mavlink_ekf_status_report_t ekf;
        mavlink_msg_ekf_status_report_decode(&message, &ekf);
        if (ekf.flags != _estimatorFlags) {
            _estimatorFlags = ekf.flags;
            emit estimatorStatusChanged();
        }
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
        // Append to existing ESC lists (1_TO_4 already set them)
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
        _gps2Latitude = g2.lat / 1e7;
        _gps2Longitude = g2.lon / 1e7;
        _gps2Eph = g2.eph;
        _gps2Yaw = g2.yaw;
        emit gps2Changed();
        break;
    }
    case MAVLINK_MSG_ID_SCALED_PRESSURE: {
        mavlink_scaled_pressure_t sp;
        mavlink_msg_scaled_pressure_decode(&message, &sp);
        _baroPressure = sp.press_abs;
        _baroTemperature = sp.temperature / 100.0;
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
        uint8_t ver = hb.mavlink_version;
        if (ver != _mavlinkVersion) {
            _mavlinkVersion = ver;
            emit mavlinkVersionChanged();
        }
        // Track companion computer (non-autopilot components)
        bool wasCompanion = _companionDetected;
        if (message.compid != 1 && message.compid != 0)
            _companionDetected = true;
        if (wasCompanion != _companionDetected)
            emit companionDetectedChanged();
        // Compute connection quality from heartbeat interval + drop rate
        {
            QDateTime now = QDateTime::currentDateTime();
            int newQuality = 100;
            if (_lastHeartbeatTime.isValid()) {
                int elapsedMs = _lastHeartbeatTime.msecsTo(now);
                // Expected heartbeat ~1000ms; degrade if late
                if (elapsedMs > 1200) {
                    newQuality = qMax(0, 100 - ((elapsedMs - 1000) / 50));
                }
            }
            // Factor in comm drop rate (0-100%)
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

    // Distance from home to first waypoint
    const QGeoCoordinate home(_homeLatitude, _homeLongitude);
    if (count > 0 && home.isValid() && !qFuzzyIsNull(_homeLatitude) && !qFuzzyIsNull(_homeLongitude)) {
        const MissionItem* first = items.first();
        double dist = home.distanceTo(first->coordinate());
        if (qAbs(dist - _missionFirstWpDistance) > 0.5) {
            _missionFirstWpDistance = dist;
            emit missionFirstWpDistanceChanged();
        }
    }

    // Total Haversine distance between consecutive waypoints
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

bool TelemetryBridge::hasParameter(const QString& name) const
{
    return _parameterCache.contains(name);
}

float TelemetryBridge::parameterValue(const QString& name, float defaultValue) const
{
    auto it = _parameterCache.find(name);
    if (it != _parameterCache.end()) {
        return it.value();
    }
    return defaultValue;
}

void TelemetryBridge::setParameterValue(const QString& name, float value)
{
    _parameterCache[name] = value;
    setProperty(name.toLatin1().constData(), static_cast<double>(value));
}
