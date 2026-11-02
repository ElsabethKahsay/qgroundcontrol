#include <QTest>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <tuple>

#include "UnitTest.h"
#include "VehicleProfileManager.h"
#include "DatabaseManager.h"
#include "PowerModel.h"
#include "PowerTypeModel.h"
#include "MotorTable.h"
#include <QCoreApplication>
#include <QFileInfo>

/**
 * Battery time estimator math: thrust→current interpolation, the known
 * acceptance datapoint (6 kg + 4 kg → ~13.5 min @ 8S3P5000), HobbyWing
 * pull-test JSON loading with voltage/propeller filtering, and the
 * motor_thrust_table DB round-trip.
 */
class BatteryEstimatorTest : public UnitTest {
    Q_OBJECT

private:
    // One process-lifetime manager: keeps VehicleProfileManager::s_instance
    // valid for every later test in the binary.
    VehicleProfileManager *mgr()
    {
        static VehicleProfileManager m;
        return &m;
    }

    QString initTempDb(DatabaseManager &db)
    {
        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate(QDir::tempPath() + QStringLiteral("/test_db_XXXXXX.db"));
        if (!tmpFile.open())
            qFatal("cannot create a temporary file for the test database");
        const QString path = tmpFile.fileName();
        tmpFile.close();
        db.initialize(path);
        return path;
    }

