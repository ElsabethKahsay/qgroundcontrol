#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class OperatorManager : public QObject
{
    Q_OBJECT
    QML_SINGLETON

    Q_PROPERTY(QVariantList operators
               READ operators
               NOTIFY operatorsChanged)

    Q_PROPERTY(int currentOperatorId
               READ currentOperatorId
               NOTIFY currentOperatorChanged)

    Q_PROPERTY(QString currentOperatorName
               READ currentOperatorName
               NOTIFY currentOperatorChanged)

    Q_PROPERTY(QString currentOperatorRole
               READ currentOperatorRole
               NOTIFY currentOperatorChanged)

    Q_PROPERTY(bool hasCurrentOperator
               READ hasCurrentOperator
               NOTIFY currentOperatorChanged)

public:
    static OperatorManager *instance();
    explicit OperatorManager(QObject* parent = nullptr);

    QVariantList operators()          const { return m_operators; }
    int          currentOperatorId()  const { return m_currentId; }
    QString      currentOperatorName()const { return m_currentName; }
    QString      currentOperatorRole()const { return m_currentRole; }
    bool         hasCurrentOperator() const { return m_currentId > 0; }

    Q_INVOKABLE void loadOperators();
    Q_INVOKABLE int addOperator(const QString& name, const QString& role);
    Q_INVOKABLE void selectOperator(int operatorId);
    Q_INVOKABLE void selectByName(const QString& name);
    Q_INVOKABLE void clearCurrentOperator();
    Q_INVOKABLE QVariantMap getOperatorById(int operatorId);

signals:
    void operatorsChanged();
    void currentOperatorChanged();

private:
    void _loadFromSettings();

    QVariantList m_operators;
    int          m_currentId   = -1;
    QString      m_currentName;
    QString      m_currentRole;
};
