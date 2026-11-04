// The "Mission check" item in the preflight Power section (copters only): can
// the copter carry the entered payload to the saved target, stay there the
// entered time, drop the payload and come back above the landing reserve?
// The legs are costed with the power type's learned model, or for a copter on
// a motor table with its remembered hover (PowerTypeModel::checkMission, via
// VehicleProfileManager::missionCheck()). A motor table is not validated
// until its flights agree with the charger, so it only warns.
//
// It blocks arming (Failed) only when the trip does not fit and the type is
// validated (its last flights were predicted within 10 %); the pilot can
// waive that for the flight (logged). Before the type is validated a trip
// that does not fit is an amber warning. With no target saved it shows the
// furthest out-and-back distance and does not block. With no power type or
// no battery pack there is nothing to cost, so it is skipped (those items
// say why).
//
// Header-only and without Q_OBJECT (it adds no signals, slots or properties),
// so it needs no CMake or moc change.
#pragma once

#include <functional>

#include <QVariantMap>

#include "AbstractCheck.h"

class MissionCheck : public AbstractCheck {
public:
    /// @p result returns VehicleProfileManager::missionCheck().
    using ResultProvider = std::function<QVariantMap()>;

    explicit MissionCheck(ResultProvider result, QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("power.mission"),
                        QStringLiteral("Mission check"),
                        CheckCategory::Power, CheckType::Action,
                        /*mandatory*/ true, /*canOverride*/ false, parent)
        , m_result(std::move(result))
    {
        // The verdict is computed, never confirmed by hand: a generic
        // confirm() from a checklist button is undone at once.
        QObject::connect(this, &AbstractCheck::statusChanged, this, [this]() {
            if (status() == CheckStatus::Passed && !m_passedByEvaluate)
                evaluate();
            m_passedByEvaluate = false;
        });
        evaluate();
    }

