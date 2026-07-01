#pragma once
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
