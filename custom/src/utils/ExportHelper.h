#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>

class ExportHelper : public QObject
{
    Q_OBJECT
public:
    explicit ExportHelper(QObject *parent = nullptr);

    // ── Existing export methods ─────────────────────────────────
    Q_INVOKABLE QString saveComplianceJson(const QString &baseName, const QString &json);
    Q_INVOKABLE QString saveCompliancePdf(const QString &baseName, const QString &content);
    Q_INVOKABLE QString saveComplianceHtml(const QString &baseName, const QString &jsonRecord);
    Q_INVOKABLE QString savePostFlightHtml(const QString &baseName, const QString &htmlContent);
    Q_INVOKABLE QString saveVehicleProfileCsv(const QString &baseName);
    Q_INVOKABLE QString saveBatteryHistoryCsv(const QString &baseName, const QString &batterySerial);
    Q_INVOKABLE QString generateHash(const QString &data);
    Q_INVOKABLE QString defaultExportDir() const;

    // ── Phase 8 export methods ──────────────────────────────────
    // Single session: flight record + check results + overrides
    Q_INVOKABLE QString saveSessionCsv(int sessionId, const QString &baseName);
    // Vehicle history: all sessions + check results + profile + battery
    Q_INVOKABLE QString saveVehicleHistoryCsv(const QString &deviceUid, const QString &baseName);
    // Fleet-wide (stub until multi-vehicle support lands)
    Q_INVOKABLE QString saveFleetCsv(const QString &baseName);
    // Human-readable HTML report for a session
    Q_INVOKABLE QString saveSessionReport(int sessionId, const QString &baseName, const QString &deviceUid);
    // Human-readable HTML report for full vehicle history
    Q_INVOKABLE QString saveVehicleReport(const QString &deviceUid, const QString &baseName);

private:
    QStringList collectOverrideLines(const QString &deviceUid) const;
    QString buildSessionHtml(int sessionId, const QString &deviceUid) const;
    QString buildVehicleHtml(const QString &deviceUid) const;
    static QString escCsv(const QString &value);
};
