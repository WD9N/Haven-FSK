#include "PTTManager.h"
#include "RadioInterface.h"
#include "dsp/Constants.h"

PTTManager::PTTManager(RadioInterface *radio, QObject *parent)
    : QObject(parent), m_radio(radio)
{
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(HavenFSK::PTT_WATCHDOG_SEC * 1000);
    QObject::connect(&m_watchdog, &QTimer::timeout, this, &PTTManager::onWatchdog);
}

PTTManager::~PTTManager()
{
    if (m_transmitting) txOff();
}

bool PTTManager::txOn()
{
    if (m_transmitting || !m_radio) return false;
    if (!m_radio->setPTT(true)) return false;
    m_transmitting = true;
    m_watchdog.start();
    emit txStarted();
    return true;
}

void PTTManager::txOff()
{
    m_watchdog.stop();
    if (m_radio) m_radio->setPTT(false);
    m_transmitting = false;
    emit txStopped();
}

void PTTManager::onWatchdog()
{
    txOff();
    emit watchdogTripped();
}
