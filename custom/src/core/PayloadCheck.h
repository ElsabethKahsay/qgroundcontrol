// The "Payload" item in the preflight checklist (Airframe & Physical). The
// pilot enters what the aircraft carries on this flight: its weight only, not
// the aircraft's own weight (that is set once in Vehicle Management). 0 kg
// means no payload. The weight is written on the flight record when the
// vehicle arms (flight_sessions.payload_weight_kg) and feeds the flight-time
// estimate.
//
// It does not block arming: the item stays Pending until a weight is entered
// for this connection, then shows the weight. The entry is cleared when the
// vehicle changes.
//
// Header-only and without Q_OBJECT (it adds no signals, slots or properties),
// so it needs no CMake or moc change.
#pragma once

#include <functional>

#include <QVariantMap>

#include "AbstractCheck.h"

class PayloadCheck : public AbstractCheck {
public:
    /// @p currentPayload returns {entered (bool), kg (double)}.
    using PayloadProvider = std::function<QVariantMap()>;

    explicit PayloadCheck(PayloadProvider currentPayload, QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("airframe.payload"),
                        QStringLiteral("Payload"),
                        CheckCategory::Airframe, CheckType::Action,
                        /*mandatory*/ false, /*canOverride*/ false, parent)
        , m_currentPayload(std::move(currentPayload))
    {
        // Only an entered weight passes: a generic confirm() from a checklist
        // button is undone at once.
        QObject::connect(this, &AbstractCheck::statusChanged, this, [this]() {
            if (status() == CheckStatus::Passed && !entered())
                evaluate();
        });
        evaluate();
    }

    bool entered() const
    {
        return m_currentPayload && m_currentPayload().value(QStringLiteral("entered")).toBool();
    }

    void evaluate() override
    {
        const QVariantMap p = m_currentPayload ? m_currentPayload() : QVariantMap();
        if (!p.value(QStringLiteral("entered")).toBool()) {
            setCurrentValue(QVariant());
            setStatus(CheckStatus::Pending,
                      QStringLiteral("Enter the payload weight for this flight (0 kg if none)"));
            return;
        }
        const double kg = p.value(QStringLiteral("kg")).toDouble();
        setCurrentValue(kg);
        setStatus(CheckStatus::Passed,
                  kg > 0.0 ? QStringLiteral("%1 kg payload").arg(kg, 0, 'f', 2)
                           : QStringLiteral("No payload (0 kg)"));
    }

    /// Re-evaluating restores the entry: it is not a manual confirmation that
    /// a checklist reset should clear.
    void reset() override
    {
        AbstractCheck::reset();
        evaluate();
    }

    QString getRationale() const override
    {
        return QStringLiteral("The payload weight is logged on each flight record and is used "
                              "by the flight-time estimate.");
    }

private:
    PayloadProvider m_currentPayload;
};
