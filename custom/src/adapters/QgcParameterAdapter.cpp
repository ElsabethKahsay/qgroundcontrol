/// @file QgcParameterAdapter.cpp
/// @brief Implementation of the QgcParameterAdapter parameter interface.

#include <QDebug>
#include <QLoggingCategory>
#include <QMetaObject>

#include "FactSystem/Fact.h"
#include "FactSystem/FactMetaData.h"
#include "FactSystem/ParameterManager.h"
#include "QGCApplication.h"
#include "Vehicle/Vehicle.h"
#include "MultiVehicleManager.h"

/// Logging category for QgcParameterAdapter debug/warning output.
Q_LOGGING_CATEGORY(qgcParamAdapterLog, "qgc.param.adapter")

QgcParameterAdapter::QgcParameterAdapter(ParameterManager* paramMgr, QObject* parent)
    : QObject(parent)
    , _paramMgr(paramMgr)
    , _ready(false)
{
    if (!_paramMgr) {
        qCWarning(qgcParamAdapterLog) << "QgcParameterAdapter constructed with null ParameterManager";
        return;
    }

    // Monitor load progress so we know when all parameters are available.
    connect(_paramMgr, &ParameterManager::loadProgressChanged,
            this, &QgcParameterAdapter::_onLoadProgressChanged);
}

QgcParameterAdapter::~QgcParameterAdapter()
{
}

/// Retrieve a parameter value by name. Uses component ID -1 to search all components.
/// Returns QVariant() and emits fallbackActivated on failure.
QVariant QgcParameterAdapter::getParam(const QString& name)
{
    if (!_paramMgr) {
        qCWarning(qgcParamAdapterLog) << "getParam: no ParameterManager";
        emit fallbackActivated();
        return QVariant();
    }

    Fact* fact = _paramMgr->getParameter(-1, name);
    if (!fact) {
        qCWarning(qgcParamAdapterLog) << "Parameter not found:" << name;
        emit fallbackActivated();
        return QVariant();
    }

    return fact->rawValue();
}

/// Write a parameter value to the vehicle. Returns true on success.
bool QgcParameterAdapter::setParam(const QString& name, const QVariant& value)
{
    if (!_paramMgr) {
        qCWarning(qgcParamAdapterLog) << "setParam: no ParameterManager";
        return false;
    }

    Fact* fact = _paramMgr->getParameter(-1, name);
    if (!fact) {
        qCWarning(qgcParamAdapterLog) << "setParam: parameter not found:" << name;
        return false;
    }

    fact->setRawValue(value);
    qCDebug(qgcParamAdapterLog) << "setParam:" << name << "=" << value;
    return true;
}

/// True when the ParameterManager reports 100% load progress.
bool QgcParameterAdapter::isReady() const
{
    if (!_paramMgr) {
        return false;
    }
    return _paramMgr->loadProgress() >= 1.0f;
}

/// Detect autopilot firmware type using a multi-step heuristic:
///   1. Check the Vehicle's firmware type flags (most reliable).
///   2. Fall back to probing for PX4-specific (SYS_AUTOSTART) or
///      ArduPilot-specific (FRAME_CLASS) parameters.
///   3. Return "Generic" if nothing matches.
QString QgcParameterAdapter::autopilotType()
{
    if (!_paramMgr) {
        return QStringLiteral("Generic");
    }

    // Primary detection via Vehicle firmware type flags.
    Vehicle* vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (vehicle) {
        if (vehicle->px4Firmware()) {
            return QStringLiteral("PX4");
        }
        if (vehicle->apmFirmware()) {
            return QStringLiteral("ArduPilot");
        }
    }

    // Fallback: check for characteristic parameters.
    QVariant sysAutostart = getParam(QStringLiteral("SYS_AUTOSTART"));
    if (sysAutostart.isValid()) {
        return QStringLiteral("PX4");
    }

    QVariant frameClass = getParam(QStringLiteral("FRAME_CLASS"));
    if (frameClass.isValid()) {
        return QStringLiteral("ArduPilot");
    }

    return QStringLiteral("Generic");
}

/// Called when ParameterManager load progress changes. Once progress reaches 100%,
/// marks the adapter as ready and caches the detected autopilot type.
void QgcParameterAdapter::_onLoadProgressChanged(float progress)
{
    if (progress >= 1.0f && !_ready) {
        _ready = true;
        _autopilot = autopilotType();
        emit readyChanged();
    }
}
