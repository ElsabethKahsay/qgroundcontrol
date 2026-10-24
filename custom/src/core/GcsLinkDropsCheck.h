// Shows, in the checklist's Communication section, how often the GCS has lost
// the connected vehicle since the app started, so an unreliable USB cable or
// telemetry radio is noticed on the ground and not when the plane flies home on
// a GCS failsafe.
//
// It reads what SessionRecorder already stores (link-lost events and
// connections that ended because the vehicle disappeared) through
// DatabaseManager::linkDropsSinceStart(); there is no second counter.
// Zero drops passes; any drop is a Warning (advice, not a block). Without a
// connected vehicle there is nothing to count yet.
//
// Header-only and without Q_OBJECT so it adds no source file to the build.
#pragma once

#include "AbstractCheck.h"
#include "utils/DatabaseManager.h"

class GcsLinkDropsCheck : public AbstractCheck {
public:
    explicit GcsLinkDropsCheck(QObject *parent = nullptr)
        : AbstractCheck(QStringLiteral("comm.gcs.link_drops"),
                        QStringLiteral("GCS Link Drops"),
                        CheckCategory::Communication, CheckType::Auto,
                        /*mandatory*/ false, /*canOverride*/ false, parent)
    {
    }

    void evaluate() override
    {
        const int drops = DatabaseManager::instance().linkDropsSinceStart();
        setCurrentValue(drops);
        if (drops < 0) {
            setStatus(CheckStatus::Pending, QStringLiteral("No vehicle connected yet"));
            return;
        }
        if (drops == 0) {
            setStatus(CheckStatus::Passed, QStringLiteral("No GCS link drops since the app started"));
            return;
        }
        setStatus(CheckStatus::Warning,
                  QStringLiteral("GCS link lost %1 time%2 since the app started — check the USB cable or "
                                 "telemetry radio before flight")
                      .arg(drops).arg(drops == 1 ? QString() : QStringLiteral("s")));
    }

    QStringList getFixSteps() const override
    {
        return {
            QStringLiteral("USB: use a short, good-quality cable, plug it straight into the computer (no hub) and "
                           "make sure it cannot be pulled while you work."),
            QStringLiteral("Telemetry radio: check antennas and power, and keep the ground radio clear of the "
                           "computer."),
            QStringLiteral("Each drop can trigger the plane's GCS failsafe if it is enabled (see the GCS Failsafe "
                           "item). The count resets when the app restarts."),
        };
    }
};
