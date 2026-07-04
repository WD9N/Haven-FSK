#include "HamlibClient.h"
#include <QDebug>

#ifdef HAVEN_HAMLIB_ENABLED

#include <hamlib/rig.h>
#include <QMetaObject>
#include <QThreadPool>
#include <algorithm>

// Verified against Hamlib's actual include/hamlib/rig.h this session
// (not assumed from memory) — see DECISIONS.md ADR for citation. Key
// facts relied on below:
//  - rig_set_freq/rig_set_ptt/rig_set_mode/rig_set_split_vfo/
//    rig_set_split_freq/rig_set_level/rig_get_level all take/return
//    plain int error codes (RIG_OK == 0 on success).
//  - Port path and serial rate are set via rig_set_conf() with tokens
//    looked up by name ("rig_pathname", "serial_speed") rather than by
//    poking rig_state struct fields directly — the token API is stable
//    across Hamlib versions, raw struct field layout is not.
//  - RIG_LEVEL_RFPOWER's value_t is a float in [0.0, 1.0] — matches
//    RadioInterface::getPowerLevel()/setPowerLevel()'s existing
//    normalized-0-1 convention exactly (same convention RigctldClient
//    already uses against rigctld's RFPOWER level).
//  - rig_strrmode()/rig_parse_mode() convert between Hamlib's rmode_t
//    and the same plain mode strings ("USB", "PKTUSB", etc.) that
//    RigctldClient's rigctld text protocol already uses — no separate
//    string convention needed here.
//  - rig_load_all_backends() must be called once before rig_list_foreach()
//    will see anything beyond whatever backends happen to already be
//    loaded (confirmed via header comment at rig_load_all_backends()'s
//    declaration).

HamlibClient::HamlibClient(const QString& portPath, int baudRate,
                            int rigModel, QObject* parent)
    : RadioInterface(parent)
    , m_portPath(portPath)
    , m_baudRate(baudRate)
    , m_rigModel(rigModel)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);

    QObject::connect(m_pollTimer, &QTimer::timeout,
                     this, &HamlibClient::onPollTimer);
    QObject::connect(m_reconnectTimer, &QTimer::timeout,
                     this, &HamlibClient::onReconnectTimer);
}

HamlibClient::~HamlibClient() {
    disconnect();
}

QString HamlibClient::rigName() const {
    if (!m_rig) return "Hamlib";
    return QString("Hamlib: %1").arg(m_portPath);
}

bool HamlibClient::connect() {
    m_userDisconnected = false;
    if (m_connected) return true;
    if (m_connectInProgress) return true;  // attempt already in flight

    m_connectInProgress = true;
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);

    // rig_init/rig_open block on serial I/O — run them on a worker thread
    // so a bad/nonexistent COM port can never freeze the UI (or, worse,
    // application startup itself: MainWindow's constructor calls
    // startRadio() before the window is shown). Only primitive values and
    // the cancellation flag are captured; `self` is used solely as the
    // context object for the queued invoke back onto the main thread —
    // it is never dereferenced on the worker thread, so it's safe even if
    // this HamlibClient is destroyed while the attempt is in flight (see
    // applyConnectResult()/disconnect()/~HamlibClient()).
    QString portPath  = m_portPath;
    int     baudRate  = m_baudRate;
    int     rigModel  = m_rigModel;
    auto    cancelFlag = m_cancelFlag;
    HamlibClient* self = this;

    QThreadPool::globalInstance()->start([portPath, baudRate, rigModel,
                                           cancelFlag, self]() {
        ConnectResult result;

        RIG* rig = rig_init(static_cast<rig_model_t>(rigModel));
        if (!rig) {
            result.errorMessage =
                QString("Hamlib: rig_init failed for model %1").arg(rigModel);
        } else {
            // Configure port path and baud via the token API (stable
            // across Hamlib versions) before opening, rather than writing
            // rig_state struct fields directly.
            hamlib_token_t pathToken = rig_token_lookup(rig, "rig_pathname");
            hamlib_token_t rateToken = rig_token_lookup(rig, "serial_speed");
            rig_set_conf(rig, pathToken, portPath.toUtf8().constData());
            rig_set_conf(rig, rateToken,
                         QByteArray::number(baudRate).constData());

            int err = rig_open(rig);
            if (err != RIG_OK) {
                result.errorMessage = QString("Hamlib: cannot open %1 — %2")
                                       .arg(portPath, rigerror(err));
                rig_cleanup(rig);
            } else {
                result.ok  = true;
                result.rig = rig;
            }
        }

        if (cancelFlag->load()) {
            // disconnect() or ~HamlibClient() ran while this attempt was
            // in flight — clean up here rather than touching a possibly-
            // destroyed `self`.
            if (result.ok && result.rig) {
                rig_close(result.rig);
                rig_cleanup(result.rig);
            }
            return;
        }

        // Context-object overload: if `self` has been destroyed by the
        // time this is delivered, Qt simply drops the call rather than
        // invoking it on a dangling pointer.
        QMetaObject::invokeMethod(self, [self, result]() {
            self->applyConnectResult(result);
        }, Qt::QueuedConnection);
    });

    return true;
}

