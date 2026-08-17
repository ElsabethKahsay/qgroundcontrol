#pragma once

#include <QTest>

#include "UnitTest.h"

/// Smoke test for AbstractCheck lifecycle, ManualConfirmCheck, ChecklistItemModel,
/// and ChecklistEngine. Does not require a Vehicle or TelemetryBridge.
class CheckSmokeTest : public UnitTest {
    Q_OBJECT

private slots:
    void init() override { UnitTest::init(); }
    void cleanup() override { UnitTest::cleanup(); }

    // AbstractCheck lifecycle
    void testCheckInitialState();
    void testCheckStatusTransitions();
    void testCheckOverrideMechanism();
    void testCheckCannotOverrideAuto();

    // ManualConfirmCheck
    void testManualConfirmDefault();
    void testManualConfirmFlow();
    void testManualInspectionItems();

    // ChecklistItemModel
    void testModelLoadFromJson();
    void testModelSetItemStatus();
    void testModelReset();

    // ChecklistEngine
    void testEngineAllPassedDetection();
};
