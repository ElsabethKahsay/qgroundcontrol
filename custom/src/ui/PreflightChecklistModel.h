/**
 * @file PreflightChecklistModel.h
 * @brief QAbstractListModel wrapping preflight checks for display in QML.
 *
 * Each row corresponds to a single AbstractCheck owned by PreflightManager.
 * The model exposes per-check data (label, status, category, type, etc.)
 * through Qt roles, and provides summary properties (counts, completion %,
 * first blocking failure) so the QML UI can render overall preflight status
 * without querying individual rows.
 *
 * PreflightManager is the source of truth; this model rebuilds entirely when
 * the check list changes and emits incremental data updates when only statuses
 * change.
 */

#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QByteArray>

class PreflightManager;

/// QAbstractListModel that exposes preflight checklist items to QML.
/// Each row corresponds to one AbstractCheck (sensor, manual, action, etc.).
/// Provides summary properties (counts, completion %, first blocking failure)
/// so the QML UI can display overall preflight status at a glance.
class PreflightChecklistModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int criticalCount READ criticalCount NOTIFY countChanged)
    Q_PROPERTY(int passedCount READ passedCount NOTIFY countChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY countChanged)
    Q_PROPERTY(int warnCount READ warnCount NOTIFY countChanged)
    Q_PROPERTY(int blockingFailedCount READ blockingFailedCount NOTIFY countChanged)
    Q_PROPERTY(int completionPercent READ completionPercent NOTIFY countChanged)
    Q_PROPERTY(QString firstBlockingFailure READ firstBlockingFailure NOTIFY countChanged)
    Q_PROPERTY(QString statusSummary READ statusSummary NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        StatusRole,
        MessageRole,
        CategoryRole,
        TypeRole,
        CurrentValueRole,
        RecommendedActionRole,
        UserMessageRole,
        IsManualRole,
        MandatoryRole,
        CheckObjectRole,
        RationaleRole,
        FixStepsRole,
        ThresholdRole,
        CurrentValueStringRole,
        IsActionRole,
        ActionButtonTextRole
    };
    Q_ENUM(Role)

    explicit PreflightChecklistModel(QObject *parent = nullptr);

    /// Binds the model to a PreflightManager. Connects rebuild/data-change
    /// signals so the model stays in sync with the check list.
    void setPreflightManager(PreflightManager *mgr);

    /// Number of checks (rows) in the model.
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /// Returns the value for a single role at the given row, mapping Qt roles
    /// to AbstractCheck accessors (label, status, category, etc.).
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /// Maps custom Role enum values to QML property names (e.g. "checkId", "label").
    QHash<int, QByteArray> roleNames() const override;

    /// Count of checks with failed or warning status (statusInt 2, 3, or 4).
    int criticalCount() const;
    int passedCount() const;
    int pendingCount() const;
    int warnCount() const;
    /// Count of mandatory checks that have failed (blocks takeoff).
    int blockingFailedCount() const;
    int completionPercent() const;
    /// Label of the first mandatory check that failed, for prominent UI display.
    QString firstBlockingFailure() const;
    QString statusSummary() const;

    /// Return all role data for a given row as a QVariantMap (QML-friendly helper).
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private slots:
    void rebuild();

private:
    PreflightManager *m_manager = nullptr;
};
