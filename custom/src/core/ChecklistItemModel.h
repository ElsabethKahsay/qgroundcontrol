#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

struct ChecklistItemData {
    QString id;
    QString label;
    QString bindProperty;
    double requiredValue = 0.0;
    double tolerance = 0.0;
    QString unit;
    bool isManual = false;
    int status = 0;       // 0=Pending, 1=Passed, 2=Failed
    QString message;
};

class ChecklistItemModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        BindPropertyRole,
        RequiredValueRole,
        ToleranceRole,
        UnitRole,
        IsManualRole,
        StatusRole,
        MessageRole,
    };

    explicit ChecklistItemModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void loadFromJson(const QJsonArray &items);
    Q_INVOKABLE void setItemStatus(int row, int status, const QString &message = {});
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE int pendingCount() const;

    const QVector<ChecklistItemData> &items() const { return m_items; }

signals:
    void countChanged();
    void itemStatusChanged(int row, int status, const QString &message);

private:
    QVector<ChecklistItemData> m_items;
};
