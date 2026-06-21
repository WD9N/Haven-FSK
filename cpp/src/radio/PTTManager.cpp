#include "PTTManager.h"
#include "RadioInterface.h"
#include "../dsp/Constants.h"
#include <QDebug>
#include <random>
#include <chrono>

PTTManager::PTTManager(RadioInterface* radio, QObject* parent)
    : QObject(parent)
    , m_radio(radio)
    , m_rng(static_cast<uint32_t>(
          std::chrono::steady_clock::now().time_since_epoch().count()))
{
    // Watchdog timer — trips after PTT_WATCHDOG_SEC (120s) continuous TX
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(HavenFSK::PTT_WATCHDOG_SEC * 1000);
    QObject::connect(&m_watchdog, &QTimer::timeout,
                     this, &PTTManager::onWatchdog);

    // Backoff timer — fires when pre-TX random delay expires
    m_backoffTimer.setSingleShot(true);
    QObject::connect(&m_backoffTimer, &QTimer::timeout,
                     this, &PTTManager::onBackoffComplete);
}

PTTManager::~PTTManager() {
    txOff();
}

void PTTManager::setOperatingMode(HavenFSK::OperatingMode mode) {
    m_mode = mode;
    qDebug() << "PTTManager: operating mode ="
             << (mode == HavenFSK::OperatingMode::Activator
                 ? "Activator" : "Standard");
}

int PTTManager::calculateBackoff(bool isCQ) const {
    // Three-tier backoff per ADR-015:
    // CQ = 0ms (operator has checked frequency)
    // Activator = 0-50ms random
    // Standard = 50-300ms random (floor prevents collision with activator)

    if (isCQ)
        return HavenFSK::BACKOFF_CQ_MS;

    int minMs, maxMs;
    if (m_mode == HavenFSK::OperatingMode::Activator) {
        minMs = HavenFSK::BACKOFF_ACTIVATOR_MIN_MS;
        maxMs = HavenFSK::BACKOFF_ACTIVATOR_MAX_MS;
    } else {
        minMs = HavenFSK::BACKOFF_STANDARD_MIN_MS;
        maxMs = HavenFSK::BACKOFF_STANDARD_MAX_MS;
    }

    if (minMs >= maxMs) return minMs;
    std::uniform_int_distribution<int> dist(minMs, maxMs);
    return dist(const_cast<std::mt19937&>(m_rng));
}

bool PTTManager::requestTX(bool isCQ, bool dcdActive) {
    if (m_transmitting) {
        qWarning() << "PTTManager: requestTX called while transmitting";
        return false;
    }

    if (dcdActive) {
        qDebug() << "PTTManager: channel busy — TX denied";
        emit channelBusy();
        return false;
    }

    int backoffMs = calculateBackoff(isCQ);

    if (backoffMs <= 0) {
        onBackoffComplete();
    } else {
        qDebug() << "PTTManager: backoff" << backoffMs << "ms";
        emit backoffStarted(backoffMs);
        m_backoffTimer.start(backoffMs);
    }
    return true;
}

void PTTManager::onBackoffComplete() {
    if (m_radio && m_radio->isConnected())
        m_radio->setPTT(true);
    m_transmitting = true;
    m_watchdog.start();
    qDebug() << "PTTManager: TX on";
    emit txStarted();
}

void PTTManager::txOff() {
    m_backoffTimer.stop();
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
