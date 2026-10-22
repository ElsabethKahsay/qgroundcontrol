// Step 3 item 7: the operator must name the battery pack in use before every
// flight. Shown as the "Battery pack" item in the preflight Power section; its
// button opens the pack dialog. The arming gate also refuses a GCS arm with no
// pack selected (ArmRequest::batteryPackSelected), in every session mode.
//
// Header-only and without Q_OBJECT (it adds no signals, slots or properties),
// so it needs no CMake or moc change.
#pragma once

#include <functional>

#include <QVariantMap>

#include "AbstractCheck.h"

/// Fraction of a pack's rated capacity a flight must use to count as one
/// charge cycle (user decision, 22 Oct).
inline constexpr double kBatteryCycleUsedFraction = 0.20;

/// True when a flight that used @p usedMah counts as one cycle of a pack rated
/// @p ratedMah. A pack with no rated capacity never counts (nothing to compare).
inline bool batteryFlightCountsAsCycle(double usedMah, int ratedMah)
{
    return ratedMah > 0 && usedMah >= kBatteryCycleUsedFraction * ratedMah;
}

class BatteryPackCheck : public AbstractCheck {
public:
    /// @p currentPack returns the selected pack as {serial, label, ratedMah,
    /// cycles}, or an empty map when none is selected.
    using PackProvider = std::function<QVariantMap()>;

    explicit BatteryPackCheck(PackProvider currentPack, QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("power.battery.pack"),
                        QStringLiteral("Battery pack"),
                        CheckCategory::Power, CheckType::Action,
                        /*mandatory*/ true, /*canOverride*/ false, parent)
        , m_currentPack(std::move(currentPack))
    {
        // Only a selected pack passes: a generic confirm() from a checklist
        // button (or an override) is undone at once.
        QObject::connect(this, &AbstractCheck::statusChanged, this, [this]() {
            if (status() == CheckStatus::Passed && !hasPack())
                evaluate();
        });
        evaluate();
    }

    bool hasPack() const
    {
        return m_currentPack && !m_currentPack().value(QStringLiteral("serial")).toString().isEmpty();
    }

    void evaluate() override
    {
        const QVariantMap pack = m_currentPack ? m_currentPack() : QVariantMap();
        const QString serial = pack.value(QStringLiteral("serial")).toString();
        if (serial.isEmpty()) {
            setCurrentValue(QVariant());
            setStatus(CheckStatus::Pending,
                      QStringLiteral("Select the battery pack fitted for this flight"));
            return;
        }
        const QString label = pack.value(QStringLiteral("label")).toString();
        const int rated = pack.value(QStringLiteral("ratedMah")).toInt();
        const int cycles = pack.value(QStringLiteral("cycles")).toInt();
        const QString name = label.isEmpty() ? serial : QStringLiteral("%1 (%2)").arg(label, serial);
        setCurrentValue(name);
        setStatus(CheckStatus::Passed,
                  QStringLiteral("%1 — %2 cycles, %3 mAh rated")
                      .arg(name).arg(cycles).arg(rated > 0 ? QString::number(rated) : QStringLiteral("?")));
    }

    /// Re-evaluating restores the selection: a pack choice is not a manual
    /// confirmation that a reset should clear.
    void reset() override
    {
        AbstractCheck::reset();
        evaluate();
    }

    QString getRationale() const override
    {
        return QStringLiteral("Each flight is logged against the pack that powered it, so pack "
                              "cycle counts and health history stay correct.");
    }

private:
    PackProvider m_currentPack;
};
