#include "RigctldClient.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <algorithm>

RigctldClient::RigctldClient(const QString& host,
                               uint16_t port,
                               QObject* parent)
    : RadioInterface(parent)
    , m_host(host)
    , m_port(port)
{
    m_socket         = new QTcpSocket(this);
    m_pollTimer      = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);

    QObject::connect(m_socket, &QTcpSocket::connected,
                     this, &RigctldClient::onConnected);
    QObject::connect(m_socket, &QTcpSocket::disconnected,
                     this, &RigctldClient::onDisconnected);
    QObject::connect(m_socket, &QAbstractSocket::errorOccurred,
                     this, &RigctldClient::onError);
    QObject::connect(m_pollTimer, &QTimer::timeout,
                     this, &RigctldClient::onPollTimer);
    QObject::connect(m_reconnectTimer, &QTimer::timeout,
                     this, &RigctldClient::onReconnectTimer);
}

RigctldClient::~RigctldClient() {
    disconnect();
}

bool RigctldClient::connect() {
    m_userDisconnected = false;
    if (m_connected) return true;

    m_socket->connectToHost(m_host, m_port);
    if (!m_socket->waitForConnected(CONNECT_TIMEOUT_MS)) {
        QString msg = QString("rigctld: cannot connect to %1:%2 — %3")
                      .arg(m_host).arg(m_port)
                      .arg(m_socket->errorString());
        qWarning() << msg;
        emit rigError(msg);
        scheduleReconnect();
        return false;
    }
    return true;
}

void RigctldClient::disconnect() {
    m_userDisconnected = true;
    m_reconnectTimer->stop();
    m_reconnectAttempt = 0;
    m_pollTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        m_socket->waitForDisconnected(1000);
    }
    m_connected = false;
}

void RigctldClient::onConnected() {
    m_connected = true;
    m_everConnected = true;
    m_reconnectAttempt = 0;
    m_reconnectTimer->stop();
    m_pollTick = 0;
    m_lastMode.clear();
    m_pollTimer->start();
    qDebug() << "RigctldClient: connected to" << m_host << ":" << m_port;
    emit connected();
    // Query immediately — don't wait for first poll timer tick (2s)
    requestFrequency();
}

void RigctldClient::onDisconnected() {
    m_connected = false;
    m_pollTimer->stop();
    qDebug() << "RigctldClient: disconnected";
    emit disconnected();
    if (!m_userDisconnected)
        scheduleReconnect();
}

void RigctldClient::onError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error)
    emit rigError(QString("rigctld error: %1").arg(m_socket->errorString()));
    if (!m_userDisconnected && !m_connected)
        scheduleReconnect();
}

void RigctldClient::scheduleReconnect() {
    if (m_userDisconnected) return;

    // A connection that has never succeeded gets a bounded number of
    // attempts, then gives up cleanly instead of retrying forever — a bad
    // host/port is a configuration error, not a transient outage. A
    // connection that WAS working and later drops keeps retrying
    // indefinitely below (m_everConnected true skips this).
    if (!m_everConnected && m_reconnectAttempt >= MAX_INITIAL_CONNECT_ATTEMPTS) {
        QString msg = QString(
            "rigctld: giving up after %1 failed attempts to connect to "
            "%2:%3 — check host/port in Radio -> Configure")
            .arg(m_reconnectAttempt).arg(m_host).arg(m_port);
        qWarning() << msg;
        m_reconnectAttempt = 0;  // so a later manual connect() starts fresh
        emit connectFailed(msg);
        return;
    }

    int shift = std::min(m_reconnectAttempt, 10);  // avoid shift overflow on long outages
    int delayMs = std::min(RECONNECT_MIN_MS << shift, RECONNECT_MAX_MS);
    m_reconnectAttempt++;
    qDebug() << "RigctldClient: reconnecting in" << delayMs << "ms (attempt"
             << m_reconnectAttempt << ")";
    m_reconnectTimer->start(delayMs);
}

void RigctldClient::onReconnectTimer() {
    if (m_userDisconnected || m_connected) return;
    qDebug() << "RigctldClient: attempting reconnect";
    m_socket->connectToHost(m_host, m_port);
    // Let onConnected()/onError() drive success/failure and any further
    // backoff — do not block here with waitForConnected().
}

void RigctldClient::onPollTimer() {
    uint64_t hz = getFrequency();
    if (hz > 0) emit frequencyChanged(hz);

    if (++m_pollTick >= MODE_POLL_EVERY_N_TICKS) {
        m_pollTick = 0;
        QString mode = getMode();
        if (!mode.isEmpty() && mode != m_lastMode) {
            m_lastMode = mode;
            emit modeChanged(mode);
        }
    }
}

