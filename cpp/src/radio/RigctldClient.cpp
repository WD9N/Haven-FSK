#include "RigctldClient.h"
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

RigctldClient::RigctldClient(const QString& host,
                               uint16_t port,
                               QObject* parent)
    : RadioInterface(parent)
    , m_host(host)
    , m_port(port)
{
    m_socket    = new QTcpSocket(this);
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);

    QObject::connect(m_socket, &QTcpSocket::connected,
                     this, &RigctldClient::onConnected);
    QObject::connect(m_socket, &QTcpSocket::disconnected,
                     this, &RigctldClient::onDisconnected);
    QObject::connect(m_socket, &QAbstractSocket::errorOccurred,
                     this, &RigctldClient::onError);
    QObject::connect(m_pollTimer, &QTimer::timeout,
                     this, &RigctldClient::onPollTimer);
}

RigctldClient::~RigctldClient() {
    disconnect();
}

bool RigctldClient::connect() {
    if (m_connected) return true;

    m_socket->connectToHost(m_host, m_port);
    if (!m_socket->waitForConnected(CONNECT_TIMEOUT_MS)) {
        QString msg = QString("rigctld: cannot connect to %1:%2 — %3")
                      .arg(m_host).arg(m_port)
                      .arg(m_socket->errorString());
        qWarning() << msg;
        emit rigError(msg);
        return false;
    }
    return true;
}

void RigctldClient::disconnect() {
    m_pollTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        m_socket->waitForDisconnected(1000);
    }
    m_connected = false;
}

void RigctldClient::onConnected() {
    m_connected = true;
    m_pollTimer->start();
    qDebug() << "RigctldClient: connected to" << m_host << ":" << m_port;
    emit connected();
}

void RigctldClient::onDisconnected() {
    m_connected = false;
    m_pollTimer->stop();
    qDebug() << "RigctldClient: disconnected";
    emit disconnected();
}

void RigctldClient::onError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error)
    emit rigError(QString("rigctld error: %1").arg(m_socket->errorString()));
}

void RigctldClient::onPollTimer() {
    uint64_t hz = getFrequency();
    if (hz > 0) emit frequencyChanged(hz);
}

QString RigctldClient::sendCommand(const QString& cmd, int timeoutMs) {
    if (!m_connected) return QString();

    m_socket->write(cmd.toUtf8());
    if (!m_socket->waitForBytesWritten(timeoutMs)) return QString();
    if (!m_socket->waitForReadyRead(timeoutMs))    return QString();

    QByteArray response;
    while (m_socket->bytesAvailable())
        response += m_socket->readAll();

    return QString::fromUtf8(response).trimmed();
}

bool RigctldClient::setPTT(bool active) {
    if (!m_connected) return false;
    QString cmd  = active ? "T 1\n" : "T 0\n";
    QString resp = sendCommand(cmd);
    bool ok = resp.contains("RPRT 0");
    if (!ok)
        qWarning() << "RigctldClient: PTT" << (active ? "ON" : "OFF")
                   << "failed:" << resp;
    emit pttChanged(active);
    return ok;
}

uint64_t RigctldClient::getFrequency() {
    if (!m_connected) return 0;
    QString resp = sendCommand("f\n");
    bool ok = false;
    uint64_t hz = resp.toULongLong(&ok);
    return ok ? hz : 0;
}

bool RigctldClient::setFrequency(uint64_t hz) {
    if (!m_connected) return false;
    QString resp = sendCommand(QString("F %1\n").arg(hz));
    return resp.contains("RPRT 0");
}

bool RigctldClient::setMode(const QString& mode) {
    if (!m_connected) return false;
    // rigctld mode command: M {mode} {passband_hz}, 0 = radio default
    QString resp = sendCommand(QString("M %1 0\n").arg(mode));
    return resp.contains("RPRT 0");
}

QString RigctldClient::getMode() {
    if (!m_connected) return QString();
    QString resp = sendCommand("m\n");
    // Response: mode\npassband\n
    return resp.split('\n').first().trimmed();
}

bool RigctldClient::setSplit(bool enable, uint64_t txHz) {
    Q_UNUSED(enable) Q_UNUSED(txHz)
    return false;  // Stub — implemented in future phase
}
