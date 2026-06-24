#pragma once
#include <QObject>
#include <QString>
#include <cstdint>

class RadioInterface : public QObject
{
    Q_OBJECT
public:
    explicit RadioInterface(QObject* parent = nullptr)
        : QObject(parent) {}
    virtual ~RadioInterface() override = default;

    // ── Connection ────────────────────────────────────────────────────────
    virtual bool connect()          = 0;
    virtual void disconnect()       = 0;
    virtual bool isConnected() const = 0;

    // Human-readable name for UI display (e.g. "rigctld", "Thetis TCI")
    virtual QString rigName() const = 0;

    // ── PTT ──────────────────────────────────────────────────────────────
    // Key or unkey the transmitter. Returns false if not connected.
    virtual bool setPTT(bool active) = 0;

    // ── Frequency ─────────────────────────────────────────────────────────
    // Get current VFO A frequency in Hz. Returns 0 if not connected.
    virtual uint64_t getFrequency() = 0;

    // Set VFO A frequency in Hz. Returns false if not connected.
    virtual bool setFrequency(uint64_t hz) = 0;

    // Request current frequency from radio — emits frequencyChanged().
    // Default no-op; implementations query the radio and emit the signal.
    // MainWindow calls this immediately after connection is established.
    virtual void requestFrequency() {}

    // ── Mode ──────────────────────────────────────────────────────────────
    // Set operating mode (e.g. "USB", "LSB", "DIGU", "DIGL").
    virtual bool setMode(const QString& mode) = 0;

    // Get current mode string. Returns empty string if not connected.
    virtual QString getMode() = 0;

    // ── Split ─────────────────────────────────────────────────────────────
    // Enable split operation. txHz = 0 means use current VFO B.
    virtual bool setSplit(bool enable, uint64_t txHz = 0) = 0;

signals:
    void connected();
    void disconnected();
    void pttChanged(bool active);

    // Emitted when radio reports a frequency change
    void frequencyChanged(uint64_t hz);

    // Emitted when radio reports a mode change
    void modeChanged(const QString& mode);

    // Emitted on any rig control error
    void rigError(const QString& message);
};
