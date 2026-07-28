#include "ExportHelper.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include "DatabaseManager.h"

ExportHelper::ExportHelper(QObject *parent)
    : QObject(parent)
{
}

// Local helper: returns ~/Documents/UAVPreflightReports/, creating it if needed.
static QString exportDir()
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (d.isEmpty()) d = QDir::homePath();
    QDir dir(d + QStringLiteral("/UAVPreflightReports"));
    dir.mkpath(".");
    return dir.absolutePath();
}

QString ExportHelper::defaultExportDir() const
{
    return exportDir();
}

// ── Compliance log access ───────────────────────────────────────────────
// Thin passthrough to DatabaseManager for QML convenience.

bool ExportHelper::saveComplianceLog(const QString &logId, const QString &vehicleId,
                                     const QString &vehicleType, const QString &operatorId,
                                     const QString &logJson, const QString &telemetrySnapshot)
{
    return DatabaseManager::instance().saveComplianceLog(logId, vehicleId, vehicleType,
                                                         operatorId, logJson, telemetrySnapshot);
}

QString ExportHelper::loadComplianceLog(const QString &logId)
{
    return DatabaseManager::instance().loadComplianceLog(logId);
}

QStringList ExportHelper::listComplianceLogs(const QString &vehicleId, int limit)
{
    return DatabaseManager::instance().listComplianceLogs(vehicleId, limit);
}

bool ExportHelper::deleteComplianceLog(const QString &logId)
{
    return DatabaseManager::instance().deleteComplianceLog(logId);
}

/// Construct the full file path inside ~/Documents/UAVPreflightReports/.
static QString makePath(const QString &baseName, const QString &ext)
{
    return exportDir() + QStringLiteral("/") + baseName + ext;
}

QString ExportHelper::saveComplianceJson(const QString &baseName, const QString &json)
{
    QString path = makePath(baseName, QStringLiteral(".json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ExportHelper: Failed to open" << path;
        return {};
    }
    file.write(json.toUtf8());
    file.close();
    return path;
}

QString ExportHelper::saveCompliancePdf(const QString &baseName, const QString &content)
{
    QString path = makePath(baseName, QStringLiteral(".pdf"));
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(20, 20, 20, 20));

    QPainter painter(&writer);
    if (!painter.begin(&writer)) {
        qWarning() << "ExportHelper: QPainter failed for" << path;
        return {};
    }

    QFont titleFont = painter.font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(Qt::black);
    painter.drawText(QRectF(0, 0, writer.width(), 40), Qt::AlignLeft,
                     QStringLiteral("UAV Pre-Flight Compliance Report"));

    QFont bodyFont = painter.font();
    bodyFont.setPointSize(10);
    bodyFont.setBold(false);
    painter.setFont(bodyFont);

    QStringList lines = content.split('\n');
    qreal y = 50;
    for (const QString &line : lines) {
        painter.drawText(QRectF(0, y, writer.width(), 16), Qt::TextSingleLine, line);
        y += 16;
        if (y > writer.height() - 40) {
            writer.newPage();
            y = 20;
        }
    }
    painter.end();

    QFile out(path);
    if (!out.exists() || out.size() == 0) {
        qWarning() << "ExportHelper: PDF empty:" << path;
        return {};
    }
    return path;
}

static QString escHtml(const QString &s)
{
    QString r = s;
    r.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
    r.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
    r.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
    r.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
    return r;
}

// Local helper: generates an HTML <span> badge with the given label and background color.
static QString badge(const QString &label, const QString &color)
{
    return QStringLiteral("<span style=\"display:inline-block;padding:2px 10px;border-radius:4px;"
                          "font-size:12px;font-weight:bold;color:#fff;background:%1;\">%2</span>")
        .arg(color, escHtml(label));
}

/**
 * @brief Generate a styled HTML compliance report from a JSON record.
 *
 * Renders telemetry snapshot, sensor health, checklist item results,
 * final preflight checks, and payload status into a single-page report
 * with pass/fail badges.  Writes to ~/Documents/UAVPreflightReports/.
 */
