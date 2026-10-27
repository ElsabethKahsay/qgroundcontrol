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

    // Validated = the last three flights each predicted within 10 %; a flight
    // more than 20 % off is flagged.
    void testPowerTypeValidationAndOutliers()
    {
        using namespace PowerTypeModel;
        QVector<Flight> flights = sky03Flights();
        const Model m = fit(flights);
        QVERIFY(isValidated(m, flights));
        QVERIFY(!isValidated(m, flights.mid(0, 2)));

        QVector<Flight> drift = flights;
        drift.last().chargerAh *= 1.15;   // last flight 15 % over the model
        QVERIFY(!isValidated(m, drift));
        QVERIFY(!isOutlier(m, drift.last()));

        drift.last().chargerAh = flights.last().chargerAh * 1.30;   // 23 % off the charger number
        QVERIFY(isOutlier(m, drift.last()));
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
        QVERIFY(sum.value(QStringLiteral("validated")).toBool());
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
};

UT_REGISTER_TEST(BatteryEstimatorTest)

#include "BatteryEstimatorTest.moc"
