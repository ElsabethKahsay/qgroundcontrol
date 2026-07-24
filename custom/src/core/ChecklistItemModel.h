/**
 * @file ChecklistItemModel.h
 * @brief QAbstractListModel providing checklist data to QML for the preflight UI.
 *
 * Each row represents a single checklist item with fields for telemetry binding,
 * required value, tolerance, and current status. The ChecklistEngine populates
 * items from JSON and drives status updates; QML binds to the roles for display.
 */

#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

/// Data for a single checklist item. Populated from JSON by ChecklistItemModel::loadFromJson().
struct ChecklistItemData {
    QString id;               ///< Unique item identifier
    QString label;            ///< Display label for the checklist UI
    QString bindProperty;     ///< TelemetryBridge property name to monitor (empty for manual items)
    double requiredValue = 0.0; ///< Expected value for pass condition
    double tolerance = 0.0;  ///< Allowed deviation from requiredValue (±)
    QString unit;             ///< Display unit (e.g. "V", "sat")
    bool isManual = false;    ///< If true, requires operator confirmation (no auto-evaluation)
    int status = 0;           ///< 0=Pending, 1=Passed, 2=Failed
    QString message;          ///< Human-readable status detail (e.g. "12.4V (ok: 11.0–14.0)")
};

/**
 * List model exposing checklist items to QML.
 *
 * Roles map directly to ChecklistItemData fields for use in ListView delegates.
 * ChecklistEngine calls setItemStatus() to update items; the model emits
 * dataChanged() to trigger QML UI updates.
 */
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
    /// Update the status and message for a single item, emitting dataChanged.
    Q_INVOKABLE void setItemStatus(int row, int status, const QString &message = {});
    /// Reset all items to Pending (status=0).
    Q_INVOKABLE void resetAll();
    /// Count of items still in Pending status.
    Q_INVOKABLE int pendingCount() const;

    const QVector<ChecklistItemData> &items() const { return m_items; }

signals:
    void countChanged();
    void itemStatusChanged(int row, int status, const QString &message);

private:
    QVector<ChecklistItemData> m_items;
};
