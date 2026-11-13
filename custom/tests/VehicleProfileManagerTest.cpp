/**
 * @file VehicleProfileManagerTest.cpp
 * @brief Tests for VehicleProfileManager battery config, SOC/time estimation,
 *        and capacity calculation.
 *
 * Covers:
 *   - Battery config: series, parallel, cell mAh, reserve, type
 *   - Capacity calculation: parallel × cellMah / 1000
 *   - Vehicle kind resolution
 *   - Motor count resolution
 *   - Battery serial tracking
 *   - Live SOC/time estimation logic
 */

#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "VehicleProfileManager.h"
#include "mocks/MockTelemetryBridge.h"

class VehicleProfileManagerTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    void testDefaultBatteryConfig();
    void testCapacityCalculation();
    void testBatteryConfigSignals();
    void testSetBatterySerial();
    void testKindFromMavType();
    void testKindFromTypeString();
    void testKindString();
    void testLiveSOCDefault();
    void testBatteryReserveBounds();
    void testUsableAhStopsAtTheReserve();
    void testPackSocFromSelectedPackNotFcCapacity();
    void testTakeoffPredictionForAnItem();
    void testReturnNowHysteresis();
    void testCruiseMeasuredAgainstAMeasuredHover();
};

// Default battery config: 6S 1P 5000mAh LiPo 20% reserve
void VehicleProfileManagerTest::testDefaultBatteryConfig()
{
    VehicleProfileManager vpm;
    QCOMPARE(vpm.batterySeriesCells(), 6);
    QCOMPARE(vpm.batteryParallelCells(), 1);
    QCOMPARE(vpm.batteryCellMah(), 5000);
    QCOMPARE(vpm.batteryReserve(), 0.20);
    QCOMPARE(vpm.batteryType(), QStringLiteral("LiPo"));
}

// Capacity = parallel × cellMah / 1000
void VehicleProfileManagerTest::testCapacityCalculation()
{
    VehicleProfileManager vpm;

    // 1P × 5000mAh = 5.0 Ah
    vpm.setBatterySeriesCells(6);
    vpm.setBatteryParallelCells(1);
    vpm.setBatteryCellMah(5000);
    QCOMPARE(vpm.capacityAh(), 5.0);

    // 2P × 5000mAh = 10.0 Ah
    vpm.setBatteryParallelCells(2);
    QCOMPARE(vpm.capacityAh(), 10.0);

    // 1P × 2200mAh = 2.2 Ah
    vpm.setBatteryParallelCells(1);
    vpm.setBatteryCellMah(2200);
    QCOMPARE(vpm.capacityAh(), 2.2);
}

// Battery config changes emit batteryConfigChanged signal
void VehicleProfileManagerTest::testBatteryConfigSignals()
{
    VehicleProfileManager vpm;
    QSignalSpy spy(&vpm, &VehicleProfileManager::batteryConfigChanged);

    vpm.setBatterySeriesCells(4);
    QVERIFY(spy.count() >= 1);

    vpm.setBatteryParallelCells(2);
    QVERIFY(spy.count() >= 2);

    vpm.setBatteryCellMah(3000);
    QVERIFY(spy.count() >= 3);

    vpm.setBatteryReserve(0.30);
    QVERIFY(spy.count() >= 4);

    vpm.setBatteryType(QStringLiteral("LiIon"));
    QVERIFY(spy.count() >= 5);
}

// Battery serial tracking
void VehicleProfileManagerTest::testSetBatterySerial()
{
    VehicleProfileManager vpm;
    QSignalSpy spy(&vpm, &VehicleProfileManager::currentBatteryChanged);

    vpm.setBatterySerial(QStringLiteral("BAT-001"));
    QCOMPARE(vpm.currentBatterySerial(), QStringLiteral("BAT-001"));
    QVERIFY(spy.count() >= 1);
}

// VehicleKind resolution from MAV_TYPE integer
void VehicleProfileManagerTest::testKindFromMavType()
{
    // Quad
    QCOMPARE(VehicleProfileManager::kindFromMavType(MAV_TYPE_QUADROTOR), VehicleKind::Multirotor);
    // Hexacopter
    QCOMPARE(VehicleProfileManager::kindFromMavType(MAV_TYPE_HEXAROTOR), VehicleKind::Multirotor);
    // Fixed wing
    QCOMPARE(VehicleProfileManager::kindFromMavType(MAV_TYPE_FIXED_WING), VehicleKind::FixedWing);
    // Unknown
    QCOMPARE(VehicleProfileManager::kindFromMavType(int(MAV_TYPE_GENERIC)), VehicleKind::Unknown);
}

