// The "Power type" item in the preflight Power section (copters only). The
// aircraft must be linked to a power type (frame + props + cells, see
// POWER_TYPES_PLAN.md), and the pilot confirms on every flight that the props
// fitted are the ones the type names: the flight controller can check the
// frame and the cell count but not the props. Without a type the pilot can
// waive it for the flight (logged); the flight-time estimate is then the
// rough one, and there is no mission estimate (it needs a motor table).
//
// A copter linked to a motor table (MOTOR_TABLE_PLAN.md) needs no power type:
// the table is the maker's test of its exact motor and prop, so it passes.
// When the table's setup checks turn the estimate off (another frame or pack,
// no pack size) it is a warning that says why.
//
// Mandatory, so Pending blocks arming. Warnings (frame or cell count not
// matching the type, no current estimate on the flight controller) do not.
//
// Header-only and without Q_OBJECT (it adds no signals, slots or properties),
// so it needs no CMake or moc change.
#pragma once

#include <functional>

#include <QVariantMap>

#include "AbstractCheck.h"

class PowerTypeCheck : public AbstractCheck {
public:
    /// @p state returns {applicable (copter), typeId, typeName, propsConfirmed,
    /// waivedBy, warnings (QStringList: frame/cells/BATT_MONITOR mismatches),
    /// tableName (the motor table linked, if any), tableOffText (why its
    /// estimate is off, if it is)}.
    using StateProvider = std::function<QVariantMap()>;

    explicit PowerTypeCheck(StateProvider state, QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("power.power_type"),
                        QStringLiteral("Motor table"),
                        CheckCategory::Power, CheckType::Action,
                        /*mandatory*/ true, /*canOverride*/ false, parent)
        , m_state(std::move(state))
    {
        // Only a confirmed type (or a waiver) passes: a generic confirm()
        // from a checklist button is undone at once.
        QObject::connect(this, &AbstractCheck::statusChanged, this, [this]() {
            if ((status() == CheckStatus::Passed || status() == CheckStatus::Warning) && !satisfied())
                evaluate();
        });
        evaluate();
    }

    bool satisfied() const
    {
        const QVariantMap s = m_state ? m_state() : QVariantMap();
        if (!s.value(QStringLiteral("applicable")).toBool())
            return true;
        return !s.value(QStringLiteral("tableName")).toString().isEmpty()
            || !s.value(QStringLiteral("waivedBy")).toString().isEmpty()
            || (s.value(QStringLiteral("typeId")).toInt() > 0 && s.value(QStringLiteral("propsConfirmed")).toBool());
    }

    void evaluate() override
    {
        const QVariantMap s = m_state ? m_state() : QVariantMap();
        if (!s.value(QStringLiteral("applicable")).toBool()) {
            setCurrentValue(QVariant());
            setStatus(CheckStatus::Skipped, QStringLiteral("Copters only"));
            return;
        }
        const QString table = s.value(QStringLiteral("tableName")).toString();
        if (!table.isEmpty()) {
            setCurrentValue(table);
            const QString off = s.value(QStringLiteral("tableOffText")).toString();
            if (!off.isEmpty())
                setStatus(CheckStatus::Warning, QStringLiteral("%1 (motor table) — %2: flight-time estimate off").arg(table, off));
            else
                setStatus(CheckStatus::Passed, QStringLiteral("%1 (motor table)").arg(table));
            return;
        }
        const QString waivedBy = s.value(QStringLiteral("waivedBy")).toString();
        if (!waivedBy.isEmpty()) {
            setCurrentValue(QVariant());
            setStatus(CheckStatus::Warning,
                      QStringLiteral("No motor table — waived by %1: rough flight-time estimate only, "
                                     "no mission estimate").arg(waivedBy));
            return;
        }
        const QString name = s.value(QStringLiteral("typeName")).toString();
        if (s.value(QStringLiteral("typeId")).toInt() <= 0) {
            setCurrentValue(QVariant());
            setStatus(CheckStatus::Pending,
                      QStringLiteral("Link this copter to its motor table (Analyze → Vehicles → Motor table), or waive it"));
            return;
        }
        setCurrentValue(name);
        if (!s.value(QStringLiteral("propsConfirmed")).toBool()) {
            setStatus(CheckStatus::Pending, QStringLiteral("%1 — confirm these props are fitted").arg(name));
            return;
        }
        const QStringList warnings = s.value(QStringLiteral("warnings")).toStringList();
        if (!warnings.isEmpty()) {
            setStatus(CheckStatus::Warning, QStringLiteral("%1 — %2").arg(name, warnings.join(QStringLiteral("; "))));
            return;
        }
        setStatus(CheckStatus::Passed, QStringLiteral("%1 — props confirmed").arg(name));
    }

    /// Re-evaluating restores the state: the confirmation lives in
    /// VehicleProfileManager and ends with the flight, not with a reset.
    void reset() override
    {
        AbstractCheck::reset();
        evaluate();
    }

    QString getRationale() const override
    {
        return QStringLiteral("The flight-time estimate and the mission estimate come from the maker's motor "
                              "table linked to this copter (its motors, props and cells).");
    }

private:
    StateProvider m_state;
};
