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

    void setPreflightManager(PreflightManager *mgr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
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