    /// Build a pull-test style JSON payload from (voltage, prop, thrust, current) rows.
    static QString makeDatasheet(
        const QList<std::tuple<double, QString, double, double>> &rows,
        const QString &motor = QStringLiteral("HobbyWing X9"))
    {
        QJsonArray pts;
        for (const auto &r : rows) {
            QJsonObject p;
            p.insert(QStringLiteral("voltage_v"), std::get<0>(r));
            if (!std::get<1>(r).isEmpty())
                p.insert(QStringLiteral("propeller"), std::get<1>(r));
            p.insert(QStringLiteral("thrust_g"), std::get<2>(r));
            p.insert(QStringLiteral("current_a"), std::get<3>(r));
            pts.append(p);
        }
        QJsonObject root;
        root.insert(QStringLiteral("motor"), motor);
        root.insert(QStringLiteral("source"), QStringLiteral("Motor Propeller Pull Test Data"));
        root.insert(QStringLiteral("points"), pts);
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    /// The four learning flights of the worked example in POWER_TYPES_PLAN.md
    /// (SKY-03: hover 32 A + 6 A/kg, cruise 21 A + 4 A/kg; the FC's synthetic
    /// current reads 1/1.2 of the charger).
    static QVector<PowerTypeModel::Flight> sky03Flights()
    {
        auto flight = [](double hover, double cruise, double kg, double chargerMah) {
            PowerTypeModel::Flight f;
            if (kg > 0.0) { f.hoverMinLoaded = hover; f.cruiseMinLoaded = cruise; }
            else          { f.hoverMinEmpty  = hover; f.cruiseMinEmpty  = cruise; }
            f.payloadKg = kg;
            f.chargerAh = chargerMah / 1000.0;
            f.fcAh      = chargerMah / 1.2 / 1000.0;
            return f;
        };
        return { flight(3, 10, 0.0, 5100.0), flight(8, 4, 0.0, 5666.7),
                 flight(3, 10, 1.5, 6550.0), flight(8, 4, 1.5, 7266.7) };
    }

    static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    // ── Pre-flight estimate from the selected pack ───────────────────────
    // Energy comes from the pack itself (cells × nominal volts × Ah above the
    // reserve), not from a voltage guess; time = energy ÷ Wh per minute and
    // range = time × cruise speed when the vehicle has no flight history.
    void testUsablePackEnergyFromThePack()
    {
        QCOMPARE(PowerModel::nominalCellVolts(QStringLiteral("LiPo")), 3.7);
        QCOMPARE(PowerModel::nominalCellVolts(QStringLiteral("LiIon")), 3.6);
        QCOMPARE(PowerModel::nominalCellVolts(QStringLiteral("LiHV")), 3.85);
        QCOMPARE(PowerModel::nominalCellVolts(QStringLiteral("LiFe")), 3.2);

        // 6S1P 5000 mAh LiPo, full, 20 % reserve: 4 Ah usable × 6 × 3.7 V.
        QVERIFY(qAbs(PowerModel::usablePackWh(6, 1, 5000, QStringLiteral("LiPo"), 100, 0.20) - 88.8) < 1e-9);
        // Half charged: 1.5 Ah usable.
        QVERIFY(qAbs(PowerModel::usablePackWh(6, 1, 5000, QStringLiteral("LiPo"), 50, 0.20) - 33.3) < 1e-9);
        QCOMPARE(PowerModel::usablePackWh(6, 1, 5000, QStringLiteral("LiPo"), 20, 0.20), 0.0);   // at the reserve
        QCOMPARE(PowerModel::usablePackWh(0, 1, 5000, QStringLiteral("LiPo"), 100, 0.20), -1.0); // no pack set

        // The full-pack energy no longer guesses the cell count from the
        // voltage (a 6S at 22 V used to count as 5S).
        VehicleProfileManager vpm;
        vpm.setBatterySeriesCells(6);
        vpm.setBatteryParallelCells(1);
        vpm.setBatteryCellMah(5000);
        vpm.setBatteryType(QStringLiteral("LiPo"));
        QVERIFY(qAbs(vpm.batteryWh() - 111.0) < 1e-9);
    }

    void testAirframeDecidesTheDefaults()
    {
        QCOMPARE(PowerModel::airframeForKind(QStringLiteral("FIXED_WING")), QStringLiteral("FixedWing"));
        QCOMPARE(PowerModel::airframeForKind(QStringLiteral("MULTIROTOR")), QStringLiteral("MultiRotor"));
        QCOMPARE(PowerModel::airframeForKind(QStringLiteral("VTOL_CONVENTIONAL")), QStringLiteral("VTOL"));
        QCOMPARE(PowerModel::airframeForKind(QStringLiteral("UNKNOWN")), QStringLiteral("Unknown"));

        PowerModel pm;
        // No device id: defaults only. 88.8 Wh on a plane (2 Wh/min) lasts
        // 44.4 min and at 60 km/h covers 44.4 km ...
        PowerEstimate plane = pm.estimateFromEnergy(QString(), 0.0, 88.8, QStringLiteral("FixedWing"));
        QVERIFY(qAbs(plane.flightTimeMin - 44.4) < 1e-9);
        QVERIFY(qAbs(plane.rangeKm - 44.4) < 1e-9);
        QVERIFY(!plane.isCalibrated);
        // ... while a multirotor (8 Wh/min, 30 km/h) gets 11.1 min and 5.55 km.
        PowerEstimate quad = pm.estimateFromEnergy(QString(), 0.0, 88.8, QStringLiteral("MultiRotor"));
        QVERIFY(qAbs(quad.flightTimeMin - 11.1) < 1e-9);
        QVERIFY(qAbs(quad.rangeKm - 5.55) < 1e-9);
        QVERIFY(plane.flightTimeMin > 3.0 * quad.flightTimeMin);
        // No energy left: no time.
        QCOMPARE(pm.estimateFromEnergy(QString(), 0.0, 0.0, QStringLiteral("FixedWing")).flightTimeMin, 0.0);
    }

    // ── Interpolation edges ──────────────────────────────────────────────

    void testCurrentForThrustEdges()
    {
        // Default table spans 500..4000 g.
        mgr()->loadMotorDatasheet(QString(), 0);  // no-op guard: empty input must not throw
        QCOMPARE(mgr()->minTableThrust(), 500.0);
        QCOMPARE(mgr()->maxTableThrust(), 4000.0);

        // Exact table point → exact current (acceptance row for 1500 g).
        QCOMPARE(mgr()->currentForThrust(1500), 8.0);
        QCOMPARE(mgr()->currentForThrust(2500), 12.8);

        // Between points → linear interpolation: halfway 1500→2000 is 9.25 A.
        QCOMPARE(mgr()->currentForThrust(1750), 9.25);

        // Below minimum / above maximum → clamped to end currents.
        QCOMPARE(mgr()->currentForThrust(100), 3.0);
        QCOMPARE(mgr()->currentForThrust(5000), 25.0);
    }

    void testEmptySetThrustTableRejected()
    {
        // Intentionally leaked: the ctor re-points the singleton s_instance,
        // and a stack object would leave it dangling after this slot returns.
        auto *fresh = new VehicleProfileManager();

        // An empty hand-entered table must be rejected, not wipe the active one.
        fresh->setThrustTable({});
        QVERIFY(fresh->thrustTableWarning().contains(QStringLiteral("Rejected")));
        QCOMPARE(fresh->currentForThrust(2000), 10.5);
    }

    // ── Known acceptance datapoint ───────────────────────────────────────

    void testKnownDatapointDuration()
    {
        // 6 kg UAV + 4 kg payload, 4 motors, 8S 3P 5000 mAh, I_FC = 2 A.
        const double uavKg = 6.0, payloadKg = 4.0;
        const int nMotors = 4;
        const double iFc = 2.0;
        const double capacityAh = 3 * 5000 / 1000.0;
        const double usableAh = capacityAh * 0.80;

        const double thrustPerMotor = (uavKg + payloadKg) * 1000 / nMotors;   // 2500 g
        QCOMPARE(thrustPerMotor, 2500.0);

        const double iPerMotor = mgr()->currentForThrust(thrustPerMotor);     // 12.8 A
        QCOMPARE(iPerMotor, 12.8);

        const double iTotal = iPerMotor * nMotors + iFc;                      // 53.2 A
        QCOMPARE(iTotal, 53.2);

        const double durationMin = (usableAh / iTotal) * 60;                  // ≈13.5 min
        QVERIFY2(durationMin > 13.4 && durationMin < 13.7,
                 qPrintable(QStringLiteral("duration=%1").arg(durationMin)));

        // Base case (no payload): 1500 g/motor → 8 A → 34 A total ≈ 21.2 min.
        const double iBasePerMotor = mgr()->currentForThrust(uavKg * 1000 / nMotors);
        QCOMPARE(iBasePerMotor, 8.0);
        const double baseMin = (usableAh / (iBasePerMotor * nMotors + iFc)) * 60;
        QVERIFY(baseMin > 21.0 && baseMin < 21.4);
    }

    // ── Datasheet JSON loading ───────────────────────────────────────────

    void testLoadValidPayloadReplacesDefault()
    {
        const QString json = makeDatasheet({
            { 33.6, QStringLiteral("24x8"), 1000, 5.2 },
            { 33.6, QStringLiteral("24x8"), 2000, 10.1 },
            { 33.6, QStringLiteral("24x8"), 3000, 16.4 },
        });
        QVERIFY(mgr()->loadMotorDatasheet(json, 33.6));
        QCOMPARE(mgr()->maxTableThrust(), 3000.0);
        QCOMPARE(mgr()->currentForThrust(2000), 10.1);
        QVERIFY(mgr()->thrustTableInfo().contains(QStringLiteral("HobbyWing X9")));
        QVERIFY(mgr()->thrustTableWarning().isEmpty());
    }

    void testNearestVoltageSelectionAndWarning()
    {
        const QString json = makeDatasheet({
            { 22.2, QStringLiteral("24x8"), 1000, 4.9 },
            { 22.2, QStringLiteral("24x8"), 2000, 9.7 },
            { 33.6, QStringLiteral("24x8"), 1000, 5.2 },
            { 33.6, QStringLiteral("24x8"), 2000, 10.1 },
        });

        // Exact voltage match → no fallback warning.
        QVERIFY(mgr()->loadMotorDatasheet(json, 33.6));
        QVERIFY(mgr()->thrustTableWarning().isEmpty());
        QCOMPARE(mgr()->currentForThrust(2000), 10.1);

        // No exact match (wanted 22.3) → nearest group (22.2) plus a warning.
        QVERIFY(mgr()->loadMotorDatasheet(json, 22.3));
        QVERIFY2(mgr()->thrustTableWarning().contains(QStringLiteral("nearest")),
                 qPrintable(mgr()->thrustTableWarning()));
        QCOMPARE(mgr()->currentForThrust(2000), 9.7);
    }

    void testPropellerMajorityFilter()
    {
        const QString json = makeDatasheet({
            { 33.6, QStringLiteral("24x8"), 1000, 5.2 },
            { 33.6, QStringLiteral("24x8"), 2000, 10.1 },
            { 33.6, QStringLiteral("24x8"), 3000, 16.4 },
            { 33.6, QStringLiteral("30x10"), 1000, 6.0 },
            { 33.6, QStringLiteral("30x10"), 2000, 11.5 },
        });
        // 24x8 has more samples → its points win.
        QVERIFY(mgr()->loadMotorDatasheet(json, 33.6));
        QCOMPARE(mgr()->currentForThrust(2000), 10.1);
        QCOMPARE(mgr()->maxTableThrust(), 3000.0);
    }

    void testInvalidJsonRejectedKeepsPreviousTable()
    {
        QVERIFY(mgr()->loadMotorDatasheet(
            makeDatasheet({ { 33.6, QString(), 2000, 10.1 } }), 33.6));
        const double before = mgr()->currentForThrust(2000);

        // Point missing current_a → no usable points → rejected, table intact.
        QJsonObject badPoint;
        badPoint.insert(QStringLiteral("thrust_g"), 1500);
        QJsonArray pts{ badPoint };
        QJsonObject root;
        root.insert(QStringLiteral("points"), pts);
        const QString bad = QString::fromUtf8(
            QJsonDocument(root).toJson(QJsonDocument::Compact));

        QVERIFY(!mgr()->loadMotorDatasheet(bad, 33.6));
        QCOMPARE(mgr()->currentForThrust(2000), before);
    }

    void testLoadFromFilePath()
    {
        const QString json = makeDatasheet({ { 33.6, QString(), 1500, 7.7 },
                                             { 33.6, QString(), 2500, 12.4 } });
        QTemporaryFile f(QDir::tempPath() + QStringLiteral("/ds_XXXXXX.json"));
        QVERIFY(f.open());
        f.write(json.toUtf8());
        f.flush();
        const QString path = f.fileName();
        f.close();

        QVERIFY(mgr()->loadMotorDatasheet(path, 33.6));
        QCOMPARE(mgr()->currentForThrust(2500), 12.4);

        // Nonexistent path → rejected with a readable warning, table intact.
        QVERIFY(!mgr()->loadMotorDatasheet(QStringLiteral("/nonexistent/ds.json"), 33.6));
        QVERIFY(mgr()->thrustTableWarning().contains(QStringLiteral("Cannot read")));
        QCOMPARE(mgr()->currentForThrust(2500), 12.4);
    }

    void testManualSetThrustTable()
    {
        QVariantList manual{
            QVariantMap{ { QStringLiteral("thrust_g"), 1200.0 },
                         { QStringLiteral("current_a"), 6.4 } },
            QVariantMap{ { QStringLiteral("thrust_g"), 2200.0 },
                         { QStringLiteral("current_a"), 11.0 } },
        };
        mgr()->setThrustTable(manual);
        QCOMPARE(mgr()->currentForThrust(1700), 8.7);   // midpoint interpolation
        QCOMPARE(mgr()->maxTableThrust(), 2200.0);
        QVERIFY(mgr()->thrustTableInfo().contains(QStringLiteral("operator")));
    }

    // ── DB round-trip ────────────────────────────────────────────────────

    void testMotorThrustTablePersistence()
    {
        // DatabaseManager is a singleton with a private ctor — test through it.
        auto &db = DatabaseManager::instance();
        db.reset();
        const QString path = initTempDb(db);

        const QString uid = QStringLiteral("uid-thrust-test");
        QVERIFY(db.upsertVehicleEx(uid, QStringLiteral("Test Rig"),
                                   QStringLiteral("ardupilotmega"), QStringLiteral("quad")));

        // Default: no table stored.
        QCOMPARE(db.vehicleMotorThrustTable(uid), QString());

        const QString json = makeDatasheet({ { 33.6, QString(), 2000, 10.1 } });
        QVERIFY(db.updateVehicleMotorThrustTable(uid, json));
        QCOMPARE(db.vehicleMotorThrustTable(uid), json);
        QCOMPARE(db.vehicleMotorThrustTable(QStringLiteral("uid-unknown")), QString());

        // The successful UPDATE above proves the v16 column exists (an UPDATE
        // against a missing column fails in execOrWarn); assert the migration
        // version as well.
        QCOMPARE(db.storedSchemaVersion(), db.schemaVersion());

        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }
    // ── Power types (PowerTypeModel.h) ───────────────────────────────────
    // Hover and cruise current per kg are learned from charger numbers: two
    // payload-free flights with a different hover/cruise mix give the 0 kg
    // currents, payload flights give the extra per kg.
    void testPowerTypeFitFromChargerFlights()
    {
        using namespace PowerTypeModel;
        const QVector<Flight> all = sky03Flights();

        Model m = fit(all.mid(0, 1));
        QCOMPARE(m.status, FitStatus::NotEnoughFlights);
        QVERIFY(!m.usable());

        m = fit(all.mid(0, 2));
        QCOMPARE(m.status, FitStatus::PayloadNotModelled);
        QVERIFY2(near(m.hover0, 32.0, 0.01) && near(m.cruise0, 21.0, 0.01),
                 qPrintable(QStringLiteral("hover %1 cruise %2").arg(m.hover0).arg(m.cruise0)));
        QCOMPARE(m.hoverK, 0.0);

        // One payload flight: same percentage in both phases (+19 %/kg here).
        m = fit(all.mid(0, 3));
        QCOMPARE(m.status, FitStatus::PayloadProportional);
        QVERIFY2(near(m.hoverK, 6.07, 0.02) && near(m.cruiseK, 3.98, 0.02),
                 qPrintable(QStringLiteral("hoverK %1 cruiseK %2").arg(m.hoverK).arg(m.cruiseK)));

        m = fit(all);
        QCOMPARE(m.status, FitStatus::Full);
        QVERIFY2(near(m.hoverK, 6.0, 0.01) && near(m.cruiseK, 4.0, 0.01),
                 qPrintable(QStringLiteral("hoverK %1 cruiseK %2").arg(m.hoverK).arg(m.cruiseK)));
        QVERIFY(near(m.hoverA(0.8), 36.8, 0.01));
        QVERIFY(near(m.cruiseA(0.8), 24.2, 0.01));

        // Two near-identical mixes cannot separate hover from cruise; both
        // are mostly cruise, so the advice is a mostly-hover flight.
        QVector<Flight> alike = all.mid(0, 1);
        Flight f2 = all[0];
        f2.hoverMinEmpty = 3.3; f2.cruiseMinEmpty = 11.0; f2.chargerAh = 5.61;
        alike << f2;
        m = fit(alike);
        QCOMPARE(m.status, FitStatus::TooAlike);
        QVERIFY2(m.advice.contains(QStringLiteral("mostly-hover")), qPrintable(m.advice));
    }

    // The worked example: 0.8 kg to a 5 km target, 2 min there, dropped,
    // back empty, on 12.8 Ah usable (16 Ah, 20 % reserve).
    void testPowerTypeMissionCheck()
    {
        using namespace PowerTypeModel;
        const Model m = fit(sky03Flights());
        Mission x;
        x.payloadKg = 0.8; x.targetKm = 5.0; x.minAtTarget = 2.0;
        x.outSpeedMs = 15.0; x.backSpeedMs = 15.0; x.usableAh = 12.8;

        MissionResult r = checkMission(m, x);
        const QString got = QStringLiteral("legs %1 %2 %3 %4 %5 total %6 Ah %7 min max %8 km")
                                .arg(r.takeoffAh).arg(r.outAh).arg(r.targetAh).arg(r.backAh).arg(r.landAh)
                                .arg(r.totalAh).arg(r.totalMin).arg(r.maxTargetKm);
        QVERIFY2(near(r.takeoffAh, 0.613, 0.002) && near(r.outAh, 2.241, 0.002)
                     && near(r.targetAh, 1.227, 0.002), qPrintable(got));
        // The way back and the landing are costed without the dropped payload.
        QVERIFY2(near(r.backAh, 1.944, 0.002) && near(r.landAh, 0.533, 0.002), qPrintable(got));
        QVERIFY2(near(r.totalAh, 6.559, 0.003) && near(r.totalMin, 15.11, 0.01), qPrintable(got));
        QVERIFY(r.fits);
        QVERIFY(near(r.spareAh, 6.24, 0.01));
        QVERIFY2(near(r.maxTargetKm, 12.46, 0.01), qPrintable(got));

        // A 5 m/s headwind on the way out: 10 m/s out, 20 m/s back.
        x.outSpeedMs = 10.0; x.backSpeedMs = 20.0;
        r = checkMission(m, x);
        QVERIFY2(near(r.totalAh, 7.193, 0.003), qPrintable(QString::number(r.totalAh)));

        // Not enough charge: the check fails and says by how much.
        x.outSpeedMs = 15.0; x.backSpeedMs = 15.0; x.usableAh = 6.0;
        r = checkMission(m, x);
        QVERIFY(!r.fits);
        QVERIFY(near(r.spareAh, -0.559, 0.003));
    }

    // The FC's synthetic current is corrected by charger mAh ÷ FC mAh, and the
    // suggested BATT_AMP_PERVLT follows ArduPilot's recalibration formula.
    void testPowerTypeChargerFactorAndAmpPerVolt()
    {
        using namespace PowerTypeModel;
        QVERIFY(near(chargerFactor(sky03Flights()), 1.20, 0.001));
        QCOMPARE(chargerFactor({}), 1.0);
        QVERIFY(near(suggestedAmpPerVolt(120.0, 5100.0, 4250.0), 144.0, 0.01));
        QCOMPARE(suggestedAmpPerVolt(120.0, -1.0, 4250.0), 120.0);

        const Model seed = seedFromSynthetic(26.7, 17.5, 1.2);
        QCOMPARE(seed.status, FitStatus::PayloadNotModelled);
        QVERIFY(near(seed.hover0, 32.04, 0.01) && near(seed.cruise0, 21.0, 0.01));
        QVERIFY(!seedFromSynthetic(0.0, 17.5, 1.2).usable());
    }

    // In flight: charge used from the FC × factor, the model current scaled by
    // the live current within ±30 %, and RETURN NOW when the rest of the trip
    // needs more than is left above the reserve.
    void testPowerTypeInFlight()
    {
        using namespace PowerTypeModel;
        const Model m = fit(sky03Flights());

        QVERIFY(near(chargeUsedAh(4.333, 1.2), 5.2, 0.001));
        QVERIFY(chargeUsedAh(-1.0, 1.2) < 0.0);

        QVERIFY(near(liveRatio(23.3, 1.2, 24.2), 1.155, 0.001));
        QCOMPARE(liveRatio(40.0, 1.2, 24.2), kLiveRatioMax);
        QCOMPARE(liveRatio(5.0, 1.2, 24.2), kLiveRatioMin);
        QCOMPARE(liveRatio(0.0, 1.2, 24.2), 1.0);
        QVERIFY(near(throttleRatio(0.297, 0.27), std::pow(1.1, 1.5), 0.001));
        QCOMPARE(throttleRatio(0.9, 0.27), kLiveRatioMax);

        // Half way out: 2.5 km to go with 0.8 kg, 2 min there, 5 km home.
        RemainingTrip t;
        t.kmToTarget = 2.5; t.minAtTargetLeft = 2.0; t.kmHome = 5.0;
        t.payloadAboard = true; t.payloadKg = 0.8;
        t.outSpeedMs = 15.0; t.backSpeedMs = 15.0;
        const double plain = remainingTripAh(m, t);
        QVERIFY2(near(plain, 1.120 + 1.227 + 1.944 + 0.533, 0.003), qPrintable(QString::number(plain)));
        t.ratio = 1.155;
        QVERIFY(near(remainingTripAh(m, t), plain * 1.155, 1e-9));

        QCOMPARE(returnShortAh(7.6, 4.8), 0.0);
        QVERIFY(near(returnShortAh(4.0, 4.4), 0.4, 1e-9));

        // On the way back the payload is gone.
        RemainingTrip back;
        back.kmHome = 3.0; back.backSpeedMs = 15.0; back.payloadKg = 0.8;
        QVERIFY(near(remainingTripAh(m, back), 21.0 * 3.333 / 60.0 + 32.0 / 60.0, 0.003));
    }

    // The Fly view's live estimate: charge left above the reserve from the
    // FC's mAh × the charger factor, the expected current for the phase
    // (model × live ratio), time/distance left, and RETURN NOW only while
    // airborne when the rest of the trip no longer fits.
    void testPowerTypeLiveEstimate()
    {
        using namespace PowerTypeModel;
        const double north = 1.0 / 111194.93;            // degrees per metre
        LiveInput in;
        in.model = fit(sky03Flights());
        in.factor = 1.2;
        in.capacityAh = 16.0;
        in.reserve = 0.20;
        in.fcMahUsed = 4333.0;
        in.currentA = 23.3;
        in.groundSpeedMs = 13.0;
        in.altitudeM = 30.0;
        in.homeLat = -35.0; in.homeLon = 149.0;
        in.lat = -35.0 + 2500.0 * north; in.lon = 149.0;            // half way out
        in.targetLat = -35.0 + 5000.0 * north; in.targetLon = 149.0;
        in.payloadAboard = true; in.payloadKg = 0.8; in.minAtTarget = 2.0;
        in.cruiseSpeedMs = 15.0;
        in.armed = true;

        LiveResult r = liveEstimate(in);
        const QString got = QStringLiteral("usable %1 A %2 ratio %3 left %4 min %5 km need %6 short %7")
                                .arg(r.usableAh).arg(r.currentA).arg(r.ratio).arg(r.timeLeftMin)
                                .arg(r.distLeftKm).arg(r.tripNeedAh).arg(r.tripShortAh);
        QVERIFY(r.valid && r.airborne && !r.hover);
        QVERIFY2(near(r.usableAh, 7.600, 0.002), qPrintable(got));
        QVERIFY2(near(r.ratio, 1.1554, 0.0005) && near(r.currentA, 27.96, 0.01), qPrintable(got));
        QVERIFY2(near(r.timeLeftMin, 16.31, 0.01) && near(r.distLeftKm, 12.72, 0.01), qPrintable(got));
        // The rest of the trip: 2.5 km out loaded, 2 min there, 5 km home empty, land.
        QVERIFY2(near(r.tripNeedAh, 4.8248 * 1.1554, 0.005), qPrintable(got));
        QCOMPARE(r.tripShortAh, 0.0);
        QVERIFY(!r.returnNow);

        // A target 20 km out: no longer fits, RETURN NOW.
        in.targetLat = -35.0 + 20000.0 * north;
        r = liveEstimate(in);
        QVERIFY2(near(r.tripNeedAh, 17.380 * 1.1554, 0.01) && near(r.tripShortAh, 12.48, 0.01), qPrintable(QString::number(r.tripShortAh)));
        QVERIFY(r.returnNow);

        // On the ground the same shortfall is not RETURN NOW (and no live ratio).
        LiveInput ground = in;
        ground.altitudeM = 0.0;
        ground.groundSpeedMs = 0.0;
        r = liveEstimate(ground);
        QVERIFY(!r.airborne && r.tripShortAh > 0.0 && !r.returnNow);
        QCOMPARE(r.ratio, 1.0);

        // After the drop the way home is costed empty, from here.
        in.payloadAboard = false;
        r = liveEstimate(in);
        QCOMPARE(r.ratio, kLiveRatioMax);                                   // 27.96 A vs 21 A: limited
        QVERIFY2(near(r.tripNeedAh, (21.0 * (2500.0 / 15.0 / 60.0) / 60.0 + 32.0 / 60.0) * 1.3, 0.005),
                 qPrintable(QString::number(r.tripNeedAh)));

        // Before the arm, full pack: endurance with the payload entered.
        LiveInput pre;
        pre.model = in.model;
        pre.capacityAh = 16.0;
        pre.fcMahUsed = 0.0;
        pre.payloadAboard = true; pre.payloadKg = 0.8;
        pre.cruiseSpeedMs = 15.0;
        r = liveEstimate(pre);
        QVERIFY2(near(r.hoverMin, 12.8 / 36.8 * 60.0, 0.01) && near(r.cruiseMin, 12.8 / 24.2 * 60.0, 0.01)
                     && near(r.cruiseKm, 12.8 / 24.2 * 60.0 * 60.0 * 15.0 / 1000.0, 0.01),
                 qPrintable(QStringLiteral("%1 %2 %3").arg(r.hoverMin).arg(r.cruiseMin).arg(r.cruiseKm)));
        QVERIFY(!r.tripKnown);                                              // no position, no home

        // No mAh counter: the FC's % instead.
        pre.fcMahUsed = -1.0;
        pre.socPct = 50.0;
        QVERIFY(near(liveEstimate(pre).usableAh, 4.8, 1e-9));
        // No model, or no pack: nothing.
        pre.model = Model();
        QVERIFY(!liveEstimate(pre).valid);
    }

    // Copters without a current that follows throttle: a constant FC current
    // (ArduPilot's synthetic current on a multicopter, seen in SITL as 0.3 A
    // in hover and cruise) is recognised, and the GCS counts the charge
    // itself: model current × throttle correction (±30 %) × time.
    void testPowerTypeGcsChargeCount()
    {
        using namespace PowerTypeModel;
        QVERIFY(!currentFollowsThrottle(0.3, 0.3));
        QVERIFY(!currentFollowsThrottle(0.3, 0.3001));
        QVERIFY(currentFollowsThrottle(26.7, 17.5));
        QVERIFY(!currentFollowsThrottle(0.0, 17.5));

        const Model m = fit(sky03Flights());
        QVERIFY(near(chargeStepAh(m, true, 0.8, 1.0, 60.0), m.hoverA(0.8) / 60.0, 1e-12));   // 1 min hover, 0.8 kg (≈ 36.8 A)
        QVERIFY(near(chargeStepAh(m, false, 0.0, 1.0, 120.0), m.cruiseA(0.0) * 2.0 / 60.0, 1e-12));
        QVERIFY(near(chargeStepAh(m, false, 0.0, 2.0, 60.0), m.cruiseA(0.0) * 1.3 / 60.0, 1e-12)); // limited to +30 %
        QVERIFY(near(chargeStepAh(m, false, 0.0, -1.0, 60.0), m.cruiseA(0.0) / 60.0, 1e-12));    // no throttle: model only
        QCOMPARE(chargeStepAh(m, true, 0.0, 1.0, 0.0), 0.0);
        QCOMPARE(chargeStepAh(Model(), true, 0.0, 1.0, 60.0), 0.0);

        // The throttle correction in place of the FC current, while flying.
        LiveInput in;
        in.model = m;
        in.capacityAh = 16.0;
        in.fcMahUsed = 5200.0;
        in.factor = 1.0;
        in.currentA = 0.3;                                  // the constant synthetic current: ignored
        in.throttleRatio = throttleRatio(0.297, 0.27);      // 10 % over the cruise reference
        in.groundSpeedMs = 13.0;
        in.altitudeM = 30.0;
        in.armed = true;
        LiveResult r = liveEstimate(in);
        QVERIFY2(near(r.ratio, std::pow(1.1, 1.5), 1e-6) && near(r.currentA, m.cruiseA(0.0) * std::pow(1.1, 1.5), 1e-6),
                 qPrintable(QStringLiteral("%1 %2").arg(r.ratio).arg(r.currentA)));
        in.armed = false;
        QCOMPARE(liveEstimate(in).ratio, 1.0);
    }

    // Validated = at least 5 flights, and each of the last three predicted
    // within 10 % by a model learned WITHOUT it (leave one out): a model
    // learned from a flight always matches it, so that proves nothing. A
    // flight more than 20 % off is flagged.
    void testPowerTypeValidationAndOutliers()
    {
        using namespace PowerTypeModel;
        QVector<Flight> flights = sky03Flights();
        const Model m = fit(flights);
        // Four flights: the fit goes through every one exactly, yet that is no
        // evidence; not validated.
        QVERIFY(near(m.predictAh(flights.last()), flights.last().chargerAh, 1e-3));
        QVERIFY(!isValidated(flights));

        // Two more flights that agree with the others (hover 32 + 6 A/kg,
        // cruise 21 + 4 A/kg): validated.
        QVector<Flight> six = flights;
        Flight f5; f5.hoverMinEmpty = 5; f5.cruiseMinEmpty = 6; f5.chargerAh = (32.0 * 5 + 21.0 * 6) / 60.0;
        Flight f6; f6.hoverMinLoaded = 5; f6.cruiseMinLoaded = 6; f6.payloadKg = 1.5;
        f6.chargerAh = (41.0 * 5 + 27.0 * 6) / 60.0;
        six << f5 << f6;
        QVERIFY(isValidated(six));

        // Four consistent flights without payload pass the left-out check
        // (any three still give the model), but four are fewer than five.
        auto empty = [](double hover, double cruise) {
            Flight f; f.hoverMinEmpty = hover; f.cruiseMinEmpty = cruise;
            f.chargerAh = (32.0 * hover + 21.0 * cruise) / 60.0;
            return f;
        };
        QVector<Flight> fourEmpty{ empty(3, 10), empty(8, 4), empty(5, 6), empty(10, 2) };
        QVERIFY(!isValidated(fourEmpty));
        fourEmpty << empty(6, 8);
        QVERIFY(isValidated(fourEmpty));                          // the fifth makes it

        // Five flights that are not consistent: each still fits a model
        // learned from all of them, but the left-out check catches it.
        QVector<Flight> bad = six.mid(0, 5);
        bad[4].chargerAh *= 1.4;                                  // flight 5 used 40 % more
        QVERIFY(!isValidated(bad));

        QVector<Flight> drift = six;
        drift.last().chargerAh *= 1.15;   // last flight 15 % over the model
        QVERIFY(!isValidated(drift));
        QVERIFY(!isOutlier(m, drift.last()));

        Flight off = flights.last();
        off.chargerAh *= 1.30;                                    // 23 % off the charger number
        QVERIFY(isOutlier(m, off));
        QVERIFY(!isOutlier(m, flights.first()));
    }

    // Stored flights → the model the screens show: only flights marked "used"
    // (with a charger number) are fitted; until there are enough, the FC's
    // synthetic current seeds a model labelled "synthetic".
    void testPowerTypeSummaryFromStoredFlights()
    {
        DatabaseManager &db = DatabaseManager::instance();
        QTemporaryDir dir;
        QVERIFY(db.initialize(dir.filePath(QStringLiteral("summary.db"))));
        const QString uid = QStringLiteral("SKY-03");
        QVERIFY(db.upsertVehicleEx(uid, QStringLiteral("Sky 3"), QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor")));
        const int type = mgr()->savePowerType(0, QStringLiteral("Quad X"), 15.0, 5.5, 6, {});
        QVERIFY(type > 0);

        // Nothing recorded yet.
        QVariantMap sum = mgr()->powerTypeSummary(type);
        QCOMPARE(sum.value(QStringLiteral("status")).toString(), QStringLiteral("none"));
        QCOMPARE(sum.value(QStringLiteral("source")).toString(), QStringLiteral("none"));

        // The worked example's four flights, recorded and marked "used".
        QList<int> sessions;
        for (const PowerTypeModel::Flight &f : sky03Flights()) {
            const int id = db.startFlightSession(uid, QString(), f.payloadKg);
            FlightPowerRecord r;
            r.powerTypeId = type;
            r.hoverMinEmpty = f.hoverMinEmpty; r.hoverMinLoaded = f.hoverMinLoaded;
            r.cruiseMinEmpty = f.cruiseMinEmpty; r.cruiseMinLoaded = f.cruiseMinLoaded;
            r.fcMah = f.fcAh * 1000.0; r.synthHoverA = 26.7; r.synthCruiseA = 17.5;
            r.startedFull = true; r.telemetryCoverage = 1.0;
            QVERIFY(db.saveFlightPowerRecord(id, r));
            sessions << id;
        }

        // Recorded but no charger numbers yet: seeded from the synthetic current.
        sum = mgr()->powerTypeSummary(type);
        QCOMPARE(sum.value(QStringLiteral("source")).toString(), QStringLiteral("synthetic"));
        QCOMPARE(sum.value(QStringLiteral("pendingFlights")).toInt(), 4);
        QVERIFY(near(sum.value(QStringLiteral("hover0")).toDouble(), 26.7, 0.001));
        QVERIFY(!sum.value(QStringLiteral("validated")).toBool());

        const QVector<PowerTypeModel::Flight> flights = sky03Flights();
        for (int i = 0; i < sessions.size(); ++i) {
            QVERIFY(mgr()->setFlightChargerMah(sessions[i], flights[i].chargerAh * 1000.0));
            QVERIFY(mgr()->setFlightLearningState(sessions[i], QStringLiteral("used")));
        }
        sum = mgr()->powerTypeSummary(type);
        const QString got = QStringLiteral("%1 hover %2+%3 cruise %4+%5 factor %6")
                                .arg(sum.value(QStringLiteral("status")).toString())
                                .arg(sum.value(QStringLiteral("hover0")).toDouble())
                                .arg(sum.value(QStringLiteral("hoverK")).toDouble())
                                .arg(sum.value(QStringLiteral("cruise0")).toDouble())
                                .arg(sum.value(QStringLiteral("cruiseK")).toDouble())
                                .arg(sum.value(QStringLiteral("chargerFactor")).toDouble());
        QCOMPARE(sum.value(QStringLiteral("status")).toString(), QStringLiteral("full"));
        QCOMPARE(sum.value(QStringLiteral("source")).toString(), QStringLiteral("charger"));
        QVERIFY2(near(sum.value(QStringLiteral("hover0")).toDouble(), 32.0, 0.01)
                     && near(sum.value(QStringLiteral("hoverK")).toDouble(), 6.0, 0.01)
                     && near(sum.value(QStringLiteral("cruise0")).toDouble(), 21.0, 0.01)
                     && near(sum.value(QStringLiteral("cruiseK")).toDouble(), 4.0, 0.01), qPrintable(got));
        QVERIFY2(near(sum.value(QStringLiteral("chargerFactor")).toDouble(), 1.2, 0.001), qPrintable(got));
        QVERIFY(!sum.value(QStringLiteral("validated")).toBool());   // 4 flights: not enough to validate
        QCOMPARE(sum.value(QStringLiteral("usedFlights")).toInt(), 4);
        QCOMPARE(sum.value(QStringLiteral("pendingFlights")).toInt(), 0);

        // Dismissing a flight takes it out of the model.
        QVERIFY(mgr()->setFlightLearningState(sessions[3], QStringLiteral("dismissed")));
        sum = mgr()->powerTypeSummary(type);
        QCOMPARE(sum.value(QStringLiteral("status")).toString(), QStringLiteral("payloadProportional"));
        QCOMPARE(sum.value(QStringLiteral("usedFlights")).toInt(), 3);

        const QVariantList rows = mgr()->powerTypeFlights(type);
        QCOMPARE(rows.size(), 4);
        QVERIFY(near(rows[0].toMap().value(QStringLiteral("predictedMah")).toDouble(), 5100.0, 1.0));
        QVERIFY(!rows[0].toMap().value(QStringLiteral("outlier")).toBool());
        QVERIFY(sum.value(QStringLiteral("fcCurrentFollowsThrottle")).toBool());   // 26.7 A hover vs 17.5 A cruise

        // A copter on ArduPilot's synthetic current: the same 0.3 A in hover
        // and cruise. It seeds no model, is flagged, and the used flights'
        // throttle per phase is kept as the reference for the GCS's count.
        const int synthType = mgr()->savePowerType(0, QStringLiteral("Quad X"), 13.0, 4.5, 3, {});
        QVERIFY(synthType > 0);
        for (double thr : {0.34, 0.38}) {
            const int id = db.startFlightSession(uid, QString(), 0.0);
            FlightPowerRecord r;
            r.powerTypeId = synthType;
            r.hoverMinEmpty = 2.0; r.cruiseMinEmpty = 2.0;
            r.synthHoverA = 0.3; r.synthCruiseA = 0.3; r.fcMah = 13.0;
            r.refThrottleHover = thr; r.refThrottleCruise = thr + 0.04;
            r.startedFull = true; r.telemetryCoverage = 1.0;
            QVERIFY(db.saveFlightPowerRecord(id, r));
            QVERIFY(db.setFlightChargerMah(id, 900.0));
            QVERIFY(db.setFlightLearningState(id, QStringLiteral("used")));
        }
        sum = mgr()->powerTypeSummary(synthType);
        QVERIFY(!sum.value(QStringLiteral("fcCurrentFollowsThrottle"), true).toBool());
        QVERIFY2(sum.value(QStringLiteral("source")).toString() != QLatin1String("synthetic"),
                 qPrintable(sum.value(QStringLiteral("source")).toString()));
        QVERIFY(near(sum.value(QStringLiteral("refThrottleHover")).toDouble(), 0.36, 1e-9));
        QVERIFY(near(sum.value(QStringLiteral("refThrottleCruise")).toDouble(), 0.40, 1e-9));

        db.reset();
    }

    // The tracker on its own: with no target the payload stays aboard for the
    // whole flight; ground time counts for coverage but not as hover; a phase
    // with no current reported averages to -1, not 0.
    void testPowerTypeTrackerWithoutTarget()
    {
        using namespace PowerTypeModel;
        FlightTracker t;
        t.start(std::nan(""), std::nan(""));
        TrackSample s;
        s.lat = -35.0; s.lon = 149.0;
        s.groundSpeedMs = 0.0; s.altitudeM = 0.2;
        for (int i = 0; i < 30; ++i) t.tick(1.0, s);          // on the ground
        s.altitudeM = 15.0; s.currentA = 30.0;
        for (int i = 0; i < 60; ++i) t.tick(1.0, s);          // hover
        s.groundSpeedMs = 12.0;  s.currentA = -1.0;
        for (int i = 0; i < 60; ++i) t.tick(1.0, s);          // cruise, no current
        QVERIFY(!t.payloadDropped());
        QCOMPARE(t.hoverMinLoaded(), 1.0);
        QCOMPARE(t.hoverMinEmpty(), 0.0);
        QCOMPARE(t.cruiseMinLoaded(), 1.0);
        QCOMPARE(t.hoverCurrentA(), 30.0);
        QCOMPARE(t.cruiseCurrentA(), -1.0);
        QCOMPARE(t.cruiseSpeedMs(), 12.0);
        QCOMPARE(t.coverage(), 1.0);
        // A fast-moving copter at low altitude is airborne (cruise).
        s.altitudeM = 0.5;
        t.tick(60.0, s);
        QCOMPARE(t.cruiseMinLoaded(), 2.0);
        QVERIFY(std::abs(distanceM(-35.0, 149.0, -35.0 + 500.0 / 111195.0, 149.0) - 500.0) < 1.0);
    }

    // A pack that is not full: its charge from the resting cell voltage on a
    // typical discharge curve, and the live estimate starting from it.
    void testPowerTypePartialPack()
    {
        using namespace PowerTypeModel;
        QCOMPARE(restingSocPct(4.20, QStringLiteral("LiPo")), 100.0);
        QCOMPARE(restingSocPct(3.84, QStringLiteral("LiPo")), 50.0);
        QCOMPARE(restingSocPct(3.95, QStringLiteral("LiPo")), 70.0);
        QVERIFY(near(restingSocPct(3.70, QStringLiteral("LiPo")), 12.5, 1e-9));   // between 3.69 (10) and 3.71 (15)
        QCOMPARE(restingSocPct(3.0, QStringLiteral("LiPo")), 0.0);
        QCOMPARE(restingSocPct(4.10, QStringLiteral("LiHV")), 70.0);              // LiHV: 0.15 V higher
        QCOMPARE(restingSocPct(3.70, QStringLiteral("LiIon")), 50.0);
        QCOMPARE(restingSocPct(1.2, QStringLiteral("LiPo")), -1.0);                // not a cell voltage
        QCOMPARE(fullCellVolts(QStringLiteral("LiHV")), 4.30);
        QCOMPARE(fullCellVolts(QStringLiteral("LiPo")), kFullCellV);

        // A 16 Ah pack at 70 %: 11.2 Ah, less the 3.2 Ah reserve = 8.0 Ah, then
        // less what was counted since the arm.
        LiveInput in;
        in.model = fit(sky03Flights());
        in.capacityAh = 16.0;
        in.fcMahUsed = 0.0;
        in.startSocPct = 70.0;
        QVERIFY(near(liveEstimate(in).usableAh, 8.0, 1e-9));
        in.fcMahUsed = 2000.0;
        QVERIFY(near(liveEstimate(in).usableAh, 6.0, 1e-9));
        in.startSocPct = 100.0;
        QVERIFY(near(liveEstimate(in).usableAh, 10.8, 1e-9));
    }

    // What the recorder decides each second and at arming.
    void testPowerTypeRecorderHelpers()
    {
        using namespace PowerTypeModel;
        QVERIFY(isHover(2.9));
        QVERIFY(!isHover(3.0));

        QVERIFY(cellsPlausible(22.2, 6));
        QVERIFY(!cellsPlausible(16.4, 6));   // looks like 4S on a 6S type
        QVERIFY(cellsPlausible(16.4, 4));

        QVERIFY(fullAtArm(24.9, 6));
        QVERIFY(!fullAtArm(24.8, 6));

        Flight f; f.chargerAh = 5.1;
        QVERIFY(usableForLearning(f, true, 0.95));
        QVERIFY(!usableForLearning(f, false, 0.95));
        QVERIFY(!usableForLearning(f, true, 0.89));
        f.chargerAh = -1.0;
        QVERIFY(!usableForLearning(f, true, 0.95));
    }

    // ── Motor table (MotorTable.h) ───────────────────────────────────────

    // The rows compiled into the app are the ones in the table's CSV, and the
    // CSV is the maker's 23 V table moved to 30.7 V: throttle × 23 ÷ 30.7,
    // the same thrust, power and speed.
    void testMotorTableMatchesItsCsv()
    {
        QDir src(QCoreApplication::applicationDirPath());
        const QString rel = QStringLiteral("custom/tools/motor_tables/");
        for (int i = 0; i < 5 && !QFileInfo::exists(src.filePath(rel)); ++i)
            src.cdUp();
        auto readCsv = [&](const QString &name) {
            QVector<QStringList> rows;
            QFile f(src.filePath(rel + name));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                return rows;
            bool header = true;
            while (!f.atEnd()) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;
                if (header) { header = false; continue; }
                rows.append(line.split(QLatin1Char(',')));
            }
            return rows;
        };
        const QVector<QStringList> derived = readCsv(QStringLiteral("M6215_HighProt_320KV_MSC22x7.8_8S_30.7V_derived.csv"));
        const QVector<QStringList> measured = readCsv(QStringLiteral("M6215_HighProt_320KV_MSC22x7.8_23V.csv"));
        const MotorTable::Table &t = MotorTable::m6215_320kv_22x78_8s();
        QCOMPARE(derived.size(), t.rows.size());
        QCOMPARE(measured.size(), t.rows.size());
        for (int i = 0; i < t.rows.size(); ++i) {
            const MotorTable::Row &r = t.rows.at(i);
            QCOMPARE(derived.at(i).at(0).toDouble(), r.throttlePct);
            QCOMPARE(derived.at(i).at(1).toDouble(), r.thrustG);
            QCOMPARE(derived.at(i).at(3).toDouble(), r.powerInW);
            QCOMPARE(derived.at(i).at(4).toDouble(), r.rpm);
            QVERIFY(qAbs(derived.at(i).at(2).toDouble() - r.powerInW / t.volts) < 0.051);
            QVERIFY(qAbs(measured.at(i).at(0).toDouble() * 23.0 / t.volts - r.throttlePct) < 0.051);
            QCOMPARE(measured.at(i).at(3).toDouble(), r.powerInW);
            QCOMPARE(measured.at(i).at(1).toDouble(), r.thrustG);
        }
        QCOMPARE(MotorTable::byId(t.id), &t);
        QVERIFY(!MotorTable::byId(QStringLiteral("nope")));
    }

    // The 12 kg quad from MOTOR_TABLE_PLAN.md: 3000 g per motor needs 37.1 %
    // throttle, 357 W, 11.6 A per motor at 30.7 V, 47.6 A for the battery; a
    // drained 28 V pack holds the same hover at a higher throttle and draws
    // the same power, so more amps.
    void testMotorTableWorkedExample()
    {
        using namespace MotorTable;
        const Table &t = m6215_320kv_22x78_8s();
        auto near = [](double a, double b, double tol) { return qAbs(a - b) <= tol; };

        QVERIFY(near(powerAt(t, 0.450).watts, 562.6, 1e-9));       // a row exactly
        QVERIFY(near(powerAt(t, 0.461).watts, (562.6 + 630.1) / 2, 1e-6));   // half way between rows

        const double hover = throttleForThrust(t, 3000.0);
        QVERIFY2(near(hover, 0.37086, 1e-4), qPrintable(QString::number(hover, 'f', 5)));
        const MotorCurrent one = motorCurrent(t, hover, 30.7);
        QVERIFY2(near(one.amps, 11.636, 0.005), qPrintable(QString::number(one.amps)));
        QVERIFY(!one.beyond);
        const BatteryCurrent all4 = batteryCurrent(t, {hover, hover, hover, hover}, 30.7);
        QVERIFY2(near(all4.amps, 47.54, 0.02), qPrintable(QString::number(all4.amps)));
        QCOMPARE(all4.motorsBeyond, 0);

        // 28 V: the same motor voltage needs throttle × 30.7 ÷ 28; the same
        // power over 28 V.
        const double hover28 = hover * 30.7 / 28.0;
        QVERIFY(near(motorCurrent(t, hover28, 28.0).amps, one.amps * 30.7 / 28.0, 1e-6));
        // An unknown pack voltage reads the table at its own voltage.
        QVERIFY(near(motorCurrent(t, hover, -1.0).amps, one.amps, 1e-9));
        QVERIFY(near(motorCurrent(t, hover, qQNaN()).amps, one.amps, 1e-9));

        // Uneven motors (wind) draw more than the same average throttle.
        const BatteryCurrent windy = batteryCurrent(t, {hover + 0.05, hover + 0.05, hover - 0.05, hover - 0.05}, 30.7);
        QVERIFY(windy.amps > all4.amps);

        // Usable 16 Ah of the 20 Ah pack at 47.5 A: about 20 minutes of hover.
        QVERIFY(near(16.0 / all4.amps * 60.0, 20.2, 0.1));
    }

    void testMotorTableOutsideItsRows()
    {
        using namespace MotorTable;
        const Table &t = m6215_320kv_22x78_8s();
        auto near = [](double a, double b, double tol) { return qAbs(a - b) <= tol; };

        // Below the first row: the first two rows' curve, down to 0 at 0 %.
        QCOMPARE(powerAt(t, 0.0).watts, 0.0);
        QCOMPARE(powerAt(t, -0.1).watts, 0.0);
        const double k = std::log(146.9 / 127.5) / std::log(26.2 / 24.7);
        QVERIFY(near(powerAt(t, 0.10).watts, 127.5 * std::pow(10.0 / 24.7, k), 1e-9));
        QVERIFY(powerAt(t, 0.10).watts < powerAt(t, 0.20).watts);
        QVERIFY(!powerAt(t, 0.10).beyond);

        // Above the last row: throttle³ from it, and marked.
        const PowerAt hi = powerAt(t, 0.80);
        QVERIFY(hi.beyond);
        QVERIFY(near(hi.watts, 1705.3 * std::pow(80.0 / 74.9, 3.0), 1e-9));
        QVERIFY(!powerAt(t, 0.749).beyond);
        const BatteryCurrent b = batteryCurrent(t, {0.80, 0.80, 0.40, 0.40}, 30.7);
        QCOMPARE(b.motorsBeyond, 2);

        // Thrust outside the table has no throttle.
        QCOMPARE(throttleForThrust(t, 1000.0), -1.0);
        QCOMPARE(throttleForThrust(t, 9000.0), -1.0);
        QVERIFY(near(throttleForThrust(t, 1392.0), 0.247, 1e-9));
    }

    void testMotorTableInputs()
    {
        using namespace MotorTable;
        auto near = [](double a, double b, double tol) { return qAbs(a - b) <= tol; };

        QVERIFY(near(pwmToThrottle(1371, 1000, 2000), 0.371, 1e-9));
        QVERIFY(near(pwmToThrottle(1500, 1100, 1900), 0.5, 1e-9));
        QCOMPARE(pwmToThrottle(900, 1000, 2000), 0.0);
        QCOMPARE(pwmToThrottle(2100, 1000, 2000), 1.0);
        QCOMPARE(pwmToThrottle(1500, 0, 2000), -1.0);      // MOT_PWM_MIN 0: unknown range
        QCOMPARE(pwmToThrottle(1500, 2000, 1000), -1.0);

        // Motors 1–4 on SERVO1–4, in any order; motor order, not channel order.
        QCOMPARE(motorChannels({33, 34, 35, 36}), QVector<int>({1, 2, 3, 4}));
        QCOMPARE(motorChannels({0, 35, 33, 0, 36, 34}), QVector<int>({3, 6, 2, 5}));
        QCOMPARE(motorChannels({33, 34, 0, 36}), QVector<int>({1, 2, 4}));   // motor 3 has no output
        QVERIFY(motorChannels({0, 0, 19, 70}).isEmpty());

        QCOMPARE(cellsFromVoltMax(33.6), 8);
        QCOMPARE(cellsFromVoltMax(25.2), 6);
        QCOMPARE(cellsFromVoltMax(0.0), 0);

        // ArduPilot's hover output for this drone: MOT_THST_HOVER 0.1297,
        // default spin and expo, 8S voltage compensation, 30.7 V resting:
        // curve 0.2525 ÷ (30.7 ÷ 33.6) = 0.2763 → 0.15 + 0.8 × 0.2763 = 0.371.
        HoverParams p;
        p.thstHover = 0.1297;
        p.battVoltMax = 33.6;
        p.battVoltMin = 26.4;
        QVERIFY2(near(hoverMotorThrottle(p, 30.7), 0.371, 0.001), qPrintable(QString::number(hoverMotorThrottle(p, 30.7))));
        // A lower pack voltage raises the output (until MOT_BAT_VOLT_MIN).
        QVERIFY(hoverMotorThrottle(p, 28.0) > hoverMotorThrottle(p, 30.7));
        QCOMPARE(hoverMotorThrottle(p, 20.0), hoverMotorThrottle(p, 26.4));
        QCOMPARE(hoverMotorThrottle(p, 40.0), hoverMotorThrottle(p, 33.6));
        // No voltage compensation (MOT_BAT_VOLT_MAX 0): the curve alone.
        HoverParams flat = p;
        flat.battVoltMax = 0.0;
        QVERIFY(near(hoverMotorThrottle(flat, 30.7), 0.15 + 0.8 * 0.2525, 0.001));
        // Linear thrust curve (expo 0).
        HoverParams lin = flat;
        lin.expo = 0.0;
        QVERIFY(near(hoverMotorThrottle(lin, 30.7), 0.15 + 0.8 * 0.1297, 1e-9));
        // MOT_BAT_VOLT_MIN below 0.6 × max is raised to it, as ArduPilot does.
        HoverParams lowMin = p;
        lowMin.battVoltMin = 10.0;
        QCOMPARE(hoverMotorThrottle(lowMin, 15.0), hoverMotorThrottle(lowMin, 0.6 * 33.6));
        HoverParams unknown;
        QCOMPARE(hoverMotorThrottle(unknown, 30.7), -1.0);
    }

    // Each motor's throttle from the 16 output signals: the ESC range ArduPilot
    // uses, outputs per SERVOn_FUNCTION (or outputs 1–N when not known), and
    // nothing when a motor's output is missing.
    void testMotorTableMotorThrottles()
    {
        using namespace MotorTable;
        auto near = [](double a, double b) { return qAbs(a - b) <= 1e-9; };

        QCOMPARE(escRange(1100, 1900).min, 1100.0);
        QCOMPARE(escRange(1100, 1900).max, 1900.0);
        QCOMPARE(escRange(0, 0).min, 1000.0);          // not set: ArduPilot's 1000–2000
        QCOMPARE(escRange(0, 0).max, 2000.0);
        QCOMPARE(escRange(1500, 1400).min, 1000.0);    // unusable: the defaults
        QCOMPARE(escRange(0.5, 2000).min, 1000.0);

        QVector<int> out(16, 0);
        out[0] = 1371; out[1] = 1371; out[2] = 1500; out[3] = 1000;
        const QVector<double> t = motorThrottles(out, {1, 2, 3, 4}, 4, escRange(0, 0));
        QCOMPARE(t.size(), 4);
        QVERIFY(near(t[0], 0.371) && near(t[1], 0.371) && near(t[2], 0.5) && near(t[3], 0.0));

        // Motor order follows the channel list, not the output order.
        QVector<int> mixed(16, 0);
        mixed[2] = 1200; mixed[5] = 1600; mixed[1] = 1300; mixed[4] = 1400;
        const QVector<double> m = motorThrottles(mixed, motorChannels({0, 35, 33, 0, 36, 34}), 4, escRange(0, 0));
        QCOMPARE(m.size(), 4);
        QVERIFY(near(m[0], 0.2) && near(m[1], 0.6) && near(m[2], 0.3) && near(m[3], 0.4));

        // Output functions unknown: motors on outputs 1–4.
        QCOMPARE(motorThrottles(out, {}, 4, escRange(0, 0)).size(), 4);
        // A motor's output not sent (0), or fewer motor outputs than the frame has.
        QVector<int> gap = out;
        gap[2] = 0;
        QVERIFY(motorThrottles(gap, {1, 2, 3, 4}, 4, escRange(0, 0)).isEmpty());
        QVERIFY(motorThrottles(out, {1, 2, 4}, 4, escRange(0, 0)).isEmpty());
        QVERIFY(motorThrottles(out, {}, 0, escRange(0, 0)).isEmpty());
        QVERIFY(motorThrottles({}, {1, 2, 3, 4}, 4, escRange(0, 0)).isEmpty());
        // The ESC range scales the signal.
        QVERIFY(near(motorThrottles(out, {3}, 1, escRange(1100, 1900)).at(0), 0.5));
    }
};

UT_REGISTER_TEST(BatteryEstimatorTest)

#include "BatteryEstimatorTest.moc"
