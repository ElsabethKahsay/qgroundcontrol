#include "PreflightChecklistFilterModel.h"

#include "PreflightChecklistModel.h"

PreflightChecklistFilterModel::PreflightChecklistFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    // Keep the QML-visible count property in sync as rows change.
    connect(this, &QAbstractItemModel::rowsInserted, this, &PreflightChecklistFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &PreflightChecklistFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &PreflightChecklistFilterModel::countChanged);
}

void PreflightChecklistFilterModel::setCategoryId(int id)
{
    if (m_categoryId != id) {
        m_categoryId = id;
        emit categoryIdChanged();
        invalidateFilter();
        // Full reset ensures QML views immediately reflect the filtered set.
        beginResetModel();
        endResetModel();
    }
}

/// Accept a row if no category filter is set (-1) or if the check's category matches.
bool PreflightChecklistFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_categoryId < 0)
        return true;
    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    int cat = sourceModel()->data(idx, PreflightChecklistModel::CategoryRole).toInt();
    return cat == m_categoryId;
}

QVariantMap PreflightChecklistFilterModel::get(int row) const
{
    auto* src = qobject_cast<PreflightChecklistModel*>(sourceModel());
    if (!src) return {};
    QModelIndex proxyIndex = index(row, 0);
    if (!proxyIndex.isValid()) return {};
    QModelIndex srcIndex = mapToSource(proxyIndex);
    if (!srcIndex.isValid()) return {};
    return src->get(srcIndex.row());
}