// VehicleKind resolution from type string
void VehicleProfileManagerTest::testKindFromTypeString()
{
    QCOMPARE(VehicleProfileManager::kindFromTypeString(QStringLiteral("MULTIROTOR")), VehicleKind::Multirotor);
    QCOMPARE(VehicleProfileManager::kindFromTypeString(QStringLiteral("FIXED_WING")), VehicleKind::FixedWing);
    QCOMPARE(VehicleProfileManager::kindFromTypeString(QStringLiteral("VTOL_CONVENTIONAL")), VehicleKind::VtolConventional);
    QCOMPARE(VehicleProfileManager::kindFromTypeString(QStringLiteral("UNKNOWN")), VehicleKind::Unknown);
    // Case insensitive
    QCOMPARE(VehicleProfileManager::kindFromTypeString(QStringLiteral("multirotor")), VehicleKind::Multirotor);
}

// Kind string output
void VehicleProfileManagerTest::testKindString()
{
    QCOMPARE(VehicleProfileManager::kindString(VehicleKind::Multirotor), QStringLiteral("MULTIROTOR"));
    QCOMPARE(VehicleProfileManager::kindString(VehicleKind::FixedWing), QStringLiteral("FIXED_WING"));
    QCOMPARE(VehicleProfileManager::kindString(VehicleKind::VtolConventional), QStringLiteral("VTOL_CONVENTIONAL"));
    QCOMPARE(VehicleProfileManager::kindString(VehicleKind::Unknown), QStringLiteral("UNKNOWN"));
}

// Live SOC default is -1 (unknown)
void VehicleProfileManagerTest::testLiveSOCDefault()
{
    VehicleProfileManager vpm;
    QCOMPARE(vpm.liveSOC(), -1);
    QCOMPARE(vpm.liveTimeMins(), -1.0);
}

// Battery reserve should be clamped to valid range
void VehicleProfileManagerTest::testBatteryReserveBounds()
{
    VehicleProfileManager vpm;

    vpm.setBatteryReserve(0.50);
    QCOMPARE(vpm.batteryReserve(), 0.50);

    // Negative should be clamped
    vpm.setBatteryReserve(-0.10);
    QVERIFY(vpm.batteryReserve() >= 0.0);

    // Very high should be clamped
    vpm.setBatteryReserve(1.0);
    QVERIFY(vpm.batteryReserve() <= 0.9);
}

// Flight time near the end must not be overstated: the reserve is a share of
// the WHOLE pack, so usable Ah = capacity × (SOC − reserve) and reaches 0 at the
// reserve ("land now"). The old rule, remaining × (1 − reserve), still showed
// 0.8 Ah (2.4 min at 20 A) at 20 % SOC with a 20 % reserve.
void VehicleProfileManagerTest::testUsableAhStopsAtTheReserve()
{
    auto usable = &VehicleProfileManager::usableAhAboveReserve;
    QVERIFY(qAbs(usable(5.0, 60, 0.20) - 2.0) < 1e-9);
    QVERIFY(qAbs(usable(5.0, 30, 0.20) - 0.5) < 1e-9);
    QCOMPARE(usable(5.0, 20, 0.20), 0.0);       // at the reserve: land
    QCOMPARE(usable(5.0, 10, 0.20), 0.0);       // below it: still 0, never negative
    QCOMPARE(usable(0.0, 50, 0.20), -1.0);      // pack not configured
    QCOMPARE(usable(5.0, -1, 0.20), -1.0);      // SOC not reported

    // Minutes at 20 A for the documented example: 1.5 min at 30 %, 0 at 20 %.
    QVERIFY(qAbs(usable(5.0, 30, 0.20) / 20.0 * 60.0 - 1.5) < 1e-9);

    VehicleProfileManager vpm;
    vpm.setBatteryParallelCells(1);
    vpm.setBatteryCellMah(5000);
    vpm.setBatteryReserve(0.25);
    QVERIFY(qAbs(vpm.usableAhAboveReserveFor(45) - 1.0) < 1e-9);   // 5 Ah × (0.45 − 0.25)
    QCOMPARE(vpm.usableAhAboveReserveFor(25), 0.0);
}

// The charge comes from the SELECTED pack (rated mAh − FC mAh used), not the
// FC's own % (relative to BATT_CAPACITY). Example: the FC said 81 %
// after 621 mAh, i.e. BATT_CAPACITY ≈ 3300 mAh; with a 5000 mAh pack the real
// charge was ~88 %.
void VehicleProfileManagerTest::testPackSocFromSelectedPackNotFcCapacity()
{
    auto soc = &VehicleProfileManager::packSocPct;
    QCOMPARE(soc(81, 621.0, 5.0), 88);        // pack-based, not the FC's 81
    QCOMPARE(soc(50, 0.0, 5.0), 100);         // fresh counter
    QCOMPARE(soc(10, 6000.0, 5.0), 0);        // used more than rated: 0, not negative
    QCOMPARE(soc(73, -1.0, 5.0), 73);         // no mAh counter: FC % as fallback
    QCOMPARE(soc(73, 500.0, 0.0), 73);        // no pack configured: FC % as fallback
    QCOMPARE(soc(-1, -1.0, 5.0), -1);         // nothing known
}

