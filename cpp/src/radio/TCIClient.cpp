#include "TCIClient.h"
#include <QDebug>
#include <QUrl>

TCIClient::TCIClient(const QString& host,
                      uint16_t port,
                      QObject* parent)
    : RadioInterface(parent)
    , m_host(host)
    , m_port(port)
{
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_reconnTimer = new QTimer(this);
    m_reconnTimer->setInterval(RECONNECT_INTERVAL_MS);
    m_reconnTimer->setSingleShot(false);

    QObject::connect(m_socket, &QWebSocket::connected,
                     this, &TCIClient::onConnected);
    QObject::connect(m_socket, &QWebSocket::disconnected,
                     this, &TCIClient::onDisconnected);
    QObject::connect(m_socket, &QWebSocket::textMessageReceived,
                     this, &TCIClient::onTextMessageReceived);
    QObject::connect(m_socket, &QWebSocket::errorOccurred,
                     this, &TCIClient::onError);
    QObject::connect(m_reconnTimer, &QTimer::timeout,
                     this, &TCIClient::onReconnectTimer);
}

TCIClient::~TCIClient() {
    disconnect();
}

bool TCIClient::connect() {
    if (m_connected) return true;
    m_inInit = true;
    m_ready  = false;
    QString url = QString("ws://%1:%2").arg(m_host).arg(m_port);
    qDebug() << "TCIClient: connecting to" << url;
    m_socket->open(QUrl(url));
    return true;  // Connection is async — monitor connected() signal
}

void TCIClient::disconnect() {
    m_reconnTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->close();
    m_connected = false;
    m_ready     = false;
}

void TCIClient::onConnected() {
    qDebug() << "TCIClient: WebSocket connected";
    // Wait for server init block — do not send anything until "ready;"
}

void TCIClient::onDisconnected() {
    m_connected = false;
    m_ready     = false;
    qDebug() << "TCIClient: disconnected — will retry in"
             << RECONNECT_INTERVAL_MS / 1000 << "seconds";
    emit disconnected();
    m_reconnTimer->start();
}

void TCIClient::onReconnectTimer() {
    // Fix 7: only reconnect if socket is not already in use
    if (!m_connected && !m_socket->isValid()) {
        qDebug() << "TCIClient: attempting reconnect";
        connect();
    }
}

void TCIClient::onError(QAbstractSocket::SocketError error) {
    // Fix 7: ignore OperationError during initial connect — it fires
    // before the socket is established and is not a real failure.
    if (!m_connected && error == QAbstractSocket::OperationError) return;

    emit rigError(QString("TCI WebSocket error: %1")
                  .arg(m_socket->errorString()));
}

void TCIClient::onTextMessageReceived(const QString& message) {
    // TCI messages are semicolon-separated, may arrive in one frame
    QStringList cmds = message.split(';', Qt::SkipEmptyParts);
    for (const QString& cmd : cmds)
        parseTCIMessage(cmd.trimmed());
}

void TCIClient::parseTCIMessage(const QString& msg) {
    if (msg.isEmpty()) return;

    // ── Handshake / init block ────────────────────────────────────────────
    if (m_inInit) {
        if (msg.startsWith("protocol:")) {
            m_protocolVer = msg.mid(9);
            qDebug() << "TCIClient: protocol version:" << m_protocolVer;
        } else if (msg.startsWith("device:")) {
            m_deviceName = msg.mid(7);
            qDebug() << "TCIClient: device:" << m_deviceName;
        } else if (msg == "ready") {
            m_inInit    = false;
            m_connected = true;
            m_reconnTimer->stop();
            qDebug() << "TCIClient: ready — connected to"
                     << m_deviceName << "protocol" << m_protocolVer;
            emit connected();
        }
        return;
    }

    // ── Runtime messages ──────────────────────────────────────────────────

    // Frequency: vfo:0,0,{hz}  (trx, channel, frequency)
    if (msg.startsWith("vfo:")) {
        QStringList parts = msg.mid(4).split(',');
        if (parts.size() >= 3 && parts[0] == "0" && parts[1] == "0") {
            bool ok = false;
            uint64_t hz = parts[2].toULongLong(&ok);
            if (ok) {
                m_frequency = hz;
                emit frequencyChanged(hz);
            }
        }
        return;
    }

    // Mode: modulation:0,USB
    if (msg.startsWith("modulation:")) {
        QStringList parts = msg.mid(11).split(',');
        if (parts.size() >= 2 && parts[0] == "0") {
            m_mode = parts[1];
            emit modeChanged(m_mode);
        }
        return;
    }

    // PTT state echo: trx:0,true or trx:0,false
    if (msg.startsWith("trx:")) {
        QStringList parts = msg.mid(4).split(',');
        if (parts.size() >= 2 && parts[0] == "0") {
            bool active = (parts[1] == "true");
            emit pttChanged(active);
        }
        return;
    }

    // All other messages silently ignored per TCI specification
}

void TCIClient::sendTCI(const QString& cmd) {
    if (!m_ready) {
        qWarning() << "TCIClient: sendTCI called before ready:" << cmd;
        return;
    }
    m_socket->sendTextMessage(cmd);
}

bool TCIClient::setPTT(bool active) {
    if (!m_connected) return false;
    sendTCI(active ? "trx:0,true;" : "trx:0,false;");
    return true;
}

uint64_t TCIClient::getFrequency() {
    return m_frequency;  // Returns last value from server push
}

bool TCIClient::setFrequency(uint64_t hz) {
    if (!m_connected) return false;
    sendTCI(QString("vfo:0,0,%1;").arg(hz));
    return true;
}

bool TCIClient::setMode(const QString& mode) {
    if (!m_connected) return false;
    sendTCI(QString("modulation:0,%1;").arg(mode));
    return true;
}

QString TCIClient::getMode() {
    return m_mode;  // Returns last value from server push
}

bool TCIClient::setSplit(bool enable, uint64_t txHz) {
    Q_UNUSED(enable) Q_UNUSED(txHz)
    return false;  // Stub — implemented in future phase
}

QString TCIClient::rigName() const {
    if (!m_deviceName.isEmpty())
        return QString("TCI: %1").arg(m_deviceName);
    return QString("TCI (%1:%2)").arg(m_host).arg(m_port);
}
