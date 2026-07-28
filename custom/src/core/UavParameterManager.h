/**
 * @file UavParameterManager.h
 * @brief Tracks parameter loading progress for the preflight checklist.
 *
 * Given a "watchlist" of parameter names, monitors which have been received
 * from the vehicle and transitions through Loading -> Ready (all received) or
 * Loading -> Fallback (timeout). Also provides PX4 <-> ArduPilot parameter
 * name mapping and a simple key-value cache for quick parameter lookup.
 */

#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QMap>
#include <QString>

/// Tracks parameter loading progress for the preflight checklist.
/// Given a "watchlist" of parameter names, it monitors which have been received
/// and transitions through Loading -> Ready (all received) or Loading -> Fallback (timeout).
/// Also provides a PX4 <-> ArduPilot parameter name mapping and a simple key-value cache.
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

    /// Define the set of parameters we care about; resets received set and starts the timeout.
    void setWatchlist(const QSet<QString> &params);
    /// Called when a parameter arrives from the vehicle; tracks progress toward readiness.
    void notifyParamReceived(const QString &name);
    Status status() const { return m_status; }
    int receivedCount() const { return m_received.size(); }
    int totalCount() const { return m_watchlist.size(); }
    bool isFallback() const { return m_status == Fallback; }
    bool isReady() const { return m_status == Ready; }

    void setAutopilotType(AutopilotType type);
    AutopilotType autopilotType() const { return m_autopilotType; }

    /// Translate a parameter name between PX4 and ArduPilot naming conventions.
    static QString mapParamName(const QString &name, AutopilotType from, AutopilotType to);
    /// Convenience: translate a canonical name to the current autopilot's local name.
    QString toLocalName(const QString &name) const;

    Q_INVOKABLE void reset();

    /// Retrieve a cached parameter value (translated to local name); returns defaultVal if absent.
    float paramValue(const QString &name, float defaultVal = 0.0f) const;
    /// Store a parameter value in the local cache (keyed by local autopilot name).
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
    /// Check if all watched parameters have arrived and update status accordingly.
    void reevaluate();

    QSet<QString> m_watchlist;   // Parameters we expect to receive.
    QSet<QString> m_received;    // Parameters that have arrived so far.
    QMap<QString, float> m_cache; // Local-name -> value cache for quick parameter lookup.
    QTimer m_timeout;            // Single-shot timer: if watchlist isn't complete in time, go to Fallback.
    Status m_status = Loading;
    AutopilotType m_autopilotType = Generic;

    // PX4 <-> ArduPilot parameter name map
    static const QMap<QString, QMap<QString, QString>> s_paramMappings;
};
