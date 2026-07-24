#pragma once

/// @file QgcParameterAdapter.h
/// @brief Lightweight QML-friendly adapter around QGC's ParameterManager.
///
/// QgcParameterAdapter provides a simplified get/set interface to vehicle parameters
/// and detects the autopilot firmware type (PX4 vs ArduPilot). It monitors the
/// ParameterManager's load progress and exposes a "ready" property that becomes
/// true once all parameters have been downloaded from the vehicle.
///
/// This adapter is useful for QML components that need to read or write a few
/// specific parameters without coupling to the full ParameterManager API.

#include <QObject>
#include <QVariant>
#include <QString>

class ParameterManager;
class Vehicle;

/// @class QgcParameterAdapter
/// @brief QML-friendly wrapper for reading/writing vehicle parameters and
///        detecting autopilot type. Exposes a single "ready" Q_PROPERTY.
class QgcParameterAdapter : public QObject {
    Q_OBJECT

    /// True once the ParameterManager has finished loading all parameters (progress >= 1.0).
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
public:
    /// Construct with a ParameterManager. If paramMgr is null, all operations become no-ops.
    explicit QgcParameterAdapter(ParameterManager* paramMgr, QObject* parent = nullptr);
    ~QgcParameterAdapter() override;

    /// Read a parameter by name. Returns QVariant() if not found; emits fallbackActivated.
    Q_INVOKABLE QVariant getParam(const QString& name);

    /// Write a parameter value. Returns false if the ParameterManager is unavailable
    /// or the parameter does not exist.
    Q_INVOKABLE bool setParam(const QString& name, const QVariant& value);

    /// Whether all parameters have been loaded from the vehicle.
    Q_INVOKABLE bool isReady() const;

    /// Detect the autopilot firmware type ("PX4", "ArduPilot", or "Generic").
    Q_INVOKABLE QString autopilotType();

signals:
    /// Emitted when the ready state transitions to true.
    void readyChanged();

    /// Emitted when a parameter value is successfully read or written.
    void parameterChanged(const QString& name, const QVariant& value);

    /// Emitted when a getParam or setParam call cannot reach the ParameterManager.
    void fallbackActivated();

private slots:
    /// Monitors ParameterManager load progress; marks ready when progress hits 100%.
    void _onLoadProgressChanged(float progress);

private:
    ParameterManager* _paramMgr;  ///< QGC ParameterManager for the active vehicle.
    bool _ready;                  ///< True after parameters are fully loaded.
    QString _autopilot;           ///< Cached autopilot type string.
};
