#pragma once
#include <QObject>
#include <QTimer>

class RadioInterface;

class PTTManager : public QObject
{
    Q_OBJECT
public:
    explicit PTTManager(RadioInterface *radio, QObject *parent = nullptr);
    ~PTTManager() override;

    bool txOn();
    void txOff();
    bool isTransmitting() const { return m_transmitting; }

signals:
    void txStarted();
    void txStopped();
    void watchdogTripped();

private slots:
    void onWatchdog();

private:
    RadioInterface *m_radio{nullptr};
    QTimer          m_watchdog;
    bool            m_transmitting{false};
};
