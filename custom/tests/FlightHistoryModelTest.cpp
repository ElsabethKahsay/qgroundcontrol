#include <QTest>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QDir>
#include <QVariantList>
#include <QSqlQuery>
#include <QSqlDatabase>

#include "UnitTest.h"
#include "DatabaseManager.h"
#include "FlightHistoryModel.h"

class FlightHistoryModelTest : public UnitTest {
    Q_OBJECT

private:
    QString initTempDb(DatabaseManager &db) {
        QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_XXXXXX.db");
        QTemporaryFile tmpFile;
        tmpFile.setFileTemplate(tmpPath);
        tmpFile.open();
        QString path = tmpFile.fileName();
        tmpFile.close();
        db.initialize(path);
        return path;
    }

    int seedOperator(DatabaseManager &db, const QString &name) {
        return db.insertOperator(name, QStringLiteral("Pilot"));
    }

private slots:
    void init() override {
        UnitTest::init();
        DatabaseManager::instance().reset();
    }
    void cleanup() override { UnitTest::cleanup(); }

    void testFilterByMode();
    void testFilterByVehicleAndOperator();
    void testFilterByDateRange();
    void testFilterBySearch();
    void testSortByDuration();
    void testPagination();
    void testRoles();
    void testFiltersViaSetters();
    void testGetFlightDetail();
};

void FlightHistoryModelTest::testFilterByMode()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_mode_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int op = seedOperator(db, QStringLiteral("Alice"));
    QVERIFY(op > 0);

    int flight = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QStringLiteral("p1"),
                               QString(), QString(), QString());
    int train = db.openFlight(op, 1, QStringLiteral("TRAINING"), QStringLiteral("p2"),
                              QString(), QString(), QString());
    int test = db.openFlight(op, 1, QStringLiteral("TESTING"), QStringLiteral("p3"),
                             QString(), QString(), QString());
    QVERIFY(flight > 0 && train > 0 && test > 0);

    QVariantMap all = db.queryFlights(0);
    QCOMPARE(all.value(QStringLiteral("totalCount")).toInt(), 3);

    QVariantMap flights = db.queryFlights(0, QString(), QString(), 0, 0,
                                          QStringLiteral("FLIGHT"));
    QCOMPARE(flights.value(QStringLiteral("totalCount")).toInt(), 1);
    QCOMPARE(flights.value(QStringLiteral("rows")).toList().size(), 1);

    QVariantMap training = db.queryFlights(0, QString(), QString(), 0, 0,
                                           QStringLiteral("TRAINING"));
    QCOMPARE(training.value(QStringLiteral("totalCount")).toInt(), 1);

    QVariantMap none = db.queryFlights(0, QString(), QString(), 0, 0,
                                       QStringLiteral("UNKNOWN_MODE"));
    QCOMPARE(none.value(QStringLiteral("totalCount")).toInt(), 0);
}

void FlightHistoryModelTest::testFilterByVehicleAndOperator()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_veh_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int opA = seedOperator(db, QStringLiteral("Alice"));
    int opB = seedOperator(db, QStringLiteral("Bob"));
    QVERIFY(opA > 0 && opB > 0);

    // Vehicle with sysid 1 (registers a row in vehicles with friendly_name).
    QVERIFY(db.registerNewVehicle(QStringLiteral("FP-A"), 1, 1,
                                  QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor"),
                                  QStringLiteral("Quadcopter"), QStringLiteral("4.5.2"),
                                  0, QString(), QString(), QStringLiteral("AlphaWing"),
                                  QStringLiteral("SYSID_TYPE_FALLBACK")));

    int fA = db.openFlight(opA, 1, QStringLiteral("FLIGHT"), QString(), QString(),
                           QString(), QString());
    int fA2 = db.openFlight(opA, 2, QStringLiteral("FLIGHT"), QString(), QString(),
                            QString(), QString());
    int fB = db.openFlight(opB, 1, QStringLiteral("FLIGHT"), QString(), QString(),
                           QString(), QString());
    QVERIFY(fA > 0 && fA2 > 0 && fB > 0);

    QVariantMap byVehicle = db.queryFlights(0, QString(), QString(), 1);
    QCOMPARE(byVehicle.value(QStringLiteral("totalCount")).toInt(), 2);

    QVariantMap byOperator = db.queryFlights(0, QString(), QString(), 0, opA);
    QCOMPARE(byOperator.value(QStringLiteral("totalCount")).toInt(), 2);

    QVariantMap both = db.queryFlights(0, QString(), QString(), 1, opB);
    QCOMPARE(both.value(QStringLiteral("totalCount")).toInt(), 1);
}

