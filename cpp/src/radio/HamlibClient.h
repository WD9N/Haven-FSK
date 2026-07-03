#pragma once
#include "RadioInterface.h"
#include <QTimer>
#include <QVector>
#include <cstdint>

// HamlibClient — direct libhamlib linking for CAT control (e.g. a
// Kenwood TS-590SG over USB/serial) without requiring a separately-
// launched rigctld process. See DECISIONS.md for the ADR; this extends
// rather than replaces RigctldClient (TCP to external rigctld) and
// TCIClient (WebSocket to SDR software) — pick whichever fits your setup
// in Radio -> Configure.
//
// Header stays Hamlib-free (only src/radio/HamlibClient.cpp includes
// <hamlib/rig.h>) so the rest of the app never needs to know whether
// this build was compiled with Hamlib support. Call isAvailable() before
// relying on real behavior — if the Hamlib SDK wasn't found at build
// time (HAVEN_ENABLE_HAMLIB), every method degrades to stub behavior
// (return false/empty, emit rigError()) rather than failing to compile
// or link.
//
// Mirrors RigctldClient's shape (reconnect/backoff fields, poll cadence)
// since it's the most recently hardened RadioInterface implementation in
// this codebase — see RigctldClient.h/.cpp for the sibling pattern.

// Hamlib's actual struct tag is s_rig (typedef struct s_rig RIG;),
// confirmed against include/hamlib/rig.h — not the more obvious guess
// "struct rig" (a real bug this forward declaration had until then).
struct s_rig;
typedef struct s_rig RIG;

class HamlibClient : public RadioInterface
{
    Q_OBJECT
public:
    // portPath: e.g. "COM5" (Windows) or "/dev/ttyUSB0" (Linux/Pi).
    // rigModel: Hamlib's numeric rig_model_t ID — see availableRigs().
    explicit HamlibClient(const QString& portPath,
                          int baudRate,
                          int rigModel,
                          QObject* parent = nullptr);
    ~HamlibClient() override;

    // ── RadioInterface implementation ─────────────────────────────────────
    bool     connect()           override;
    void     disconnect()        override;
    bool     isConnected() const override { return m_connected; }
    QString  rigName()     const override;

    bool     setPTT(bool active)          override;
    uint64_t getFrequency()               override;
    bool     setFrequency(uint64_t hz)    override;
    void     requestFrequency()           override;
    bool     setMode(const QString& mode) override;
    QString  getMode()                    override;
    bool     setSplit(bool enable, uint64_t txHz = 0) override;
    float    getPowerLevel()              override;
    bool     setPowerLevel(float level0to1) override;

    // ── Configuration ─────────────────────────────────────────────────────
    void     setPortPath(const QString& path) { m_portPath = path; }
    void     setBaudRate(int baud)             { m_baudRate = baud; }
    void     setRigModel(int model)            { m_rigModel = model; }
    QString  portPath()  const { return m_portPath; }
    int      baudRate()  const { return m_baudRate; }
    int      rigModel()  const { return m_rigModel; }

    // ── Rig enumeration (for UI) ────────────────────────────────────────────
    struct HamlibRigInfo {
        int     model;
        QString mfgName;
        QString modelName;
    };
    // Lazily loads and caches Hamlib's compiled-in rig list on first call
    // (calls rig_load_all_backends() + rig_list_foreach() once). Returns
    // an empty list if this build wasn't compiled with Hamlib support.
    static const QVector<HamlibRigInfo>& availableRigs();

    // True iff this build was compiled with HAVEN_HAMLIB_ENABLED (i.e.
    // the Hamlib SDK was found at build time). If false, every instance
    // method degrades to stub behavior — check this before surfacing the
    // "Direct/Hamlib" option as usable in the UI.
    static bool isAvailable();

private slots:
    void onPollTimer();
    void onReconnectTimer();

private:
    void scheduleReconnect();

    QString  m_portPath;
    int      m_baudRate;
    int      m_rigModel;
    RIG*     m_rig       {nullptr};
    bool     m_connected {false};

    QTimer*  m_pollTimer {nullptr};  // polls frequency every 2s, mode every 5th tick
    int      m_pollTick  {0};
    QString  m_lastMode;

    // Reconnect/retry with exponential backoff — same convention as
    // RigctldClient. Note: unlike RigctldClient's non-blocking
    // QTcpSocket::connectToHost(), Hamlib has no async connect — each
    // reconnect attempt briefly blocks on serial I/O (bounded by
    // Hamlib's own serial timeout), same accepted tradeoff as this
    // class's poll-timer reads.
    QTimer*  m_reconnectTimer   {nullptr};
    int      m_reconnectAttempt {0};
    bool     m_userDisconnected {false};

    static constexpr int POLL_INTERVAL_MS        = 2000;
    static constexpr int MODE_POLL_EVERY_N_TICKS = 5;   // ~10s
    static constexpr int RECONNECT_MIN_MS        = 1000;
    static constexpr int RECONNECT_MAX_MS        = 30000;
};