// The in-flight list of reached items shows the take-off prediction beside
// the real figures: the item's time from the arm and its pessimistic charge,
// found by the item's number in the estimate kept at the arm (not by its
// place in the list); an item the estimate does not have gives -1, -1.
void VehicleProfileManagerTest::testTakeoffPredictionForAnItem()
{
    auto wp = [](int seq, double timeS, double likely, double pess) {
        return QVariantMap{{QStringLiteral("seq"), seq}, {QStringLiteral("timeS"), timeS},
                           {QStringLiteral("socLikelyPct"), likely}, {QStringLiteral("socPessimisticPct"), pess}};
    };
    const QVariantMap estimate{{QStringLiteral("waypoints"),
                                QVariantList{wp(1, 12.0, 96.0, 95.0), wp(3, 75.5, 81.0, 78.0), wp(2, 40.0, 90.0, 88.0)}}};
    const auto p3 = VehicleProfileManager::takeoffPredictionFor(estimate, 3);
    QCOMPARE(p3.first, 75.5);
    QCOMPARE(p3.second, 78.0);                          // the pessimistic charge, not the likely one
    const auto p2 = VehicleProfileManager::takeoffPredictionFor(estimate, 2);
    QCOMPARE(p2.first, 40.0);
    QCOMPARE(p2.second, 88.0);
    const auto none = VehicleProfileManager::takeoffPredictionFor(estimate, 7);
    QCOMPARE(none.first, -1.0);
    QCOMPARE(none.second, -1.0);
    const auto empty = VehicleProfileManager::takeoffPredictionFor(QVariantMap(), 1);
    QCOMPARE(empty.first, -1.0);
    QCOMPARE(empty.second, -1.0);
}

// RETURN NOW does not flicker: on as soon as the return no longer fits
// (margin below 0); once on, it stays on until the margin is back to
// kReturnNowClearPts (3 points of the pack) or more; never while already
// returning or landing; an update that cannot work out the margin keeps the
// last state. A margin wobbling around 0, as in a simulated flight where it
// came on five times, now turns it on once.
void VehicleProfileManagerTest::testReturnNowHysteresis()
{
    auto held = &VehicleProfileManager::returnNowHeld;
    const double clear = VehicleProfileManager::kReturnNowClearPts;
    QCOMPARE(clear, 3.0);

    QVERIFY(!held(false, 0.5, false));                  // fits: off
    QVERIFY(!held(false, 0.0, false));                  // exactly at the reserve: still fits
    QVERIFY(held(false, -0.1, false));                  // no longer fits: on
    QVERIFY(held(true, 0.5, false));                    // back above, but not clearly: stays on
    QVERIFY(held(true, clear - 0.01, false));
    QVERIFY(!held(true, clear, false));                 // clearly back: off
    QVERIFY(!held(true, -5.0, true));                   // already returning: off
    QVERIFY(!held(false, -5.0, true));
    QVERIFY(held(true, qQNaN(), false));                // margin not known: keeps the last state
    QVERIFY(!held(false, qQNaN(), false));

    // A wobbling margin, one value per 2 s update.
    const double wobble[] = {0.8, -0.2, 0.4, -0.1, 1.2, 0.3, -0.3, 2.0, 2.9, 3.4, 1.0};
    bool on = false;
    int turnedOn = 0;
    for (double m : wobble) {
        const bool next = held(on, m, false);
        turnedOn += (next && !on) ? 1 : 0;
        on = next;
    }
    QCOMPARE(turnedOn, 1);
    QVERIFY(!on);                                       // off after 3.4, not back on at 1.0
}

// A cruise ratio (cruise power ÷ hover power) is measured only against a
// hover that was measured: this flight's own steady hover once it has its
// 15 samples (30 s), else the hover remembered from an earlier flight; never
// one worked out from MOT_THST_HOVER (in a simulated flight that gave a
// ratio of 1.94, which would have been saved for the next estimate).
void VehicleProfileManagerTest::testCruiseMeasuredAgainstAMeasuredHover()
{
    auto ref = &VehicleProfileManager::cruiseReferenceThrustG;
    const int enough = MotorTable::kMinHoverSamples;
    const QString last = QStringLiteral("last flight");
    const QString param = QStringLiteral("MOT_THST_HOVER");

    // This flight's own hover, once it has enough samples: it wins.
    QCOMPARE(ref(3000.0 * enough, enough, 2800.0, last), 3000.0);
    QCOMPARE(ref(3000.0 * enough, enough, 2500.0, param), 3000.0);
    // Not enough of its own yet: the remembered hover.
    QCOMPARE(ref(3000.0 * (enough - 1), enough - 1, 2800.0, last), 2800.0);
    // Only MOT_THST_HOVER, or nothing: no ratio.
    QCOMPARE(ref(3000.0 * (enough - 1), enough - 1, 2500.0, param), -1.0);
    QCOMPARE(ref(0.0, 0, 2500.0, param), -1.0);
    QCOMPARE(ref(0.0, 0, -1.0, QString()), -1.0);
    QCOMPARE(ref(0.0, 0, 0.0, last), -1.0);              // a remembered hover of nothing is no hover
}

UT_REGISTER_TEST(VehicleProfileManagerTest)

#include "VehicleProfileManagerTest.moc"
