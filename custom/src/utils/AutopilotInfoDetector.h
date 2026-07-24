#pragma once

// ============================================================================
// AutopilotInfoDetector — Identifies the connected autopilot type from
// MAVLink HEARTBEAT messages and triggers appropriate parameter loading.
//
// Detection flow:
//   1. consumeHeartbeat() is called with the MAV_AUTOPILOT enum value
//   2. Maps the enum to PX4, ArduPilot, Generic, or Unknown
//   3. For PX4/ArduPilot, invokes the registered ParamMapLoader callback
//      to load autopilot-specific parameter mappings
//   4. For Generic/Unknown, emits genericModeActivated() and falls back
//      to hardcoded default thresholds (DefaultThresholds struct)
//
// This allows the preflight checklist to adapt its checks and thresholds
// based on what autopilot firmware is running on the connected vehicle.
// ============================================================================

#include <QObject>
#include <QString>
#include <functional>

class AutopilotInfoDetector : public QObject {
    Q_OBJECT
    Q_PROPERTY(Autopilot autopilot READ autopilot NOTIFY autopilotChanged)
    Q_PROPERTY(QString autopilotName READ autopilotName NOTIFY autopilotChanged)
    Q_PROPERTY(bool detected READ isDetected NOTIFY autopilotChanged)

public:
    enum Autopilot {
        Unknown = 0,
        Generic,
        PX4,
        ArduPilot
    };
    Q_ENUM(Autopilot)

    explicit AutopilotInfoDetector(QObject *parent = nullptr);

    Autopilot autopilot() const { return m_autopilot; }
    QString autopilotName() const;
    bool isDetected() const { return m_autopilot != Unknown && m_autopilot != Generic; }

    // Called when a HEARTBEAT with autopilot enum is received
    void consumeHeartbeat(int mavAutopilotEnum);

    // Load parameter mapping callbacks
    using ParamMapLoader = std::function<void()>;
    void setPx4ParamMapLoader(ParamMapLoader loader) { m_px4Loader = loader; }
    void setArduParamMapLoader(ParamMapLoader loader) { m_arduLoader = loader; }

    // Hardcoded defaults for generic mode
    struct DefaultThresholds {
        double minBatteryVoltage = 15.0;
        double maxBatteryTemp = 45.0;
        double maxCellDelta = 0.15;
        int minGpsSatellites = 8;
        double maxEkfVariance = 1.0;
    };
    DefaultThresholds genericThresholds() const { return m_genericThresholds; }

signals:
    void autopilotChanged();
    void autopilotDetected(Autopilot autopilot, const QString &name);
    void parameterMapLoaded(Autopilot autopilot);
    void genericModeActivated();

private:
    void loadParamMap();
    Autopilot m_autopilot = Unknown;
    ParamMapLoader m_px4Loader;
    ParamMapLoader m_arduLoader;
    DefaultThresholds m_genericThresholds;
};
