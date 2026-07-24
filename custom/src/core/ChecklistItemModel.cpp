#include "ChecklistItemModel.h"
#include <QDebug>

ChecklistItemModel::ChecklistItemModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChecklistItemModel::rowCount(const QModelIndex &) const
{
    return m_items.size();
}

// Map each ChecklistItemData field to its corresponding role for QML access.
QVariant ChecklistItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const auto &item = m_items.at(index.row());
    switch (role) {
    case IdRole:             return item.id;
    case LabelRole:          return item.label;
    case BindPropertyRole:   return item.bindProperty;
    case RequiredValueRole:  return item.requiredValue;
    case ToleranceRole:      return item.tolerance;
    case UnitRole:           return item.unit;
    case IsManualRole:       return item.isManual;
    case StatusRole:         return item.status;
    case MessageRole:        return item.message;
    case Qt::DisplayRole:    return item.label;
    }
    return {};
}

QHash<int, QByteArray> ChecklistItemModel::roleNames() const
{
    return {
        { IdRole,             "itemId" },
        { LabelRole,          "label" },
        { BindPropertyRole,   "bindProperty" },
        { RequiredValueRole,  "requiredValue" },
        { ToleranceRole,      "tolerance" },
        { UnitRole,           "unit" },
        { IsManualRole,       "isManual" },
        { StatusRole,         "status" },
        { MessageRole,        "message" },
    };
}

// Load checklist items from a JSON array. Each object should have keys like
// "id", "label", "bindProperty", "requiredValue", "tolerance", "unit", "isManual".
// Performs a full model reset — QML views will be rebuilt.
void ChecklistItemModel::loadFromJson(const QJsonArray &items)
{
    beginResetModel();
    m_items.clear();
    m_items.reserve(items.size());
    for (const auto &val : items) {
        QJsonObject obj = val.toObject();
        ChecklistItemData d;
        d.id             = obj.value(QStringLiteral("id")).toString();
        d.label          = obj.value(QStringLiteral("label")).toString(d.id);
        d.bindProperty   = obj.value(QStringLiteral("bindProperty")).toString();
        d.requiredValue  = obj.value(QStringLiteral("requiredValue")).toDouble();
        d.tolerance      = obj.value(QStringLiteral("tolerance")).toDouble();
        d.unit           = obj.value(QStringLiteral("unit")).toString();
        d.isManual       = obj.value(QStringLiteral("isManual")).toBool();
        d.status         = 0;
        m_items.append(d);
    }
    endResetModel();
    emit countChanged();
    qDebug().noquote() << QStringLiteral("ChecklistItemModel: loaded %1 items").arg(m_items.size());
}

// Update a single item's status and message, then notify QML of the change.
// Skips the update if status and message are unchanged to avoid unnecessary repaints.
void ChecklistItemModel::setItemStatus(int row, int status, const QString &message)
{
    if (row < 0 || row >= m_items.size())
        return;
    auto &item = m_items[row];
    if (item.status == status && item.message == message)
        return;
    item.status = status;
    item.message = message;
    emit dataChanged(index(row), index(row));
    emit itemStatusChanged(row, status, message);
}

// Reset all items to Pending status (status=0) and clear messages.
// Emits a single dataChanged spanning the entire model range.
void ChecklistItemModel::resetAll()
{
    for (int i = 0; i < m_items.size(); ++i) {
        m_items[i].status = 0;
        m_items[i].message.clear();
    }
    emit dataChanged(index(0), index(qMax(0, m_items.size() - 1)));
}

int ChecklistItemModel::pendingCount() const
{
    int n = 0;
    for (const auto &item : m_items) {
        if (item.status == 0)
            ++n;
    }
    return n;
}
