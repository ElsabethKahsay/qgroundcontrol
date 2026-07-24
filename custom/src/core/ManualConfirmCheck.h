// Generic manual confirmation check that displays a prompt for the user to verify.
// Optionally appends parameter values to the prompt; stays pending until confirmed.
#pragma once
#include "AbstractCheck.h"

class ManualConfirmCheck : public AbstractCheck {
    Q_OBJECT
public:
    ManualConfirmCheck(const QString &id, const QString &label,
                       CheckCategory category,
                       const QString &prompt,
                       const QStringList &paramNames = {},
                       QObject *parent = nullptr);
    void evaluate() override;

private:
    QString m_prompt;
    QStringList m_paramNames;
};