void HamlibClient::applyConnectResult(const ConnectResult& result) {
    m_connectInProgress = false;

    if (m_userDisconnected) {
        // Disconnected while this attempt was in flight — discard
        // whatever it found rather than silently connecting anyway.
        if (result.ok && result.rig) {
            rig_close(result.rig);
            rig_cleanup(result.rig);
        }
        return;
    }

    if (!result.ok) {
        qWarning() << result.errorMessage;
        emit rigError(result.errorMessage);
        scheduleReconnect();
        return;
    }

    m_rig = result.rig;
    m_connected = true;
    m_everConnected = true;
    m_reconnectAttempt = 0;
    m_reconnectTimer->stop();
    m_pollTick = 0;
    m_lastMode.clear();
    m_pollTimer->start();
    qDebug() << "HamlibClient: connected to" << m_portPath
             << "at" << m_baudRate << "baud, model" << m_rigModel;
    emit connected();
    requestFrequency();
}

void HamlibClient::disconnect() {
    m_userDisconnected = true;
    if (m_cancelFlag) m_cancelFlag->store(true);
    m_connectInProgress = false;
    m_reconnectTimer->stop();
    m_reconnectAttempt = 0;
    m_pollTimer->stop();

    bool wasConnected = m_connected;
    if (m_rig) {
        rig_close(m_rig);
        rig_cleanup(m_rig);
        m_rig = nullptr;
    }
    m_connected = false;

    // Unlike RigctldClient's QTcpSocket, Hamlib has no async
    // disconnected() signal to rely on — emit it directly.
    if (wasConnected) {
        qDebug() << "HamlibClient: disconnected";
        emit disconnected();
    }
}

void HamlibClient::scheduleReconnect() {
    if (m_userDisconnected) return;

    // A connection that has never succeeded gets a bounded number of
    // attempts, then gives up cleanly instead of retrying forever — a bad
    // COM port/rig model is a configuration error, not a transient
    // outage. A connection that WAS working and later drops keeps
    // retrying indefinitely below (m_everConnected true skips this).
    if (!m_everConnected && m_reconnectAttempt >= MAX_INITIAL_CONNECT_ATTEMPTS) {
        QString msg = QString(
            "Hamlib: giving up after %1 failed attempts to open %2 — "
            "check COM port and rig model in Radio -> Configure")
            .arg(m_reconnectAttempt).arg(m_portPath);
        qWarning() << msg;
        m_reconnectAttempt = 0;  // so a later manual connect() starts fresh
        emit connectFailed(msg);
        return;
    }

    int shift = std::min(m_reconnectAttempt, 10);
    int delayMs = std::min(RECONNECT_MIN_MS << shift, RECONNECT_MAX_MS);
    m_reconnectAttempt++;
    qDebug() << "HamlibClient: reconnecting in" << delayMs << "ms (attempt"
             << m_reconnectAttempt << ")";
    m_reconnectTimer->start(delayMs);
}

void HamlibClient::onReconnectTimer() {
    if (m_userDisconnected || m_connected || m_connectInProgress) return;
    qDebug() << "HamlibClient: attempting reconnect";
    connect();
}

bool HamlibClient::setPTT(bool active) {
    if (!m_connected) return false;
    int err = rig_set_ptt(m_rig, RIG_VFO_CURR,
                           active ? RIG_PTT_ON : RIG_PTT_OFF);
    if (err != RIG_OK) {
        qWarning() << "HamlibClient: setPTT failed:" << rigerror(err);
        return false;
    }
    emit pttChanged(active);
    return true;
}

uint64_t HamlibClient::getFrequency() {
    if (!m_connected) return 0;
    freq_t freq = 0;
    int err = rig_get_freq(m_rig, RIG_VFO_CURR, &freq);
    if (err != RIG_OK) return 0;
    return static_cast<uint64_t>(freq);
}

bool HamlibClient::setFrequency(uint64_t hz) {
    if (!m_connected) return false;
    int err = rig_set_freq(m_rig, RIG_VFO_CURR, static_cast<freq_t>(hz));
    if (err != RIG_OK) {
        qWarning() << "HamlibClient: setFrequency failed:" << rigerror(err);
        return false;
    }
    return true;
}

void HamlibClient::requestFrequency() {
    if (!m_connected) return;
    uint64_t hz = getFrequency();
    if (hz > 0) emit frequencyChanged(hz);
}

bool HamlibClient::setMode(const QString& mode) {
    if (!m_connected) return false;
    rmode_t rmode = rig_parse_mode(mode.toUtf8().constData());
    if (rmode == RIG_MODE_NONE) {
        qWarning() << "HamlibClient: unrecognized mode string:" << mode;
        return false;
    }
    int err = rig_set_mode(m_rig, RIG_VFO_CURR, rmode, RIG_PASSBAND_NORMAL);
    if (err != RIG_OK) {
        qWarning() << "HamlibClient: setMode failed:" << rigerror(err);
        return false;
    }
    return true;
}

