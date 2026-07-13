#pragma once

#include <QObject>
#include <QString>

struct MockVehicle : public QObject {
    Q_OBJECT
public:
    explicit MockVehicle(uint8_t sysId = 1, QObject *parent = nullptr)
        : QObject(parent), m_sysId(sysId) {}

    uint8_t id() const { return m_sysId; }
    bool isConnected() const { return m_connected; }

    void setConnected(bool v) { m_connected = v; }

    uint8_t m_sysId = 1;
    bool m_connected = false;
};
