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
#include "MissionWalk.h"
#include "MissionCost.h"
#include "MissionAssess.h"
#include "MissionEstimateCheck.h"
#include "WindFromLean.h"
#include "TerrainClearance.h"
#include "MissionRecord.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>

// Test helpers kept out of the app's headers: they are used only here.
namespace MissionWalk {

/// Reads a Mission Planner / QGroundControl .waypoints file ("QGC WPL 110",
/// then one tab-separated item per line). Empty with @p error set when it is
/// not one.
inline QVector<Item> parseWaypoints(const QString &text, QString *error = nullptr)
{
    QVector<Item> out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.isEmpty() || !lines.first().trimmed().startsWith(QStringLiteral("QGC WPL 110"))) {
        if (error)
            *error = QStringLiteral("Not a .waypoints file (no \"QGC WPL 110\" line)");
        return out;
    }
    for (int n = 1; n < lines.size(); ++n) {
        const QString line = lines[n].trimmed();
        if (line.isEmpty())
            continue;
        const QStringList f = line.split(QRegularExpression(QStringLiteral("\\s+")));
        bool ok = f.size() == 12;
        QVector<double> v;
        for (int k = 0; ok && k < 12; ++k) {
            v.append(f[k].toDouble(&ok));
        }
        if (!ok) {
            if (error)
                *error = QStringLiteral("Line %1 is not a mission item").arg(n + 1);
            return {};
        }
        Item it;
        it.seq = static_cast<int>(v[0]);
        it.frame = static_cast<int>(v[2]);
        it.command = static_cast<int>(v[3]);
        it.p1 = v[4];
        it.p2 = v[5];
        it.p3 = v[6];
        it.p4 = v[7];
        it.lat = v[8];
        it.lon = v[9];
        it.alt = v[10];
        it.autocontinue = v[11] != 0.0;
        out.append(it);
    }
    return out;
}

}   // namespace MissionWalk

