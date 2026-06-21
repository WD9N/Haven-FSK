#pragma once
#include "RadioInterface.h"
#include <QWebSocket>
#include <QTimer>
#include <QString>
#include <cstdint>

// TCIClient implements the TCI (Transceiver Control Interface) protocol
// over WebSocket. TCI is used by Thetis/OpenHPSDR (Hermes Lite 2, ANAN),
// ExpertSDR2/3 (SunSDR series), and other SDR platforms.
//
// Default connection: localhost:50001 (standard Thetis default)
// Configurable in Settings → Radio Control
//
// TCI protocol: server sends init block ending with "ready;"
// PTT: "trx:0,true;" / "trx:0,false;"
// Frequency: parsed from "vfo:0,0,{hz};" server messages
//
// Protocol version is parsed from handshake for diagnostics.
// Core PTT and frequency commands are identical across all TCI
// versions from 1.5 onwards — no version branching needed.
//
// Unknown server messages are silently ignored per TCI specification,
// ensuring forward compatibility with future TCI versions.

class TCIClient : public RadioInterface
{
    Q_OBJECT
public:
    explicit TCIClient(const QString& host = "localhost",
                       uint16_t port = 50001,
                       QObject* parent = nullptr);
    ~TCIClient() override;

    // ── RadioInterface implementation ─────────────────────────────────────
    bool     connect()           override;
    void     disconnect()        override;
    bool     isConnected() const override { return m_connected; }
    QString  rigName()     const override;

    bool     setPTT(bool active)          override;
    uint64_t getFrequency()               override;
    bool     setFrequency(uint64_t hz)    override;
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
    void onTextMessageReceived(const QString& message);
    void onError(QAbstractSocket::SocketError error);
    void onReconnectTimer();

private:
    void sendTCI(const QString& cmd);
    void parseTCIMessage(const QString& msg);

    QString     m_host;
    uint16_t    m_port;
    QWebSocket* m_socket      {nullptr};
    bool        m_connected   {false};
    bool        m_ready       {false};   // true after "ready;" received
    QString     m_protocolVer;           // from handshake, for diagnostics
    QString     m_deviceName;            // from handshake
    uint64_t    m_frequency   {0};       // last known VFO A frequency
    QString     m_mode;                  // last known mode
    QTimer*     m_reconnTimer {nullptr};

    // Buffer for init block parsing
    bool        m_inInit {true};

    static constexpr int RECONNECT_INTERVAL_MS = 10000;
};
