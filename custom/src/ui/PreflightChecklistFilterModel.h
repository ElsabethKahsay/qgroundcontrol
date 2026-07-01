#pragma once
#include <QSortFilterProxyModel>
#include <QAbstractListModel>

class PreflightChecklistFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int categoryId READ categoryId WRITE setCategoryId NOTIFY categoryIdChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit PreflightChecklistFilterModel(QObject *parent = nullptr);

    int categoryId() const { return m_categoryId; }
    void setCategoryId(int id);
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void categoryIdChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_categoryId = -1;
};
