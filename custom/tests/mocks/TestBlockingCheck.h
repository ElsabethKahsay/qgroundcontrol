#pragma once

#include "AbstractCheck.h"

/// Test-only blocking check that can be forced to fail/succeed deterministically.
/// Used by multiple test suites to drive the ArmingGate into a blocked state.
class TestBlockingCheck : public AbstractCheck {
    Q_OBJECT
public:
    explicit TestBlockingCheck(const QString& id, CheckType type = CheckType::Auto,
                               bool mandatory = true, QObject* parent = nullptr);
    void evaluate() override;
    void forceStatus(CheckStatus s, const QString& msg = {});
};