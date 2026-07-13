#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariant>

struct MockParameterManager : public QObject {
    Q_OBJECT
public:
    explicit MockParameterManager(QObject *parent = nullptr)
        : QObject(parent) {}

    void setParameter(const QString &name, float value) {
        m_params[name] = value;
        emit parameterChanged(name, value);
    }

    float getParameter(const QString &name, float defaultValue = 0.0f) const {
        return m_params.value(name, defaultValue);
    }

    bool hasParameter(const QString &name) const {
        return m_params.contains(name);
    }

    void clear() { m_params.clear(); }

signals:
    void parameterChanged(const QString &name, float value);

private:
    QHash<QString, float> m_params;
};
