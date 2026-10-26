// The operator must name the battery pack in use before every
// flight. Shown as the "Battery pack" item in the preflight Power section; its
// button opens the pack dialog. The arming gate also refuses a GCS arm with no
// pack selected (ArmRequest::batteryPackSelected), in every session mode and
// for a bench force arm too. The pilot can instead waive the pack for one
// flight, giving up the battery flight-time estimate; the item then shows a
// warning, which does not block arming.
//
// Header-only and without Q_OBJECT (it adds no signals, slots or properties),
// so it needs no CMake or moc change.
#pragma once

#include <functional>

#include <QVariantMap>

#include "AbstractCheck.h"

/// Fraction of a pack's rated capacity a flight must use to count as one
/// charge cycle.
inline constexpr double kBatteryCycleUsedFraction = 0.20;

/// True when a flight that used @p usedMah counts as one cycle of a pack rated
/// @p ratedMah. A pack with no rated capacity never counts (nothing to compare).
inline bool batteryFlightCountsAsCycle(double usedMah, int ratedMah)
{
    return ratedMah > 0 && usedMah >= kBatteryCycleUsedFraction * ratedMah;
}

class BatteryPackCheck : public AbstractCheck {
public:
    /// BATT_CAPACITY may differ from the pack's rated mAh by this share before
    /// the item warns.
    static constexpr double kCapacityTolerance = 0.05;

    /// @p currentPack returns the selected pack as {serial, label, ratedMah,
    /// cycles, fcCapacityMah (the FC's BATT_CAPACITY, optional)}, or, with no
    /// pack, {waivedBy} when the pilot waived it, or an empty map.
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
            if ((status() == CheckStatus::Passed || status() == CheckStatus::Warning) && !hasPack()
                && !isWaived())
                evaluate();
        });
        evaluate();
    }

    bool hasPack() const
    {
        return m_currentPack && !m_currentPack().value(QStringLiteral("serial")).toString().isEmpty();
    }

    bool isWaived() const
    {
        return m_currentPack && !m_currentPack().value(QStringLiteral("waivedBy")).toString().isEmpty();
    }

    void evaluate() override
    {
        const QVariantMap pack = m_currentPack ? m_currentPack() : QVariantMap();
        const QString serial = pack.value(QStringLiteral("serial")).toString();
        const QString waivedBy = pack.value(QStringLiteral("waivedBy")).toString();
        if (serial.isEmpty() && !waivedBy.isEmpty()) {
            setCurrentValue(QVariant());
            setStatus(CheckStatus::Warning,
                      QStringLiteral("No pack \u2014 waived by %1: no flight-time estimate, "
                                     "no cycle counted").arg(waivedBy));
            return;
        }
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
        // The flight controller counts the pack's charge against BATT_CAPACITY;
        // if that is not the fitted pack's size, its battery % and failsafes
        // are wrong (a warning: it does not block arming).
        const int fcMah = pack.value(QStringLiteral("fcCapacityMah"), -1).toInt();
        if (rated > 0 && fcMah > 0 && qAbs(fcMah - rated) > rated * kCapacityTolerance) {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("%1 \u2014 flight controller BATT_CAPACITY is %2 mAh, pack is %3 mAh: "
                                     "set BATT_CAPACITY = %3").arg(name).arg(fcMah).arg(rated));
            return;
        }
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
