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
    // txOff() only performs (blocking) socket I/O when the rig is
    // believed keyed (m_pttKeyed) — a normal idle shutdown does nothing
    // here, and a shutdown mid-TX deliberately blocks to unkey the rig
    // rather than leave it transmitting.
    txOff();
}

bool PTTManager::requestTX() {
    if (m_transmitting) {
        qWarning() << "PTTManager: requestTX called while already transmitting";
        return false;
    }

    if (m_radio && m_radio->isConnected()) {
        if (!m_radio->setPTT(true)) {
            qWarning() << "PTTManager: PTT key-up failed — refusing to start TX";
            return false;
        }
        m_pttKeyed = true;
    }

    m_transmitting = true;
    m_watchdog.start();
    qDebug() << "PTTManager: TX on";
    emit txStarted();
    return true;
}

void PTTManager::txOff() {
    m_watchdog.stop();

    // Gate on m_pttKeyed, not isConnected() — see the member doc comment.
    if (m_pttKeyed && m_radio) {
        bool released = false;
        for (int attempt = 1; attempt <= UNKEY_ATTEMPTS && !released; ++attempt) {
            released = m_radio->setPTT(false);
            if (!released)
                qWarning() << "PTTManager: unkey attempt" << attempt
                           << "of" << UNKEY_ATTEMPTS << "failed";
        }
        if (released) {
            m_pttKeyed = false;
        } else {
            qCritical() << "PTTManager: could not confirm PTT release —"
                        << "rig may still be keyed";
            emit pttReleaseFailed();
        }
    }

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
