// The "Mission estimate" item in the preflight Power section (copters on a
// motor table): the assessment of the mission on the copter
// (VehicleProfileManager::missionEstimate(): waypoint times, charge on
// arrival, result and reasons).
//
// Advice only. The item is not mandatory, so the arming gate never counts
// it, whatever it shows:
//   Comfortable, Fits                       Passed (green)
//   Tight, Uses the reserve, Can't assess   Warning (amber)
//   One way only, Can't be done, Too heavy  Failed (red)
// Gusts above the gust limit, or going over the pack's current limit, make a
// green result amber (warnings, not results).
// The line reads, for example, "Fits — lands with 34 % (pessimistic 27 %),
// 18:40"; the details are in its dialog.
//
// Header-only and without Q_OBJECT (it adds no signals, slots or properties),
// so it needs no CMake or moc change.
#pragma once

#include <functional>

#include <QVariantMap>

#include "AbstractCheck.h"
#include "MissionAssess.h"

class MissionEstimateCheck : public AbstractCheck {
public:
    /// @p result returns VehicleProfileManager::missionEstimate().
    using ResultProvider = std::function<QVariantMap()>;

    explicit MissionEstimateCheck(ResultProvider result, QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("power.mission_estimate"),
                        QStringLiteral("Mission estimate"),
                        CheckCategory::Power, CheckType::Action,
                        /*mandatory*/ false, /*canOverride*/ false, parent)
        , m_result(std::move(result))
    {
        // The verdict is computed, never confirmed by hand.
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
        if (!r.value(QStringLiteral("applicable")).toBool()) {
            set(CheckStatus::Skipped, QStringLiteral("Copters only"));
            return;
        }
        const int result = r.value(QStringLiteral("result")).toInt();
        if (result == MissionAssess::CantAssess) {
            set(CheckStatus::Warning, QStringLiteral("Can't assess: %1").arg(r.value(QStringLiteral("problem")).toString()));
            return;
        }
        const QString line = QStringLiteral("%1 — lands with %2 % (pessimistic %3 %), %4")
                                 .arg(r.value(QStringLiteral("resultName")).toString())
                                 .arg(r.value(QStringLiteral("landLikelyPct")).toDouble(), 0, 'f', 0)
                                 .arg(r.value(QStringLiteral("landPessimisticPct")).toDouble(), 0, 'f', 0)
                                 .arg(r.value(QStringLiteral("totalText")).toString());
        const QString colour = r.value(QStringLiteral("colour")).toString();
        QString warnings;
        if (r.value(QStringLiteral("weatherRed")).toBool())
            warnings += QStringLiteral(" — gusts over the limit");
        if (r.value(QStringLiteral("overCurrent")).toBool())
            warnings += QStringLiteral(" — over the pack's current limit");
        if (colour == QLatin1String("red"))
            set(CheckStatus::Failed, line + warnings);
        else if (colour == QLatin1String("amber") || !warnings.isEmpty())
            set(CheckStatus::Warning, line + warnings);
        else
            set(CheckStatus::Passed, line);
    }

    void reset() override
    {
        AbstractCheck::reset();
        evaluate();
    }

    QString getRationale() const override
    {
        return QStringLiteral("The mission on the copter is timed the way ArduPilot flies it and costed from the motor "
                              "table at the weight aboard, in the site's air and wind. Advice only: it never holds arming.");
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
