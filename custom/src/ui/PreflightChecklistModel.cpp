#include "PreflightChecklistModel.h"

#include "AbstractCheck.h"
#include "PreflightManager.h"

#include <algorithm>

PreflightChecklistModel::PreflightChecklistModel(QObject *parent)
    : QAbstractListModel(parent) {}

/// Connect to PreflightManager: full rebuild when checks change,
/// incremental data update when only check statuses change.
void PreflightChecklistModel::setPreflightManager(PreflightManager *mgr)
{
    if (m_manager == mgr)
        return;
    m_manager = mgr;
    if (m_manager) {
        connect(m_manager, &PreflightManager::modelChanged, this, &PreflightChecklistModel::rebuild);
        connect(m_manager, &PreflightManager::progressChanged, this, [this]() {
            Q_EMIT dataChanged(index(0), index(rowCount() - 1));
        });
    }
    rebuild();
}

int PreflightChecklistModel::rowCount(const QModelIndex &) const
{
    return m_manager ? m_manager->totalChecks() : 0;
}

QVariant PreflightChecklistModel::data(const QModelIndex &index, int role) const
{
    if (!m_manager || !index.isValid() || index.row() >= m_manager->totalChecks())
        return {};
    auto *chk = m_manager->checks().at(index.row());
    if (!chk)
        return {};
    switch (role) {
    case IdRole:              return chk->id();
    case LabelRole:           return chk->label();
    case StatusRole:          return chk->statusInt();
    case MessageRole:         return chk->message();
    case CategoryRole:        return chk->categoryInt();
    case TypeRole:            return chk->typeInt();
    case CurrentValueRole:    return chk->currentValue();
    case RecommendedActionRole: return chk->getRecommendedAction();
    case UserMessageRole:     return chk->getUserMessage();
    case IsManualRole:        return chk->checkType() == CheckType::Manual;
    case MandatoryRole:       return chk->mandatory();
    case CheckObjectRole:     return QVariant::fromValue(chk);
    case RationaleRole:       return chk->getRationale();
    case FixStepsRole:        return QVariant::fromValue(chk->getFixSteps());
    case ThresholdRole:       return chk->getThreshold();
    case CurrentValueStringRole: return chk->getCurrentValueString();
    case IsActionRole:        return chk->typeInt() == 2; // type 2 = action test
    case ActionButtonTextRole: {
        if (chk->typeInt() == 1) {
            if (chk->statusInt() == 1) return QStringLiteral("Checked \u2713");
            return QStringLiteral("Mark as Checked");
        }
        if (chk->typeInt() == 2) {
            if (chk->statusInt() == 1) return QStringLiteral("Test Passed \u2713");
            if (chk->statusInt() == 2) return QStringLiteral("Test Failed \u2014 Retry");
            if (chk->id() == QStringLiteral("propulsion.motors.spin"))
                return QStringLiteral("Motor Test Panel");
            return QStringLiteral("Run %1").arg(chk->label());
        }
        return QString();
    }
    case Qt::DisplayRole:     return chk->label();
    }
    return {};
}

QHash<int, QByteArray> PreflightChecklistModel::roleNames() const
{
    return {
        { IdRole,              "checkId" },
        { LabelRole,           "label" },
        { StatusRole,          "status" },
        { MessageRole,         "message" },
        { CategoryRole,        "category" },
        { TypeRole,            "type" },
        { CurrentValueRole,    "currentValue" },
        { RecommendedActionRole, "recommendedAction" },
        { UserMessageRole,     "userMessage" },
        { IsManualRole,        "isManual" },
        { MandatoryRole,       "mandatory" },
        { CheckObjectRole,     "checkObject" },
        { RationaleRole,       "rationale" },
        { FixStepsRole,        "fixSteps" },
        { ThresholdRole,       "threshold" },
        { CurrentValueStringRole, "currentValueString" },
        { IsActionRole,        "isAction" },
        { ActionButtonTextRole, "actionButtonText" }
    };
}

/// Count checks with failed (2) or warning (4) status — used for the critical-failures badge.
int PreflightChecklistModel::criticalCount() const
{
    int n = 0;
    if (!m_manager) return n;
    for (auto *chk : m_manager->checks()) {
        int s = chk->statusInt();
        if (s == 2 || s == 4) n++;
    }
    return n;
}

int PreflightChecklistModel::passedCount() const
{
    return m_manager ? m_manager->passedChecks() : 0;
}

