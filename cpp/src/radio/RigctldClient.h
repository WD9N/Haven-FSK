#pragma once
#include "RadioInterface.h"
#include <QTcpSocket>
#include <QTimer>
#include <QString>
#include <cstdint>

// RigctldClient connects to a running rigctld instance over TCP.
// rigctld is part of the Hamlib project and acts as a universal
// radio control server covering hundreds of radio models.
//
// Default connection: localhost:4532 (rigctld universal default)
// Configurable in Settings → Radio Control
//
// PTT commands: "T 1\n" = TX, "T 0\n" = RX
// Frequency get: "f\n" returns Hz as ASCII integer
// Frequency set: "F {hz}\n"
//
// rigctld must be running and connected to the radio before
// HAVEN-FSK attempts to connect.

class RigctldClient : public RadioInterface
{
    Q_OBJECT
public:
    explicit RigctldClient(const QString& host = "localhost",
                           uint16_t port = 4532,
                           QObject* parent = nullptr);
    ~RigctldClient() override;

    // ── RadioInterface implementation ─────────────────────────────────────
    bool     connect()           override;
    void     disconnect()        override;
    bool     isConnected() const override { return m_connected; }
    QString  rigName()     const override { return "rigctld"; }

    bool     setPTT(bool active)          override;
    uint64_t getFrequency()               override;
    bool     setFrequency(uint64_t hz)    override;
    void     requestFrequency()           override;
    bool     setMode(const QString& mode) override;
    QString  getMode()                    override;
    bool     setSplit(bool enable, uint64_t txHz = 0) override;

    // ── Configuration ─────────────────────────────────────────────────────
    void     setHost(const QString& host) { m_host = host; }
    void     setPort(uint16_t port)       { m_port = port; }
    QString  host() const { return m_host; }
    uint16_t port() const { return m_port; }

private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void onPollTimer();

private:
    // Send a command and wait for response (blocking with timeout)
    // Returns response string or empty string on error
    QString sendCommand(const QString& cmd, int timeoutMs = 2000);

    QString     m_host;
    uint16_t    m_port;
    QTcpSocket* m_socket    {nullptr};
    bool        m_connected {false};
    QTimer*     m_pollTimer {nullptr};  // polls frequency every 2s

    static constexpr int POLL_INTERVAL_MS    = 2000;
    static constexpr int CONNECT_TIMEOUT_MS  = 5000;
};