namespace WindFromLean {

/// The attitude that gives a lean vector: the copter facing @p yaw,
/// leaning toward (hN, hE). Used to make test samples.
inline void attitudeFor(double hN, double hE, double yaw, double *roll, double *pitch)
{
    // Rotate the lean into the body frame (forward, right).
    const double f = hN * std::cos(yaw) + hE * std::sin(yaw);
    const double r = -hN * std::sin(yaw) + hE * std::cos(yaw);
    // The exact inverse of leanVector for this Euler order:
    // tan(pitch) = -f, tan(roll) = r·cos(pitch).
    *pitch = std::atan(-f);
    *roll = std::atan(r * std::cos(*pitch));
}

}   // namespace WindFromLean

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

    /// The worked example's model (SKY-03 in POWER_TYPES_PLAN.md): hover
    /// 32 A + 6 A/kg, cruise 21 A + 4 A/kg.
    static PowerTypeModel::Model sky03Model()
    {
        PowerTypeModel::Model m;
        m.hover0 = 32.0;
        m.hoverK = 6.0;
        m.cruise0 = 21.0;
        m.cruiseK = 4.0;
        m.status = PowerTypeModel::FitStatus::Full;
        return m;
    }

    static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

    // The 22-inch copter's settings, as in custom/tools/22Inches.params
    // (ArduPilot 4.7 names).
    static QHash<QString, double> params22()
    {
        return {{QStringLiteral("WP_SPD"), 10.0},          {QStringLiteral("WP_SPD_UP"), 2.5},
                {QStringLiteral("WP_SPD_DN"), 1.5},        {QStringLiteral("WP_ACC"), 2.5},
                {QStringLiteral("WP_ACC_Z"), 1.0},         {QStringLiteral("WP_JERK"), 1.0},
                {QStringLiteral("RTL_ALT_M"), 15.0},       {QStringLiteral("RTL_SPEED_MS"), 0.0},
                {QStringLiteral("RTL_LOIT_TIME"), 5000.0}, {QStringLiteral("RTL_ALT_FINAL_M"), 0.0},
                {QStringLiteral("LAND_SPD_MS"), 0.5},      {QStringLiteral("LAND_SPD_HIGH_MS"), 0.0},
                {QStringLiteral("LAND_ALT_LOW_M"), 10.0},  {QStringLiteral("CIRCLE_RATE"), 20.0}};
    }
    static MissionWalk::Lookup lookup(const QHash<QString, double> &p)
    {
        return [p](const QString &name, double *v) {
            if (!p.contains(name))
                return false;
            *v = p.value(name);
            return true;
        };
    }
    // custom/tools/mission_template.waypoints: take-off to 30 m, two
    // waypoints, return home.
    static QString templateMission()
    {
        return QStringLiteral(
            "QGC WPL 110\n"
            "0\t1\t0\t16\t0\t0\t0\t0\t37.7749000\t-122.4194000\t10.000000\t1\n"
            "1\t0\t3\t22\t15\t0\t0\t0\t37.7749000\t-122.4194000\t30.000000\t1\n"
            "2\t0\t3\t16\t0\t0\t0\t0\t37.7755000\t-122.4185000\t30.000000\t1\n"
            "3\t0\t3\t16\t0\t0\t0\t0\t37.7760000\t-122.4198000\t30.000000\t1\n"
            "4\t0\t3\t20\t0\t0\t0\t0\t0.0000000\t0.0000000\t0.000000\t1\n");
    }
    // The 22-inch copter for the costing: its table, 4 motors, a 6S Li-ion
    // 20 Ah pack, 8.09 kg without payload and 4 kg of payload, sea-level
    // table air, no wind, reserve 20 %, nothing measured.
    static MissionCost::Inputs inputs22()
    {
        MissionCost::Inputs in;
        in.table = &MotorTable::m6215_320kv_22x78_6s();
        in.motors = 4;
        in.pack.cells = 6;
        in.pack.chemistry = QStringLiteral("LiIon");
        in.pack.capacityAh = 20.0;
        in.pack.resistanceOhm = 6 * MotorTable::kDefaultCellGroupOhm;
        in.emptyKg = 8.09;
        in.payloadKg = 4.0;
        in.reserveSocPct = 20.0;
        return in;
    }
    static double hoverW(const MissionCost::Inputs &in, double kg)
    {
        return in.motors * MotorTable::powerForThrustAt(*in.table, kg * 1000.0 / in.motors, in.sigma).watts;
    }
    static MissionWalk::Item item(int seq, int command, double lat, double lon, double alt, double p1 = 0.0,
                                  double p2 = 0.0, int frame = MissionWalk::FrameGlobalRelative)
    {
        MissionWalk::Item it;
        it.seq = seq;
        it.command = command;
        it.lat = lat;
        it.lon = lon;
        it.alt = alt;
        it.p1 = p1;
        it.p2 = p2;
        it.frame = frame;
        return it;
    }

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
    // ── Battery maths (PowerTypeModel.h) ─────────────────────────────────
    // The worked example: 0.8 kg to a 5 km target, 2 min there, dropped,
    // back empty, on 12.8 Ah usable (16 Ah, 20 % reserve).
    void testPowerTypeMissionCheck()
    {
        using namespace PowerTypeModel;
        const Model m = sky03Model();
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

    // In flight: the model current scaled by the live current within ±30 %,
    // and RETURN NOW when the rest of the trip needs more than is left above
    // the reserve.
    void testPowerTypeInFlight()
    {
        using namespace PowerTypeModel;
        const Model m = sky03Model();

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
        in.model = sky03Model();
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

    // The throttle correction in place of the FC current, while flying: the
    // model current × (throttle now ÷ the phase's reference)^1.5, within
    // ±30 %; none on the ground.
    void testPowerTypeThrottleCorrection()
    {
        using namespace PowerTypeModel;
        const Model m = sky03Model();
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
        QCOMPARE(restingSocPct(3.73, QStringLiteral("LiIon")), 50.0);   // Li-ion used 4.3 → 2.9 V
        QVERIFY(qAbs(restingSocPct(3.70, QStringLiteral("LiIon")) - 46.25) < 1e-9);
        QCOMPARE(restingSocPct(1.2, QStringLiteral("LiPo")), -1.0);                // not a cell voltage
        QCOMPARE(fullCellVolts(QStringLiteral("LiHV")), 4.30);
        QCOMPARE(fullCellVolts(QStringLiteral("LiPo")), kFullCellV);

        // A 16 Ah pack at 70 %: 11.2 Ah, less the 3.2 Ah reserve = 8.0 Ah, then
        // less what was counted since the arm.
        LiveInput in;
        in.model = sky03Model();
        in.capacityAh = 16.0;
        in.fcMahUsed = 0.0;
        in.startSocPct = 70.0;
        QVERIFY(near(liveEstimate(in).usableAh, 8.0, 1e-9));
        in.fcMahUsed = 2000.0;
        QVERIFY(near(liveEstimate(in).usableAh, 6.0, 1e-9));
        in.startSocPct = 100.0;
        QVERIFY(near(liveEstimate(in).usableAh, 10.8, 1e-9));
    }

    // Hover is below 3 m/s ground speed; distances are great-circle metres.
    void testPowerTypePhaseHelpers()
    {
        using namespace PowerTypeModel;
        QVERIFY(isHover(2.9));
        QVERIFY(!isHover(3.0));
        QVERIFY(std::abs(distanceM(-35.0, 149.0, -35.0 + 500.0 / 111195.0, 149.0) - 500.0) < 1.0);
    }

    // ── Motor table (MotorTable.h) ───────────────────────────────────────

    // The rows compiled into the app are the ones in the maker's CSV.
    void testMotorTableMatchesItsCsv()
    {
        QDir src(QCoreApplication::applicationDirPath());
        const QString rel = QStringLiteral("custom/tools/motor_tables/");
        for (int i = 0; i < 5 && !QFileInfo::exists(src.filePath(rel)); ++i)
            src.cdUp();
        QVector<QStringList> rows;
        QFile f(src.filePath(rel + QStringLiteral("M6215_HighProt_320KV_MSC22x7.8_23V.csv")));
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        bool header = true;
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            if (header) { header = false; continue; }
            rows.append(line.split(QLatin1Char(',')));
        }
        const MotorTable::Table &t = MotorTable::m6215_320kv_22x78_6s();
        QCOMPARE(t.volts, 23.0);
        QCOMPARE(rows.size(), t.rows.size());
        for (int i = 0; i < t.rows.size(); ++i) {
            const MotorTable::Row &r = t.rows.at(i);
            QCOMPARE(rows.at(i).at(0).toDouble(), r.throttlePct);
            QCOMPARE(rows.at(i).at(1).toDouble(), r.thrustG);
            QCOMPARE(rows.at(i).at(3).toDouble(), r.powerInW);
            QCOMPARE(rows.at(i).at(4).toDouble(), r.rpm);
            // The maker's current column is power ÷ 23 V (to its 0.1 A rounding).
            QVERIFY(qAbs(rows.at(i).at(2).toDouble() - r.powerInW / t.volts) < 0.2);
        }
        QCOMPARE(MotorTable::byId(t.id), &t);
        QVERIFY(!MotorTable::byId(QStringLiteral("nope")));
    }

    // The 12 kg quad from MOTOR_TABLE_PLAN.md on 6S: 3000 g per motor needs
    // 49.5 % throttle, 357 W, 15.5 A per motor at 23 V, 63.1 A for the
    // battery; a drained 21 V pack holds the same hover at a higher throttle
    // and draws the same power, so more amps.
    void testMotorTableWorkedExample()
    {
        using namespace MotorTable;
        const Table &t = m6215_320kv_22x78_6s();
        auto near = [](double a, double b, double tol) { return qAbs(a - b) <= tol; };

        QVERIFY(near(powerAt(t, 0.60).watts, 562.6, 1e-9));       // a row exactly
        QVERIFY(near(powerAt(t, 0.615).watts, (562.6 + 630.1) / 2, 1e-6));   // half way between rows

        const double hover = throttleForThrust(t, 3000.0);
        QVERIFY2(near(hover, 0.49481, 1e-4), qPrintable(QString::number(hover, 'f', 5)));
        const MotorCurrent one = motorCurrent(t, hover, 23.0);
        QVERIFY2(near(one.amps, 15.531, 0.005), qPrintable(QString::number(one.amps)));
        QVERIFY(!one.beyond);
        const BatteryCurrent all4 = batteryCurrent(t, {hover, hover, hover, hover}, 23.0);
        QVERIFY2(near(all4.amps, 63.12, 0.02), qPrintable(QString::number(all4.amps)));
        QCOMPARE(all4.motorsBeyond, 0);

        // 21 V: the same motor voltage needs throttle × 23 ÷ 21; the same
        // power over 21 V.
        const double hover21 = hover * 23.0 / 21.0;
        QVERIFY(near(motorCurrent(t, hover21, 21.0).amps, one.amps * 23.0 / 21.0, 1e-6));
        // An unknown pack voltage reads the table at its own voltage.
        QVERIFY(near(motorCurrent(t, hover, -1.0).amps, one.amps, 1e-9));
        QVERIFY(near(motorCurrent(t, hover, qQNaN()).amps, one.amps, 1e-9));

        // Uneven motors (wind) draw more than the same average throttle.
        const BatteryCurrent windy = batteryCurrent(t, {hover + 0.05, hover + 0.05, hover - 0.05, hover - 0.05}, 23.0);
        QVERIFY(windy.amps > all4.amps);

        // Usable 16 Ah of the 20 Ah pack at 63.1 A: about 15 minutes of hover.
        QVERIFY(near(16.0 / all4.amps * 60.0, 15.2, 0.1));
    }

    void testMotorTableOutsideItsRows()
    {
        using namespace MotorTable;
        const Table &t = m6215_320kv_22x78_6s();
        auto near = [](double a, double b, double tol) { return qAbs(a - b) <= tol; };

        // Below the first row: the first two rows' curve, down to 0 at 0 %.
        QCOMPARE(powerAt(t, 0.0).watts, 0.0);
        QCOMPARE(powerAt(t, -0.1).watts, 0.0);
        const double k = std::log(146.9 / 127.5) / std::log(35.0 / 33.0);
        QVERIFY(near(powerAt(t, 0.10).watts, 127.5 * std::pow(10.0 / 33.0, k), 1e-9));
        QVERIFY(powerAt(t, 0.10).watts < powerAt(t, 0.20).watts);
        QVERIFY(!powerAt(t, 0.10).beyond);

        // Above the last row (a full 25.2 V pack at full throttle reads the
        // 23 V table at 110 %): throttle³ from it, and marked.
        const PowerAt hi = powerAt(t, 1.05);
        QVERIFY(hi.beyond);
        QVERIFY(near(hi.watts, 1705.3 * std::pow(105.0 / 100.0, 3.0), 1e-9));
        QVERIFY(!powerAt(t, 1.0).beyond);
        const BatteryCurrent b = batteryCurrent(t, {1.0, 1.0, 0.40, 0.40}, 25.2);
        QCOMPARE(b.motorsBeyond, 2);

        // Thrust outside the table has no throttle.
        QCOMPARE(throttleForThrust(t, 1000.0), -1.0);
        QCOMPARE(throttleForThrust(t, 9000.0), -1.0);
        QVERIFY(near(throttleForThrust(t, 1392.0), 0.33, 1e-9));
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

        QCOMPARE(cellsFromVoltMax(25.2), 6);
        QCOMPARE(cellsFromVoltMax(33.6), 8);
        QCOMPARE(cellsFromVoltMax(0.0), 0);

        // ArduPilot's hover output for this drone: MOT_THST_HOVER 0.2383,
        // default spin and expo, 6S voltage compensation, 23 V resting:
        // curve 0.3934 ÷ (23 ÷ 25.2) = 0.4310 → 0.15 + 0.8 × 0.4310 = 0.4948.
        HoverParams p;
        p.thstHover = 0.2383;
        p.battVoltMax = 25.2;
        p.battVoltMin = 19.8;
        QVERIFY2(near(hoverMotorThrottle(p, 23.0), 0.4948, 0.001), qPrintable(QString::number(hoverMotorThrottle(p, 23.0))));
        // A lower pack voltage raises the output (until MOT_BAT_VOLT_MIN).
        QVERIFY(hoverMotorThrottle(p, 21.0) > hoverMotorThrottle(p, 23.0));
        QCOMPARE(hoverMotorThrottle(p, 15.0), hoverMotorThrottle(p, 19.8));
        QCOMPARE(hoverMotorThrottle(p, 30.0), hoverMotorThrottle(p, 25.2));
        // No voltage compensation (MOT_BAT_VOLT_MAX 0): the curve alone.
        HoverParams flat = p;
        flat.battVoltMax = 0.0;
        QVERIFY(near(hoverMotorThrottle(flat, 23.0), 0.15 + 0.8 * 0.3934, 0.001));
        // Linear thrust curve (expo 0).
        HoverParams lin = flat;
        lin.expo = 0.0;
        QVERIFY(near(hoverMotorThrottle(lin, 23.0), 0.15 + 0.8 * 0.2383, 1e-9));
        // MOT_BAT_VOLT_MIN below 0.6 × max is raised to it, as ArduPilot does.
        HoverParams lowMin = p;
        lowMin.battVoltMin = 10.0;
        QCOMPARE(hoverMotorThrottle(lowMin, 12.0), hoverMotorThrottle(lowMin, 0.6 * 25.2));
        HoverParams unknown;
        QCOMPARE(hoverMotorThrottle(unknown, 23.0), -1.0);
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

    // The hover from thrust: thrust per motor turns into power (the thrust and
    // power columns, the same at any voltage), then current at the pack
    // voltage. The 12 kg quad: 3000 g per motor, 357 W, 63.1 A at 23 V,
    // 69.0 A at 21 V; the thrust read back from the motors' throttle; and
    // ArduPilot's MOT_THST_HOVER 0.2383 lands on the same hover.
    void testMotorTableHoverFromThrust()
    {
        using namespace MotorTable;
        const Table &t = m6215_320kv_22x78_6s();
        auto near = [](double a, double b, double tol) { return qAbs(a - b) <= tol; };

        QCOMPARE(thrustAt(t, 0.60).grams, 4147.0);
        QVERIFY(near(thrustAt(t, 0.615).grams, (4147.0 + 4494.0) / 2, 1e-6));
        QVERIFY(thrustAt(t, 1.05).beyond);
        QVERIFY(near(thrustAt(t, 1.05).grams, 8801.0 * std::pow(1.05, 2.0), 1e-9));
        QCOMPARE(thrustAt(t, 0.0).grams, 0.0);
        QVERIFY(thrustAt(t, 0.10).grams > 0.0 && thrustAt(t, 0.10).grams < 1392.0);

        QCOMPARE(powerForThrust(t, 4147.0).watts, 562.6);
        const double p3000 = powerForThrust(t, 3000.0).watts;
        QVERIFY2(near(p3000, 357.215, 0.01), qPrintable(QString::number(p3000)));
        QVERIFY(powerForThrust(t, 9500.0).beyond);
        QVERIFY(near(powerForThrust(t, 9500.0).watts, 1705.3 * std::pow(9500.0 / 8801.0, 1.5), 1e-9));
        QVERIFY(powerForThrust(t, 1000.0).watts > 0.0 && powerForThrust(t, 1000.0).watts < 127.5);

        QVERIFY(near(hoverCurrent(t, 3000.0, 4, 23.0), 63.12, 0.02));
        QVERIFY(near(hoverCurrent(t, 3000.0, 4, 21.0), 4 * p3000 / 21.0 + 1.0, 1e-9));
        QVERIFY(near(hoverCurrent(t, 3000.0, 4, -1.0), hoverCurrent(t, 3000.0, 4, 23.0), 1e-9));
        QCOMPARE(hoverCurrent(t, 3000.0, 0, 23.0), -1.0);
        QCOMPARE(hoverCurrent(t, -5.0, 4, 23.0), -1.0);
        QVERIFY(near(hoverThrottleAt(t, 3000.0, 23.0), 0.49481, 1e-4));
        QVERIFY(near(hoverThrottleAt(t, 3000.0, 21.0), 0.49481 * 23.0 / 21.0, 1e-4));
        QCOMPARE(hoverThrottleAt(t, 9000.0, 23.0), -1.0);

        // The motors' throttle at the pack voltage gives back the thrust.
        const double h = throttleForThrust(t, 3000.0);
        QVERIFY(near(averageThrust(t, {h, h, h, h}, 23.0), 3000.0, 0.5));
        const double h21 = h * 23.0 / 21.0;
        QVERIFY(near(averageThrust(t, {h21, h21, h21, h21}, 21.0), 3000.0, 0.5));
        QVERIFY(near(averageThrust(t, {h + 0.02, h - 0.02}, 23.0),
                     (thrustAt(t, h + 0.02).grams + thrustAt(t, h - 0.02).grams) / 2, 1e-9));
        QCOMPARE(averageThrust(t, {}, 23.0), -1.0);

        HoverParams p;
        p.thstHover = 0.2383;
        p.battVoltMax = 25.2;
        p.battVoltMin = 19.8;
        const double act = hoverMotorThrottle(p, 23.0);
        QVERIFY2(near(thrustAt(t, act).grams, 3000.0, 30.0), qPrintable(QString::number(thrustAt(t, act).grams)));
    }

    // The pack's voltage through a flight: one curve per chemistry read both
    // ways (charge from voltage, voltage from charge); Li-ion from 4.3 V full
    // to 2.9 V empty per cell.
    void testPackVoltageCurves()
    {
        using namespace PowerTypeModel;
        for (const char *chem : {"LiPo", "LiHV", "LiIon"}) {
            const QString c = QLatin1String(chem);
            for (double soc : {0.0, 5.0, 12.5, 37.0, 50.0, 81.0, 99.0, 100.0})
                QVERIFY2(qAbs(restingSocPct(restingCellVolts(soc, c), c) - soc) < 1e-6,
                         qPrintable(QStringLiteral("%1 at %2 %").arg(c).arg(soc)));
        }
        QCOMPARE(restingCellVolts(100.0, QStringLiteral("LiIon")), 4.30);
        QCOMPARE(restingCellVolts(0.0, QStringLiteral("LiIon")), 2.90);
        QCOMPARE(restingCellVolts(50.0, QStringLiteral("LiIon")), 3.73);
        QCOMPARE(restingCellVolts(100.0, QStringLiteral("LiPo")), 4.20);
        QVERIFY(qAbs(restingCellVolts(100.0, QStringLiteral("LiHV")) - 4.35) < 1e-9);
        QCOMPARE(restingCellVolts(150.0, QStringLiteral("LiIon")), 4.30);   // clamped
        QCOMPARE(restingCellVolts(-5.0, QStringLiteral("LiIon")), 2.90);
        QCOMPARE(fullCellVolts(QStringLiteral("LiIon")), 4.25);
    }

    // Time left for a power the motors take, through the rest of the pack:
    // the voltage falls as the pack empties (its curve, and the drop under
    // load), so the same power needs more current later. The 12 kg quad
    // (4 × 357.2 W) on a 6S Li-ion 20 Ah pack, 25 mΩ: the current rises
    // through the flight, and costing it all at the starting current would
    // overstate the time to the 20 % reserve.
    void testPackTimeToReserve()
    {
        using namespace MotorTable;
        auto near = [](double a, double b, double tol) { return qAbs(a - b) <= tol; };

        // Current for a power: (I − 1) × (V − I × R) = W.
        QVERIFY(near(packCurrent(1428.9, 23.0, 0.0), 1428.9 / 23.0 + 1.0, 1e-9));
        const double i = packCurrent(1428.9, 25.8, 0.025);
        QVERIFY(near((i - kElectronicsA) * (25.8 - i * 0.025), 1428.9, 1e-6));
        QVERIFY(i > 1428.9 / 25.8 + 1.0);                 // the drop under load costs current
        QCOMPARE(packCurrent(1e6, 25.8, 0.025), -1.0);    // voltage would collapse
        QCOMPARE(packCurrent(100.0, 0.0, 0.0), -1.0);

        Pack p;
        p.cells = 6;
        p.chemistry = QStringLiteral("LiIon");
        p.capacityAh = 20.0;
        p.resistanceOhm = 0.025;
        const double w = 4 * 357.2;
        QVERIFY(near(loadedVolts(p, 50.0, w), 6 * 3.73 - packCurrent(w, 6 * 3.73, 0.025) * 0.025, 1e-9));

        // The sum against a much finer one done here.
        double fine = 0.0;
        for (double soc = 100.0; soc > 20.0 + 1e-9; soc -= 0.01) {
            const double rest = 6 * PowerTypeModel::restingCellVolts(soc - 0.005, p.chemistry);
            fine += 20.0 * 0.0001 / packCurrent(w, rest, 0.025);
        }
        const double h = hoursBetween(p, w, 100.0, 20.0);
        QVERIFY2(near(h, fine, fine * 0.0001), qPrintable(QStringLiteral("%1 h vs %2 h").arg(h, 0, 'f', 7).arg(fine, 0, 'f', 7)));
        QCOMPARE(hoursBetween(p, w, 20.0, 30.0), 0.0);
        QCOMPARE(hoursBetween(p, 0.0, 100.0, 20.0), -1.0);
        Pack none;
        QCOMPARE(hoursBetween(none, w, 100.0, 20.0), -1.0);

        // Average current: between the current at the start and at the reserve.
        const double avg = averageCurrent(p, w, 100.0, 20.0);
        const double atStart = packCurrent(w, 6 * 4.30, 0.025);
        const double atReserve = packCurrent(w, 6 * PowerTypeModel::restingCellVolts(20.0, p.chemistry), 0.025);
        QVERIFY(near(avg, 16.0 / h, 1e-9));
        QVERIFY(avg > atStart && avg < atReserve);
        // Costing the flight at the starting current would overstate the time
        // to the reserve by more than 10 % on this pack.
        const double naiveH = 16.0 / atStart;
        QVERIFY2(naiveH > h * 1.10, qPrintable(QStringLiteral("naive %1 h, real %2 h").arg(naiveH).arg(h)));
        // Less charge left, less time, and part-way the average is higher.
        QVERIFY(hoursBetween(p, w, 60.0, 20.0) < h);
        QVERIFY(averageCurrent(p, w, 60.0, 20.0) > avg);

        // The pack's resistance read back from the voltage under load.
        const double rest50 = 6 * PowerTypeModel::restingCellVolts(50.0, p.chemistry);
        QVERIFY(near(observedResistance(p, 50.0, rest50 - 70.0 * 0.03, 70.0), 0.03, 1e-9));
        QCOMPARE(observedResistance(p, 50.0, rest50 - 1.0, 2.0), -1.0);    // too little current to tell
        QCOMPARE(observedResistance(p, 50.0, rest50 + 1.0, 70.0), -1.0);   // above resting: not plausible
    }

    // Air density: σ from pressure, temperature and humidity; the standard
    // atmosphere; the motor model fitted to the table; and the corrections,
    // which leave the table exact at its own density. At 4000 m (σ 0.669) the
    // 12 kg quad needs about 24 % more power to hover; in flight the reading
    // from throttle and voltage moves by only about 1 % (the motor spins
    // faster in thin air at the same throttle); and the thrust the GCS reads
    // back from the motors is the copter's real weight.
    void testMotorTableAirDensity()
    {
        using namespace MotorTable;
        const Table &t = m6215_320kv_22x78_6s();
        auto near = [](double a, double b, double tol) { return qAbs(a - b) <= tol; };

        QVERIFY(near(densityRatio(1013.25, 15.0), 1.0, 0.001));
        QVERIFY(near(densityRatio(isaPressureHpa(2900.0), isaTempC(2900.0)), 0.750, 0.002));
        QVERIFY(near(densityRatio(isaPressureHpa(4000.0), isaTempC(4000.0)), 0.669, 0.002));
        QVERIFY(near(densityRatio(isaPressureHpa(2000.0), isaTempC(2000.0)), 0.822, 0.002));
        QVERIFY(densityRatio(900.0, 25.0, 100.0) < densityRatio(900.0, 25.0, 0.0));   // humid air is lighter
        QVERIFY(densityRatio(900.0, 30.0) < densityRatio(900.0, 10.0));               // warm air is lighter
        QVERIFY(near(densityRatio(900.0, 20.0, qQNaN()), densityRatio(900.0, 20.0, 0.0), 1e-12));
        QCOMPARE(densityRatio(-1.0, 15.0), -1.0);
        QVERIFY(near(isaAltitudeM(isaPressureHpa(3500.0)), 3500.0, 0.5));

        // The losses fit matches the table's rows within 4 W. The motor voltage
        // is the table's own in its own air; in thin air the winding needs
        // R ÷ Kt (0.065 Ω, 320 KV: 2.178 V per Nm) × (1 − σ) × torque less.
        const MotorModel &m = modelOf(t);
        QVERIFY(m.valid);
        for (const Row &r : t.rows) {
            QVERIFY2(qAbs(m.losses(r.torqueNm, r.rpm) - (r.powerInW - r.powerOutW)) < 4.0, qPrintable(QString::number(r.rpm)));
            QVERIFY(near(modelVoltsAt(t, r.rpm, 1.0), t.volts * r.throttlePct / 100.0, 1e-9));
            QVERIFY(near(modelVoltsAt(t, r.rpm, 0.75), t.volts * r.throttlePct / 100.0 - 2.178 * 0.25 * r.torqueNm, 0.002));
        }

        // At the table's density nothing changes.
        QCOMPARE(densityPowerRatio(t, 11.4, kTableDensityRatio), 1.0);
        QCOMPARE(densityThrustRatio(t, 11.4, kTableDensityRatio), 1.0);
        QCOMPARE(powerForThrustAt(t, 3000.0, 1.0).watts, powerForThrust(t, 3000.0).watts);
        QCOMPARE(motorCurrent(t, 0.495, 23.0, 1.0).amps, motorCurrent(t, 0.495, 23.0).amps);
        QCOMPARE(hoverThrottleAtDensity(t, 3000.0, 23.0, 1.0), hoverThrottleAt(t, 3000.0, 23.0));

        // Hover at altitude: more power, more so the thinner the air.
        const double sea = powerForThrust(t, 3000.0).watts;
        const double s2900 = densityRatio(isaPressureHpa(2900.0), isaTempC(2900.0));
        const double s4000 = densityRatio(isaPressureHpa(4000.0), isaTempC(4000.0));
        const double p2900 = powerForThrustAt(t, 3000.0, s2900).watts;
        const double p4000 = powerForThrustAt(t, 3000.0, s4000).watts;
        QVERIFY2(p4000 / sea > 1.21 && p4000 / sea < 1.26, qPrintable(QStringLiteral("%1 W vs %2 W").arg(p4000).arg(sea)));
        QVERIFY(p2900 > sea && p2900 < p4000);
        QVERIFY(near(thrustForPowerAt(t, p4000, s4000), 3000.0, 1.0));
        QVERIFY(near(thrustForPowerAt(t, sea, 1.0), 3000.0, 1.0));
        QCOMPARE(thrustForPowerAt(t, 0.0, 1.0), -1.0);

        // At half throttle at 2900 m the motor speeds up by only a few percent.
        const double speedUp = modelRpmForVoltage(t, 11.5, s2900) / modelRpmForVoltage(t, 11.5, 1.0);
        QVERIFY2(speedUp > 1.03 && speedUp < 1.04, qPrintable(QString::number(speedUp)));

        // The throttle to hover rises in thin air: 12 kg at 2900 m is the
        // README's worked example (56.2 %).
        QVERIFY2(near(hoverThrottleAtDensity(t, 3000.0, 23.0, s2900), 0.562, 0.002),
                 qPrintable(QString::number(hoverThrottleAtDensity(t, 3000.0, 23.0, s2900))));
        const double thr4000 = hoverThrottleAtDensity(t, 3000.0, 23.0, s4000);
        QVERIFY2(thr4000 > hoverThrottleAt(t, 3000.0, 23.0) && near(thr4000, 0.592, 0.003), qPrintable(QString::number(thr4000)));

        // In flight in thin air the table lists more power than the motor
        // takes at that throttle, because the lighter prop draws less; and
        // from the hover throttle there, the GCS reads back the real thrust
        // and the power needed for it.
        const double ratio = densityPowerRatio(t, 23.0 * thr4000, s4000);
        QVERIFY2(ratio > 0.79 && ratio < 0.83, qPrintable(QString::number(ratio)));
        for (double thrustG : {2250.0, 3000.0}) {
            for (double sigma : {s2900, s4000}) {
                const double thr = hoverThrottleAtDensity(t, thrustG, 23.0, sigma);
                const double real = powerForThrustAt(t, thrustG, sigma).watts;
                const double read = motorCurrent(t, thr, 23.0, sigma).amps * 23.0;
                const double back = averageThrust(t, {thr, thr, thr, thr}, 23.0, sigma);
                QVERIFY2(near(back, thrustG, thrustG * 0.005), qPrintable(QStringLiteral("%1 g vs %2 g").arg(back).arg(thrustG)));
                QVERIFY2(near(read, real, real * 0.005), qPrintable(QStringLiteral("%1 W vs %2 W").arg(read).arg(real)));
            }
        }
        // Without the correction the sea-level table reads that throttle as a
        // heavier copter.
        QVERIFY(averageThrust(t, {thr4000, thr4000, thr4000, thr4000}, 23.0) > 3300.0);
    }

    // Cruise is measured at whatever level speed is flown, kept per 2 m/s
    // band centred on even speeds; there is no fixed cruise speed.
    void testMotorTableCruiseSample()
    {
        using namespace MotorTable;
        QVERIFY(isCruiseSample(10.0, 0.0));
        QVERIFY(isCruiseSample(19.4, 0.3));
        QVERIFY(isCruiseSample(kMinCruiseSpeedMs, 0.0));
        QVERIFY(!isCruiseSample(2.9, 0.0));          // hover-like
        QVERIFY(!isCruiseSample(10.0, 0.6));         // climbing
        QVERIFY(!isCruiseSample(10.0, -0.6));        // sinking
        QVERIFY(!isCruiseSample(qQNaN(), 0.0));
        // A copter holding 10 m/s stays in one band either side of it.
        QCOMPARE(cruiseBand(9.1), 5);
        QCOMPARE(cruiseBand(10.0), 5);
        QCOMPARE(cruiseBand(10.9), 5);
        QCOMPARE(cruiseBand(11.1), 6);
        QCOMPARE(cruiseBand(19.4), 10);
        QCOMPARE(cruiseBand(0.0), -1);
        QCOMPARE(cruiseBand(qQNaN()), -1);
    }

    // ── Waypoint times (MissionWalk) ─────────────────────────────────────

    // The copter's settings are read under the ArduPilot 4.7 names, or the
    // older names in cm and cm/s converted; what is missing is named.
    void testMissionSettingsRead()
    {
        using namespace MissionWalk;
        const Settings s = Settings::read(lookup(params22()));
        QVERIFY2(s.complete(), qPrintable(s.missing.join(QLatin1Char(','))));
        QCOMPARE(s.speedMs, 10.0);
        QCOMPARE(s.rtlAltM, 15.0);
        QCOMPARE(s.rtlLoiterS, 5.0);
        QCOMPARE(s.landAltLowM, 10.0);
        QCOMPARE(s.source.value(QStringLiteral("speed")), QStringLiteral("WP_SPD"));
        QCOMPARE(missionSpeedMs(lookup(params22())), 10.0);

        // Older firmware (the mock copter's Copter 3.6 file).
        const QHash<QString, double> old = {
            {QStringLiteral("WPNAV_SPEED"), 500.0},  {QStringLiteral("WPNAV_SPEED_UP"), 250.0},
            {QStringLiteral("WPNAV_SPEED_DN"), 150.0}, {QStringLiteral("WPNAV_ACCEL"), 100.0},
            {QStringLiteral("RTL_ALT"), 1500.0},     {QStringLiteral("RTL_SPEED"), 0.0},
            {QStringLiteral("RTL_LOIT_TIME"), 5000.0}, {QStringLiteral("LAND_SPEED"), 50.0},
            {QStringLiteral("LAND_SPEED_HIGH"), 0.0}, {QStringLiteral("LAND_ALT_LOW"), 1000.0}};
        const Settings o = Settings::read(lookup(old));
        QVERIFY2(o.complete(), qPrintable(o.missing.join(QLatin1Char(','))));
        QVERIFY(near(o.speedMs, 5.0, 1e-9));
        QVERIFY(near(o.accelMss, 1.0, 1e-9));
        QVERIFY(near(o.rtlAltM, 15.0, 1e-9));
        QVERIFY(near(o.landSpeedMs, 0.5, 1e-9));
        QVERIFY(near(o.landAltLowM, 10.0, 1e-9));
        QCOMPARE(o.accelZMss, -1.0);   // optional, not in the file
        QCOMPARE(o.source.value(QStringLiteral("speed")), QStringLiteral("WPNAV_SPEED"));
        QVERIFY(near(missionSpeedMs(lookup(old)), 5.0, 1e-9));

        // Nothing read: every needed setting is named, no speed is assumed.
        const Settings none = Settings::read(lookup({}));
        QVERIFY(!none.complete());
        QVERIFY(none.missing.contains(QStringLiteral("WP_SPD")));
        QVERIFY(none.missing.contains(QStringLiteral("LAND_ALT_LOW_M")));
        QCOMPARE(missionSpeedMs(lookup({})), -1.0);
        QHash<QString, double> zero = params22();
        zero[QStringLiteral("WP_SPD")] = 0.0;
        QVERIFY(Settings::read(lookup(zero)).missing.contains(QStringLiteral("WP_SPD")));
        QCOMPARE(missionSpeedMs(lookup(zero)), -1.0);
    }

    // Moves: an S-curve speed-up (jerk to the acceleration, then the
    // acceleration) costs half its time per stop or start.
    void testMissionMoveTime()
    {
        using namespace MissionWalk;
        // 10 m/s at 2.5 m/s² reached at 1 m/s³: 10/2.5 + 2.5/1 = 6.5 s.
        QVERIFY(near(accelTimeS(10.0, 2.5, 1.0), 6.5, 1e-9));
        // Below a²/j (6.25 m/s) the acceleration never reaches its limit.
        QVERIFY(near(accelTimeS(5.0, 2.5, 1.0), 2.0 * std::sqrt(5.0), 1e-9));
        QVERIFY(near(accelTimeS(10.0, 2.5, 0.0), 4.0, 1e-9));
        // 200 m stop to stop: 20 s at speed + 6.5 s; flown through: 20 s.
        QVERIFY(near(moveTimeS(200.0, 10.0, 2.5, 1.0, true, true), 26.5, 1e-9));
        QVERIFY(near(moveTimeS(200.0, 10.0, 2.5, 1.0, true, false), 23.25, 1e-9));
        QVERIFY(near(moveTimeS(200.0, 10.0, 2.5, 1.0, false, false), 20.0, 1e-9));
        // Too short to reach 10 m/s (65 m needed stop to stop): it peaks
        // lower, and takes longer than at 10 m/s but less than at 5 m/s.
        const double shortT = moveTimeS(40.0, 10.0, 2.5, 1.0, true, true);
        QVERIFY(shortT > 40.0 / 10.0 + 6.5 * 0.5);
        QVERIFY(shortT < moveTimeS(40.0, 5.0, 2.5, 1.0, true, true));
        QVERIFY(near(moveTimeS(0.0, 10.0, 2.5, 1.0, true, true), 0.0, 1e-12));
    }

    // The template mission with the 22-inch copter's settings, worked out by
    // hand: climb 30 m at 2.5 m/s (speed-up 2.5 s at WP_ACC_Z 1 m/s²), fly
    // through waypoint 2, stop at waypoint 3 (a return follows), return at
    // 30 m (above RTL_ALT_M, so no climb), hold 5 s, land 20 m at WP_SPD_DN
    // then 10 m at LAND_SPD_MS.
    // The operator's mission A (custom/tools/motor_tables/A.waypoints): take-off
    // to 100 m, two waypoints, then LOITER_TO_ALT (31) and no return. Copter
    // flies to item 4 and stops there; the mission then ends in the air, and
    // the RTL the pilot switches to is added and marked as assumed.
    void testMissionWalkLoiterToAlt()
    {
        using namespace MissionWalk;
        const QString a = QStringLiteral(
            "QGC WPL 110\r\n"
            "0\t1\t0\t16\t0\t0\t0\t0\t9.0481706\t38.8186616\t2632.240000\t1\r\n"
            "1\t0\t3\t22\t0\t0\t0\t0\t0\t0\t100.000000\t1\r\n"
            "2\t0\t3\t16\t0\t0\t0\t0\t9.04787650\t38.81798300\t100.000000\t1\r\n"
            "3\t0\t3\t16\t0\t0\t0\t0\t9.04717460\t38.81813590\t100.000000\t1\r\n"
            "4\t0\t3\t31\t0\t0\t0\t0\t9.04743150\t38.81884660\t100.000000\t1\r\n");
        QString err;
        QVector<Item> items = parseWaypoints(a, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        const Settings s = Settings::read(lookup(params22()));
        const Result r = walk(items, s, items[0].lat, items[0].lon);
        QVERIFY2(r.ok, qPrintable(r.problem));
        QVERIFY(r.endsLanded);
        QVERIFY(r.returnAssumed);
        // Every step records where it ends: the take-off's climb at 100 m,
        // the landing on the ground at home (the terrain check reads them).
        QCOMPARE(r.steps.first().kind, StepKind::Climb);
        QCOMPARE(r.steps.first().altM, 100.0);
        QCOMPARE(r.steps.last().kind, StepKind::Land);
        QCOMPARE(r.steps.last().altM, 0.0);
        QCOMPARE(r.steps.last().climbM, -100.0);

        const double d1 = PowerTypeModel::distanceM(items[0].lat, items[0].lon, items[2].lat, items[2].lon);
        const double d2 = PowerTypeModel::distanceM(items[2].lat, items[2].lon, items[3].lat, items[3].lon);
        const double d3 = PowerTypeModel::distanceM(items[3].lat, items[3].lon, items[4].lat, items[4].lon);
        const double climb = 100.0 / 2.5 + 2.5;
        const double leg1 = d1 / 10.0 + 6.5 / 2.0;    // flies through waypoint 2
        const double leg2 = d2 / 10.0 + 6.5 / 2.0;    // stops at 3: item 4 is not a waypoint
        const double leg3 = d3 / 10.0 + 6.5;          // stop to stop
        QCOMPARE(r.arrivals.size(), 5);
        QCOMPARE(r.arrivals[3].command, int(NavLoiterToAlt));
        QVERIFY(near(r.arrivals[2].timeS, climb + leg1 + leg2, 1e-6));
        QVERIFY(near(r.arrivals[3].timeS, climb + leg1 + leg2 + leg3, 1e-6));
        QVERIFY(near(r.missionEndS, r.arrivals[3].timeS, 1e-9));
        QVERIFY(qAbs(r.missionEndS - 79.9) < 0.1);

        // The assumed return: from 100 m (above RTL_ALT_M) straight home at
        // WP_SPD, RTL_LOIT_TIME 5 s, then land 90 m at WP_SPD_DN and 10 m at
        // LAND_SPD_MS. Its steps and arrival carry seq -1.
        const double dHome = PowerTypeModel::distanceM(items[4].lat, items[4].lon, items[0].lat, items[0].lon);
        const double back = dHome / 10.0 + 6.5 + 5.0 + 90.0 / 1.5 + 10.0 / 0.5;
        QCOMPARE(r.arrivals[4].seq, -1);
        QCOMPARE(r.arrivals[4].command, int(NavReturnToLaunch));
        QVERIFY(near(r.totalS, r.missionEndS + back, 1e-6));
        QVERIFY(near(r.arrivals[4].timeS, r.totalS, 1e-9));
        QCOMPARE(r.steps.last().seq, -1);
        QCOMPARE(r.steps.last().kind, StepKind::Land);
        QVERIFY(qAbs(r.totalS - 180.0) < 1.0);        // about 3 minutes

        // Asked for the mission alone: it ends hovering at item 4.
        Start alone;
        alone.assumeReturn = false;
        const Result ra = walk(items, s, items[0].lat, items[0].lon, alone);
        QVERIFY(ra.ok && !ra.endsLanded && !ra.returnAssumed);
        QVERIFY(near(ra.totalS, r.missionEndS, 1e-9));
        QCOMPARE(ra.arrivals.size(), 4);

        // On the spot, down to 40 m: the height change at WP_SPD_DN.
        items[4].lat = items[3].lat;
        items[4].lon = items[3].lon;
        items[4].alt = 40.0;
        const Result down = walk(items, s, items[0].lat, items[0].lon);
        QVERIFY2(down.ok, qPrintable(down.problem));
        QVERIFY(near(down.missionEndS - down.arrivals[2].timeS, heightTimeS(-60.0, s), 1e-6));
        QVERIFY(near(down.arrivals[3].altM, 40.0, 1e-9));
    }

    // The pack stepped through for a time gives back the charge the time
    // from hoursBetween() says; a headwind adds to the airspeed.
    void testMissionCostPackAndAirspeed()
    {
        const MissionCost::Inputs in = inputs22();
        const double w = 1700.0;
        const double h = MotorTable::hoursBetween(in.pack, w, 100.0, 50.0);
        QVERIFY(h > 0.0);
        bool empty = false;
        QVERIFY(near(MissionCost::socAfter(in.pack, w, 100.0, h * 3600.0, 1.0, &empty), 50.0, 1e-6));
        QVERIFY(!empty);
        // The fine-tuning factor scales the current: 10 % more current, the same
        // charge in 1/1.1 of the time.
        QVERIFY(near(MissionCost::socAfter(in.pack, w, 100.0, h * 3600.0 / 1.1, 1.1, &empty), 50.0, 1e-6));
        // Far past the end: empty.
        QCOMPARE(MissionCost::socAfter(in.pack, w, 100.0, 10 * 3600.0, 1.0, &empty), 0.0);
        QVERIFY(empty);

        QVERIFY(near(MissionCost::airspeedMs(10, 0, 5, 0), 15.0, 1e-9));     // flying north, wind from north
        QVERIFY(near(MissionCost::airspeedMs(10, 0, 5, 180), 5.0, 1e-9));    // wind from behind
        QVERIFY(near(MissionCost::airspeedMs(10, 0, 5, 90), std::sqrt(125.0), 1e-9));
        QVERIFY(near(MissionCost::airspeedMs(10, 0, 5, qQNaN()), 15.0, 1e-9));   // direction unknown: headwind
        QVERIFY(near(MissionCost::airspeedMs(10, 0, qQNaN(), 0), 10.0, 1e-9));
        QVERIFY(near(MissionCost::bearingDeg(0, 0, 1, 0), 0.0, 1e-9));
        QVERIFY(near(MissionCost::bearingDeg(0, 0, 0, 1), 90.0, 1e-9));
    }

    // Each step's power from the table at the weight aboard; cruise ratios;
    // the climb's extra; the charge on arrival and the return from each point.
    void testMissionCostSteps()
    {
        using namespace MissionWalk;
        const QVector<Item> items = parseWaypoints(templateMission());
        const Settings s = Settings::read(lookup(params22()));
        const double hLat = items[0].lat, hLon = items[0].lon;
        const Result w = walk(items, s, hLat, hLon);
        QVERIFY(w.ok);
        MissionCost::Inputs in = inputs22();
        const MissionCost::Result r = MissionCost::cost(w, s, hLat, hLon, in);
        QVERIFY2(r.ok, qPrintable(r.problem));
        QCOMPARE(r.likely.size(), w.steps.size());
        const double full = 12.09;
        const double hover = hoverW(in, full);

        // Climb: hover + m g dz / t / 0.5. Moves: hover × 1.0 likely, × 1.4
        // pessimistic (nothing measured). Hold and landing: hover.
        QCOMPARE(w.steps[0].kind, StepKind::Climb);
        QVERIFY(near(r.likely[0].motorW, hover + full * MissionCost::kG * 30.0 / w.steps[0].timeS / 0.5, 1e-6));
        QCOMPARE(w.steps[1].kind, StepKind::Move);
        QVERIFY(near(r.likely[1].motorW, hover, 1e-6));
        QVERIFY(near(r.pessimistic[1].motorW, 1.4 * hover, 1e-6));
        QCOMPARE(w.steps.last().kind, StepKind::Land);
        QVERIFY(near(r.pessimistic.last().motorW, hover, 1e-6));
        QVERIFY(!r.ratioMeasured);
        QVERIFY(r.reasons.join(QString()).contains(QStringLiteral("not measured")));
        QVERIFY(r.landPessimisticPct < r.landLikelyPct);
        QVERIFY(r.landLikelyPct < 100.0 && r.landPessimisticPct > 20.0);

        // Charge on arrival is the end of the step that reaches the point.
        QCOMPARE(r.arrivals.size(), w.arrivals.size());
        QVERIFY(near(r.arrivals[1].socLikelyPct, r.likely[1].socEndPct, 1e-9));
        // The return from waypoint 3 is the mission's own return.
        QVERIFY(near(r.arrivals[2].returnLandLikelyPct, r.landLikelyPct, 1e-6));
        QVERIFY(near(r.arrivals[2].returnLandPessimisticPct, r.landPessimisticPct, 1e-6));
        QVERIFY(r.arrivals[2].canReturn);
        QCOMPARE(r.turnBackSeq, -2);
        QVERIFY(!r.reasons.join(QString()).contains(QStringLiteral("turn-back")));

        // A measured ratio is used in both cases.
        in.measuredRatio = [](double air) { return air >= 3.0 ? 1.2 : -1.0; };
        const MissionCost::Result rm = MissionCost::cost(w, s, hLat, hLon, in);
        QVERIFY(near(rm.likely[1].motorW, 1.2 * hover, 1e-6));
        QVERIFY(near(rm.pessimistic[1].motorW, 1.2 * hover, 1e-6));
        QVERIFY(rm.ratioMeasured);
        QVERIFY(!rm.reasons.join(QString()).contains(QStringLiteral("not measured")));

        // No weight: not costed.
        MissionCost::Inputs noWeight = inputs22();
        noWeight.emptyKg = qQNaN();
        QVERIFY(MissionCost::cost(w, s, hLat, hLon, noWeight).problem.contains(QStringLiteral("Weight missing")));
    }

    // Payload comes off at its drop time: the steps after it are lighter. Two
    // 2 kg drops; the "payload comes back" case lands lower.
    void testMissionCostDrops()
    {
        using namespace MissionWalk;
        const QVector<Item> items = parseWaypoints(templateMission());
        const Settings s = Settings::read(lookup(params22()));
        const double hLat = items[0].lat, hLon = items[0].lon;
        const Result w = walk(items, s, hLat, hLon);
        MissionCost::Inputs in = inputs22();
        in.drops = {{w.arrivals[1].timeS, 2.0}, {w.arrivals[2].timeS, 2.0}};
        const MissionCost::Result r = MissionCost::cost(w, s, hLat, hLon, in);
        QVERIFY(r.ok);
        QVERIFY(near(r.likely[1].motorW, hoverW(in, 12.09), 1e-6));    // to waypoint 2: all aboard
        QVERIFY(near(r.likely[2].motorW, hoverW(in, 10.09), 1e-6));    // to 3: one dropped
        QVERIFY(near(r.likely.last().motorW, hoverW(in, 8.09), 1e-6)); // landing: both dropped
        QVERIFY(near(r.arrivals[1].weightKg, 10.09, 1e-9));
        QVERIFY(near(r.arrivals[2].weightKg, 8.09, 1e-9));
        // The return from waypoint 3 is at the weight then aboard (both
        // dropped): the same as the mission's own return from there.
        QVERIFY(near(r.arrivals[2].returnLandLikelyPct, r.landLikelyPct, 1e-6));
        QVERIFY(near(r.arrivals[2].returnLandPessimisticPct, r.landPessimisticPct, 1e-6));

        const MissionCost::Result back = MissionCost::costPayloadReturned(w, s, hLat, hLon, in);
        QVERIFY(near(back.likely.last().motorW, hoverW(in, 12.09), 1e-6));
        QVERIFY(back.landLikelyPct < r.landLikelyPct);
        QVERIFY(back.landPessimisticPct < r.landPessimisticPct);
    }

    // A headwind leg costs more than the same leg downwind; the pessimistic
    // case uses the gust.
    void testMissionCostWind()
    {
        using namespace MissionWalk;
        const Settings s = Settings::read(lookup(params22()));
        // Out 500 m north and back south, then land.
        QVector<Item> items;
        items << item(0, NavWaypoint, 9.0, 38.8, 0) << item(1, NavTakeoff, 0, 0, 30)
              << item(2, NavWaypoint, 9.0045, 38.8, 30) << item(3, NavWaypoint, 9.0, 38.8, 30)
              << item(4, NavLand, 0, 0, 0);
        const Result w = walk(items, s, 9.0, 38.8);
        QVERIFY2(w.ok, qPrintable(w.problem));
        MissionCost::Inputs in = inputs22();
        in.measuredRatio = [](double air) { return air >= 3.0 ? 1.0 + 0.02 * air : -1.0; };
        in.wind.speedMs = 5.0;
        in.wind.fromDeg = 0.0;   // from the north
        const MissionCost::Result r = MissionCost::cost(w, s, 9.0, 38.8, in);
        QVERIFY(r.ok);
        QCOMPARE(w.steps[1].kind, StepKind::Move);
        QCOMPARE(w.steps[2].kind, StepKind::Move);
        const double g1 = w.steps[1].horizM / w.steps[1].timeS;
        const double g2 = w.steps[2].horizM / w.steps[2].timeS;
        QVERIFY(near(r.likely[1].airspeedMs, g1 + 5.0, 1e-3));   // north into the wind
        QVERIFY(near(r.likely[2].airspeedMs, std::abs(g2 - 5.0), 1e-3));
        QVERIFY(r.likely[1].motorW > r.likely[2].motorW);
        in.wind.gustMs = 9.0;
        const MissionCost::Result rg = MissionCost::cost(w, s, 9.0, 38.8, in);
        QVERIFY(near(rg.pessimistic[1].airspeedMs, g1 + 9.0, 1e-3));
        QVERIFY(near(rg.likely[1].airspeedMs, g1 + 5.0, 1e-3));

        // No cruise power measured: without the drag constant every airspeed
        // over 3 m/s costs the same (the wind hardly counts) ...
        MissionCost::Inputs un = inputs22();
        un.wind.speedMs = 5.0;
        un.wind.fromDeg = 0.0;
        const MissionCost::Result ru = MissionCost::cost(w, s, 9.0, 38.8, un);
        QVERIFY(near(ru.likely[1].motorW, ru.likely[2].motorW, 1e-6));
        QVERIFY(ru.reasons.join(QString()).contains(QStringLiteral("wind is hardly costed")));
        // ... with it (learned from the lean), the hover plus the drag power:
        // into the wind costs more than with it, and the pessimistic case is
        // never below 1.4× hover.
        un.dragK = 0.6;
        const MissionCost::Result rk = MissionCost::cost(w, s, 9.0, 38.8, un);
        const double hover = hoverW(un, 12.09);
        QVERIFY(near(rk.likely[1].ratio, 1.0 + 0.6 * std::pow(rk.likely[1].airspeedMs, 3) / hover, 1e-9));
        QVERIFY(rk.likely[1].motorW > rk.likely[2].motorW);
        QVERIFY(rk.pessimistic[2].ratio >= MissionCost::kUnmeasuredPessimisticRatio);
        QVERIFY(near(rk.pessimistic[1].ratio,
                     std::max(1.4, 1.0 + 0.6 * std::pow(rk.pessimistic[1].airspeedMs, 3) / hover), 1e-9));
        QVERIFY(rk.reasons.join(QString()).contains(QStringLiteral("drag from its lean")));
        QVERIFY(!rk.ratioMeasured);
    }

    // Turn-back point, current limit, spare thrust and the autopilot's
    // battery voltages show up as reasons.
    void testMissionCostLimits()
    {
        using namespace MissionWalk;
        const Settings s = Settings::read(lookup(params22()));
        // Four legs of 500 m out, then return.
        QVector<Item> items;
        items << item(0, NavWaypoint, 9.0, 38.8, 0) << item(1, NavTakeoff, 0, 0, 30);
        for (int k = 1; k <= 4; ++k)
            items << item(1 + k, NavWaypoint, 9.0 + 0.0045 * k, 38.8, 30, 1.0);   // 1 s hold: stops each time
        items << item(6, NavReturnToLaunch, 0, 0, 0);
        const Result w = walk(items, s, 9.0, 38.8);
        QVERIFY2(w.ok, qPrintable(w.problem));
        MissionCost::Inputs in = inputs22();
        in.pack.capacityAh = 6.0;   // a small pack: the far points cannot return
        const MissionCost::Result r = MissionCost::cost(w, s, 9.0, 38.8, in);
        QVERIFY(r.ok);
        QVERIFY2(r.firstPastSeq > 2, qPrintable(QString::number(r.firstPastSeq)));
        QCOMPARE(r.turnBackSeq, r.firstPastSeq - 1);
        for (const MissionCost::ArrivalCost &a : r.arrivals) {
            if (a.seq >= 1 && a.seq < r.firstPastSeq)
                QVERIFY(a.canReturn && a.returnLandPessimisticPct >= 20.0);
            if (a.seq == r.firstPastSeq)
                QVERIFY(!a.canReturn && a.returnLandPessimisticPct < 20.0);
        }
        QVERIFY(r.reasons.join(QString()).contains(QStringLiteral("Past the turn-back point at item %1").arg(r.firstPastSeq)));

        // The turn-back point is judged on the pessimistic case: with the
        // reserve between the two cases' landings from waypoint 2, the likely
        // return would make it but the pessimistic one does not.
        MissionCost::Inputs mid = inputs22();
        const MissionCost::Result base = MissionCost::cost(w, s, 9.0, 38.8, mid);
        const MissionCost::ArrivalCost &a2 = base.arrivals[1];   // [0] is the take-off
        QCOMPARE(a2.seq, 2);
        QVERIFY(a2.returnLandLikelyPct > a2.returnLandPessimisticPct + 0.5);
        mid.reserveSocPct = (a2.returnLandLikelyPct + a2.returnLandPessimisticPct) / 2.0;
        const MissionCost::Result rmid = MissionCost::cost(w, s, 9.0, 38.8, mid);
        QVERIFY(rmid.arrivals[0].canReturn);
        QVERIFY(!rmid.arrivals[1].canReturn);
        QCOMPARE(rmid.firstPastSeq, 2);

        MissionCost::Inputs lim = inputs22();
        lim.currentLimitA = 50.0;
        lim.battLowV = 25.0;          // above the pack: reached at once
        lim.battCrtV = 10.0;          // never
        const MissionCost::Result rl = MissionCost::cost(w, s, 9.0, 38.8, lim);
        const QString why = rl.reasons.join(QStringLiteral("|"));
        QVERIFY2(why.contains(QStringLiteral("above the pack's 50 A")), qPrintable(why));
        QVERIFY(rl.overCurrent);
        QVERIFY(!r.overCurrent);                        // no limit given: not judged
        QVERIFY(MissionAssess::assess(w, rl, nullptr, MissionAssess::Limits(), MissionAssess::Context()).overCurrent);
        QVERIFY(why.contains(QStringLiteral("BATT_LOW_VOLT (25.0 V) at item 1")));
        QVERIFY(!why.contains(QStringLiteral("BATT_CRT_VOLT")));
        QVERIFY(!why.contains(QStringLiteral("Spare thrust")));
        lim.emptyKg = 26.0;           // far too heavy for 2:1
        QVERIFY(MissionCost::cost(w, s, 9.0, 38.8, lim).reasons.join(QString()).contains(QStringLiteral("Spare thrust")));
    }

    // The release servo output: a move to "released", held 300 ms, in flight,
    // releases the next payload; glitches, the ground and 0 µs do not count.
    void testPayloadReleaseSwitch()
    {
        using namespace PayloadRelease;
        QCOMPARE(threshold(1100, 1900), 1500.0);          // SERVO10_MIN / MAX on the 22-inch copter
        QCOMPARE(threshold(1000, 2100), 1550.0);
        QCOMPARE(threshold(qQNaN(), 1900), 1500.0);
        QCOMPARE(threshold(1900, 1100), 1500.0);

        Config c;
        c.totalKg = 4.0;
        Detector off;
        QCOMPARE(off.feed(c, 0, 1900, true, true), Seen::Nothing);   // no output set
        c.output = 10;

        // One 4 kg payload.
        Detector d;
        QCOMPARE(d.feed(c, 0, 1100, false, false), Seen::Nothing);  // off on the ground
        QCOMPARE(d.feed(c, 100, 1100, true, false), Seen::Nothing); // armed, off
        QCOMPARE(d.feed(c, 1000, 1100, true, true), Seen::Nothing);
        QCOMPARE(d.feed(c, 2000, 1900, true, true), Seen::Nothing);  // on, not yet held
        QCOMPARE(d.feed(c, 2200, 1100, true, true), Seen::Nothing);  // back off after 200 ms: a glitch
        QCOMPARE(d.feed(c, 2600, 1100, true, true), Seen::Nothing);
        QCOMPARE(d.released(), 0);
        QCOMPARE(d.feed(c, 3000, 1900, true, true), Seen::Nothing);
        QCOMPARE(d.feed(c, 3299, 1900, true, true), Seen::Nothing);
        QCOMPARE(d.feed(c, 3300, 1900, true, true), Seen::Released); // held 300 ms
        QCOMPARE(d.released(), 1);
        QCOMPARE(d.releasedKg(c), 4.0);
        QCOMPARE(d.feed(c, 3400, 0, true, true), Seen::Nothing);      // 0 µs: no reading, not "off"
        QCOMPARE(d.feed(c, 4000, 1900, true, true), Seen::Nothing);
        QCOMPARE(d.feed(c, 4400, 1100, true, true), Seen::Nothing);   // off …
        QCOMPARE(d.feed(c, 4800, 1100, true, true), Seen::Nothing);
        QCOMPARE(d.feed(c, 5000, 1900, true, true), Seen::Nothing);   // … and on again: nothing left
        QCOMPARE(d.feed(c, 5400, 1900, true, true), Seen::Nothing);
        QCOMPARE(d.released(), 1);
        QVERIFY(!d.tap(c));

        // Two of 2 kg: one move each.
        Config two = c;
        two.payloads = 2;
        Detector d2;
        d2.feed(two, 0, 1100, true, true);
        auto flip = [&](Detector &x, const Config &cfg, qint64 t0, bool airborne) {
            x.feed(cfg, t0, 1900, true, airborne);
            const Seen on = x.feed(cfg, t0 + 400, 1900, true, airborne);
            x.feed(cfg, t0 + 800, 1100, true, airborne);
            x.feed(cfg, t0 + 1200, 1100, true, airborne);
            return on;
        };
        QCOMPARE(flip(d2, two, 1000, true), Seen::Released);
        QCOMPARE(d2.releasedKg(two), 2.0);
        QCOMPARE(flip(d2, two, 5000, true), Seen::Released);
        QCOMPARE(d2.releasedKg(two), 4.0);
        QCOMPARE(flip(d2, two, 7000, true), Seen::Nothing);
        QCOMPARE(d2.released(), 2);

        // Output left at "released", then 1 s of 0 µs (no data) and still
        // released: the gap is not "off", so payload 2 stays aboard.
        Detector gap;
        gap.feed(two, 0, 1100, true, true);
        gap.feed(two, 1000, 1900, true, true);
        QCOMPARE(gap.feed(two, 1400, 1900, true, true), Seen::Released);
        gap.feed(two, 1500, 0, true, true);
        gap.feed(two, 2500, 0, true, true);
        QCOMPARE(gap.feed(two, 2600, 1900, true, true), Seen::Nothing);
        QCOMPARE(gap.feed(two, 3000, 1900, true, true), Seen::Nothing);
        QCOMPARE(gap.released(), 1);
        // Back, and out again: payload 2.
        gap.feed(two, 3100, 1100, true, true);
        gap.feed(two, 3500, 1100, true, true);
        QCOMPARE(flip(gap, two, 4000, true), Seen::Released);
        QCOMPARE(gap.released(), 2);

        // Armed on the ground: reported, not counted. Disarmed: nothing.
        Detector g;
        g.feed(c, 0, 1100, true, false);
        QCOMPARE(flip(g, c, 1000, false), Seen::OnOnGround);
        QCOMPARE(g.released(), 0);
        Detector dis;
        dis.feed(c, 0, 1100, false, false);
        dis.feed(c, 100, 1900, false, false);
        QCOMPARE(dis.feed(c, 500, 1900, false, false), Seen::Nothing);
        QCOMPARE(dis.released(), 0);

        // On at arming: reported; a drop then needs off and on again.
        QCOMPARE(dis.feed(c, 600, 1900, true, false), Seen::OnAtArm);
        QCOMPARE(dis.feed(c, 1000, 1900, true, true), Seen::Nothing);
        QCOMPARE(dis.released(), 0);
        dis.feed(c, 1100, 1100, true, true);
        dis.feed(c, 1500, 1100, true, true);
        QCOMPARE(flip(dis, c, 2000, true), Seen::Released);
        // A new flight (disarm, arm) has everything aboard again.
        dis.feed(c, 9000, 1100, false, false);
        dis.feed(c, 9100, 1100, true, false);
        QCOMPARE(dis.released(), 0);

        // Released when low.
        Config low = c;
        low.onWhenLow = true;
        Detector l;
        l.feed(low, 0, 1900, true, true);
        l.feed(low, 100, 1100, true, true);
        QCOMPARE(l.feed(low, 500, 1100, true, true), Seen::Released);

        // The tap releases the next one.
        Detector tp;
        QVERIFY(tp.tap(two));
        QVERIFY(tp.tap(two));
        QVERIFY(!tp.tap(two));
    }

    // The whole-mission result: each level from the landing charges against
    // the reserve, the lowest acceptable charge and the comfort margin.
    void testMissionAssessResults()
    {
        using namespace MissionWalk;
        const QVector<Item> items = parseWaypoints(templateMission());
        const Settings s = Settings::read(lookup(params22()));
        const double hLat = items[0].lat, hLon = items[0].lon;
        const Result w = walk(items, s, hLat, hLon);
        const MissionCost::Result c = MissionCost::cost(w, s, hLat, hLon, inputs22());
        QVERIFY(c.ok);
        const double L = c.landLikelyPct, P = c.landPessimisticPct;
        QVERIFY(L > P + 1.0);
        MissionAssess::Context ctx;
        auto run = [&](double reserve, double lowest) {
            MissionAssess::Limits lim;
            lim.reservePct = reserve;
            lim.lowestPct = lowest;
            return MissionAssess::assess(w, c, nullptr, lim, ctx).result;
        };
        QCOMPARE(run(20.0, 10.0), int(MissionAssess::Comfortable));       // lands ~95 %
        QCOMPARE(run(P - 10.0 + 0.01, 10.0), int(MissionAssess::Fits));   // just under 10 points of margin
        QCOMPARE(run(P - 10.0 - 0.01, 10.0), int(MissionAssess::Comfortable));
        QCOMPARE(run((L + P) / 2.0, 10.0), int(MissionAssess::Tight));
        QCOMPARE(run(L + 1.0, L - 1.0), int(MissionAssess::UsesReserve));
        QCOMPARE(run(L + 5.0, L + 1.0), int(MissionAssess::OneWayOnly));
        QCOMPARE(MissionAssess::resultColour(MissionAssess::Tight), QStringLiteral("amber"));
        QCOMPARE(MissionAssess::resultColour(MissionAssess::OneWayOnly), QStringLiteral("red"));

        // Too heavy: spare thrust below 2 at take-off, whatever the charge.
        MissionCost::Inputs heavy = inputs22();
        heavy.emptyKg = 26.0;
        const MissionCost::Result ch = MissionCost::cost(w, s, hLat, hLon, heavy);
        const MissionAssess::Assessment ah = MissionAssess::assess(w, ch, nullptr, MissionAssess::Limits(), ctx);
        QCOMPARE(ah.result, int(MissionAssess::TooHeavy));
        QVERIFY(ah.reasons.first().startsWith(QStringLiteral("Spare thrust")));
        QVERIFY(ah.reasons.first().contains(QStringLiteral("at take-off")));
        MissionAssess::Context flying = ctx;
        flying.inFlight = true;                                   // worked out from now: says so
        QVERIFY(MissionAssess::assess(w, ch, nullptr, MissionAssess::Limits(), flying).reasons.first()
                    .contains(QStringLiteral(" now, below")));
        // A pack that cannot even give the hover power (its voltage would
        // collapse): no spare thrust at all, too heavy.
        MissionCost::Inputs weak = inputs22();
        weak.pack.resistanceOhm = 2.0;
        const MissionCost::Result cw = MissionCost::cost(w, s, hLat, hLon, weak);
        QCOMPARE(cw.spareAtStart, 0.0);
        QCOMPARE(MissionAssess::assess(w, cw, nullptr, MissionAssess::Limits(), ctx).result, int(MissionAssess::TooHeavy));

        // Can't assess: says why.
        MissionCost::Inputs noWeight = inputs22();
        noWeight.emptyKg = qQNaN();
        const MissionAssess::Assessment an = MissionAssess::assess(
            w, MissionCost::cost(w, s, hLat, hLon, noWeight), nullptr, MissionAssess::Limits(), ctx);
        QCOMPARE(an.result, int(MissionAssess::CantAssess));
        QVERIFY(an.problem.contains(QStringLiteral("Weight missing")));
        QCOMPARE(MissionAssess::resultColour(an.result), QStringLiteral("grey"));
    }

    // Each waypoint's state, the autopilot's failsafe, the payload-comes-back
    // reason, gusts and how sure.
    void testMissionAssessWaypoints()
    {
        using namespace MissionWalk;
        const Settings s = Settings::read(lookup(params22()));
        QVector<Item> items;
        items << item(0, NavWaypoint, 9.0, 38.8, 0) << item(1, NavTakeoff, 0, 0, 30);
        for (int k = 1; k <= 4; ++k)
            items << item(1 + k, NavWaypoint, 9.0 + 0.0045 * k, 38.8, 30, 1.0);
        items << item(6, NavReturnToLaunch, 0, 0, 0);
        const Result w = walk(items, s, 9.0, 38.8);
        MissionCost::Inputs in = inputs22();
        in.pack.capacityAh = 6.0;
        const MissionCost::Result c = MissionCost::cost(w, s, 9.0, 38.8, in);
        QVERIFY(c.firstPastSeq > 2);
        MissionAssess::Context ctx;
        MissionAssess::Limits lim;
        const MissionAssess::Assessment a = MissionAssess::assess(w, c, nullptr, lim, ctx);
        QCOMPARE(a.waypoints.size(), c.arrivals.size());
        for (const MissionAssess::Waypoint &wp : a.waypoints) {
            if (wp.seq == c.turnBackSeq)
                QCOMPARE(wp.state, int(MissionAssess::LastSafeTurnBack));
            if (wp.seq == c.firstPastSeq && wp.socPessimisticPct >= 20.0)
                QCOMPARE(wp.state, int(MissionAssess::PastTurnBack));
            if (wp.seq >= 1 && wp.seq < c.turnBackSeq)
                QCOMPARE(wp.state, int(MissionAssess::OnTrack));
            if (wp.socPessimisticPct < 20.0 && wp.socLikelyPct > 0.5)
                QVERIFY(wp.state == MissionAssess::OnReserve || wp.state == MissionAssess::PastTurnBack
                        || wp.state == MissionAssess::OnReserve);
        }
        QVERIFY(a.result <= MissionAssess::Tight);

        // The airspeed cruise power is filed under: against the wind from the
        // lean, else the ground speed.
        QVERIFY(near(WindFromLean::airspeedMs(10.0, 0.0, -5.0, 0.0, 10.0), 15.0, 1e-9));
        QVERIFY(near(WindFromLean::airspeedMs(10.0, 0.0, qQNaN(), qQNaN(), 10.0), 10.0, 1e-9));
        // Navigation items are arrived at; DO and condition commands are not.
        QVERIFY(MissionWalk::isNavCommand(MissionWalk::NavWaypoint));
        QVERIFY(MissionWalk::isNavCommand(MissionWalk::NavLoiterToAlt));
        QVERIFY(!MissionWalk::isNavCommand(MissionWalk::DoSetServo));
        QVERIFY(!MissionWalk::isNavCommand(MissionWalk::DoChangeSpeed));
        QVERIFY(!MissionWalk::isNavCommand(-1));

        // A 1 Ah pack runs out on the way out: Can't be done, Not reachable.
        MissionCost::Inputs tiny = inputs22();
        tiny.pack.capacityAh = 1.0;
        const MissionCost::Result ct = MissionCost::cost(w, s, 9.0, 38.8, tiny);
        const MissionAssess::Assessment at = MissionAssess::assess(w, ct, nullptr, lim, ctx);
        QCOMPARE(at.result, int(MissionAssess::CantBeDone));
        QCOMPARE(at.waypoints.last().state, int(MissionAssess::NotReachable));

        // The autopilot's battery failsafe: only when it has an action.
        MissionAssess::Limits fs;
        fs.battLowV = 25.0;           // above the pack: from the first step
        const MissionAssess::Assessment noAction = MissionAssess::assess(w, c, nullptr, fs, ctx);
        for (const MissionAssess::Waypoint &wp : noAction.waypoints)
            QVERIFY(wp.state != MissionAssess::AutopilotTurnsBack);
        fs.battLowAction = 2;         // RTL
        const MissionAssess::Assessment withAction = MissionAssess::assess(w, c, nullptr, fs, ctx);
        QCOMPARE(withAction.waypoints[1].state, int(MissionAssess::AutopilotTurnsBack));
        QVERIFY(withAction.reasons.join(QString()).contains(QStringLiteral("battery failsafe (BATT_LOW_VOLT)")));

        // The template with two 2 kg drops: the payload-comes-back case is a
        // reason when it lands below the reserve.
        const QVector<Item> tItems = parseWaypoints(templateMission());
        const double hLat = tItems[0].lat, hLon = tItems[0].lon;
        const Result tw = walk(tItems, s, hLat, hLon);
        MissionCost::Inputs drops = inputs22();
        drops.drops = {{tw.arrivals[1].timeS, 2.0}, {tw.arrivals[2].timeS, 2.0}};
        const MissionCost::Result cd = MissionCost::cost(tw, s, hLat, hLon, drops);
        const MissionCost::Result cb = MissionCost::costPayloadReturned(tw, s, hLat, hLon, drops);
        QVERIFY(cb.landPessimisticPct < cd.landPessimisticPct);
        MissionAssess::Limits between;
        between.reservePct = (cb.landPessimisticPct + cd.landPessimisticPct) / 2.0;
        MissionAssess::Context withDrops;
        withDrops.hasDrops = true;
        QVERIFY(MissionAssess::assess(tw, cd, &cb, between, withDrops).reasons.join(QString())
                    .contains(QStringLiteral("If the payload comes back")));
        QVERIFY(!MissionAssess::assess(tw, cd, &cb, MissionAssess::Limits(), withDrops).reasons.join(QString())
                     .contains(QStringLiteral("If the payload comes back")));

        // Gusts at the flight height above the limit: red, with the reason;
        // exactly at the limit is not above it.
        MissionAssess::Limits gl;
        gl.gustLimitMs = 12.0;
        MissionAssess::Context gusty;
        gusty.gustMs = 12.1;
        gusty.flightHeightM = 100.0;
        gusty.weatherKnown = true;
        gusty.weatherConfidence = SiteWeather::High;
        const MissionAssess::Assessment ag = MissionAssess::assess(tw, cd, nullptr, gl, gusty);
        QVERIFY(ag.weatherRed);
        QVERIFY(ag.reasons.join(QString()).contains(QStringLiteral("Gusts 12.1 m/s at 100 m, above the 12 m/s gust limit")));
        gusty.gustMs = 12.0;
        QVERIFY(!MissionAssess::assess(tw, cd, nullptr, gl, gusty).weatherRed);

        // How sure.
        MissionAssess::Context sure;
        sure.weatherKnown = true;
        sure.weatherConfidence = SiteWeather::High;
        QCOMPARE(MissionAssess::assess(tw, cd, nullptr, gl, sure).confidence, int(SiteWeather::Low));
        sure.cruiseMeasured = true;
        QCOMPARE(MissionAssess::assess(tw, cd, nullptr, gl, sure).confidence, int(SiteWeather::Medium));
        sure.tableProven = true;
        QCOMPARE(MissionAssess::assess(tw, cd, nullptr, gl, sure).confidence, int(SiteWeather::High));
        sure.weatherConfidence = SiteWeather::Low;     // airport only: one level down
        const MissionAssess::Assessment as = MissionAssess::assess(tw, cd, nullptr, gl, sure);
        QCOMPARE(as.confidence, int(SiteWeather::Medium));
        QVERIFY(as.confidenceWhy.join(QString()).contains(QStringLiteral("airport")));
        sure.weatherKnown = false;
        QVERIFY(MissionAssess::assess(tw, cd, nullptr, gl, sure).reasons.join(QString())
                    .contains(QStringLiteral("wind not costed")));
    }

    // The "Mission estimate" item: green results pass, amber and Can't assess
    // warn, red fails; gusts make green amber. Never mandatory.
    void testMissionEstimateCheckStatuses()
    {
        QVariantMap r;
        MissionEstimateCheck item([&r]() { return r; });
        QVERIFY(!item.mandatory());
        QCOMPARE(item.id(), QStringLiteral("power.mission_estimate"));
        QCOMPARE(item.status(), CheckStatus::Skipped);           // not applicable
        r[QStringLiteral("applicable")] = true;
        r[QStringLiteral("result")] = int(MissionAssess::CantAssess);
        r[QStringLiteral("problem")] = QStringLiteral("Weight missing");
        item.evaluate();
        QCOMPARE(item.status(), CheckStatus::Warning);
        QCOMPARE(item.message(), QStringLiteral("Can't assess: Weight missing"));
        auto set = [&r](int result) {
            r[QStringLiteral("result")] = result;
            r[QStringLiteral("resultName")] = MissionAssess::resultName(result);
            r[QStringLiteral("colour")] = MissionAssess::resultColour(result);
            r[QStringLiteral("landLikelyPct")] = 84.4;
            r[QStringLiteral("landPessimisticPct")] = 81.6;
            r[QStringLiteral("totalText")] = QStringLiteral("2:59");
        };
        set(MissionAssess::Comfortable);
        item.evaluate();
        QCOMPARE(item.status(), CheckStatus::Passed);
        QCOMPARE(item.message(), QStringLiteral("Comfortable — lands with 84 % (pessimistic 82 %), 2:59"));
        set(MissionAssess::Fits);
        item.evaluate();
        QCOMPARE(item.status(), CheckStatus::Passed);
        set(MissionAssess::Tight);
        item.evaluate();
        QCOMPARE(item.status(), CheckStatus::Warning);
        set(MissionAssess::UsesReserve);
        item.evaluate();
        QCOMPARE(item.status(), CheckStatus::Warning);
        for (int red : {int(MissionAssess::OneWayOnly), int(MissionAssess::CantBeDone), int(MissionAssess::TooHeavy)}) {
            set(red);
            item.evaluate();
            QCOMPARE(item.status(), CheckStatus::Failed);
        }
        set(MissionAssess::Comfortable);
        r[QStringLiteral("weatherRed")] = true;
        item.evaluate();
        QCOMPARE(item.status(), CheckStatus::Warning);
        QVERIFY(item.message().endsWith(QStringLiteral("gusts over the limit")));
        // Over the pack's current limit: a warning, green turns amber.
        r[QStringLiteral("weatherRed")] = false;
        r[QStringLiteral("overCurrent")] = true;
        item.evaluate();
        QCOMPARE(item.status(), CheckStatus::Warning);
        QVERIFY(item.message().endsWith(QStringLiteral("over the pack's current limit")));
        set(MissionAssess::OneWayOnly);
        item.evaluate();
        QCOMPARE(item.status(), CheckStatus::Failed);     // red stays red
        r[QStringLiteral("overCurrent")] = false;
        // A pass by hand is undone: the verdict is computed.
        r[QStringLiteral("weatherRed")] = false;
        set(MissionAssess::Tight);
        item.evaluate();
        item.confirm(QStringLiteral("by hand"));
        QCOMPARE(item.status(), CheckStatus::Warning);
    }

    // The wind from the lean: samples made for a known drag constant and wind
    // (an out-and-back leg and a hover, a little noise) give them back; one
    // direction only is not enough to learn the constant; with the constant
    // known, one straight leg gives the wind.
    void testWindFromLean()
    {
        using namespace WindFromLean;
        const double k = 0.6, mass = 12.09, wN = 3.0, wE = -4.0;
        auto sample = [&](double vN, double vE, double yaw, double noise) {
            const double aN = vN - wN, aE = vE - wE, sp = std::hypot(aN, aE);
            double r, p;
            attitudeFor(k * sp * aN / (mass * kG) + noise, k * sp * aE / (mass * kG) - noise, yaw, &r, &p);
            Sample s;
            s.rollRad = r; s.pitchRad = p; s.yawRad = yaw; s.velN = vN; s.velE = vE; s.massKg = mass;
            return s;
        };
        // The lean vector is read back exactly from its attitude, at any heading.
        for (double yaw : {0.0, 0.7, 2.5, -1.9}) {
            double r, p, hN, hE;
            attitudeFor(0.21, -0.13, yaw, &r, &p);
            leanVector(r, p, yaw, &hN, &hE);
            QVERIFY(near(hN, 0.21, 1e-12) && near(hE, -0.13, 1e-12));
        }
        QVector<Sample> out, all;
        for (int i = 0; i < 30; ++i) {
            const double n = 0.002 * std::sin(i * 1.7);
            out << sample(10.0, 0.0, 0.0, n);                    // north at 10 m/s
            all << sample(10.0, 0.0, 0.0, n) << sample(-10.0, 0.0, kPi, -n) << sample(0.0, 0.0, 1.0, n);
        }
        const Fit f = learn(all);
        QVERIFY2(f.ok, qPrintable(QStringLiteral("rms %1 spread %2").arg(f.rmsTan).arg(f.spreadDeg)));
        QVERIFY2(near(f.k, k, 0.02), qPrintable(QString::number(f.k)));
        QVERIFY2(near(f.windN, wN, 0.15) && near(f.windE, wE, 0.15),
                 qPrintable(QStringLiteral("%1, %2").arg(f.windN).arg(f.windE)));
        QVERIFY(near(f.speedMs(), 5.0, 0.15));
        QVERIFY2(near(f.fromDeg(), 126.87, 2.0), qPrintable(QString::number(f.fromDeg())));   // from the south-east
        QVERIFY(f.spreadDeg > 170.0);

        // One direction only: k cannot be told from a wind along the track.
        const Fit one = learn(out);
        QVERIFY(!one.ok);
        QVERIFY(one.spreadDeg < kMinSpreadDeg);
        QVERIFY(!learn(all.mid(0, kMinSamples - 1)).ok);         // too few

        // k known: one straight leg is enough for the wind.
        const Fit w = wind(out, k);
        QVERIFY2(w.ok, qPrintable(QString::number(w.rmsTan)));
        QVERIFY2(near(w.windN, wN, 0.2) && near(w.windE, wE, 0.2),
                 qPrintable(QStringLiteral("%1, %2").arg(w.windN).arg(w.windE)));
        QVERIFY(!wind(out, 0.0).ok);

        // A wrong k: on the out-and-back legs the lean cannot be explained and
        // the fit is refused (on one straight leg it could not be told: a
        // wind along the track fits any k, which is why k is learned first).
        QVERIFY(!wind(all, 6.0).ok);
        QVERIFY(wind(all, k).ok);

        QVERIFY(isSteady(0.2, 0.1));
        QVERIFY(!isSteady(1.0, 0.1));
        QVERIFY(!isSteady(0.0, 0.8));
    }

    // Terrain clearance: the legs of a walk (moves over the ground, heights
    // above home changing evenly along each), each against the terrain under
    // it, home's ground taken from the terrain at the start of the leg that
    // leaves home. A leg closer than the margin (20 m) is reported with
    // where; a leg without terrain leaves the check incomplete; without
    // home's ground nothing is checked.
    void testTerrainClearance()
    {
        using namespace TerrainClearance;
        const double hLat = 9.0, hLon = 38.8;
        const double north = 1.0 / 111194.93;                       // degrees per metre
        const double east = north / std::cos(hLat * M_PI / 180.0);
        auto step = [](int seq, double horizM, double lat, double lon, double altM) {
            MissionWalk::Step s;
            s.seq = seq;
            s.horizM = horizM;
            s.lat = lat;
            s.lon = lon;
            s.altM = altM;
            return s;
        };
        MissionWalk::Result w;
        w.ok = true;
        w.steps = {step(1, 0.0, hLat, hLon, 50.0),                                  // climb on the spot: no leg
                   step(2, 1000.0, hLat + 1000.0 * north, hLon, 50.0),              // 1 km north at 50 m
                   step(3, 500.0, hLat + 1000.0 * north, hLon + 500.0 * east, 80.0), // 500 m east, up to 80 m
                   step(3, 0.0, hLat + 1000.0 * north, hLon + 500.0 * east, 80.0),  // hold: no leg
                   step(-1, 1118.0, hLat, hLon, 80.0)};                             // the return home
        const QVector<Leg> legs = legsFrom(w, hLat, hLon, 0.0);
        QCOMPARE(legs.size(), 3);
        QCOMPARE(legs[0].seq, 2);
        QCOMPARE(legs[0].fromAltM, 50.0);                           // after the climb
        QCOMPARE(legs[1].fromAltM, 50.0);
        QCOMPARE(legs[1].toAltM, 80.0);
        QCOMPARE(legs[2].seq, -1);
        QVERIFY(std::abs(legs[0].lengthM() - 1000.0) < 1.0);
        QVERIFY(legs[0].key() != legs[2].key());

        // Terrain at even spacing: 11 points 100 m apart on the 1 km leg.
        auto profile = [](QVector<double> h, double spacing, double last) {
            Profile p;
            p.ok = true;
            p.spacingM = spacing;
            p.finalSpacingM = last;
            p.heights = h;
            return p;
        };
        QCOMPARE(profile({1, 2, 3}, 100.0, 40.0).alongM(2), 140.0);
        QVector<double> flat1(11, 2000.0), flat2(6, 2000.0), flat3(12, 2000.0);
        QHash<QString, Profile> terrain;
        terrain[legs[0].key()] = profile(flat1, 100.0, 100.0);
        terrain[legs[1].key()] = profile(flat2, 100.0, 100.0);
        terrain[legs[2].key()] = profile(flat3, 100.0, 118.0);
        const double ground = groundAtHome(legs, terrain, hLat, hLon);
        QCOMPARE(ground, 2000.0);

        // Flat ground: 50 m clearance at the lowest, nothing reported.
        Result r = check(legs, terrain, ground);
        QVERIFY(r.complete());
        QCOMPARE(r.legsKnown, 3);
        QCOMPARE(r.lowestM, 50.0);
        QVERIFY(r.lows.isEmpty());

        // A hill rising 45 m along the first leg: 5 m at its end.
        QVector<double> hill(11);
        for (int i = 0; i < 11; ++i)
            hill[i] = 2000.0 + 4.5 * i;
        terrain[legs[0].key()] = profile(hill, 100.0, 100.0);
        QCOMPARE(groundAtHome(legs, terrain, hLat, hLon), 2000.0);   // the start of the leg leaving home
        r = check(legs, terrain, ground);
        QCOMPARE(r.lows.size(), 1);
        QCOMPARE(r.lows[0].seq, 2);
        QVERIFY(std::abs(r.lows[0].clearanceM - 5.0) < 1e-9);
        QCOMPARE(r.lows[0].alongM, 1000.0);
        QVERIFY(std::abs(r.lows[0].lat - (hLat + 1000.0 * north)) < 1e-7);
        QVERIFY(std::abs(r.lowestM - 5.0) < 1e-9);

        // The climbing leg: 80 m above home at its end over ground 60 m up
        // is 20 m, the margin itself (not reported); 61 m up is 19 m.
        terrain[legs[0].key()] = profile(flat1, 100.0, 100.0);
        QVector<double> rise(flat2);
        rise.last() = 2060.0;
        terrain[legs[1].key()] = profile(rise, 100.0, 100.0);
        QVERIFY(check(legs, terrain, ground).lows.isEmpty());
        rise.last() = 2061.0;
        terrain[legs[1].key()] = profile(rise, 100.0, 100.0);
        r = check(legs, terrain, ground);
        QCOMPARE(r.lows.size(), 1);
        QCOMPARE(r.lows[0].seq, 3);
        QVERIFY(std::abs(r.lows[0].clearanceM - 19.0) < 1e-9);
        // Half way along, the planned height is half way between 50 and 80 m.
        QVector<double> mid(flat2);
        mid[3] = 2000.0 + 50.0 + 0.6 * 30.0 - 10.0;               // 300 m of 500: 68 m planned, 10 m clear
        terrain[legs[1].key()] = profile(mid, 100.0, 100.0);
        r = check(legs, terrain, ground);
        QCOMPARE(r.lows.size(), 1);
        QVERIFY(std::abs(r.lows[0].clearanceM - 10.0) < 1e-9);
        QCOMPARE(r.lows[0].alongM, 300.0);

        // A leg without terrain: checked as far as known, not complete.
        terrain.remove(legs[2].key());
        r = check(legs, terrain, ground);
        QCOMPARE(r.legsKnown, 2);
        QVERIFY(!r.complete());
        Profile failed;
        terrain[legs[2].key()] = failed;                           // a failed query counts as unknown
        QCOMPARE(check(legs, terrain, ground).legsKnown, 2);

        // Home's ground unknown (no terrain for the leg leaving home): nothing checked.
        terrain.remove(legs[0].key());
        QVERIFY(std::isnan(groundAtHome(legs, terrain, hLat, hLon)));
        r = check(legs, terrain, groundAtHome(legs, terrain, hLat, hLon));
        QVERIFY(!r.homeKnown && !r.complete() && r.lows.isEmpty() && r.legsKnown == 0);
    }

    // The mission estimate against the flight: kept as JSON and read back,
    // the summary line, and the accuracy over flights.
    void testMissionRecord()
    {
        using namespace MissionRecord;
        Record r;
        r.resultAtTakeoff = QStringLiteral("Comfortable");
        r.predTotalS = 179.0;
        r.predLandLikely = 85.0;
        r.predLandPess = 83.0;
        r.realTotalS = 185.0;
        r.realLandSoc = 82.0;
        r.items = {{1, 42.5, 94.0, 94.0, 44.0, 93.0}, {3, 65.0, 92.0, 91.0, 68.0, 90.0}, {5, 179.0, 85.0, 83.0, -1.0, -1.0}};
        r.drops = {QVariantMap{{QStringLiteral("index"), 1}, {QStringLiteral("kg"), 4.0}, {QStringLiteral("timeS"), 68.0},
                               {QStringLiteral("how"), QStringLiteral("output")}}};
        const Record back = fromJson(toJson(r));
        QCOMPARE(back.items.size(), 3);
        QCOMPARE(back.items[1].realTimeS, 68.0);
        QCOMPARE(back.items[2].realTimeS, -1.0);
        QCOMPARE(back.drops.size(), 1);
        QCOMPARE(back.resultAtTakeoff, QStringLiteral("Comfortable"));
        QVERIFY(near(landSocErrorPts(back), -3.0, 1e-9));
        QVERIFY(near(timeErrorPct(back), 6.0 / 179.0 * 100.0, 1e-9));
        QCOMPARE(summary(back), QStringLiteral("Mission: landed after 3:05 (plan 2:59, +3 %) with 82 % (plan 85 %, -3 pt; "
                                               "pessimistic 83 %). Comfortable at take-off. 2 of 3 items reached."));
        QVERIFY(summary(Record()).isEmpty());
        QVERIFY(!fromJson(QStringLiteral("not json")).valid());

        // Three flights: off by 3, 1 and 1 points, 6, 2 and 2 s in time; one
        // landed below the pessimistic charge (82 < 83), two at or above it.
        Record r2 = r;
        r2.realTotalS = 177.0;
        r2.realLandSoc = 86.0;
        Record r3 = r2;
        r3.realLandSoc = 84.0;
        r3.realTotalS = 181.0;
        const Accuracy a = accuracy({back, r2, r3, Record()});
        QCOMPARE(a.flights, 3);
        QVERIFY(near(a.meanAbsSocPts, (3.0 + 1.0 + 1.0) / 3.0, 1e-9));
        QVERIFY(near(a.meanAbsTimePct, (6.0 + 2.0 + 2.0) / 179.0 * 100.0 / 3.0, 1e-9));
        QCOMPARE(a.atOrAbovePessimistic, 2);
        QCOMPARE(accuracy({}).flights, 0);
    }

    // The trip to a target: RETURN NOW only while still going out; already
    // returning (RTL, LAND, AUTO on its return or land item) the landing is
    // said to use the reserve instead.
    void testTripAdviceWhenReturning()
    {
        using VPM = VehicleProfileManager;
        QVERIFY(VPM::isReturning(QStringLiteral("RTL"), -1));
        QVERIFY(VPM::isReturning(QStringLiteral("Smart RTL"), -1));
        QVERIFY(VPM::isReturning(QStringLiteral("Land"), -1));
        QVERIFY(VPM::isReturning(QStringLiteral("Auto"), MissionWalk::NavReturnToLaunch));
        QVERIFY(VPM::isReturning(QStringLiteral("Auto"), MissionWalk::NavLand));
        QVERIFY(!VPM::isReturning(QStringLiteral("Auto"), MissionWalk::NavWaypoint));
        QVERIFY(!VPM::isReturning(QStringLiteral("Auto"), -1));
        QVERIFY(!VPM::isReturning(QStringLiteral("Loiter"), MissionWalk::NavReturnToLaunch));
        QVERIFY(!VPM::isReturning(QStringLiteral("Guided"), -1));

        const VPM::TripAdvice out = VPM::tripAdvice(true, false);
        QVERIFY(out.returnNow && !out.landingOnReserve);
        const VPM::TripAdvice back = VPM::tripAdvice(true, true);
        QVERIFY(!back.returnNow && back.landingOnReserve);
        const VPM::TripAdvice fine = VPM::tripAdvice(false, true);
        QVERIFY(!fine.returnNow && !fine.landingOnReserve);
        QVERIFY(!VPM::tripAdvice(false, false).returnNow);
    }

    void testMissionWalkTemplate()
    {
        using namespace MissionWalk;
        QString err;
        const QVector<Item> items = parseWaypoints(templateMission(), &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(items.size(), 5);
        QCOMPARE(items[4].command, int(NavReturnToLaunch));
        const Settings s = Settings::read(lookup(params22()));
        const Result r = walk(items, s, items[0].lat, items[0].lon);
        QVERIFY2(r.ok, qPrintable(r.problem));
        QVERIFY(r.endsLanded);
        QVERIFY(r.events.isEmpty());

        const double d1 = PowerTypeModel::distanceM(items[0].lat, items[0].lon, items[2].lat, items[2].lon);
        const double d2 = PowerTypeModel::distanceM(items[2].lat, items[2].lon, items[3].lat, items[3].lon);
        const double d3 = PowerTypeModel::distanceM(items[3].lat, items[3].lon, items[0].lat, items[0].lon);
        const double climb = 30.0 / 2.5 + 2.5;
        const double leg1 = d1 / 10.0 + 6.5 / 2.0;   // starts stopped, flies through
        const double leg2 = d2 / 10.0 + 6.5 / 2.0;   // flown into, stops
        const double home = d3 / 10.0 + 6.5;         // stop to stop
        const double landT = 20.0 / 1.5 + 10.0 / 0.5;
        QCOMPARE(r.arrivals.size(), 4);
        QCOMPARE(r.arrivals[0].seq, 1);
        QVERIFY(near(r.arrivals[0].timeS, climb, 1e-6));
        QVERIFY(near(r.arrivals[1].timeS, climb + leg1, 1e-6));
        QVERIFY(near(r.arrivals[2].timeS, climb + leg1 + leg2, 1e-6));
        QVERIFY(near(r.totalS, climb + leg1 + leg2 + home + 5.0 + landT, 1e-6));
        QCOMPARE(r.arrivals[3].seq, 4);
        QVERIFY(near(r.arrivals[3].timeS, r.totalS, 1e-9));
        // About 1 min 42 s in all.
        QVERIFY(r.totalS > 100.0 && r.totalS < 103.0);
        // Steps: climb, two moves, return, the hold above home, landing.
        QCOMPARE(r.steps.size(), 6);
        QCOMPARE(r.steps[0].kind, StepKind::Climb);
        QCOMPARE(r.steps[3].kind, StepKind::Return);
        QVERIFY(r.steps[4].hover);
        QCOMPARE(r.steps[5].kind, StepKind::Land);
        QVERIFY(near(r.steps[5].climbM, -30.0, 1e-9));

        // Below RTL_ALT_M the return climbs first.
        QVector<Item> low = items;
        low[1].alt = low[2].alt = low[3].alt = 10.0;
        const Result rl = walk(low, s, items[0].lat, items[0].lon);
        QVERIFY2(rl.ok, qPrintable(rl.problem));
        QCOMPARE(rl.steps[3].kind, StepKind::Climb);
        QVERIFY(near(rl.steps[3].climbM, 5.0, 1e-9));
        QVERIFY(near(rl.steps.last().climbM, -15.0, 1e-9));
    }

    // A servo command is where a payload may drop; a speed change sets the
    // speed of the legs after it.
    void testMissionWalkDropsAndSpeed()
    {
        using namespace MissionWalk;
        const QVector<Item> base = parseWaypoints(templateMission());
        const Settings s = Settings::read(lookup(params22()));
        QVector<Item> items = {base[0], base[1], base[2],
                               item(3, DoSetServo, 0, 0, 0, 10, 1900, FrameMission),
                               item(4, DoChangeSpeed, 0, 0, 0, 1, 5.0, FrameMission),
                               base[3], base[4]};
        items[5].seq = 5;
        items[6].seq = 6;
        const Result r = walk(items, s, base[0].lat, base[0].lon);
        QVERIFY2(r.ok, qPrintable(r.problem));
        QCOMPARE(r.events.size(), 1);
        QCOMPARE(r.events[0].command, int(DoSetServo));
        QCOMPARE(r.events[0].p1, 10.0);
        QCOMPARE(r.events[0].p2, 1900.0);
        // Released on reaching waypoint 2, there.
        QVERIFY(near(r.events[0].timeS, r.arrivals[1].timeS, 1e-9));
        QVERIFY(near(r.events[0].lat, base[2].lat, 1e-9));
        // Waypoint 3 at 5 m/s: below a²/j its speed-up takes 2√5 s.
        const double d2 = PowerTypeModel::distanceM(base[2].lat, base[2].lon, base[3].lat, base[3].lon);
        QVERIFY(near(r.arrivals[2].timeS - r.arrivals[1].timeS, d2 / 5.0 + std::sqrt(5.0), 1e-6));
        // The return is at WP_SPD (RTL_SPEED_MS 0), not the mission's 5 m/s.
        const double d3 = PowerTypeModel::distanceM(base[3].lat, base[3].lon, base[0].lat, base[0].lon);
        for (const Step &st : r.steps)
            if (st.kind == StepKind::Return)
                QVERIFY(near(st.timeS, d3 / 10.0 + 6.5, 1e-6));
    }

    // Holds, loops, landing elsewhere, starting in flight, and what cannot
    // be timed (said, not guessed).
    void testMissionWalkHoldsJumpsAndProblems()
    {
        using namespace MissionWalk;
        const QVector<Item> base = parseWaypoints(templateMission());
        const Settings s = Settings::read(lookup(params22()));
        const double hLat = base[0].lat, hLon = base[0].lon;
        const Result plain = walk(base, s, hLat, hLon);

        // A 10 s hold at waypoint 2: a stop there, then the hold.
        QVector<Item> hold = base;
        hold[2].p1 = 10.0;
        const Result rh = walk(hold, s, hLat, hLon);
        QVERIFY(near(rh.totalS - plain.totalS, 10.0 + 6.5, 1e-6));   // the hold, a stop and a start

        // Waypoints 2–3 flown twice (one jump back).
        QVector<Item> loop = base;
        loop.insert(4, item(4, DoJump, 0, 0, 0, 2, 1, FrameMission));
        loop[5].seq = 5;
        const Result rj = walk(loop, s, hLat, hLon);
        QVERIFY2(rj.ok, qPrintable(rj.problem));
        QCOMPARE(rj.arrivals.size(), 6);
        QCOMPARE(rj.arrivals[3].seq, 2);
        QVERIFY(rj.totalS > plain.totalS + 20.0);
        loop[4].p2 = -1;
        QVERIFY(walk(loop, s, hLat, hLon).problem.contains(QStringLiteral("no end")));

        // Landing at waypoint 3 instead of returning: no flight home, no hold.
        QVector<Item> landThere = base;
        landThere[4] = item(4, NavLand, base[3].lat, base[3].lon, 0);
        const Result rland = walk(landThere, s, hLat, hLon);
        QVERIFY(rland.ok && rland.endsLanded);
        QVERIFY(near(rland.totalS, plain.arrivals[2].timeS + 20.0 / 1.5 + 10.0 / 0.5, 1e-6));

        // In flight: from waypoint 2, already at speed, heading for 3.
        Start st;
        st.lat = base[2].lat;
        st.lon = base[2].lon;
        st.altM = 30.0;
        st.nextSeq = 3;
        st.moving = true;
        const Result rf = walk(base, s, hLat, hLon, st);
        QVERIFY2(rf.ok, qPrintable(rf.problem));
        QVERIFY(near(rf.totalS, plain.totalS - plain.arrivals[1].timeS, 1e-6));

        // A mission that ends in the air is timed, and says so; the return
        // after it is added unless the mission alone is asked for.
        QVector<Item> noEnd = base;
        noEnd.removeLast();
        const Result rn = walk(noEnd, s, hLat, hLon);
        QVERIFY(rn.ok && rn.endsLanded && rn.returnAssumed);
        QVERIFY(!plain.returnAssumed);
        Start alone;
        alone.assumeReturn = false;
        const Result rn2 = walk(noEnd, s, hLat, hLon, alone);
        QVERIFY(rn2.ok && !rn2.endsLanded && !rn2.returnAssumed);

        // Sea-level heights need home's height above sea level.
        QVector<Item> amsl = base;
        for (int k = 1; k <= 3; ++k) {
            amsl[k].frame = FrameGlobal;
            amsl[k].alt = 2930.0;
        }
        QVERIFY(walk(amsl, s, hLat, hLon).problem.contains(QStringLiteral("sea-level")));
        const Result ra = walk(amsl, s, hLat, hLon, Start(), 2900.0);
        QVERIFY2(ra.ok, qPrintable(ra.problem));
        QVERIFY(near(ra.totalS, plain.totalS, 1e-6));

        QVector<Item> terrain = base;
        terrain[2].frame = FrameTerrain;
        QVERIFY(walk(terrain, s, hLat, hLon).problem.contains(QStringLiteral("terrain")));
        QVector<Item> endless = base;
        endless[3] = item(3, NavLoiterUnlimited, base[3].lat, base[3].lon, 30);
        QVERIFY(walk(endless, s, hLat, hLon).problem.contains(QStringLiteral("no end")));
        QVector<Item> unknown = base;
        unknown.insert(3, item(3, 999, 0, 0, 0, 0, 0, FrameMission));
        QVERIFY(walk(unknown, s, hLat, hLon).problem.contains(QStringLiteral("command 999")));
        const Result noSettings = walk(base, Settings::read(lookup({})), hLat, hLon);
        QVERIFY(!noSettings.ok);
        QVERIFY(noSettings.problem.contains(QStringLiteral("WP_SPD")));

        // Not a .waypoints file.
        QString err;
        QVERIFY(parseWaypoints(QStringLiteral("hello"), &err).isEmpty());
        QVERIFY(!err.isEmpty());
    }

    // The charger comparison: charger ÷ count per flight that started full;
    // fine-tuning = the median of the last 5, from 2 flights, limited to
    // 0.80–1.20; proven when the last 3 each agree within 10 % with the count
    // as the GCS would have shown it (fine-tuned by the flights before only).
    void testMotorTableCalibration()
    {
        using namespace MotorTable;
        auto fl = [](double gcs, double charger, bool full = true, bool dismissed = false) {
            TableFlight f; f.gcsAh = gcs; f.chargerAh = charger; f.startedFull = full; f.dismissed = dismissed; return f;
        };
        auto near = [](double a, double b) { return qAbs(a - b) < 1e-9; };

        Calibration c = calibrate({});
        QCOMPARE(c.usable, 0); QCOMPARE(c.factor, 1.0); QVERIFY(!c.proven);
        c = calibrate({fl(14.0, 14.7)});                      // one flight: no fine-tuning yet
        QCOMPARE(c.usable, 1); QCOMPARE(c.factor, 1.0);
        c = calibrate({fl(14.0, 14.7), fl(10.0, 10.5)});      // two at 1.05
        QVERIFY(near(c.factor, 1.05)); QVERIFY(!c.proven);
        c = calibrate({fl(14.0, 14.7), fl(10.0, 10.5), fl(12.0, 12.6)});
        QVERIFY(near(c.factor, 1.05)); QVERIFY(c.proven);
        QCOMPARE(c.errors.size(), 3);
        QVERIFY(near(c.errors[2], 0.0));                       // the third was shown fine-tuned × 1.05

        // Not counted: a pack not full at the arm, a dismissed flight, no
        // charger number, a tiny count.
        c = calibrate({fl(14.0, 14.7), fl(10.0, 10.5, false), fl(10.0, 99.0, true, true), fl(10.0, 0.0), fl(0.2, 0.3)});
        QCOMPARE(c.usable, 1);

        // A flight 15 % off among the last three: not proven.
        c = calibrate({fl(14.0, 14.7), fl(10.0, 10.5), fl(12.0, 12.0 * 1.05 * 1.15)});
        QVERIFY(!c.proven);
        // The median of the last five, so one odd flight does not swing it.
        // Last five: 1.03, 1.30, 1.025, 1.03, 1.02 → median 1.03.
        c = calibrate({fl(10, 10.2), fl(10, 10.3), fl(10, 13.0), fl(10, 10.25), fl(10, 10.3), fl(10, 10.2)});
        QVERIFY(near(c.factor, 1.03));
        // Limited to 0.80–1.20.
        QCOMPARE(calibrate({fl(10, 15.0), fl(10, 16.0)}).factor, 1.20);
        QCOMPARE(calibrate({fl(10, 5.0), fl(10, 6.0)}).factor, 0.80);
    }

    // The table is for a quad on 6S with a pack size: anything else turns the
    // estimate off; hover above 60 % throttle and missing motor outputs warn.
    void testMotorTableSetupIssues()
    {
        using namespace MotorTable;
        const Table &t = m6215_320kv_22x78_6s();
        auto levels = [&](const SetupFacts &f) {
            QStringList out;
            for (const SetupIssue &i : setupIssues(t, f))
                out << i.level;
            return out;
        };
        SetupFacts ok;
        ok.motors = 4; ok.cells = 6; ok.capacityAh = 20.0; ok.thrustToWeight = 2.3; ok.outputsFresh = true;
        QVERIFY(setupIssues(t, ok).isEmpty());
        SetupFacts unknown = ok;
        unknown.motors = 0; unknown.cells = 0; unknown.thrustToWeight = -1.0;
        QVERIFY(setupIssues(t, unknown).isEmpty());   // not known is not wrong

        SetupFacts hexa = ok;  hexa.motors = 6;
        QCOMPARE(levels(hexa), QStringList({QStringLiteral("off")}));
        QVERIFY(setupIssues(t, hexa).first().text.contains(QStringLiteral("6")));
        SetupFacts eight = ok; eight.cells = 8;
        QCOMPARE(levels(eight), QStringList({QStringLiteral("off")}));
        QVERIFY(setupIssues(t, eight).first().text.contains(QStringLiteral("8S")));
        SetupFacts noPack = ok; noPack.capacityAh = 0.0;
        QCOMPARE(levels(noPack), QStringList({QStringLiteral("off")}));
        SetupFacts thin = ok; thin.thrustToWeight = 1.95;
        QCOMPARE(levels(thin), QStringList({QStringLiteral("warn")}));
        QVERIFY2(setupIssues(t, thin).first().text.contains(QStringLiteral("1.95")), qPrintable(setupIssues(t, thin).first().text));
        SetupFacts justOk = ok; justOk.thrustToWeight = 2.0;
        QVERIFY(setupIssues(t, justOk).isEmpty());

        // Full-throttle thrust: the table's top row at 23 V; less on a lower
        // pack, and less in thin air.
        QCOMPARE(maxThrustAt(t, 23.0).grams, 8801.0);
        QVERIFY(!maxThrustAt(t, 23.0).beyond);
        QVERIFY(maxThrustAt(t, 25.8).beyond);   // a fresh pack is above the table's voltage
        QVERIFY(maxThrustAt(t, 18.5).grams < 8801.0 && maxThrustAt(t, 18.5).grams > 5000.0);
        const double s4000 = densityRatio(isaPressureHpa(4000.0), isaTempC(4000.0));
        QVERIFY(maxThrustAt(t, 18.5, s4000).grams < maxThrustAt(t, 18.5).grams);
        // 12 kg by the reserve voltage: at about the 2.0 limit at 2000 m and
        // under it from 2900 m up.
        const double s2000 = densityRatio(isaPressureHpa(2000.0), isaTempC(2000.0));
        const double s2900 = densityRatio(isaPressureHpa(2900.0), isaTempC(2900.0));
        const double tw2000 = maxThrustAt(t, 18.71, s2000).grams / 3000.0;
        const double tw2900 = maxThrustAt(t, 18.60, s2900).grams / 3000.0;
        const double tw = maxThrustAt(t, 18.44, s4000).grams / 3000.0;
        QVERIFY2(tw2000 > 1.97 && tw2000 < 2.04, qPrintable(QString::number(tw2000)));
        QVERIFY2(tw2900 > 1.81 && tw2900 < 1.91, qPrintable(QString::number(tw2900)));
        QVERIFY2(tw > 1.63 && tw < 1.75, qPrintable(QString::number(tw)));
        QCOMPARE(maxThrustAt(t, 0.0).grams, 0.0);
        // The pack's continuous rating (4C on 20 Ah = 80 A): hover over it, on
        // average or by the reserve, warns.
        QCOMPARE(t.packCRating, 4.0);
        SetupFacts rated = ok; rated.ratingA = 80.0; rated.hoverAvgA = 76.5; rated.hoverPeakA = 79.9;
        QVERIFY(setupIssues(t, rated).isEmpty());
        SetupFacts peak = rated; peak.hoverPeakA = 84.0;
        QCOMPARE(levels(peak), QStringList({QStringLiteral("warn")}));
        QVERIFY2(setupIssues(t, peak).first().text.contains(QStringLiteral("84.0 A by the reserve")), qPrintable(setupIssues(t, peak).first().text));
        SetupFacts avg = rated; avg.hoverAvgA = 85.7; avg.hoverPeakA = 94.3;
        QCOMPARE(levels(avg), QStringList({QStringLiteral("warn")}));
        QVERIFY2(setupIssues(t, avg).first().text.contains(QStringLiteral("85.7 A on average (94.3 A by the reserve)")),
                 qPrintable(setupIssues(t, avg).first().text));
        SetupFacts noRating = avg; noRating.ratingA = 0.0;
        QVERIFY(setupIssues(t, noRating).isEmpty());
        SetupFacts noOutputs = ok; noOutputs.outputsFresh = false;
        QCOMPARE(levels(noOutputs), QStringList({QStringLiteral("warn")}));
    }
};

UT_REGISTER_TEST(BatteryEstimatorTest)

#include "BatteryEstimatorTest.moc"