QString ExportHelper::saveComplianceHtml(const QString &baseName, const QString &jsonRecord)
{
    QJsonDocument doc = QJsonDocument::fromJson(jsonRecord.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "ExportHelper: invalid JSON record";
        return {};
    }
    QJsonObject rec = doc.object();

    QString ts = rec.value(QStringLiteral("timestamp")).toString();
    QString vehicleName = rec.value(QStringLiteral("vehicleName")).toString();
    QString vehicleType = rec.value(QStringLiteral("vehicleType")).toString();
    QString result = rec.value(QStringLiteral("result")).toString();
    bool pass = (result == QStringLiteral("pass"));

    QJsonObject tel = rec.value(QStringLiteral("telemetry")).toObject();
    QJsonArray items = rec.value(QStringLiteral("checklist")).toArray();

    QString html;
    html += QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    html += QStringLiteral("<title>UAV Pre-Flight Report</title>");
    html += QStringLiteral("<style>"
        "body{font-family:system-ui,-apple-system,sans-serif;max-width:800px;margin:40px auto;padding:0 20px;color:#333;}"
        "h1{font-size:22px;margin-bottom:4px;}"
        ".meta{color:#666;font-size:13px;margin-bottom:24px;}"
        ".verdict{font-size:28px;font-weight:bold;text-align:center;padding:20px;border-radius:8px;margin:20px 0;}"
        ".pass{background:#e6ffe6;color:#1a7a1a;}"
        ".fail{background:#ffe6e6;color:#cc0000;}"
        "table{width:100%;border-collapse:collapse;margin:12px 0 20px;}"
        "th,td{text-align:left;padding:6px 10px;border-bottom:1px solid #ddd;font-size:13px;}"
        "th{background:#f5f5f5;font-weight:600;}"
        ".check-pass{color:#1a7a1a;font-weight:bold;}"
        ".check-fail{color:#cc0000;font-weight:bold;}"
        ".check-pending{color:#999;}"
        "h2{font-size:16px;margin:20px 0 4px;border-bottom:1px solid #eee;padding-bottom:4px;}"
        ".footer{color:#999;font-size:11px;text-align:center;margin-top:40px;border-top:1px solid #eee;padding-top:12px;}"
        ".section{margin-bottom:20px;}"
    "</style></head><body>");

    html += QStringLiteral("<h1>UAV Pre-Flight Compliance Report</h1>");
    html += QStringLiteral("<div class=\"meta\">Generated: %1 &middot; %2 &middot; %3</div>")
        .arg(escHtml(ts), escHtml(vehicleName), escHtml(vehicleType));

    html += QStringLiteral("<div class=\"verdict %1\">%2</div>")
        .arg(pass ? QStringLiteral("pass") : QStringLiteral("fail"),
             pass ? QStringLiteral("&#10004; PASS") : QStringLiteral("&#10008; FAIL"));

    html += QStringLiteral("<div class=\"section\"><h2>Telemetry Snapshot</h2><table>");
    auto addTel = [&](const QString &label, const QString &key, const QString &unit) {
        double val = tel.value(key).toDouble();
        html += QStringLiteral("<tr><td>%1</td><td>%2 %3</td></tr>")
            .arg(escHtml(label)).arg(val, 0, 'f', 1).arg(escHtml(unit));
    };
    addTel(QStringLiteral("Battery Voltage"), QStringLiteral("batteryVoltage"), QStringLiteral("V"));
    addTel(QStringLiteral("Battery %"),      QStringLiteral("batteryPercent"), QStringLiteral("%"));
    addTel(QStringLiteral("GPS Satellites"),  QStringLiteral("satellites"),     QString());
    addTel(QStringLiteral("Altitude"),        QStringLiteral("altitude"),       QStringLiteral("m"));
    addTel(QStringLiteral("Ground Speed"),    QStringLiteral("groundSpeed"),    QStringLiteral("m/s"));
    addTel(QStringLiteral("Heading"),         QStringLiteral("heading"),        QStringLiteral("deg"));
    addTel(QStringLiteral("RC RSSI"),         QStringLiteral("rcRssi"),         QStringLiteral("%"));
    html += QStringLiteral("</table></div>");

    html += QStringLiteral("<div class=\"section\"><h2>Sensors</h2><table>");
    auto addBool = [&](const QString &label, const QString &key) {
        bool ok = tel.value(key).toBool();
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
            .arg(escHtml(label), ok ? badge(QStringLiteral("OK"), QStringLiteral("#1a7a1a"))
                                    : badge(QStringLiteral("FAIL"), QStringLiteral("#cc0000")));
    };
    addBool(QStringLiteral("IMU"),     QStringLiteral("imuHealthy"));
    addBool(QStringLiteral("Compass"), QStringLiteral("compassHealthy"));
    html += QStringLiteral("</table></div>");

    html += QStringLiteral("<div class=\"section\"><h2>Checklist</h2><table><tr>"
                           "<th>Item</th><th>Type</th><th>Status</th></tr>");
    for (const QJsonValue &v : items) {
        QJsonObject it = v.toObject();
        QString name = it.value(QStringLiteral("name")).toString();
        QString itType = it.value(QStringLiteral("type")).toString();
        QString status = it.value(QStringLiteral("status")).toString();
        QString req = it.value(QStringLiteral("required")).toBool() ? QStringLiteral("*") : QString();
        QString cls;
        if (status == QStringLiteral("passed")) cls = QStringLiteral("check-pass");
        else if (status == QStringLiteral("failed")) cls = QStringLiteral("check-fail");
        else cls = QStringLiteral("check-pending");
        html += QStringLiteral("<tr><td>%1%2</td><td>%3</td><td class=\"%4\">%5</td></tr>")
            .arg(escHtml(name), escHtml(req), escHtml(itType), cls, escHtml(status));
    }
    html += QStringLiteral("</table></div>");

    QJsonObject fin = rec.value(QStringLiteral("finalChecks")).toObject();
    html += QStringLiteral("<div class=\"section\"><h2>Final Checks</h2><table>");
    auto addFin = [&](const QString &label, const QString &key) {
        bool ok = fin.value(key).toBool();
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
            .arg(escHtml(label), ok ? badge(QStringLiteral("OK"), QStringLiteral("#1a7a1a"))
                                    : badge(QStringLiteral("FAIL"), QStringLiteral("#cc0000")));
    };
    addFin(QStringLiteral("Airspace Clear"), QStringLiteral("airspaceClear"));
    addFin(QStringLiteral("Wind OK"),         QStringLiteral("windOk"));
    addFin(QStringLiteral("Home Point Set"),  QStringLiteral("homePointSet"));
    bool allFin = fin.value(QStringLiteral("allFinalChecksPassed")).toBool();
    html += QStringLiteral("<tr><td><strong>All Final Checks</strong></td><td>%1</td></tr>")
        .arg(allFin ? badge(QStringLiteral("PASS"), QStringLiteral("#1a7a1a"))
                    : badge(QStringLiteral("FAIL"), QStringLiteral("#cc0000")));
    html += QStringLiteral("</table></div>");

    html += QStringLiteral("<div class=\"section\"><h2>Payload &amp; Hardware</h2><table>");
    bool payload = rec.value(QStringLiteral("payloadSecured")).toBool();
    html += QStringLiteral("<tr><td>Payload Secured</td><td>%1</td></tr>")
        .arg(payload ? badge(QStringLiteral("YES"), QStringLiteral("#1a7a1a"))
                     : badge(QStringLiteral("NO"), QStringLiteral("#cc0000")));
    bool hwPass = rec.value(QStringLiteral("hardwareTestPassed")).toBool();
    html += QStringLiteral("<tr><td>Hardware Test</td><td>%1</td></tr>")
        .arg(hwPass ? badge(QStringLiteral("PASS"), QStringLiteral("#1a7a1a"))
                    : badge(QStringLiteral("FAIL"), QStringLiteral("#cc0000")));
    html += QStringLiteral("</table></div>");

    html += QStringLiteral("<div class=\"footer\">");
    html += QStringLiteral("Autopilot: %1 &middot; Connection: %2 &middot; MAVLink URL: %3")
        .arg(escHtml(rec.value(QStringLiteral("autopilotType")).toString()),
             escHtml(rec.value(QStringLiteral("connectionStatus")).toString()),
             escHtml(rec.value(QStringLiteral("mavlinkUrl")).toString()));
    html += QStringLiteral("<br>Generated by UAV Preflight &mdash; %1</div>")
        .arg(escHtml(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    html += QStringLiteral("</body></html>");

    QString path = makePath(baseName, QStringLiteral(".html"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ExportHelper: Failed to open" << path;
        return {};
    }
    file.write(html.toUtf8());
    file.close();
    return path;
}

QString ExportHelper::savePostFlightHtml(const QString &baseName, const QString &htmlContent)
{
    QString path = makePath(baseName, QStringLiteral(".html"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ExportHelper: Failed to open" << path;
        return {};
    }
    file.write(htmlContent.toUtf8());
    file.close();
    return path;
}

QString ExportHelper::saveVehicleProfileCsv(const QString &baseName)
{
    QStringList vehicles = DatabaseManager::instance().listVehicles();
    if (vehicles.isEmpty()) return {};

    QString csv = QStringLiteral("deviceUid,friendlyName,autopilotType,airframeType,"
                                 "firstSeen,lastSeen,totalFlightCount,totalFlightHours,identitySource\n");
    for (const QString &uid : vehicles) {
        QString v = DatabaseManager::instance().getVehicle(uid);
        if (v.isEmpty()) continue;
        QJsonObject o = QJsonDocument::fromJson(v.toUtf8()).object();
        QStringList row;
        row << o.value(QStringLiteral("deviceUid")).toString()
            << QStringLiteral("\"%1\"").arg(o.value(QStringLiteral("friendlyName")).toString())
            << o.value(QStringLiteral("autopilotType")).toString()
            << o.value(QStringLiteral("airframeType")).toString()
            << o.value(QStringLiteral("firstSeen")).toString()
            << o.value(QStringLiteral("lastSeen")).toString()
            << QString::number(o.value(QStringLiteral("totalFlightCount")).toInt())
            << QString::number(o.value(QStringLiteral("totalFlightHours")).toDouble(), 'f', 2)
            << o.value(QStringLiteral("identitySource")).toString();
        csv += row.join(QStringLiteral(",")) + QStringLiteral("\n");
    }

    QString path = makePath(baseName, QStringLiteral("_vehicle_profiles.csv"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(csv.toUtf8());
    file.close();
    return path;
}

QString ExportHelper::saveBatteryHistoryCsv(const QString &baseName, const QString &batterySerial)
{
    QString cycles = DatabaseManager::instance().getBatteryCycles(batterySerial, 100);
    if (cycles.isEmpty() || cycles == QStringLiteral("[]")) return {};

    QJsonArray arr = QJsonDocument::fromJson(cycles.toUtf8()).array();
    QString csv = QStringLiteral("id,flightSessionId,capacityAtFullMah,voltageSagV,"
                                 "restingVoltageV,cycleCount,recordedAt\n");
    for (const QJsonValue &v : arr) {
        QJsonObject o = v.toObject();
        QStringList row;
        row << QString::number(o.value(QStringLiteral("id")).toInt())
            << QString::number(o.value(QStringLiteral("flightSessionId")).toInt())
            << QString::number(o.value(QStringLiteral("capacityAtFullMah")).toDouble(), 'f', 1)
            << QString::number(o.value(QStringLiteral("voltageSagV")).toDouble(), 'f', 3)
            << QString::number(o.value(QStringLiteral("restingVoltageV")).toDouble(), 'f', 3)
            << QString::number(o.value(QStringLiteral("cycleCount")).toInt())
            << o.value(QStringLiteral("recordedAt")).toString();
        csv += row.join(QStringLiteral(",")) + QStringLiteral("\n");
    }

    QString path = makePath(baseName, QStringLiteral("_battery_%1.csv").arg(batterySerial.simplified().replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"))));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(csv.toUtf8());
    file.close();
    return path;
}

QString ExportHelper::generateHash(const QString &data)
{
    QByteArray hash = QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

// ── Phase 8: Structured CSV export ─────────────────────────────────────
// Sectioned CSV files with [Section] headers that can be parsed by
// downstream tools.  Each export includes session metadata, check
// results, overrides, and optionally battery health data.

QString ExportHelper::escCsv(const QString &value)
{
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"')) || value.contains(QLatin1Char('\n'))) {
        QString escaped = value;
        escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return value;
}

/**
 * @brief Gather operator overrides from QSettings for inclusion in exports.
 *
 * Overrides are stored under "preflight_overrides/<sysId>" groups in
 * QSettings.  Returns CSV lines in the format: sysId,checkId,status:reason.
 */
QStringList ExportHelper::collectOverrideLines(const QString &deviceUid) const
{
    Q_UNUSED(deviceUid)
    // Overrides stored in QSettings under group "preflight_overrides/<sysId>"
    QSettings settings;
    QStringList lines;

    // List all override groups that match the device UID pattern
    QStringList children = settings.childGroups();
    for (const QString &group : children) {
        if (!group.startsWith(QStringLiteral("preflight_overrides/"))) continue;
        settings.beginGroup(group);
        QStringList keys = settings.childKeys();
        for (const QString &key : keys) {
            QString val = settings.value(key).toString();
            lines << QStringLiteral("%1,%2,%3")
                       .arg(escCsv(group.section(QLatin1Char('/'), 1)),
                            escCsv(key),
                            escCsv(val));
        }
        settings.endGroup();
    }
    return lines;
}

QString ExportHelper::saveSessionCsv(int sessionId, const QString &baseName)
{
    DatabaseManager &db = DatabaseManager::instance();

    // Get check results for this session
    QString checksJson = db.getCheckResults(sessionId);
    QJsonArray checks = QJsonDocument::fromJson(checksJson.toUtf8()).array();

    // Get device UID from session ID (infer from check results)
    QString deviceUid;
    if (!checks.isEmpty()) {
        deviceUid = checks.first().toObject().value(QStringLiteral("deviceUid")).toString();
    }

    // Build CSV
    QString csv = QStringLiteral("# Preflight Export — Session %1\n"
                                 "# Generated: %2\n"
                                 "# Format: session data + check results + overrides\n"
                                 "#\n").arg(sessionId)
                     .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    // Section: Session metadata
    csv += QStringLiteral("\n[Session]\n");
    csv += QStringLiteral("sessionId,%1\n").arg(sessionId);
    csv += QStringLiteral("deviceUid,%1\n").arg(escCsv(deviceUid));

    // Section: Check results
    csv += QStringLiteral("\n[Check Results]\n");
    csv += QStringLiteral("checkId,status,message,evaluatedAt\n");
    for (const QJsonValue &v : checks) {
        QJsonObject o = v.toObject();
        QStringList row;
        row << escCsv(o.value(QStringLiteral("checkId")).toString())
            << escCsv(o.value(QStringLiteral("status")).toString())
            << escCsv(o.value(QStringLiteral("message")).toString())
            << escCsv(o.value(QStringLiteral("evaluatedAt")).toString());
        csv += row.join(QStringLiteral(",")) + QStringLiteral("\n");
    }

    // Section: Overrides
    csv += QStringLiteral("\n[Overrides]\n");
    csv += QStringLiteral("sysId,checkId,status:reason\n");
    QStringList overrides = collectOverrideLines(deviceUid);
    for (const QString &line : overrides) {
        csv += line + QStringLiteral("\n");
    }

    QString path = makePath(baseName, QStringLiteral("_session_%1.csv").arg(sessionId));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(csv.toUtf8());
    file.close();
    return path;
}

QString ExportHelper::saveVehicleHistoryCsv(const QString &deviceUid, const QString &baseName)
{
    DatabaseManager &db = DatabaseManager::instance();

    // Vehicle profile
    QString vehicleJson = db.getVehicle(deviceUid);
    QJsonObject vehicle = QJsonDocument::fromJson(vehicleJson.toUtf8()).object();

    // All flight sessions
    QString sessionsJson = db.getFlightSessions(deviceUid, 100);
    QJsonArray sessions = QJsonDocument::fromJson(sessionsJson.toUtf8()).array();

    // Battery info
    QString batterySerial = vehicle.value(QStringLiteral("lastBatterySerial")).toString();

    QString csv = QStringLiteral("# Preflight Export — Vehicle History\n"
                                 "# Generated: %1\n"
                                 "# Vehicle: %2\n"
                                 "#\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate))
                     .arg(escCsv(deviceUid));

    // Section: Vehicle profile
    csv += QStringLiteral("\n[Vehicle Profile]\n");
    csv += QStringLiteral("field,value\n");
    auto addField = [&](const QString &key, const QString &label) {
        QJsonValue val = vehicle.value(key);
        if (!val.isUndefined()) {
            csv += escCsv(label) + QStringLiteral(",") + escCsv(
                val.isDouble() ? QString::number(val.toDouble(), 'f', 2)
                               : val.toString()) + QStringLiteral("\n");
        }
    };
    addField(QStringLiteral("deviceUid"),          QStringLiteral("Device UID"));
    addField(QStringLiteral("friendlyName"),        QStringLiteral("Friendly Name"));
    addField(QStringLiteral("autopilotType"),       QStringLiteral("Autopilot Type"));
    addField(QStringLiteral("airframeType"),        QStringLiteral("Airframe Type"));
    addField(QStringLiteral("totalFlightCount"),    QStringLiteral("Total Flights"));
    addField(QStringLiteral("totalFlightHours"),    QStringLiteral("Total Flight Hours"));
    addField(QStringLiteral("identitySource"),      QStringLiteral("Identity Source"));

    // Section: Flight sessions
    csv += QStringLiteral("\n[Flight Sessions]\n");
    csv += QStringLiteral("sessionId,startedAt,endedAt,durationSeconds,payloadWeightKg,batterySerial\n");
    for (const QJsonValue &v : sessions) {
        QJsonObject o = v.toObject();
        QStringList row;
        row << QString::number(o.value(QStringLiteral("id")).toInt())
            << escCsv(o.value(QStringLiteral("startedAt")).toString())
            << escCsv(o.value(QStringLiteral("endedAt")).toString())
            << QString::number(o.value(QStringLiteral("durationSeconds")).toDouble(), 'f', 1)
            << QString::number(o.value(QStringLiteral("payloadWeightKg")).toDouble(), 'f', 2)
            << escCsv(o.value(QStringLiteral("batterySerial")).toString());
        csv += row.join(QStringLiteral(",")) + QStringLiteral("\n");
    }

    // Section: Check results for each session
    csv += QStringLiteral("\n[Check Results]\n");
    csv += QStringLiteral("sessionId,checkId,status,message,evaluatedAt\n");
    for (const QJsonValue &v : sessions) {
        int sid = v.toObject().value(QStringLiteral("id")).toInt();
        QString checksJson = db.getCheckResults(sid);
        QJsonArray checks = QJsonDocument::fromJson(checksJson.toUtf8()).array();
        for (const QJsonValue &cv : checks) {
            QJsonObject co = cv.toObject();
            QStringList row;
            row << QString::number(sid)
                << escCsv(co.value(QStringLiteral("checkId")).toString())
                << escCsv(co.value(QStringLiteral("status")).toString())
                << escCsv(co.value(QStringLiteral("message")).toString())
                << escCsv(co.value(QStringLiteral("evaluatedAt")).toString());
            csv += row.join(QStringLiteral(",")) + QStringLiteral("\n");
        }
    }

    // Section: Overrides
    csv += QStringLiteral("\n[Overrides]\n");
    csv += QStringLiteral("sysId,checkId,status:reason\n");
    QStringList overrides = collectOverrideLines(deviceUid);
    for (const QString &line : overrides) {
        csv += line + QStringLiteral("\n");
    }

    // Section: Battery cycles
    if (!batterySerial.isEmpty()) {
        QString cyclesJson = db.getBatteryCycles(batterySerial, 100);
        QJsonArray cycles = QJsonDocument::fromJson(cyclesJson.toUtf8()).array();
        if (!cycles.isEmpty()) {
            csv += QStringLiteral("\n[Battery Cycles]\n");
            csv += QStringLiteral("id,flightSessionId,capacityAtFullMah,voltageSagV,restingVoltageV,cycleCount,recordedAt\n");
            for (const QJsonValue &v : cycles) {
                QJsonObject o = v.toObject();
                QStringList row;
                row << QString::number(o.value(QStringLiteral("id")).toInt())
                    << QString::number(o.value(QStringLiteral("flightSessionId")).toInt())
                    << QString::number(o.value(QStringLiteral("capacityAtFullMah")).toDouble(), 'f', 1)
                    << QString::number(o.value(QStringLiteral("voltageSagV")).toDouble(), 'f', 3)
                    << QString::number(o.value(QStringLiteral("restingVoltageV")).toDouble(), 'f', 3)
                    << QString::number(o.value(QStringLiteral("cycleCount")).toInt())
                    << escCsv(o.value(QStringLiteral("recordedAt")).toString());
                csv += row.join(QStringLiteral(",")) + QStringLiteral("\n");
            }
        }
    }

    QString path = makePath(baseName, QStringLiteral("_vehicle_%1.csv")
        .arg(deviceUid.simplified().replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"))));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(csv.toUtf8());
    file.close();
    return path;
}

/** @brief Stub — fleet-wide CSV export awaiting multi-vehicle support (Phase 5). */
QString ExportHelper::saveFleetCsv(const QString &baseName)
{
    // Stub: multi-vehicle / fleet-wide export not yet available
    QString path = makePath(baseName, QStringLiteral("_fleet_STUB.csv"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    QString content = QStringLiteral(
        "# Preflight Export — Fleet-Wide (STUB)\n"
        "# Generated: %1\n"
        "# Fleet-wide export requires Phase 5 (multi-vehicle support).\n"
        "# Export individual vehicles from the Maintenance page instead.\n"
    ).arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    file.write(content.toUtf8());
    file.close();
    return path;
}

// ── Phase 8: Human-readable HTML report ─────────────────────────────

/**
 * @brief Build a styled HTML report for a single preflight session.
 *
 * Includes summary counts (passed/failed/warned/skipped), a table of
 * all check results with color-coded badges, and any operator overrides.
 */
QString ExportHelper::buildSessionHtml(int sessionId, const QString &deviceUid) const
{
    DatabaseManager &db = DatabaseManager::instance();

    QString checksJson = db.getCheckResults(sessionId);
    QJsonArray checks = QJsonDocument::fromJson(checksJson.toUtf8()).array();

    QString vehicleJson = db.getVehicle(deviceUid);
    QJsonObject vehicle = QJsonDocument::fromJson(vehicleJson.toUtf8()).object();

    QStringList overrides = collectOverrideLines(deviceUid);

    QString html;
    html += QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    html += QStringLiteral("<title>Preflight Session Report</title>");
    html += QStringLiteral("<style>"
        "body{font-family:system-ui,-apple-ui,sans-serif;max-width:800px;margin:40px auto;padding:0 20px;color:#333;}"
        "h1{font-size:22px;margin-bottom:4px;}"
        ".meta{color:#666;font-size:13px;margin-bottom:24px;}"
        "table{width:100%;border-collapse:collapse;margin:12px 0 20px;}"
        "th,td{text-align:left;padding:6px 10px;border-bottom:1px solid #ddd;font-size:13px;}"
        "th{background:#f5f5f5;font-weight:600;}"
        "h2{font-size:16px;margin:20px 0 4px;border-bottom:1px solid #eee;padding-bottom:4px;}"
        ".pass{color:#1a7a1a;font-weight:bold;}"
        ".fail{color:#cc0000;font-weight:bold;}"
        ".warn{color:#b8860b;font-weight:bold;}"
        ".skip{color:#999;}"
        ".pending{color:#666;}"
        ".stale{color:#ff6600;}"
        ".badge{display:inline-block;padding:2px 8px;border-radius:4px;font-size:11px;font-weight:bold;color:#fff;}"
        ".badge-pass{background:#1a7a1a;}"
        ".badge-fail{background:#cc0000;}"
        ".badge-warn{background:#b8860b;}"
        ".badge-skip{background:#999;}"
        ".footer{color:#999;font-size:11px;text-align:center;margin-top:40px;border-top:1px solid #eee;padding-top:12px;}"
        ".section{margin-bottom:20px;}"
    "</style></head><body>");

    html += QStringLiteral("<h1>Preflight Session Report</h1>");
    html += QStringLiteral("<div class=\"meta\">Session #%1 &middot; %2 &middot; %3 %4</div>")
        .arg(sessionId)
        .arg(escHtml(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)))
        .arg(escHtml(vehicle.value(QStringLiteral("autopilotType")).toString()),
             escHtml(vehicle.value(QStringLiteral("airframeType")).toString()));

    // Summary
    int passed = 0, failed = 0, warned = 0, skipped = 0, pending = 0;
    for (const QJsonValue &v : checks) {
        QString s = v.toObject().value(QStringLiteral("status")).toString();
        if (s == QStringLiteral("Passed"))  ++passed;
        else if (s == QStringLiteral("Failed")) ++failed;
        else if (s == QStringLiteral("Warning")) ++warned;
        else if (s == QStringLiteral("Skipped")) ++skipped;
        else ++pending;
    }
    html += QStringLiteral("<div class=\"section\"><h2>Check Results</h2>");
    html += QStringLiteral("<p>%1 passed, %2 failed, %3 warnings, %4 skipped, %5 pending</p>")
        .arg(passed).arg(failed).arg(warned).arg(skipped).arg(pending);

    html += QStringLiteral("<table><tr><th>Check</th><th>Status</th><th>Message</th></tr>");
    for (const QJsonValue &v : checks) {
        QJsonObject o = v.toObject();
        QString status = o.value(QStringLiteral("status")).toString();
        QString cls;
        QString badgeColor;
        if (status == QStringLiteral("Passed")) {
            cls = QStringLiteral("pass"); badgeColor = QStringLiteral("badge-pass");
        } else if (status == QStringLiteral("Failed")) {
            cls = QStringLiteral("fail"); badgeColor = QStringLiteral("badge-fail");
        } else if (status == QStringLiteral("Warning")) {
            cls = QStringLiteral("warn"); badgeColor = QStringLiteral("badge-warn");
        } else {
            cls = QStringLiteral("skip"); badgeColor = QStringLiteral("badge-skip");
        }
        html += QStringLiteral("<tr><td>%1</td>"
                               "<td><span class=\"badge %2\">%3</span></td>"
                               "<td>%4</td></tr>")
            .arg(escHtml(o.value(QStringLiteral("checkId")).toString()))
            .arg(badgeColor, escHtml(status))
            .arg(escHtml(o.value(QStringLiteral("message")).toString()));
    }
    html += QStringLiteral("</table></div>");

    // Overrides
    if (!overrides.isEmpty()) {
        html += QStringLiteral("<div class=\"section\"><h2>Overrides</h2><table><tr>"
                               "<th>System</th><th>Check</th><th>Override Details</th></tr>");
        for (const QString &line : overrides) {
            QStringList parts = line.split(QLatin1Char(','));
            if (parts.size() >= 3) {
                html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                    .arg(escHtml(parts[0]), escHtml(parts[1]), escHtml(parts.mid(2).join(QStringLiteral(","))));
            }
        }
        html += QStringLiteral("</table></div>");
    }

    html += QStringLiteral("<div class=\"footer\">"
                           "Generated by UAV Preflight &mdash; %1"
                           "<br>This is a preflight system export, not an autopilot flight log.</div>")
        .arg(escHtml(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    html += QStringLiteral("</body></html>");
    return html;
}

/**
 * @brief Build a styled HTML report for a vehicle's full history.
 *
 * Sections: vehicle summary stats, flight session table, battery cycle
 * history with capacity/sag/voltage, and operator override history.
 */
QString ExportHelper::buildVehicleHtml(const QString &deviceUid) const
{
    DatabaseManager &db = DatabaseManager::instance();

    QString vehicleJson = db.getVehicle(deviceUid);
    QJsonObject vehicle = QJsonDocument::fromJson(vehicleJson.toUtf8()).object();
    QString sessionsJson = db.getFlightSessions(deviceUid, 100);
    QJsonArray sessions = QJsonDocument::fromJson(sessionsJson.toUtf8()).array();
    QString batterySerial = vehicle.value(QStringLiteral("lastBatterySerial")).toString();
    QStringList overrides = collectOverrideLines(deviceUid);

    QString html;
    html += QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    html += QStringLiteral("<title>Vehicle History Report</title>");
    html += QStringLiteral("<style>"
        "body{font-family:system-ui,-apple-ui,sans-serif;max-width:800px;margin:40px auto;padding:0 20px;color:#333;}"
        "h1{font-size:22px;margin-bottom:4px;}"
        ".meta{color:#666;font-size:13px;margin-bottom:24px;}"
        "table{width:100%;border-collapse:collapse;margin:12px 0 20px;}"
        "th,td{text-align:left;padding:6px 10px;border-bottom:1px solid #ddd;font-size:13px;}"
        "th{background:#f5f5f5;font-weight:600;}"
        "h2{font-size:16px;margin:20px 0 4px;border-bottom:1px solid #eee;padding-bottom:4px;}"
        ".badge{display:inline-block;padding:2px 8px;border-radius:4px;font-size:11px;font-weight:bold;color:#fff;}"
        ".badge-pass{background:#1a7a1a;}"
        ".badge-fail{background:#cc0000;}"
        ".badge-warn{background:#b8860b;}"
        ".badge-skip{background:#999;}"
        ".footer{color:#999;font-size:11px;text-align:center;margin-top:40px;border-top:1px solid #eee;padding-top:12px;}"
        ".section{margin-bottom:20px;}"
    "</style></head><body>");

    html += QStringLiteral("<h1>Vehicle History Report</h1>");
    html += QStringLiteral("<div class=\"meta\">Vehicle: %1 &middot; %2 %3 &middot; Generated: %4</div>")
        .arg(escHtml(deviceUid),
             escHtml(vehicle.value(QStringLiteral("autopilotType")).toString()),
             escHtml(vehicle.value(QStringLiteral("airframeType")).toString()),
             escHtml(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

    // Summary stats
    double totalHours = vehicle.value(QStringLiteral("totalFlightHours")).toDouble();
    int totalFlights = vehicle.value(QStringLiteral("totalFlightCount")).toInt();
    html += QStringLiteral("<div class=\"section\"><h2>Summary</h2><table>"
                           "<tr><td>Total Flights</td><td>%1</td></tr>"
                           "<tr><td>Total Flight Hours</td><td>%2</td></tr>"
                           "<tr><td>Identity Source</td><td>%3</td></tr>"
                           "</table></div>")
        .arg(totalFlights)
        .arg(totalHours, 0, 'f', 1)
        .arg(escHtml(vehicle.value(QStringLiteral("identitySource")).toString()));

    // Sessions
    html += QStringLiteral("<div class=\"section\"><h2>Flight Sessions</h2>");
    if (sessions.isEmpty()) {
        html += QStringLiteral("<p>No flights recorded.</p>");
    } else {
        html += QStringLiteral("<table><tr><th>Session ID</th><th>Start</th><th>Duration</th>"
                               "<th>Payload (kg)</th><th>Battery</th></tr>");
        for (const QJsonValue &v : sessions) {
            QJsonObject o = v.toObject();
            double secs = o.value(QStringLiteral("durationSeconds")).toDouble();
            QString dur;
            if (secs > 0) {
                int m = static_cast<int>(secs) / 60;
                int s = static_cast<int>(secs) % 60;
                dur = QStringLiteral("%1m %2s").arg(m).arg(s);
            } else {
                dur = QStringLiteral("—");
            }
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                .arg(o.value(QStringLiteral("id")).toInt())
                .arg(escHtml(o.value(QStringLiteral("startedAt")).toString()))
                .arg(dur)
                .arg(o.value(QStringLiteral("payloadWeightKg")).toDouble(), 0, 'f', 2)
                .arg(escHtml(o.value(QStringLiteral("batterySerial")).toString()));
        }
        html += QStringLiteral("</table>");
    }
    html += QStringLiteral("</div>");

    // Battery cycles
    if (!batterySerial.isEmpty()) {
        QString cyclesJson = db.getBatteryCycles(batterySerial, 100);
        QJsonArray cycles = QJsonDocument::fromJson(cyclesJson.toUtf8()).array();
        if (!cycles.isEmpty()) {
            html += QStringLiteral("<div class=\"section\"><h2>Battery: %1</h2><table><tr>"
                                   "<th>Cycle</th><th>Capacity (mAh)</th><th>Sag (V)</th>"
                                   "<th>Resting (V)</th></tr>").arg(escHtml(batterySerial));
            for (const QJsonValue &v : cycles) {
                QJsonObject o = v.toObject();
                html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                    .arg(o.value(QStringLiteral("cycleCount")).toInt())
                    .arg(o.value(QStringLiteral("capacityAtFullMah")).toDouble(), 0, 'f', 1)
                    .arg(o.value(QStringLiteral("voltageSagV")).toDouble(), 0, 'f', 3)
                    .arg(o.value(QStringLiteral("restingVoltageV")).toDouble(), 0, 'f', 3);
            }
            html += QStringLiteral("</table></div>");
        }
    }

    // Overrides
    if (!overrides.isEmpty()) {
        html += QStringLiteral("<div class=\"section\"><h2>Override History</h2><table><tr>"
                               "<th>System</th><th>Check</th><th>Details</th></tr>");
        for (const QString &line : overrides) {
            QStringList parts = line.split(QLatin1Char(','));
            if (parts.size() >= 3) {
                html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                    .arg(escHtml(parts[0]), escHtml(parts[1]), escHtml(parts.mid(2).join(QStringLiteral(","))));
            }
        }
        html += QStringLiteral("</table></div>");
    }

    html += QStringLiteral("<div class=\"footer\">"
                           "Generated by UAV Preflight &mdash; %1"
                           "<br>This is a preflight system export, not an autopilot flight log.</div>")
        .arg(escHtml(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
    html += QStringLiteral("</body></html>");
    return html;
}

/** @brief Build and save a session HTML report to disk. */
QString ExportHelper::saveSessionReport(int sessionId, const QString &baseName, const QString &deviceUid)
{
    QString html = buildSessionHtml(sessionId, deviceUid);
    if (html.isEmpty()) return {};
    QString path = makePath(baseName, QStringLiteral("_session_%1_report.html").arg(sessionId));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(html.toUtf8());
    file.close();
    return path;
}

/** @brief Build and save a full vehicle history HTML report to disk. */
QString ExportHelper::saveVehicleReport(const QString &deviceUid, const QString &baseName)
{
    QString html = buildVehicleHtml(deviceUid);
    if (html.isEmpty()) return {};
    QString sanitized = deviceUid.simplified().replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"));
    QString path = makePath(baseName, QStringLiteral("_vehicle_%1_history.html").arg(sanitized));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(html.toUtf8());
    file.close();
    return path;
}
