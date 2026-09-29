#include "TestBlockingCheck.h"

TestBlockingCheck::TestBlockingCheck(const QString& id, CheckType type,
                                     bool mandatory, QObject* parent)
    : AbstractCheck(id, QStringLiteral("Test ") + id,
                    CheckCategory::Safety, type, mandatory, true, parent) {}

void TestBlockingCheck::evaluate() {}

void TestBlockingCheck::forceStatus(CheckStatus s, const QString& msg) {
    setStatus(s, msg);
}