int PreflightChecklistModel::pendingCount() const
{
    return m_manager ? m_manager->pendingChecks() : 0;
}

int PreflightChecklistModel::warnCount() const
{
    int n = 0;
    if (!m_manager) return n;
    for (auto *chk : m_manager->checks()) {
        if (chk->statusInt() == 3) n++;
    }
    return n;
}

/// Count mandatory checks that have failed or are in warning state (block takeoff).
int PreflightChecklistModel::blockingFailedCount() const
{
    if (!m_manager) return 0;
    auto checks = m_manager->checks();
    return std::count_if(checks.begin(), checks.end(), [](AbstractCheck *c) {
        auto s = c->statusInt();
        return c->mandatory() && (s == 2 || s == 4);
    });
}

int PreflightChecklistModel::completionPercent() const
{
    if (!m_manager || m_manager->totalChecks() == 0) return 0;
    return (m_manager->passedChecks() * 100) / m_manager->totalChecks();
}

/// Return the label of the first mandatory check that has failed, or empty if none.
QString PreflightChecklistModel::firstBlockingFailure() const
{
    if (!m_manager) return {};
    for (auto *chk : m_manager->checks()) {
        auto s = chk->statusInt();
        if (chk->mandatory() && (s == 2 || s == 4))
            return chk->label();
    }
    return {};
}

/// Human-readable summary string for the preflight status bar.
QString PreflightChecklistModel::statusSummary() const
{
    if (!m_manager || m_manager->totalChecks() == 0)
        return QStringLiteral("No checks");
    int crit = criticalCount();
    int pend = m_manager->pendingChecks();
    if (crit > 0)
        return QStringLiteral("%1 critical failure(s)").arg(crit);
    if (pend > 0)
        return QStringLiteral("%1 item(s) pending").arg(pend);
    return QStringLiteral("All checks passed");
}

QVariantMap PreflightChecklistModel::get(int row) const
{
    QVariantMap map;
    if (!m_manager || row < 0 || row >= m_manager->totalChecks())
        return map;
    auto *chk = m_manager->checks().at(row);
    if (!chk) return map;
    map[QStringLiteral("checkId")] = chk->id();
    map[QStringLiteral("label")] = chk->label();
    map[QStringLiteral("status")] = chk->statusInt();
    map[QStringLiteral("message")] = chk->message();
    map[QStringLiteral("category")] = chk->categoryInt();
    map[QStringLiteral("type")] = chk->typeInt();
    map[QStringLiteral("currentValue")] = chk->currentValue();
    map[QStringLiteral("recommendedAction")] = chk->getRecommendedAction();
    map[QStringLiteral("userMessage")] = chk->getUserMessage();
    map[QStringLiteral("isManual")] = chk->checkType() == CheckType::Manual;
    map[QStringLiteral("mandatory")] = chk->mandatory();
    map[QStringLiteral("checkObject")] = QVariant::fromValue(chk);
    map[QStringLiteral("rationale")] = chk->getRationale();
    map[QStringLiteral("fixSteps")] = QVariant::fromValue(chk->getFixSteps());
    map[QStringLiteral("threshold")] = chk->getThreshold();
    map[QStringLiteral("currentValueString")] = chk->getCurrentValueString();
    map[QStringLiteral("isAction")] = chk->typeInt() == 2;
    if (chk->typeInt() == 1) {
        if (chk->statusInt() == 1)
            map[QStringLiteral("actionButtonText")] = QStringLiteral("Checked \u2713");
        else
            map[QStringLiteral("actionButtonText")] = QStringLiteral("Mark as Checked");
    } else if (chk->typeInt() == 2) {
        if (chk->statusInt() == 1)
            map[QStringLiteral("actionButtonText")] = QStringLiteral("Test Passed \u2713");
        else if (chk->statusInt() == 2)
            map[QStringLiteral("actionButtonText")] = QStringLiteral("Test Failed \u2014 Retry");
        else if (chk->id() == QStringLiteral("propulsion.motors.spin"))
            map[QStringLiteral("actionButtonText")] = QStringLiteral("Motor Test Panel");
        else
            map[QStringLiteral("actionButtonText")] = QStringLiteral("Run %1").arg(chk->label());
    }
    return map;
}

/// Force a full model reset so QML re-reads all rows from the PreflightManager.
void PreflightChecklistModel::rebuild()
{
    beginResetModel();
    endResetModel();
    Q_EMIT countChanged();
}
