#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <random>

class RadioInterface;

namespace HavenFSK {

// Operating mode — determines which backoff tier is used
enum class OperatingMode {
    Standard,   // 50-300ms random backoff
    Activator,  // 0-50ms random backoff (POTA/SOTA reference set)
};

} // namespace HavenFSK

class PTTManager : public QObject
{
    Q_OBJECT
public:
    explicit PTTManager(RadioInterface* radio,
                        QObject* parent = nullptr);
    ~PTTManager() override;

    // ── TX control ────────────────────────────────────────────────────────
    // Request TX. Applies backoff logic before keying.
    // isCQ: true if TX message contains "CQ" — bypasses backoff entirely.
    // dcdActive: current DCD state — if true, channel is busy.
    // Returns false immediately if channel busy or already transmitting.
    bool requestTX(bool isCQ, bool dcdActive);

    // Unkey transmitter immediately.
    void txOff();

    bool isTransmitting() const { return m_transmitting; }

    // ── Operating mode ────────────────────────────────────────────────────
    void setOperatingMode(HavenFSK::OperatingMode mode);
    HavenFSK::OperatingMode operatingMode() const { return m_mode; }

signals:
    void txStarted();
    void txStopped();
    void watchdogTripped();
    void channelBusy();           // emitted when requestTX finds DCD active
    void backoffStarted(int ms);  // emitted with actual backoff duration

private slots:
    void onWatchdog();
    void onBackoffComplete();

private:
    int calculateBackoff(bool isCQ) const;

    RadioInterface*          m_radio        {nullptr};
    QTimer                   m_watchdog;
    QTimer                   m_backoffTimer;
    bool                     m_transmitting {false};
    HavenFSK::OperatingMode  m_mode         {HavenFSK::OperatingMode::Standard};
    std::mt19937             m_rng;
};