    void evaluate() override
    {
        const QVariantMap r = m_result ? m_result() : QVariantMap();
        auto num = [&r](const char *key) { return r.value(QLatin1String(key)).toDouble(); };
        auto flag = [&r](const char *key) { return r.value(QLatin1String(key)).toBool(); };

        if (!flag("applicable")) {
            set(CheckStatus::Skipped, QStringLiteral("Copters only"));
            return;
        }
        if (!flag("linked") || !r.value(QStringLiteral("powerTypeWaivedBy")).toString().isEmpty()) {
            set(CheckStatus::Skipped, QStringLiteral("No power type: mission check off"));
            return;
        }
        if (flag("packWaived")) {
            set(CheckStatus::Skipped, QStringLiteral("No battery pack: nothing to check against"));
            return;
        }
        if (!flag("modelUsable")) {
            // A motor table says what it is missing; a power type, what to fly.
            const QString notReady = r.value(QStringLiteral("notReadyText")).toString();
            const QString advice = r.value(QStringLiteral("advice")).toString();
            set(CheckStatus::Warning, !notReady.isEmpty() ? notReady
                                      : QStringLiteral("Not enough learned flights yet%1")
                                            .arg(advice.isEmpty() ? QString() : QStringLiteral(" — ") + advice));
            return;
        }

        const bool validated = flag("validated");
        const double kg = num("payloadKg");
        // A motor table's warnings (heavy for its motors, no motor outputs)
        // keep the item amber and are shown after the verdict.
        const QString setupWarning = r.value(QStringLiteral("setupWarning")).toString();
        // Said, but not amber: a motor table's cruise power before a flight
        // has measured it.
        const QString cruiseNote = r.value(QStringLiteral("cruiseNote")).toString();
        const QString note = (flag("speedAssumed") ? QStringLiteral(" (cruise speed assumed %1 m/s)")
                                                         .arg(num("cruiseSpeedMs"), 0, 'f', 0)
                                                   : QString())
                             + (cruiseNote.isEmpty() ? QString() : QStringLiteral(" (") + cruiseNote + QStringLiteral(")"))
                             + (setupWarning.isEmpty() ? QString() : QStringLiteral(" — ") + setupWarning);
        // A pack that is not full: its charge is estimated from the resting
        // voltage (rough). A validated type holds the arm until a full pack is
        // fitted or the pilot waives it; before validation it is a note.
        const bool packFull = r.value(QStringLiteral("packFull"), true).toBool();
        const QString partialBy = r.value(QStringLiteral("partialPackWaivedBy")).toString();
        const QString packNote = packFull ? QString()
            : QStringLiteral(" (pack not full: %1 V per cell, about %2 % from its voltage%3)")
                  .arg(num("packCellV"), 0, 'f', 2).arg(qRound(num("packSocPct")))
                  .arg(partialBy.isEmpty() ? QString() : QStringLiteral(", waived by ") + partialBy);
        // Unwaived and not full: amber, whatever the trip says.
        const CheckStatus okStatus = ((packFull || !partialBy.isEmpty()) && setupWarning.isEmpty())
                                         ? CheckStatus::Passed : CheckStatus::Warning;

        if (!flag("hasTarget")) {
            set(okStatus, QStringLiteral("No target — max out-and-back %1 km with %2 kg%3%4")
                              .arg(num("maxTargetKm"), 0, 'f', 1).arg(kg, 0, 'f', 1).arg(note, packNote));
            return;
        }
        // A validated type can block, so it must not be dodged by leaving the
        // payload out or before the position is known.
        if (!flag("payloadEntered")) {
            set(validated ? CheckStatus::Pending : CheckStatus::Warning,
                QStringLiteral("Enter the payload (and time at target) to check the trip"));
            return;
        }
        if (!flag("positionKnown")) {
            set(validated ? CheckStatus::Pending : CheckStatus::Warning,
                QStringLiteral("Waiting for the GPS position"));
            return;
        }

        if (!packFull && partialBy.isEmpty() && validated) {
            set(CheckStatus::Pending, QStringLiteral("Pack not full (%1 V per cell, about %2 %): fit a full pack, or waive")
                                          .arg(num("packCellV"), 0, 'f', 2).arg(qRound(num("packSocPct"))));
            return;
        }

        const QString trip = QStringLiteral("Tgt %1 km · %2 kg dropped · %3 min at target")
                                 .arg(num("targetKm"), 0, 'f', 1).arg(kg, 0, 'f', 1)
                                 .arg(num("minAtTarget"), 0, 'f', 0);
        const QString assumed = (flag("timeAtTargetEntered") ? QString() : QStringLiteral(" (0 min at target assumed)"))
                                + packNote;
        if (flag("fits")) {
            set(okStatus, QStringLiteral("%1 → OK, %2 Ah spare (max %3 km)%4%5")
                                         .arg(trip).arg(num("spareAh"), 0, 'f', 1)
                                         .arg(num("maxTargetKm"), 0, 'f', 1).arg(assumed, note));
            return;
        }
        const QString shortBy = QStringLiteral("%1 → short by %2 Ah (needs %3 of %4 Ah, max %5 km)%6%7")
                                    .arg(trip).arg(-num("spareAh"), 0, 'f', 1).arg(num("totalAh"), 0, 'f', 1)
                                    .arg(num("usableAh"), 0, 'f', 1).arg(num("maxTargetKm"), 0, 'f', 1)
                                    .arg(assumed, note);
        const QString waivedBy = r.value(QStringLiteral("waivedBy")).toString();
        if (!waivedBy.isEmpty())
            set(CheckStatus::Warning, QStringLiteral("%1 — waived by %2").arg(shortBy, waivedBy));
        else if (validated)
            set(CheckStatus::Failed, shortBy);
        else
            set(CheckStatus::Warning, QStringLiteral("%1 — estimate not yet validated").arg(shortBy));
    }

    void reset() override
    {
        AbstractCheck::reset();
        evaluate();
    }

    QString getRationale() const override
    {
        return QStringLiteral("The round trip to the target is costed with what this power type has "
                              "learned from its flights' charger numbers.");
    }

private:
    void set(CheckStatus s, const QString &message)
    {
        m_passedByEvaluate = s == CheckStatus::Passed;
        setStatus(s, message);
    }

    ResultProvider m_result;
    bool m_passedByEvaluate = false;
};
