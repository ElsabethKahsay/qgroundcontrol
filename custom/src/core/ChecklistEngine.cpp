/**
 * @file ChecklistEngine.cpp
 * @brief Signal-driven engine that evaluates checklist items against live telemetry.
 *
 * Builds a reverse mapping from TelemetryBridge property names to model rows,
 * connects to the bridge's propertyChanged signals, and re-evaluates only the
 * items affected by each telemetry update. Manual items require explicit
 * operator confirmation via confirmItem().
 */

#include "ChecklistEngine.h"
#include "ChecklistItemModel.h"
#include "TelemetryBridge.h"

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(checklistEngineLog, "preflight.engine")

ChecklistEngine::ChecklistEngine(QObject *parent)
    : QObject(parent)
{
}

// Connect to a TelemetryBridge and rebuild signal bindings if a model is already set.
void ChecklistEngine::setTelemetryBridge(TelemetryBridge *bridge)
{
    if (m_bridge == bridge)
        return;
    m_bridge = bridge;
    if (m_bridge && m_model)
        _rebuildBindings();
}

// Set the data model and connect to its structural change signals
// (modelReset, rowsInserted, rowsRemoved) to rebuild bindings when items change.
void ChecklistEngine::setModel(ChecklistItemModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::modelReset, this, &ChecklistEngine::_rebuildBindings);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &ChecklistEngine::_rebuildBindings);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &ChecklistEngine::_rebuildBindings);
    }
    if (m_bridge && m_model)
        _rebuildBindings();
}

void ChecklistEngine::start()
{
    if (m_running || !m_bridge || !m_model)
        return;
    m_running = true;
    evaluateAll();
}

void ChecklistEngine::stop()
{
    m_running = false;
}

void ChecklistEngine::evaluateAll()
{
    if (!m_model)
        return;
    for (int i = 0; i < m_model->rowCount(); ++i)
        _evaluateItem(i);
    emit evaluationCompleted();
}

// Mark a manual checklist item as confirmed by the operator.
// Only works on items where isManual == true.
void ChecklistEngine::confirmItem(int row, const QString &operatorId)
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return;
    const auto &items = m_model->items();
    if (row >= items.size())
        return;
    if (!items[row].isManual)
        return;
    m_model->setItemStatus(row, 1,
        QStringLiteral("Confirmed by %1").arg(operatorId.isEmpty() ? QStringLiteral("operator") : operatorId));
    emit itemConfirmed(row, operatorId);
    emit evaluationCompleted();
}

int ChecklistEngine::totalItems() const
{
    return m_model ? m_model->rowCount() : 0;
}

int ChecklistEngine::passedItems() const
{
    if (!m_model) return 0;
    int n = 0;
    for (const auto &item : m_model->items()) {
        if (item.status == 1) ++n;
    }
    return n;
}

int ChecklistEngine::pendingItems() const
{
    if (!m_model) return 0;
    int n = 0;
    for (const auto &item : m_model->items()) {
        if (item.status == 0) ++n;
    }
    return n;
}

int ChecklistEngine::failedItems() const
{
    if (!m_model) return 0;
    int n = 0;
    for (const auto &item : m_model->items()) {
        if (item.status == 2) ++n;
    }
    return n;
}

bool ChecklistEngine::allPassed() const
{
    if (!m_model || m_model->rowCount() == 0) return false;
    for (const auto &item : m_model->items()) {
        if (item.status != 1) return false;
    }
    return true;
}

// Rebuild the property-to-row binding map and signal connections.
//
// For each checklist item with a bindProperty, we:
//   1. Map the property name → set of model row indices
//   2. Connect TelemetryBridge's propertyChanged signal to _onTelemetryPropertyChanged
//      using QMetaObject::connect for runtime signal lookup
void ChecklistEngine::_rebuildBindings()
{
    // Disconnect previous connections
    for (auto &conn : m_connections)
        disconnect(conn);
    m_connections.clear();
    m_bindings.clear();

    if (!m_bridge || !m_model)
        return;

    // Build reverse mapping: property → model rows
    const auto &items = m_model->items();
    for (int i = 0; i < items.size(); ++i) {
        const QString &prop = items[i].bindProperty;
        if (!prop.isEmpty())
            m_bindings[prop].append(i);
    }

    // Register per-property signal connections using QMetaObject::connect
    const QMetaObject *bridgeMo = m_bridge->metaObject();
    int slotIdx = metaObject()->indexOfSlot("_onTelemetryPropertyChanged()");
    if (slotIdx < 0) {
        qCWarning(checklistEngineLog) << "Cannot find _onTelemetryPropertyChanged slot";
        return;
    }

    for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it) {
        QByteArray sigName = _signalNameForProperty(it.key()).toLatin1();
        int signalIdx = bridgeMo->indexOfSignal(sigName.constData());
        if (signalIdx < 0) {
            qCDebug(checklistEngineLog) << "No signal" << sigName << "on TelemetryBridge";
            continue;
        }
        QMetaObject::Connection conn = QMetaObject::connect(m_bridge, signalIdx, this, slotIdx);
        if (conn) {
            m_connections.append(conn);
        } else {
            qCWarning(checklistEngineLog) << "Failed to connect signal" << sigName;
        }
    }

    qCDebug(checklistEngineLog).noquote()
        << QStringLiteral("ChecklistEngine: %1 bindings, %2 connections")
               .arg(m_bindings.size())
               .arg(m_connections.size());

    if (m_running)
        evaluateAll();
}