void FlightHistoryModelTest::testFilterByDateRange()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_date_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int op = seedOperator(db, QStringLiteral("Alice"));
    QVERIFY(op > 0);

    // Manually control started_at so date filtering is deterministic.
    int f1 = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QString(), QString(),
                           QString(), QString());
    int f2 = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QString(), QString(),
                           QString(), QString());
    QVERIFY(f1 > 0 && f2 > 0);

    // All flights started today (CURRENT_TIMESTAMP), so a range covering today
    // matches both and a past range matches none.
    const QString today = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd"));
    QVariantMap all = db.queryFlights(0, today, today);
    QCOMPARE(all.value(QStringLiteral("totalCount")).toInt(), 2);

    QVariantMap past = db.queryFlights(0, QStringLiteral("2000-01-01"), QStringLiteral("2000-01-02"));
    QCOMPARE(past.value(QStringLiteral("totalCount")).toInt(), 0);
}

void FlightHistoryModelTest::testFilterBySearch()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_search_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int op = seedOperator(db, QStringLiteral("Pegasus"));
    QVERIFY(op > 0);

    QVERIFY(db.registerNewVehicle(QStringLiteral("FP-B"), 1, 1,
                                  QStringLiteral("ArduPilot"), QStringLiteral("FixedWing"),
                                  QStringLiteral("Fixed Wing"), QStringLiteral("4.5.2"),
                                  0, QString(), QString(), QStringLiteral("SkyRunner"),
                                  QStringLiteral("SYSID_TYPE_FALLBACK")));

    int fPurpose = db.openFlight(op, 1, QStringLiteral("FLIGHT"),
                                 QStringLiteral("survey mission"), QString(), QString(), QString());
    int fLoc = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QString(),
                             QStringLiteral("Bole Field"), QString(), QString());
    QVERIFY(fPurpose > 0 && fLoc > 0);

    QVariantMap byPurpose = db.queryFlights(0, QString(), QString(), 0, 0, QString(),
                                            QStringLiteral("survey"));
    QCOMPARE(byPurpose.value(QStringLiteral("totalCount")).toInt(), 1);

    QVariantMap byLocation = db.queryFlights(0, QString(), QString(), 0, 0, QString(),
                                             QStringLiteral("bole"));
    QCOMPARE(byLocation.value(QStringLiteral("totalCount")).toInt(), 1);

    QVariantMap byOperator = db.queryFlights(0, QString(), QString(), 0, 0, QString(),
                                             QStringLiteral("pegasus"));
    QCOMPARE(byOperator.value(QStringLiteral("totalCount")).toInt(), 2);

    QVariantMap byVehicle = db.queryFlights(0, QString(), QString(), 0, 0, QString(),
                                            QStringLiteral("skyrunner"));
    QCOMPARE(byVehicle.value(QStringLiteral("totalCount")).toInt(), 2);
}

