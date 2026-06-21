#pragma once
#include <QObject>
#include <QString>

class RadioInterface : public QObject
{
    Q_OBJECT
public:
    explicit RadioInterface(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~RadioInterface() override = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool setPTT(bool active) = 0;
    virtual bool isConnected() const = 0;

signals:
    void connected();
    void disconnected();
    void pttChanged(bool active);
};
