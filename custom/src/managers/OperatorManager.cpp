#include "OperatorManager.h"
#include "DatabaseManager.h"

#include <QSettings>

static OperatorManager *s_instance = nullptr;

OperatorManager *OperatorManager::instance()
{
    return s_instance;
}

OperatorManager::OperatorManager(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
    _loadFromSettings();
}

void OperatorManager::loadOperators()
{
    m_operators.clear();
    const QList<QVariantMap> ops = DatabaseManager::instance().getAllOperators();
    for (const QVariantMap &op : ops) {
        QVariantMap entry;
        entry["id"] = op.value("id");
        entry["name"] = op.value("name");
        entry["role"] = op.value("role");
        entry["total_flights"] = op.value("total_flights");
        entry["total_training"] = op.value("total_training");
        m_operators.append(QVariant::fromValue(entry));
    }
    emit operatorsChanged();
}

int OperatorManager::addOperator(const QString& name, const QString& role)
{
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return -1;

    for (const QVariant &v : m_operators) {
        QVariantMap existing = v.toMap();
        if (existing.value("name").toString().compare(trimmed, Qt::CaseInsensitive) == 0)
            return -1;
    }

    int id = DatabaseManager::instance().insertOperator(trimmed, role);
    if (id > 0)
        loadOperators();
    return id;
}

void OperatorManager::selectOperator(int operatorId)
{
    for (const QVariant &v : m_operators) {
        QVariantMap op = v.toMap();
        if (op.value("id").toInt() == operatorId) {
            m_currentId = operatorId;
            m_currentName = op.value("name").toString();
            m_currentRole = op.value("role").toString();
            QSettings settings;
            settings.setValue(QStringLiteral("session/lastOperatorId"), operatorId);
            emit currentOperatorChanged();
            return;
        }
    }
}

void OperatorManager::selectByName(const QString& name)
{
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;

    for (const QVariant &v : m_operators) {
        QVariantMap op = v.toMap();
        if (op.value("name").toString().compare(trimmed, Qt::CaseInsensitive) == 0) {
            selectOperator(op.value("id").toInt());
            return;
        }
    }
}

void OperatorManager::clearCurrentOperator()
{
    m_currentId = -1;
    m_currentName.clear();
    m_currentRole.clear();
    QSettings settings;
    settings.remove(QStringLiteral("session/lastOperatorId"));
    emit currentOperatorChanged();
}

QVariantMap OperatorManager::getOperatorById(int operatorId)
{
    for (const QVariant &v : m_operators) {
        QVariantMap op = v.toMap();
        if (op.value("id").toInt() == operatorId)
            return op;
    }
    return QVariantMap();
}

void OperatorManager::_loadFromSettings()
{
    QSettings settings;
    int savedId = settings.value(QStringLiteral("session/lastOperatorId"), -1).toInt();
    if (savedId > 0) {
        loadOperators();
        selectOperator(savedId);
    }
}
