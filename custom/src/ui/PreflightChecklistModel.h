#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QByteArray>

class PreflightManager;

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

    int criticalCount() const;
    int passedCount() const;
    int pendingCount() const;
    int warnCount() const;
    int blockingFailedCount() const;
    int completionPercent() const;
    QString firstBlockingFailure() const;
    QString statusSummary() const;

    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private slots:
    void rebuild();

private:
    PreflightManager *m_manager = nullptr;
};
