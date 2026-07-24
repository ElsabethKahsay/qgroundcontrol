#pragma once

// ============================================================================
// ExportHelper — Generates reports and exports in multiple formats (JSON,
// CSV, HTML, PDF) from the checklist and vehicle data stored in
// DatabaseManager.  All exports are written to ~/Documents/UAVPreflightReports/.
//
// Provides two levels of export:
//   1. Raw data — JSON blobs, CSV spreadsheets for spreadsheet analysis
//   2. Human-readable — styled HTML reports with pass/fail badges and
//      tabular data suitable for printing or sharing.
//
// Also exposes compliance log CRUD as a thin convenience layer over
// DatabaseManager so QML callers don't need to import the DB layer
// directly for common operations.
// ============================================================================

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

    // ── Compliance log access ─────────────────────────────────────
    Q_INVOKABLE bool saveComplianceLog(const QString &logId, const QString &vehicleId,
                                       const QString &vehicleType, const QString &operatorId,
                                       const QString &logJson, const QString &telemetrySnapshot);
    Q_INVOKABLE QString loadComplianceLog(const QString &logId);
    Q_INVOKABLE QStringList listComplianceLogs(const QString &vehicleId, int limit = 50);
    Q_INVOKABLE bool deleteComplianceLog(const QString &logId);

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
