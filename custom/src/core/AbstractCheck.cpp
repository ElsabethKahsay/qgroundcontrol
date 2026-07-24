#include "AbstractCheck.h"

#include <QDebug>
#include <QJsonObject>
#include <QMetaObject>
#include <QMetaProperty>

#include "DatabaseManager.h"
#include "TelemetryBridge.h"

#include <cmath>

AbstractCheck::AbstractCheck(const QString &id, const QString &label,
                             CheckCategory category, CheckType type,
                             bool mandatory, bool canOverride, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_label(label)
    , m_category(category)
    , m_type(type)
    , m_mandatory(mandatory)
    , m_canOverride(canOverride)
{
}

void AbstractCheck::applyVehicleConfig(const QJsonObject &config)
{
    Q_UNUSED(config)
}

QString AbstractCheck::statusText() const
{
    switch (m_status) {
    case CheckStatus::Pending: return QStringLiteral("pending");
    case CheckStatus::Passed:  return QStringLiteral("passed");
    case CheckStatus::Failed:  return QStringLiteral("failed");
    case CheckStatus::Warning: return QStringLiteral("warning");
    case CheckStatus::Error:   return QStringLiteral("error");
    case CheckStatus::Skipped: return QStringLiteral("skipped");
    case CheckStatus::Stale:   return QStringLiteral("stale");
    }
    return QStringLiteral("pending");
}

bool AbstractCheck::requiresReevaluation() const
{
    // Manual and action checks only change via explicit user interaction
    if (m_type != CheckType::Auto)
        return false;

    if (!m_lastEvalTime.isValid())
        return true;
    if (m_status == CheckStatus::Passed)
        return false;
    return m_lastEvalTime.msecsTo(QDateTime::currentDateTime()) > configInt(QStringLiteral("stale_timeout_ms"), 5000);
}

bool AbstractCheck::overrideStatus(const QString &newStatus, const QString &reason)
{
    if (!m_canOverride || m_type == CheckType::Auto)
        return false;

    CheckStatus s = statusFromString(newStatus);
    if (s == m_status)
        return false;

    OverrideRecord rec;
    rec.checkId = m_id;
    rec.previousStatus = statusText();
    rec.newStatus = newStatus;
    rec.reason = reason.isEmpty() ? QStringLiteral("Operator override") : reason;
    rec.timestamp = QDateTime::currentDateTime();
    m_overrideHistory.append(rec);

    setStatus(s, reason);
    emit overrideLogged(m_id, rec.previousStatus, rec.newStatus, reason);
    return true;
}

bool AbstractCheck::confirm(const QString &reason)
{
    if (m_status == CheckStatus::Passed)
        return false;

    OverrideRecord rec;
    rec.checkId = m_id;
    rec.previousStatus = statusText();
    rec.newStatus = QStringLiteral("passed");
    rec.reason = reason.isEmpty() ? QStringLiteral("Operator confirmed") : reason;
    rec.timestamp = QDateTime::currentDateTime();
    m_overrideHistory.append(rec);

    setStatus(CheckStatus::Passed, rec.reason);
    emit overrideLogged(m_id, rec.previousStatus, rec.newStatus, rec.reason);
    return true;
}

void AbstractCheck::reset()
{
    m_status = CheckStatus::Pending;
    m_message.clear();
    m_lastEvalTime = QDateTime();
    m_overrideHistory.clear();
    m_configCache.clear();
    emit statusChanged(m_id, static_cast<int>(m_status));
    emit messageChanged(m_id, m_message);
}

void AbstractCheck::setStatus(CheckStatus newStatus, const QString &message)
{
    if (m_status == newStatus && m_message == message)
        return;

    CheckStatus oldStatus = m_status;
    m_status = newStatus;
    m_lastEvalTime = QDateTime::currentDateTime();

    if (!message.isEmpty())
        m_message = message;

    emit statusChanged(m_id, static_cast<int>(m_status));
    if (!message.isEmpty())
        emit messageChanged(m_id, m_message);

    if (newStatus == CheckStatus::Passed && oldStatus != CheckStatus::Passed)
        emit checkPassed(m_id);
    else if (newStatus == CheckStatus::Failed && oldStatus != CheckStatus::Failed)
        emit checkFailed(m_id, m_message);
}

void AbstractCheck::setCurrentValue(const QVariant &value)
{
    if (m_currentValue == value)
        return;
    m_currentValue = value;
    emit currentValueChanged(m_id, value);
}

bool AbstractCheck::hasTelemetry() const
{
    if (!m_telemetry)
        return false;
    QVariant connected = m_telemetry->property("isConnected");
    if (!connected.isValid() || !connected.toBool())
        return false;
    QVariant quality = m_telemetry->property("connectionQuality");
    if (!quality.isValid() || quality.toInt() < 10)
        return false;
    return true;
}

bool AbstractCheck::isParamAvailable(const QString &prop) const
{
    if (!m_telemetry) return false;
#ifdef QT_DEBUG
    { auto it = m_testOverrides.constFind(prop); if (it != m_testOverrides.constEnd()) return true; }
#endif
    QVariant dyn = m_telemetry->property(prop.toLatin1().constData());
    if (dyn.isValid()) return true;
    if (prop.startsWith(QLatin1String("param_"))) {
        dyn = m_telemetry->property(prop.mid(6).toLatin1().constData());
        if (dyn.isValid()) return true;
    }
    return false;
}

double AbstractCheck::getTelemetryDouble(const QString &prop) const
{
    if (!m_telemetry) return qQNaN();
#ifdef QT_DEBUG
    { auto it = m_testOverrides.constFind(prop); if (it != m_testOverrides.constEnd()) return it->toDouble(); }
#endif
    const QMetaObject *meta = m_telemetry->metaObject();
    int idx = meta->indexOfProperty(prop.toLatin1().constData());
    if (idx < 0) {
        // Fallback: try dynamic property (setProperty from _loadParameters)
        QVariant dyn = m_telemetry->property(prop.toLatin1().constData());
        if (dyn.isValid())
            return dyn.toDouble();
        // Try without param_ prefix — params are stored as raw names (e.g. "RTL_ALT")
        if (prop.startsWith(QLatin1String("param_"))) {
            dyn = m_telemetry->property(prop.mid(6).toLatin1().constData());
            if (dyn.isValid())
                return dyn.toDouble();
        }
        return qQNaN();
    }
    bool ok = false;
    double val = meta->property(idx).read(m_telemetry).toDouble(&ok);
    return ok ? val : qQNaN();
}

bool AbstractCheck::getTelemetryBool(const QString &prop) const
{
    if (!m_telemetry) return false;
#ifdef QT_DEBUG
    { auto it = m_testOverrides.constFind(prop); if (it != m_testOverrides.constEnd()) return it->toBool(); }
#endif
    const QMetaObject *meta = m_telemetry->metaObject();
    int idx = meta->indexOfProperty(prop.toLatin1().constData());
    if (idx < 0) {
        // Fallback: try dynamic property
        QVariant dyn = m_telemetry->property(prop.toLatin1().constData());
        if (dyn.isValid())
            return dyn.toBool();
        qWarning() << "AbstractCheck: unknown property" << prop << "for check" << m_id;
        return false;
    }
    return meta->property(idx).read(m_telemetry).toBool();
}

QVariant AbstractCheck::getTelemetryVariant(const QString &prop) const
{
    if (!m_telemetry) return {};
#ifdef QT_DEBUG
    { auto it = m_testOverrides.constFind(prop); if (it != m_testOverrides.constEnd()) return *it; }
#endif
    const QMetaObject *meta = m_telemetry->metaObject();
    int idx = meta->indexOfProperty(prop.toLatin1().constData());
    if (idx < 0) {
        QVariant dyn = m_telemetry->property(prop.toLatin1().constData());
        if (dyn.isValid())
            return dyn;
        qWarning() << "AbstractCheck: unknown property" << prop << "for check" << m_id;
        return {};
    }
    return meta->property(idx).read(m_telemetry);
}

double AbstractCheck::configDouble(const QString &key, double defaultVal) const
{
    auto it = m_configCache.constFind(key);
    if (it != m_configCache.constEnd())
        return it->toDouble();
    QString val = DatabaseManager::instance().getCheckConfig(m_id, key);
    if (val.isEmpty()) {
        m_configCache.insert(key, QVariant(defaultVal));
        return defaultVal;
    }
    bool ok = false;
    double result = val.toDouble(&ok);
    m_configCache.insert(key, QVariant(ok ? result : defaultVal));
    return ok ? result : defaultVal;
}

int AbstractCheck::configInt(const QString &key, int defaultVal) const
{
    auto it = m_configCache.constFind(key);
    if (it != m_configCache.constEnd())
        return it->toInt();
    QString val = DatabaseManager::instance().getCheckConfig(m_id, key);
    if (val.isEmpty()) {
        m_configCache.insert(key, QVariant(defaultVal));
        return defaultVal;
    }
    bool ok = false;
    int result = val.toInt(&ok);
    m_configCache.insert(key, QVariant(ok ? result : defaultVal));
    return ok ? result : defaultVal;
}

void AbstractCheck::clearConfigCache() const
{
    m_configCache.clear();
}

QString AbstractCheck::getUserMessage() const
{
    if (!m_message.isEmpty())
        return m_message;

    switch (m_status) {
    case CheckStatus::Pending: return QStringLiteral("Waiting for evaluation...");
    case CheckStatus::Passed:  return QStringLiteral("Check passed");
    case CheckStatus::Failed:  return QStringLiteral("Check failed \u2014 review required");
    case CheckStatus::Warning: return QStringLiteral("Check passed with warning");
    case CheckStatus::Error:   return QStringLiteral("Evaluation error");
    case CheckStatus::Skipped: return QStringLiteral("Check skipped");
    case CheckStatus::Stale:   return QStringLiteral("No recent telemetry - waiting for data");
    }
    return {};
}

QString AbstractCheck::getRecommendedAction() const
{
    switch (m_status) {
    case CheckStatus::Pending:
        return QStringLiteral("Wait for telemetry data");
    case CheckStatus::Failed:
        if (m_type == CheckType::Auto)
            return QStringLiteral("Review %1 and resolve before arming").arg(m_label);
        else
            return QStringLiteral("Manually confirm %1").arg(m_label);
    case CheckStatus::Warning:
        return QStringLiteral("Investigate %1 before flight").arg(m_label);
    case CheckStatus::Error:
        return QStringLiteral("Restart checklist or reconnect vehicle");
    case CheckStatus::Skipped:
        return QStringLiteral("Verify this check is not required");
    default:
        return {};
    }
}

QString AbstractCheck::getRationale() const
{
    return QStringLiteral("Safety check required before flight — ensures vehicle operates within safe parameters.");
}

QStringList AbstractCheck::getFixSteps() const
{
    return {
        QStringLiteral("Check the vehicle status and telemetry data"),
        QStringLiteral("Resolve any reported issues"),
        QStringLiteral("Re-run the check to verify")
    };
}

QString AbstractCheck::getCurrentValueString() const
{
    return m_currentValue.toString();
}

QString AbstractCheck::getThreshold() const
{
    return {};
}

CheckStatus AbstractCheck::statusFromString(const QString &s) const
{
    if (s == QStringLiteral("passed")) return CheckStatus::Passed;
    if (s == QStringLiteral("failed")) return CheckStatus::Failed;
    if (s == QStringLiteral("warning")) return CheckStatus::Warning;
    if (s == QStringLiteral("error")) return CheckStatus::Error;
    if (s == QStringLiteral("skipped")) return CheckStatus::Skipped;
    return CheckStatus::Pending;
}
