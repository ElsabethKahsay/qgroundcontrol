#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QMap>
#include <QString>

class UavParameterManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(int receivedCount READ receivedCount NOTIFY statusChanged)
    Q_PROPERTY(int totalCount READ totalCount CONSTANT)

public:
    enum Status { Loading = 0, Ready, Fallback };
    Q_ENUM(Status)

    enum AutopilotType { Generic = 0, PX4, ArduPilot };
    Q_ENUM(AutopilotType)

    explicit UavParameterManager(QObject *parent = nullptr);

    void setWatchlist(const QSet<QString> &params);
    void notifyParamReceived(const QString &name);
    Status status() const { return m_status; }
    int receivedCount() const { return m_received.size(); }
    int totalCount() const { return m_watchlist.size(); }
    bool isFallback() const { return m_status == Fallback; }
    bool isReady() const { return m_status == Ready; }

    void setAutopilotType(AutopilotType type);
    AutopilotType autopilotType() const { return m_autopilotType; }

    // PX4 → ArduPilot name mapping (and vice versa)
    static QString mapParamName(const QString &name, AutopilotType from, AutopilotType to);
    QString toLocalName(const QString &name) const;

    Q_INVOKABLE void reset();

    // Parameter cache storage
    float paramValue(const QString &name, float defaultVal = 0.0f) const;
    void storeParam(const QString &name, float value);

signals:
    void statusChanged();

    // Phase 1 ParameterCache signals
    void ready();
    void fallback(const QString &warning);
    void parameterChanged(const QString &id);

private slots:
    void onTimeout();

private:
    void reevaluate();

    QSet<QString> m_watchlist;
    QSet<QString> m_received;
    QMap<QString, float> m_cache;
    QTimer m_timeout;
    Status m_status = Loading;
    AutopilotType m_autopilotType = Generic;

    // PX4 ↔ ArduPilot parameter name map
    static const QMap<QString, QMap<QString, QString>> s_paramMappings;
};
