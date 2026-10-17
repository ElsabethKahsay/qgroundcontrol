#include "RcCalibrationCheck.h"

#include "TelemetryBridge.h"

#include <QtMath>

RcCalibrationCheck::RcCalibrationCheck(TelemetryBridge *telemetry, QObject *parent)
    : AbstractCheck(QStringLiteral("com.rc.calibration"),
                    QStringLiteral("RC Calibration"),
                    CheckCategory::Communication, CheckType::Auto, false, false, parent)
{
    m_telemetry = telemetry;
}

void RcCalibrationCheck::evaluate()
{
    if (!hasTelemetry()) {
        setStatus(CheckStatus::Pending, QStringLiteral("Waiting for telemetry"));
        return;
    }

    // Phase 1: Try parameter-based verification (RC1_MIN, RC1_MAX, RC1_TRIM …)
    RcCalData cal = _readCalParams();

    QVariantList channels = m_telemetry->property("rcChannelValues").toList();

    if (cal.valid) {
        // ── Verify endpoints ──────────────────────────────────────────
        QStringList failures;

        double minLo = configDouble(QStringLiteral("rc_min_endpoint_low"), 800.0);
        double minHi = configDouble(QStringLiteral("rc_min_endpoint_high"), 1200.0);
        double maxLo = configDouble(QStringLiteral("rc_max_endpoint_low"), 1800.0);
        double maxHi = configDouble(QStringLiteral("rc_max_endpoint_high"), 2200.0);
        double trimCenter = configDouble(QStringLiteral("rc_trim_center"), 1500.0);
        double trimTol   = configDouble(QStringLiteral("rc_trim_tolerance"), 150.0);

        for (int i = 0; i < 4; ++i) {
            if (cal.min[i] < minLo || cal.min[i] > minHi) {
                failures << QStringLiteral("Ch%1 min %2 (expected %3\u2013%4)")
                                .arg(i + 1).arg(cal.min[i], 0, 'f', 0)
                                .arg(minLo, 0, 'f', 0)
                                .arg(minHi, 0, 'f', 0);
            }
            if (cal.max[i] < maxLo || cal.max[i] > maxHi) {
                failures << QStringLiteral("Ch%1 max %2 (expected %3\u2013%4)")
                                .arg(i + 1).arg(cal.max[i], 0, 'f', 0)
                                .arg(maxLo, 0, 'f', 0)
                                .arg(maxHi, 0, 'f', 0);
            }
            if (cal.min[i] >= cal.max[i]) {
                failures << QStringLiteral("Ch%1 min \u2265 max (%2 \u2265 %3)")
                                .arg(i + 1).arg(cal.min[i], 0, 'f', 0).arg(cal.max[i], 0, 'f', 0);
            }
            if (cal.trim[i] > 0 &&
                (cal.trim[i] < trimCenter - trimTol ||
                 cal.trim[i] > trimCenter + trimTol)) {
                failures << QStringLiteral("Ch%1 trim %2 far from center %3")
                                .arg(i + 1).arg(cal.trim[i], 0, 'f', 0)
                                .arg(trimCenter, 0, 'f', 0);
            }
        }

        // ── Factory-default detection ─────────────────────────────────
        if (_isFactoryDefault(cal)) {
            failures << QStringLiteral("All endpoints at factory defaults (min=1100, max=1900) \u2014 calibration required");
        }

        if (failures.isEmpty()) {
            setStatus(CheckStatus::Passed,
                      QStringLiteral("RC calibration OK \u2014 Ch1: %1/%2 Ch2: %3/%4 Ch3: %5/%6 Ch4: %7/%8")
                          .arg(cal.min[0], 0, 'f', 0).arg(cal.max[0], 0, 'f', 0)
                          .arg(cal.min[1], 0, 'f', 0).arg(cal.max[1], 0, 'f', 0)
                          .arg(cal.min[2], 0, 'f', 0).arg(cal.max[2], 0, 'f', 0)
                          .arg(cal.min[3], 0, 'f', 0).arg(cal.max[3], 0, 'f', 0));
        } else {
            setStatus(CheckStatus::Warning,
                      QStringLiteral("RC calibration issues: %1").arg(failures.join(QStringLiteral("; "))));
        }
        return;
    }

    // Phase 2: Fallback — channel responsiveness check (no params available).
    // Four channels are required below, so a short list must not fall through.
    if (channels.size() < 4) {
        setStatus(CheckStatus::Pending, QStringLiteral("No RC channel data"));
        return;
    }

    uint16_t ch1 = static_cast<uint16_t>(channels[0].toUInt());
    uint16_t ch2 = static_cast<uint16_t>(channels[1].toUInt());
    uint16_t ch3 = static_cast<uint16_t>(channels[2].toUInt());
    uint16_t ch4 = static_cast<uint16_t>(channels[3].toUInt());

    bool chOk = ch1 > 0 && ch1 < UINT16_MAX
             && ch2 > 0 && ch2 < UINT16_MAX
             && ch3 > 0 && ch3 < UINT16_MAX
             && ch4 > 0 && ch4 < UINT16_MAX;

    if (chOk) {
        setStatus(CheckStatus::Passed,
                  QStringLiteral("Ch: %1/%2/%3/%4 \u2014 OK (no params)").arg(ch1).arg(ch2).arg(ch3).arg(ch4));
    } else {
        setStatus(CheckStatus::Warning, QStringLiteral("RC channels not responding"));
    }
}