void FlightHistoryModelTest::testSortByDuration()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_sort_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int op = seedOperator(db, QStringLiteral("Alice"));
    QVERIFY(op > 0);

    int shortF = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QString(), QString(),
                               QString(), QString());
    int longF = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QString(), QString(),
                              QString(), QString());
    QVERIFY(shortF > 0 && longF > 0);
    QVERIFY(db.closeFlight(shortF, 30, 0, 0, 0, 0));
    QVERIFY(db.closeFlight(longF, 300, 0, 0, 0, 0));

    QVariantMap desc = db.queryFlights(0, QString(), QString(), 0, 0, QString(),
                                       QString(), QStringLiteral("duration_desc"));
    const QVariantList descRows = desc.value(QStringLiteral("rows")).toList();
    QCOMPARE(descRows.size(), 2);
    QCOMPARE(descRows.at(0).toMap().value(QStringLiteral("duration_sec")).toInt(), 300);
    QCOMPARE(descRows.at(1).toMap().value(QStringLiteral("duration_sec")).toInt(), 30);
}

void FlightHistoryModelTest::testPagination()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_page_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int op = seedOperator(db, QStringLiteral("Alice"));
    QVERIFY(op > 0);
    for (int i = 0; i < 120; ++i) {
        int fid = db.openFlight(op, 1, QStringLiteral("FLIGHT"), QString(), QString(),
                                QString(), QString());
        QVERIFY(fid > 0);
    }

    // pageSize 50 → 120 records → 3 pages.
    QVariantMap p0 = db.queryFlights(0);
    QCOMPARE(p0.value(QStringLiteral("totalCount")).toInt(), 120);
    QCOMPARE(p0.value(QStringLiteral("rows")).toList().size(), 50);

    QVariantMap p1 = db.queryFlights(1);
    QCOMPARE(p1.value(QStringLiteral("rows")).toList().size(), 50);

    QVariantMap p2 = db.queryFlights(2);
    QCOMPARE(p2.value(QStringLiteral("rows")).toList().size(), 20);
}

void FlightHistoryModelTest::testRoles()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_roles_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int op = seedOperator(db, QStringLiteral("Carol"));
    QVERIFY(op > 0);
    QVERIFY(db.registerNewVehicle(QStringLiteral("FP-C"), 1, 1,
                                  QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor"),
                                  QStringLiteral("Quadcopter"), QStringLiteral("4.5.2"),
                                  0, QString(), QString(), QStringLiteral("DeltaOne"),
                                  QStringLiteral("SYSID_TYPE_FALLBACK")));

    int fid = db.openFlight(op, 1, QStringLiteral("TRAINING"),
                            QStringLiteral("practice"), QStringLiteral("Home"),
                            QString(), QString());
    QVERIFY(fid > 0);
    QVERIFY(db.setFlightPreChecklistComplete(fid));
    QVERIFY(db.setFlightPostChecklistComplete(fid));
    QVERIFY(db.setFlightArmedAt(fid, QDateTime::currentDateTimeUtc()));
    QVERIFY(db.closeFlight(fid, 125, 42.0, 11.5, 12.9, 2, 20.0, 3.5, 800.0, 12.2,
                           4, 1, 2, 1));
    QVERIFY(db.insertTelemetryEvent(fid, QStringLiteral("BATTERY_WARN"),
                                    QString(), 20.5, 30.0, 12, QString()));

    FlightHistoryModel model;
    model.setFilterVehicleId(1);
    QCOMPARE(model.rowCount(), 1);

    const QModelIndex idx = model.index(0, 0);
    QCOMPARE(model.data(idx, FlightHistoryModel::FlightIdRole).toInt(), fid);
    QCOMPARE(model.data(idx, FlightHistoryModel::OperatorNameRole).toString(), QStringLiteral("Carol"));
    QCOMPARE(model.data(idx, FlightHistoryModel::VehicleNameRole).toString(), QStringLiteral("DeltaOne"));
    QCOMPARE(model.data(idx, FlightHistoryModel::ModeRole).toString(), QStringLiteral("TRAINING"));
    QCOMPARE(model.data(idx, FlightHistoryModel::PurposeRole).toString(), QStringLiteral("practice"));
    QCOMPARE(model.data(idx, FlightHistoryModel::LocationRole).toString(), QStringLiteral("Home"));
    QCOMPARE(model.data(idx, FlightHistoryModel::DurationSecRole).toInt(), 125);
    QCOMPARE(model.data(idx, FlightHistoryModel::DurationStrRole).toString(), QStringLiteral("2m 5s"));
    QCOMPARE(model.data(idx, FlightHistoryModel::AnomalyCountRole).toInt(), 1);
    QCOMPARE(model.data(idx, FlightHistoryModel::IsCompleteRole).toBool(), true);
    QCOMPARE(model.data(idx, FlightHistoryModel::MaxAltRole).toDouble(), 42.0);
    QCOMPARE(model.data(idx, FlightHistoryModel::MinBattRole).toDouble(), 11.5);
    QVERIFY(!model.data(idx, FlightHistoryModel::ArmedAtRole).toString().isEmpty());
    QVERIFY(!model.data(idx, FlightHistoryModel::DateRole).toString().isEmpty());

    const QHash<int, QByteArray> names = model.roleNames();
    QCOMPARE(names.value(FlightHistoryModel::FlightIdRole), QByteArray("flightId"));
    QCOMPARE(names.value(FlightHistoryModel::DurationStrRole), QByteArray("durationStr"));
    QCOMPARE(names.value(FlightHistoryModel::PrePassRateRole), QByteArray("prePassRate"));
}

