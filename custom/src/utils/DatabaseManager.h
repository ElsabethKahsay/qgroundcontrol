// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 UAV Preflight Contributors
// File: src/DatabaseManager.h
// Description: SQLite wrapper for templates, compliance logs, vehicle/battery profiles.

#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    static DatabaseManager &instance();

    Q_INVOKABLE bool initialize(const QString &dbPath = QString());

    // ── Template CRUD ─────────────────────────────────────────────────
    Q_INVOKABLE bool saveTemplate(const QString &templateId, const QString &vehicleType,
                                const QString &templateName, const QString &templateJson);
    Q_INVOKABLE QString loadTemplate(const QString &templateId);
    Q_INVOKABLE QStringList listTemplates(const QString &vehicleType);
    Q_INVOKABLE bool deleteTemplate(const QString &templateId);

    // ── Compliance log CRUD ───────────────────────────────────────────
    Q_INVOKABLE bool saveComplianceLog(const QString &logId, const QString &vehicleId,
                                       const QString &vehicleType, const QString &operatorId,
                                       const QString &logJson, const QString &telemetrySnapshot);
    Q_INVOKABLE QString loadComplianceLog(const QString &logId);
    Q_INVOKABLE QStringList listComplianceLogs(const QString &vehicleId, int limit = 50);
    Q_INVOKABLE bool deleteComplianceLog(const QString &logId);

    // ── Hardware test event logging ───────────────────────────────────
    Q_INVOKABLE bool logHardwareTestStep(
        int flightId, const QString &stepName, int servoInstance,
        int targetPwm, int feedbackPwm, int toleranceMin, int toleranceMax,
        bool operatorConfirmed, const QString &result,
        const QString &failureReason = "", const QString &operatorId = "", int durationMs = 0);
    Q_INVOKABLE QString getHardwareTestEvents(int flightId);

    // ── Component maintenance CRUD ────────────────────────────────────
    Q_INVOKABLE bool addComponent(const QString &name, const QString &type,
                                  double maxHours, int maxCycles);
    Q_INVOKABLE bool updateComponentHours(int id, double hours);
    Q_INVOKABLE bool incrementComponentCycle(int id);
    Q_INVOKABLE bool resetComponentMaintenance(int id, const QString &notes = QString());
    Q_INVOKABLE bool deleteComponent(int id);
    Q_INVOKABLE QString listComponentsJson();

    // ── Vehicle profile CRUD ──────────────────────────────────────────
    Q_INVOKABLE bool upsertVehicle(const QString &deviceUid, const QString &friendlyName,
                                   const QString &autopilotType, const QString &airframeType);
    Q_INVOKABLE QString getVehicle(const QString &deviceUid);
    Q_INVOKABLE QStringList listVehicles();
    Q_INVOKABLE bool deleteVehicle(const QString &deviceUid);
    Q_INVOKABLE bool incrementFlightCount(const QString &deviceUid);
    Q_INVOKABLE bool updateFlightHours(const QString &deviceUid, double hours);

    // ── Battery CRUD ──────────────────────────────────────────────────
    Q_INVOKABLE bool upsertBattery(const QString &serialNumber, const QString &operatorLabel);
    Q_INVOKABLE QString getBattery(const QString &serialNumber);
    Q_INVOKABLE QStringList listBatteries();

    // ── Battery cycle CRUD ────────────────────────────────────────────
    Q_INVOKABLE bool saveBatteryCycle(const QString &serialNumber, int flightSessionId,
                                      double capacityAtFullMah, double voltageSagV,
                                      double restingVoltageV, int cycleCount);
    Q_INVOKABLE QString getBatteryCycles(const QString &serialNumber, int limit = 20);
    Q_INVOKABLE QString getBatteryHealthTrend(const QString &serialNumber);

    // ── Flight session CRUD ───────────────────────────────────────────
    Q_INVOKABLE int startFlightSession(const QString &deviceUid, const QString &batterySerial,
                                        double payloadWeightKg = 0.0);
    Q_INVOKABLE bool updateFlightSessionPayload(int sessionId, double payloadWeightKg);
    Q_INVOKABLE bool endFlightSession(int sessionId, double durationSeconds);
    Q_INVOKABLE QString getFlightSessions(const QString &deviceUid, int limit = 10);
    Q_INVOKABLE QString getVehicleHistory(const QString &deviceUid);

    // ── Check result audit ────────────────────────────────────────────
    Q_INVOKABLE bool saveCheckResult(const QString &deviceUid, int flightSessionId,
                                     const QString &checkId, const QString &status,
                                     const QString &message);
    Q_INVOKABLE QString getCheckResults(int flightSessionId);

    int schemaVersion() const;

private:
    DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager() override;

    QString expandPath(const QString &path);
    bool createTables();
    bool execOrWarn(QSqlQuery &query, const char *tableName);
    QString escapeJson(const QString &raw);

    QSqlDatabase m_db;
    bool m_initialized = false;
};
