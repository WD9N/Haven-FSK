#include "HamlibClient.h"
#include <QDebug>

#ifdef HAVEN_HAMLIB_ENABLED

#include <hamlib/rig.h>
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

    m_rig = rig_init(static_cast<rig_model_t>(m_rigModel));
    if (!m_rig) {
        QString msg = QString("Hamlib: rig_init failed for model %1")
                      .arg(m_rigModel);
        qWarning() << msg;
        emit rigError(msg);
        scheduleReconnect();
        return false;
    }

    // Configure port path and baud via the token API (stable across
    // Hamlib versions) before opening, rather than writing rig_state
    // struct fields directly.
    hamlib_token_t pathToken = rig_token_lookup(m_rig, "rig_pathname");
    hamlib_token_t rateToken = rig_token_lookup(m_rig, "serial_speed");
    rig_set_conf(m_rig, pathToken, m_portPath.toUtf8().constData());
    rig_set_conf(m_rig, rateToken,
                 QByteArray::number(m_baudRate).constData());

    int err = rig_open(m_rig);
    if (err != RIG_OK) {
        QString msg = QString("Hamlib: cannot open %1 — %2")
                      .arg(m_portPath, rigerror(err));
        qWarning() << msg;
        emit rigError(msg);
        rig_cleanup(m_rig);
        m_rig = nullptr;
        scheduleReconnect();
        return false;
    }

    m_connected = true;
    m_reconnectAttempt = 0;
    m_reconnectTimer->stop();
    m_pollTick = 0;
    m_lastMode.clear();
    m_pollTimer->start();
    qDebug() << "HamlibClient: connected to" << m_portPath
             << "at" << m_baudRate << "baud, model" << m_rigModel;
    emit connected();
    requestFrequency();
    return true;
}

void HamlibClient::disconnect() {
    m_userDisconnected = true;
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
    int shift = std::min(m_reconnectAttempt, 10);
    int delayMs = std::min(RECONNECT_MIN_MS << shift, RECONNECT_MAX_MS);
    m_reconnectAttempt++;
    qDebug() << "HamlibClient: reconnecting in" << delayMs << "ms (attempt"
             << m_reconnectAttempt << ")";
    m_reconnectTimer->start(delayMs);
}

void HamlibClient::onReconnectTimer() {
    if (m_userDisconnected || m_connected) return;
    qDebug() << "HamlibClient: attempting reconnect";
    // Blocking — Hamlib has no non-blocking-connect equivalent to
    // QTcpSocket::connectToHost(); bounded by Hamlib's own serial
    // timeout and capped reconnect interval (30s), same accepted
    // tradeoff as this class's poll-timer reads.
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
