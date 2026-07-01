#include "QgcParameterAdapter.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QMetaObject>

#include "FactSystem/Fact.h"
#include "FactSystem/FactMetaData.h"
#include "FactSystem/ParameterManager.h"
#include "QGCApplication.h"
#include "Vehicle/Vehicle.h"
#include "MultiVehicleManager.h"

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

    connect(_paramMgr, &ParameterManager::loadProgressChanged,
            this, &QgcParameterAdapter::_onLoadProgressChanged);
}

QgcParameterAdapter::~QgcParameterAdapter()
{
}

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

bool QgcParameterAdapter::isReady() const
{
    if (!_paramMgr) {
        return false;
    }
    return _paramMgr->loadProgress() >= 1.0f;
}

QString QgcParameterAdapter::autopilotType()
{
    if (!_paramMgr) {
        return QStringLiteral("Generic");
    }

    // Try to detect autopilot from vehicle firmware type
    Vehicle* vehicle = MultiVehicleManager::instance()->activeVehicle();
    if (vehicle) {
        if (vehicle->px4Firmware()) {
            return QStringLiteral("PX4");
        }
        if (vehicle->apmFirmware()) {
            return QStringLiteral("ArduPilot");
        }
    }

    // Fallback: try reading a hint parameter
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

void QgcParameterAdapter::_onLoadProgressChanged(float progress)
{
    if (progress >= 1.0f && !_ready) {
        _ready = true;
        _autopilot = autopilotType();
        emit readyChanged();
    }
}