void FlightHistoryModelTest::testFiltersViaSetters()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_setters_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int opA = seedOperator(db, QStringLiteral("Alice"));
    int opB = seedOperator(db, QStringLiteral("Bob"));
    QVERIFY(opA > 0 && opB > 0);

    int fa = db.openFlight(opA, 1, QStringLiteral("FLIGHT"), QStringLiteral("alpha"),
                           QString(), QString(), QString());
    int fb = db.openFlight(opB, 2, QStringLiteral("TRAINING"), QStringLiteral("beta"),
                           QString(), QString(), QString());
    QVERIFY(fa > 0 && fb > 0);

    FlightHistoryModel model;

    model.setFilterMode(QStringLiteral("TRAINING"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), FlightHistoryModel::ModeRole).toString(),
             QStringLiteral("TRAINING"));

    model.setFilterMode(QString());
    model.setFilterOperatorId(opB);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), FlightHistoryModel::PurposeRole).toString(),
             QStringLiteral("beta"));

    model.setFilterOperatorId(0);
    model.setFilterSearch(QStringLiteral("alpha"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), FlightHistoryModel::PurposeRole).toString(),
             QStringLiteral("alpha"));

    // totalCount reflects the current filter.
    model.setFilterSearch(QString());
    QCOMPARE(model.totalCount(), 2);
    QCOMPARE(model.currentPage(), 0);
    QCOMPARE(model.pageSize(), 50);

    // Pagination signals.
    QSignalSpy pageSpy(&model, &FlightHistoryModel::currentPageChanged);
    model.nextPage();
    QVERIFY(model.currentPage() == 0);  // totalCount < pageSize → no advance
    QCOMPARE(pageSpy.count(), 0);
}