// Slot called when any connected TelemetryBridge property changes.
// Uses senderSignalIndex() to identify which property changed, then
// re-evaluates only the model rows that bind to that property.
void ChecklistEngine::_onTelemetryPropertyChanged()
{
    if (!m_running || !m_model || !m_bridge)
        return;

    int sigIdx = senderSignalIndex();
    if (sigIdx < 0) {
        // Fallback: evaluate everything
        for (int i = 0; i < m_model->rowCount(); ++i)
            _evaluateItem(i);
        emit evaluationCompleted();
        return;
    }

    // Determine property name from the signal index
    const QMetaObject *mo = m_bridge->metaObject();
    QMetaMethod method = mo->method(sigIdx);
    QString sigName = QString::fromLatin1(method.methodSignature());

    // Strip "Changed" to get property name
    // Signal: "batteryVoltageChanged" → property: "batteryVoltage"
    QString prop;
    if (sigName.endsWith(QStringLiteral("Changed")))
        prop = sigName.chopped(7); // "Changed" length

    if (prop.isEmpty()) {
        for (int i = 0; i < m_model->rowCount(); ++i)
            _evaluateItem(i);
        emit evaluationCompleted();
        return;
    }

    auto it = m_bindings.constFind(prop);
    if (it != m_bindings.cend()) {
        for (int row : it.value())
            _evaluateItem(row);
    }

    emit evaluationCompleted();
}

// Evaluate a single checklist item:
//   - Manual items: skipped (wait for confirmItem())
//   - No binding: marked as pending with "No telemetry binding" message
//   - Value unavailable (NaN): marked as pending with "Waiting for telemetry"
//   - Value in range [required ± tolerance]: passed
//   - Value out of range: failed
void ChecklistEngine::_evaluateItem(int row)
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return;
    const auto &items = m_model->items();
    if (row >= items.size())
        return;
    const auto &item = items[row];

    if (item.isManual) {
        // Manual items wait for confirmItem()
        return;
    }

    if (item.bindProperty.isEmpty()) {
        m_model->setItemStatus(row, 0, QStringLiteral("No telemetry binding"));
        return;
    }

    double value = _readBridgeProperty(item.bindProperty);
    if (qIsNaN(value)) {
        m_model->setItemStatus(row, 0, QStringLiteral("Waiting for telemetry"));
        return;
    }

    double lower = item.requiredValue - item.tolerance;
    double upper = item.requiredValue + item.tolerance;

    if (value >= lower && value <= upper) {
        m_model->setItemStatus(row, 1,
            QStringLiteral("%1 %2 (ok: %3\u2013%4)")
                .arg(value, 0, 'f', 2)
                .arg(item.unit)
                .arg(lower, 0, 'f', 2)
                .arg(upper, 0, 'f', 2));
    } else {
        m_model->setItemStatus(row, 2,
            QStringLiteral("%1 %2 (expected %3\u2013%4)")
                .arg(value, 0, 'f', 2)
                .arg(item.unit)
                .arg(lower, 0, 'f', 2)
                .arg(upper, 0, 'f', 2));
    }
}

// Read a double property from TelemetryBridge. Returns NaN if unavailable.
double ChecklistEngine::_readBridgeProperty(const QString &prop) const
{
    if (!m_bridge)
        return qQNaN();
    QVariant val = m_bridge->property(prop.toUtf8().constData());
    if (!val.isValid() || val.isNull())
        return qQNaN();
    return val.toDouble();
}

// Derive the Qt signal name from a property name using the TelemetryBridge convention:
// "batteryVoltage" → "batteryVoltageChanged"
QString ChecklistEngine::_signalNameForProperty(const QString &prop)
{
    if (prop.isEmpty())
        return {};
    // TelemetryBridge convention: property "batteryVoltage" → signal "batteryVoltageChanged"
    return prop + QStringLiteral("Changed");
}
