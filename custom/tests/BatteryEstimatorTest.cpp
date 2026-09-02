#include <QTest>
#include <QTemporaryFile>
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
        tmpFile.open();
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

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

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
        f.open();
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
        QCOMPARE(db.storedSchemaVersion(), 16);

        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }
};

UT_REGISTER_TEST(BatteryEstimatorTest)

#include "BatteryEstimatorTest.moc"