RcCalibrationCheck::RcCalData RcCalibrationCheck::_readCalParams() const
{
    RcCalData cal;
    const char *channels[] = {"1", "2", "3", "4"};

    bool anyFound = false;
    for (int i = 0; i < 4; ++i) {
        QString minParam = QStringLiteral("RC%1_MIN").arg(channels[i]);
        QString maxParam = QStringLiteral("RC%1_MAX").arg(channels[i]);
        QString trimParam = QStringLiteral("RC%1_TRIM").arg(channels[i]);

        if (isParamAvailable(minParam) && isParamAvailable(maxParam)) {
            cal.min[i] = getTelemetryDouble(minParam);
            cal.max[i] = getTelemetryDouble(maxParam);
            cal.trim[i] = isParamAvailable(trimParam) ? getTelemetryDouble(trimParam) : -1.0;
            anyFound = true;
        } else {
            cal.min[i] = 0.0;
            cal.max[i] = 0.0;
            cal.trim[i] = -1.0;
        }
    }
    cal.valid = anyFound;
    return cal;
}

bool RcCalibrationCheck::_isFactoryDefault(const RcCalData &cal) const
{
    // A freshly-flashed autopilot reports the standard ArduPilot/PX4 RC
    // endpoint defaults (1100/1900) on every channel until RC calibration is
    // run. Compare against those fixed defaults — NOT against channel 1's
    // values, which may themselves already have been calibrated and would then
    // mask an uncalibrated channel 2-4.
    static constexpr double kFactoryMin = 1100.0;
    static constexpr double kFactoryMax = 1900.0;

    for (int i = 0; i < 4; ++i) {
        if (qFuzzyCompare(cal.min[i], kFactoryMin) &&
            qFuzzyCompare(cal.max[i], kFactoryMax)) {
            continue;
        }
        return false;
    }
    return true;
}

QString RcCalibrationCheck::getRationale() const
{
    return QStringLiteral("Verifies RC transmitter calibration: endpoint ranges, "
                          "center trims, and that calibration is not at factory defaults.");
}

QStringList RcCalibrationCheck::getFixSteps() const
{
    return {
        QStringLiteral("Run RC calibration in your autopilot firmware"),
        QStringLiteral("Set channel endpoints so min \u2264 1100 \u00b5s and max \u2265 1900 \u00b5s"),
        QStringLiteral("Center all trims on your transmitter"),
        QStringLiteral("Verify sub-trim values are near zero in autopilot parameters"),
    };
}

QString RcCalibrationCheck::getThreshold() const
{
    return QStringLiteral("Min \u2208 [800,1200], Max \u2208 [1800,2200], Trim \u2208 [1350,1650]");
}

QString RcCalibrationCheck::getCurrentValueString() const
{
    if (!hasTelemetry())
        return QStringLiteral("No telemetry");
    RcCalData cal = _readCalParams();
    if (cal.valid) {
        return QStringLiteral("Min %1/%2/%3/%4 \u00b5s")
            .arg(cal.min[0], 0, 'f', 0).arg(cal.min[1], 0, 'f', 0)
            .arg(cal.min[2], 0, 'f', 0).arg(cal.min[3], 0, 'f', 0);
    }
    QVariantList ch = m_telemetry->property("rcChannelValues").toList();
    if (ch.size() >= 4)
        return QStringLiteral("Raw ch: %1/%2/%3/%4")
            .arg(ch[0].toUInt()).arg(ch[1].toUInt()).arg(ch[2].toUInt()).arg(ch[3].toUInt());
    return QStringLiteral("No RC data");
}