void FlightHistoryModelTest::testGetFlightDetail()
{
    DatabaseManager &db = DatabaseManager::instance();
    QString tmpPath = QDir::tempPath() + QStringLiteral("/test_fhm_detail_XXXXXX.db");
    QTemporaryFile tmpFile;
    tmpFile.setFileTemplate(tmpPath);
    tmpFile.open();
    QVERIFY(db.initialize(tmpFile.fileName()));

    int op = seedOperator(db, QStringLiteral("Alice"));
    QVERIFY(op > 0);
    QVERIFY(db.registerNewVehicle(QStringLiteral("FP-D"), 1, 1,
                                  QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor"),
                                  QStringLiteral("Quadcopter"), QStringLiteral("4.5.2"),
                                  0, QString(), QString(), QStringLiteral("AlphaWing"),
                                  QStringLiteral("SYSID_TYPE_FALLBACK")));

    int fid = db.openFlight(op, 1, QStringLiteral("FLIGHT"),
                            QStringLiteral("survey"), QString(), QString(), QString());
    QVERIFY(fid > 0);

    // Pre/post check results.
    QVERIFY(db.insertFlightCheckResult(fid, QStringLiteral("rc"),
                                       QStringLiteral("Communication"), false,
                                       QStringLiteral("PASS"), QString()));
    QVERIFY(db.insertFlightCheckResult(fid, QStringLiteral("frame"),
                                       QStringLiteral("Frame"), true,
                                       QStringLiteral("WARNING"), QStringLiteral("scuff")));

    // Telemetry events (one normal, one handover, one anomaly).
    QVERIFY(db.insertTelemetryEvent(fid, QStringLiteral("ARM"), QString(),
                                    12.6, 0.0, 10, QStringLiteral("STABILIZE")));
    QVERIFY(db.insertTelemetryEvent(fid, QStringLiteral("HANDOVER_TO_TRAINEE"),
                                    QStringLiteral("instructor"), 12.5, 30.0, 11,
                                    QStringLiteral("LOITER")));
    QVERIFY(db.insertTelemetryEvent(fid, QStringLiteral("BATTERY_WARN"), QString(),
                                    20.8, 30.0, 11, QString()));

    // Motor + surface tests (motor keyed by sysid = flight vehicle_id).
    QVERIFY(db.logMotorTestResult(1, 1, 50, 3, 1150, 1148, 2, QStringLiteral("PASS")));
    QVERIFY(db.logSurfaceTestResult(fid, QStringLiteral("aileron"), 2, 1100, 1900, 1,
                                    QStringLiteral("PASS")));
    QVERIFY(db.insertComplianceRecord(fid, op, QStringLiteral("Clear"), QString(), QString()));

    FlightHistoryModel model;
    model.setFilterVehicleId(1);
    QCOMPARE(model.rowCount(), 1);

    const QVariantMap detail = model.getFlightDetail(fid);
    QVERIFY(detail.contains(QStringLiteral("flight")));
    QVERIFY(detail.contains(QStringLiteral("checks_pre")));
    QVERIFY(detail.contains(QStringLiteral("checks_post")));
    QVERIFY(detail.contains(QStringLiteral("events")));
    QVERIFY(detail.contains(QStringLiteral("handovers")));
    QVERIFY(detail.contains(QStringLiteral("motor_tests")));
    QVERIFY(detail.contains(QStringLiteral("surface_tests")));
    QVERIFY(detail.contains(QStringLiteral("compliance")));

    QCOMPARE(detail.value(QStringLiteral("flight")).toMap()
                 .value(QStringLiteral("id")).toInt(), fid);
    QCOMPARE(detail.value(QStringLiteral("checks_pre")).toList().size(), 1);
    QCOMPARE(detail.value(QStringLiteral("checks_post")).toList().size(), 1);
    QCOMPARE(detail.value(QStringLiteral("events")).toList().size(), 3);
    QCOMPARE(detail.value(QStringLiteral("handovers")).toList().size(), 1);
    QCOMPARE(detail.value(QStringLiteral("motor_tests")).toList().size(), 1);
    QCOMPARE(detail.value(QStringLiteral("surface_tests")).toList().size(), 1);
    QCOMPARE(detail.value(QStringLiteral("compliance")).toList().size(), 1);

    // Spot-check nested content.
    const QVariantMap event = detail.value(QStringLiteral("events")).toList().at(0).toMap();
    QCOMPARE(event.value(QStringLiteral("event_type")).toString(), QStringLiteral("ARM"));

    const QVariantMap motor = detail.value(QStringLiteral("motor_tests")).toList().at(0).toMap();
    QCOMPARE(motor.value(QStringLiteral("motor_index")).toInt(), 1);

    const QVariantMap surface = detail.value(QStringLiteral("surface_tests")).toList().at(0).toMap();
    QCOMPARE(surface.value(QStringLiteral("surface_id")).toString(), QStringLiteral("aileron"));
}

UT_REGISTER_TEST(FlightHistoryModelTest)

#include "FlightHistoryModelTest.moc"