QString HamlibClient::getMode() {
    if (!m_connected) return QString();
    rmode_t rmode = RIG_MODE_NONE;
    pbwidth_t width = 0;
    int err = rig_get_mode(m_rig, RIG_VFO_CURR, &rmode, &width);
    if (err != RIG_OK || rmode == RIG_MODE_NONE) return QString();
    return QString::fromUtf8(rig_strrmode(rmode));
}

bool HamlibClient::setSplit(bool enable, uint64_t txHz) {
    if (!m_connected) return false;
    int err = rig_set_split_vfo(m_rig, RIG_VFO_CURR,
                                 enable ? RIG_SPLIT_ON : RIG_SPLIT_OFF,
                                 RIG_VFO_B);
    if (err != RIG_OK) {
        qWarning() << "HamlibClient: setSplit failed:" << rigerror(err);
        return false;
    }
    if (enable && txHz != 0) {
        err = rig_set_split_freq(m_rig, RIG_VFO_CURR,
                                  static_cast<freq_t>(txHz));
        if (err != RIG_OK) {
            qWarning() << "HamlibClient: setSplit TX frequency failed:"
                       << rigerror(err);
            return false;
        }
    }
    return true;
}

float HamlibClient::getPowerLevel() {
    if (!m_connected) return -1.0f;
    value_t val;
    int err = rig_get_level(m_rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, &val);
    if (err != RIG_OK) return -1.0f;
    return val.f;
}

bool HamlibClient::setPowerLevel(float level0to1) {
    if (!m_connected) return false;
    value_t val;
    val.f = level0to1;
    int err = rig_set_level(m_rig, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, val);
    if (err != RIG_OK) {
        qWarning() << "HamlibClient: setPowerLevel failed:" << rigerror(err);
        return false;
    }
    return true;
}

void HamlibClient::onPollTimer() {
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

// ── Rig enumeration ─────────────────────────────────────────────────────

static QVector<HamlibClient::HamlibRigInfo> g_rigCache;
static bool g_rigCacheLoaded = false;

static int collectRigCaps(const struct rig_caps* caps, rig_ptr_t) {
    HamlibClient::HamlibRigInfo info;
    info.model     = caps->rig_model;
    info.mfgName   = QString::fromUtf8(caps->mfg_name);
    info.modelName = QString::fromUtf8(caps->model_name);
    g_rigCache.push_back(info);
    return 1;  // continue enumeration
}

const QVector<HamlibClient::HamlibRigInfo>& HamlibClient::availableRigs() {
    if (!g_rigCacheLoaded) {
        rig_load_all_backends();
        rig_list_foreach(collectRigCaps, nullptr);
        std::sort(g_rigCache.begin(), g_rigCache.end(),
                  [](const HamlibRigInfo& a, const HamlibRigInfo& b) {
                      if (a.mfgName != b.mfgName) return a.mfgName < b.mfgName;
                      return a.modelName < b.modelName;
                  });
        g_rigCacheLoaded = true;
        qDebug() << "HamlibClient: enumerated" << g_rigCache.size()
                 << "supported rig models";
    }
    return g_rigCache;
}

bool HamlibClient::isAvailable() { return true; }

#else  // !HAVEN_HAMLIB_ENABLED — stub: this build has no Hamlib SDK linked

HamlibClient::HamlibClient(const QString& portPath, int baudRate,
                            int rigModel, QObject* parent)
    : RadioInterface(parent)
    , m_portPath(portPath)
    , m_baudRate(baudRate)
    , m_rigModel(rigModel)
{
}

HamlibClient::~HamlibClient() = default;

QString HamlibClient::rigName() const { return "Hamlib (not compiled in)"; }

bool HamlibClient::connect() {
    emit rigError("Hamlib support not compiled into this build");
    return false;
}
void HamlibClient::disconnect() {}
bool HamlibClient::setPTT(bool)                    { return false; }
uint64_t HamlibClient::getFrequency()               { return 0; }
bool HamlibClient::setFrequency(uint64_t)           { return false; }
void HamlibClient::requestFrequency()               {}
bool HamlibClient::setMode(const QString&)          { return false; }
QString HamlibClient::getMode()                     { return {}; }
bool HamlibClient::setSplit(bool, uint64_t)         { return false; }
float HamlibClient::getPowerLevel()                 { return -1.0f; }
bool HamlibClient::setPowerLevel(float)             { return false; }
void HamlibClient::onPollTimer()      {}
void HamlibClient::onReconnectTimer() {}
void HamlibClient::scheduleReconnect() {}

const QVector<HamlibClient::HamlibRigInfo>& HamlibClient::availableRigs() {
    static const QVector<HamlibRigInfo> empty;
    return empty;
}

bool HamlibClient::isAvailable() { return false; }

#endif  // HAVEN_HAMLIB_ENABLED