// A reply is complete once it has expectedLines newline-terminated lines,
// or once its last complete line is "RPRT n" — the terminal line of every
// set-command reply and every error reply, regardless of expectedLines.
static bool replyComplete(const QByteArray& response, int expectedLines) {
    if (response.count('\n') >= expectedLines) return true;
    if (response.endsWith('\n')) {
        int lastLineStart =
            response.lastIndexOf('\n', response.size() - 2) + 1;
        if (response.mid(lastLineStart).startsWith("RPRT")) return true;
    }
    return false;
}

QString RigctldClient::sendCommand(const QString& cmd, int timeoutMs,
                                    int expectedLines) {
    if (!m_connected) return QString();

    // Discard stale bytes from an earlier command that timed out. Without
    // this, a late reply is prepended to the next command's response and
    // gets misattributed — e.g. a stale "RPRT 0" from a frequency poll
    // read as a successful PTT command.
    if (m_socket->bytesAvailable() > 0) {
        QByteArray stale = m_socket->readAll();
        qWarning() << "RigctldClient: discarded" << stale.size()
                   << "stale reply bytes:" << stale.trimmed();
    }

    m_socket->write(cmd.toUtf8());
    if (!m_socket->waitForBytesWritten(timeoutMs)) return QString();

    // Read until the reply is structurally complete, not just until the
    // first readyRead — a reply can arrive split across TCP segments, and
    // taking the first burst truncates it (the two-line "m" reply being
    // the likely victim).
    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (!replyComplete(response, expectedLines)) {
        qint64 remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0 ||
            !m_socket->waitForReadyRead(static_cast<int>(remaining)))
            break;  // timeout — partial/empty response reads as failure
        response += m_socket->readAll();
    }

    return QString::fromUtf8(response).trimmed();
}

bool RigctldClient::setPTT(bool active) {
    if (!m_connected) return false;
    QString cmd  = active ? "T 1\n" : "T 0\n";
    QString resp = sendCommand(cmd);
    bool ok = resp.contains("RPRT 0");
    if (!ok) {
        qWarning() << "RigctldClient: PTT" << (active ? "ON" : "OFF")
                   << "failed:" << resp;
        return false;
    }
    emit pttChanged(active);
    return true;
}

uint64_t RigctldClient::getFrequency() {
    if (!m_connected) return 0;
    QString resp = sendCommand("f\n");
    bool ok = false;
    uint64_t hz = resp.toULongLong(&ok);
    return ok ? hz : 0;
}

void RigctldClient::requestFrequency() {
    if (!m_connected) return;
    qDebug() << "RigctldClient: requestFrequency";
    uint64_t hz = getFrequency();
    if (hz > 0) emit frequencyChanged(hz);
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
    // Response: mode\npassband\n — two lines
    QString resp = sendCommand("m\n", 2000, /*expectedLines=*/2);
    return resp.split('\n').first().trimmed();
}

bool RigctldClient::setSplit(bool enable, uint64_t txHz) {
    if (!m_connected) return false;
    // rigctld split command: S {0|1} {vfo}. TX VFO is assumed to be
    // VFOB, matching the interface doc comment's "txHz = 0 means use
    // current VFO B" convention.
    QString cmd = QString("S %1 %2\n").arg(enable ? 1 : 0).arg(enable ? "VFOB" : "VFOA");
    QString resp = sendCommand(cmd);
    if (!resp.contains("RPRT 0")) {
        qWarning() << "RigctldClient: setSplit(" << enable << ") failed:" << resp;
        return false;
    }

    if (enable && txHz != 0) {
        // rigctld split TX frequency command: I {hz}
        QString freqResp = sendCommand(QString("I %1\n").arg(txHz));
        if (!freqResp.contains("RPRT 0")) {
            qWarning() << "RigctldClient: setSplit TX frequency failed:" << freqResp;
            return false;
        }
    }
    return true;
}

float RigctldClient::getPowerLevel() {
    if (!m_connected) return -1.0f;
    // rigctld level get command: l {LEVEL_NAME} — response is a single
    // float line (e.g. "0.500000") normalized to the radio's max power.
    QString resp = sendCommand("l RFPOWER\n");
    bool ok = false;
    float level = resp.split('\n').first().trimmed().toFloat(&ok);
    return ok ? level : -1.0f;
}

bool RigctldClient::setPowerLevel(float level0to1) {
    if (!m_connected) return false;
    QString resp = sendCommand(QString("L RFPOWER %1\n").arg(level0to1, 0, 'f', 3));
    return resp.contains("RPRT 0");
}
