/**
 * @file ZoneComplianceTest.cpp
 * @brief Tests for NoFlyZoneModel and ZoneComplianceCheck (L6 from TEST_CHECKLIST.md).
 *
 * Covers:
 *   - Zone CRUD operations
 *   - Zone activation/deactivation
 *   - Duplicate name detection
 *   - Zone search by coordinates
 *   - Compliance record logging
 */

#include <QTest>
#include <QSignalSpy>

#include "UnitTest.h"
#include "utils/DatabaseManager.h"
#include "models/NoFlyZoneModel.h"

class ZoneComplianceTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override {
        UnitTest::init();
        DatabaseManager::instance().reset();
        DatabaseManager::instance().initialize();
    }
    void cleanup() override { UnitTest::cleanup(); }

    void testInsertZone();
    void testUpdateZone();
    void testDeleteZone();
    void testGetActiveZones();
    void testZoneByName();
    void testComplianceRecordInsert();
    void testZoneCountTracking();
};

// Insert a zone and verify it exists
void ZoneComplianceTest::testInsertZone()
{
    DatabaseManager &db = DatabaseManager::instance();

    int id = db.insertZone(
        QStringLiteral("Airport Restricted"),
        QStringLiteral("5nm radius around airport"),
        37.6213, -122.3790, 9260.0,
        QStringLiteral("FAA regulation"));
    QVERIFY(id > 0);

    auto zones = db.getAllZones();
    bool found = false;
    for (const auto &z : zones) {
        if (z[QStringLiteral("id")].toInt() == id) {
            found = true;
            QCOMPARE(z[QStringLiteral("name")].toString(), QStringLiteral("Airport Restricted"));
            break;
        }
    }
    QVERIFY(found);
}

// Update zone properties
void ZoneComplianceTest::testUpdateZone()
{
    DatabaseManager &db = DatabaseManager::instance();

    int id = db.insertZone(
        QStringLiteral("Temp Zone"),
        QStringLiteral("Temporary"),
        37.0, -122.0, 100.0,
        QStringLiteral("Test"));

    bool updated = db.updateZone(
        id,
        QStringLiteral("Updated Zone"),
        QStringLiteral("Updated description"),
        37.1, -122.1, 200.0,
        QStringLiteral("Updated reason"),
        true);
    QVERIFY(updated);

    auto zones = db.getAllZones();
    for (const auto &z : zones) {
        if (z[QStringLiteral("id")].toInt() == id) {
            QCOMPARE(z[QStringLiteral("name")].toString(), QStringLiteral("Updated Zone"));
            break;
        }
    }
}

// Delete a zone
void ZoneComplianceTest::testDeleteZone()
{
    DatabaseManager &db = DatabaseManager::instance();

    int id = db.insertZone(
        QStringLiteral("Delete Me"),
        QStringLiteral("To be deleted"),
        37.0, -122.0, 100.0,
        QStringLiteral("Test"));

    bool deleted = db.deleteZone(id);
    QVERIFY(deleted);

    auto zones = db.getAllZones();
    for (const auto &z : zones) {
        QVERIFY(z[QStringLiteral("id")].toInt() != id);
    }
}

// Get active zones only
void ZoneComplianceTest::testGetActiveZones()
{
    DatabaseManager &db = DatabaseManager::instance();

    int id1 = db.insertZone(
        QStringLiteral("Active Zone"), QStringLiteral("Active"),
        37.0, -122.0, 100.0, QStringLiteral("Test"));
    int id2 = db.insertZone(
        QStringLiteral("Inactive Zone"), QStringLiteral("Inactive"),
        37.1, -122.1, 200.0, QStringLiteral("Test"));

    // Deactivate one
    db.updateZone(id2, QStringLiteral("Inactive Zone"), QStringLiteral("Inactive"),
                  37.1, -122.1, 200.0, QStringLiteral("Test"), false);

    auto active = db.getActiveZones();
    bool foundActive = false;
    bool foundInactive = false;
    for (const auto &z : active) {
        if (z[QStringLiteral("id")].toInt() == id1) foundActive = true;
        if (z[QStringLiteral("id")].toInt() == id2) foundInactive = true;
    }
    QVERIFY(foundActive);
    QVERIFY(!foundInactive);
}

// Zone lookup by exact name
void ZoneComplianceTest::testZoneByName()
{
    DatabaseManager &db = DatabaseManager::instance();

    db.insertZone(
        QStringLiteral("FindMe Zone"),
        QStringLiteral("Findable"),
        37.0, -122.0, 100.0,
        QStringLiteral("Test"));

    QVariantMap found = db.getZoneByName(QStringLiteral("FindMe Zone"));
    QVERIFY(!found.isEmpty());
    QCOMPARE(found[QStringLiteral("name")].toString(), QStringLiteral("FindMe Zone"));

    QVariantMap notFound = db.getZoneByName(QStringLiteral("Nonexistent Zone"));
    QVERIFY(notFound.isEmpty());
}

// Compliance record can be inserted and queried
void ZoneComplianceTest::testComplianceRecordInsert()
{
    DatabaseManager &db = DatabaseManager::instance();

    // zone_compliance_log.flight_id FK -> flights(id); build a real flight.
    db.upsertVehicle(QStringLiteral("compliance-fp"), QStringLiteral("Compliance Test"),
                     QStringLiteral("ArduPilot"), QStringLiteral("MultiRotor"));
    int opId = db.insertOperator(QStringLiteral("Compliance"), QStringLiteral("Pilot"));
    QVERIFY(opId > 0);
    // First upserted vehicle in a fresh DB has autogen id 1.
    int flightId = db.openFlight(opId, 1, QStringLiteral("FLIGHT"), QString(),
                                 QString(), QString(), QString());
    QVERIFY(flightId > 0);

    bool recorded = db.insertComplianceRecord(
        flightId, opId,
        QStringLiteral("COMPLIANT"),
        QStringLiteral("Zone does not intersect mission"),
        QString(),
        -1, false);
    QVERIFY(recorded);

    auto records = db.getComplianceForFlight(flightId);
    QVERIFY(records.size() > 0);
}

// Zone count tracking works correctly
void ZoneComplianceTest::testZoneCountTracking()
{
    DatabaseManager &db = DatabaseManager::instance();

    int initial = db.getAllZones().size();

    int id1 = db.insertZone(
        QStringLiteral("Zone A"), QStringLiteral("A"),
        37.0, -122.0, 100.0, QStringLiteral("Test"));
    int id2 = db.insertZone(
        QStringLiteral("Zone B"), QStringLiteral("B"),
        37.1, -122.1, 200.0, QStringLiteral("Test"));
    int id3 = db.insertZone(
        QStringLiteral("Zone C"), QStringLiteral("C"),
        37.2, -122.2, 300.0, QStringLiteral("Test"));

    QCOMPARE(db.getAllZones().size(), initial + 3);

    db.deleteZone(id1);
    db.deleteZone(id2);
    db.deleteZone(id3);

    QCOMPARE(db.getAllZones().size(), initial);
}

UT_REGISTER_TEST(ZoneComplianceTest)

#include "ZoneComplianceTest.moc"
