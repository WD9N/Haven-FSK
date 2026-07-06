#pragma once
#include <QObject>
#include <QTimer>

class RadioInterface;

class PTTManager : public QObject
{
    Q_OBJECT
public:
    explicit PTTManager(RadioInterface* radio,
                        QObject* parent = nullptr);
    ~PTTManager() override;

    // Key PTT and start watchdog. Returns false if already transmitting
    // or if the rig rejected the key-up command — a failed key-up is
    // fatal to the transmission (callers must not start TX audio into an
    // unkeyed rig).
    bool requestTX();

    // Unkey transmitter immediately. Retries a failed unkey command and
    // emits pttReleaseFailed() if the rig could not be confirmed unkeyed
    // — the one PTT failure that must never pass silently.
    void txOff();

    bool isTransmitting() const { return m_transmitting; }

signals:
    void txStarted();
    void txStopped();
    void watchdogTripped();

    // The unkey command could not be delivered/confirmed after retries:
    // the rig may still be keyed and transmitting. UI must surface this
    // loudly (modal alarm), not as a status-bar footnote.
    void pttReleaseFailed();

private slots:
    void onWatchdog();

private:
    RadioInterface* m_radio        {nullptr};
    QTimer          m_watchdog;
    bool            m_transmitting {false};

    // True while WE believe the rig is keyed (setPTT(true) succeeded and
    // no successful unkey since). Gates the unkey path independently of
    // isConnected(): if the connection dropped mid-TX the rig may still
    // be keyed, and a racing auto-reconnect may let the command through —
    // so a keyed rig always gets the unkey attempt. Stays true after a
    // failed release so any later txOff() retries.
    bool            m_pttKeyed     {false};

    // Total unkey attempts before declaring failure. Attempts against a
    // dropped connection fail instantly; against a hung-but-connected
    // socket each can block up to the sendCommand timeout (~2s) on the
    // GUI thread — acceptable for the emergency-unkey path only.
    static constexpr int UNKEY_ATTEMPTS = 3;
};
