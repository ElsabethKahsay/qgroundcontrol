// The "Motor table" item in the preflight Power section (copters only). The
// copter is linked to the maker's motor table for its exact motor and prop
// (MOTOR_TABLE_PLAN.md; the link is made on Analyze → Vehicles → Motor
// table), which the flight-time estimate and the mission estimate come from.
// Without a table the pilot can waive it for the copter (logged with the
// pilot's name; kept until a table is linked); the flight-time estimate is
// then the rough one, and there is no mission estimate.
//
// States: table linked → Passed, or Warning when the table's setup checks
// turn the estimate off (another frame or pack, no pack size) and say why;
// waived → Warning; otherwise Pending.
//
// Mandatory, so Pending blocks arming.
//
// Header-only and without Q_OBJECT (it adds no signals, slots or properties),
// so it needs no CMake or moc change.
#pragma once

#include <functional>

#include <QVariantMap>

#include "AbstractCheck.h"

class MotorTableCheck : public AbstractCheck {
public:
    /// @p state returns {applicable (copter), waivedBy, tableName (the motor
    /// table linked, if any), tableOffText (why its estimate is off, if it is)}.
    using StateProvider = std::function<QVariantMap()>;

    explicit MotorTableCheck(StateProvider state, QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("power.power_type"),
                        QStringLiteral("Motor table"),
                        CheckCategory::Power, CheckType::Action,
                        /*mandatory*/ true, /*canOverride*/ false, parent)
        , m_state(std::move(state))
    {
        // Only a linked table (or a waiver) passes: a generic confirm() from
        // a checklist button is undone at once.
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
            || !s.value(QStringLiteral("waivedBy")).toString().isEmpty();
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
        setCurrentValue(QVariant());
        const QString waivedBy = s.value(QStringLiteral("waivedBy")).toString();
        if (!waivedBy.isEmpty()) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("No motor table — waived for this copter by %1: rough flight-time "
                                     "estimate only, no mission estimate").arg(waivedBy));
            return;
        }
        setStatus(CheckStatus::Pending,
                  QStringLiteral("Link this copter to its motor table (Analyze → Vehicles → Motor table), or waive it"));
    }

    /// Re-evaluating restores the state: the link and the waiver live in
    /// VehicleProfileManager, not in the checklist, so a reset keeps them.
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
