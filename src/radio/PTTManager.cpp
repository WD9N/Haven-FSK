#include "PTTManager.h"
#include "RadioInterface.h"
#include "../dsp/Constants.h"
#include <QDebug>

PTTManager::PTTManager(RadioInterface* radio, QObject* parent)
    : QObject(parent)
    , m_radio(radio)
{
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(HavenFSK::PTT_WATCHDOG_SEC * 1000);
    QObject::connect(&m_watchdog, &QTimer::timeout,
                     this, &PTTManager::onWatchdog);
}

PTTManager::~PTTManager() {
    txOff();
}

bool PTTManager::requestTX() {
    if (m_transmitting) {
        qWarning() << "PTTManager: requestTX called while already transmitting";
        return false;
    }

    if (m_radio && m_radio->isConnected())
        m_radio->setPTT(true);

    m_transmitting = true;
    m_watchdog.start();
    qDebug() << "PTTManager: TX on";
    emit txStarted();
    return true;
}

void PTTManager::txOff() {
    m_watchdog.stop();

    if (m_radio && m_radio->isConnected())
        m_radio->setPTT(false);

    if (m_transmitting) {
        m_transmitting = false;
        qDebug() << "PTTManager: TX off";
        emit txStopped();
    }
}

void PTTManager::onWatchdog() {
    qWarning() << "PTTManager: TX watchdog tripped at"
               << HavenFSK::PTT_WATCHDOG_SEC << "seconds";
    txOff();
    emit watchdogTripped();
}
