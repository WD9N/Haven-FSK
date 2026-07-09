#pragma once
#include <vector>
#include <string>
#include <memory>

// IModem — abstraction boundary between DspPipeline (Qt-facing RX/TX glue)
// and a concrete mode implementation (MfskModem, Psk31Modem, ...).
// Interface itself is Qt-free (std:: only) so it can be consulted/tested
// independent of Qt. See DECISIONS.md for the ADR covering this split.

namespace HavenFSK {

enum class ModemMode { Mfsk16, Psk31 };

// RX state, mode-agnostic. "Collecting" covers any in-progress decode
// (MFSK: frame collection after preamble; PSK31: mid-word varicode stream).
enum class ModemRxState { Idle, Collecting };

// One decode event from processAudioChunk(). MFSK emits at most one
// hasMessage=true event per completed frame; PSK31 (continuous character
// stream) may emit several per chunk, or events with hasMessage=false and
// only progress/preamble fields set, so callers should inspect flags
// rather than assume every returned event carries a full message.
struct ModemRxEvent {
    bool        hasMessage    = false;
    std::string text;
    bool        crcOk         = false;   // MFSK only; unused (false) for PSK31 (no CRC)
    bool        converged     = false;   // MFSK only (FEC); unused for PSK31
    int         nBlocks       = 0;       // MFSK only
    int         fecIterations = 0;       // 0 for PSK31

    // true (default): `text` is a complete, discrete decoded message
    // (MFSK: one full frame) — DspPipeline emits messageReceived() and
    // the UI shows it as its own timestamped row with CRC/FEC badges.
    // false: `text` is a fragment of a continuous character stream
    // (PSK31: one or a few decoded characters at a time, with no CRC/FEC
    // concept at all) — DspPipeline emits textCharacterReceived() instead,
    // and the UI appends it in place as flowing text with no per-fragment
    // timestamp or badges. crcOk/converged/nBlocks/fecIterations are
    // meaningless and ignored when this is false.
    bool        isFramedMessage = true;

    // Optional live-progress info, mirrors what the pre-refactor
    // DspPipeline exposed via preambleDetected()/rxProgress() signals.
    bool        preambleDetected = false;
    float       preambleScore    = 0.0f;
    int         symbolsReceived  = 0;
    int         symbolsExpected  = 0;    // 0 if unknown/not applicable
};

// Optional per-mode construction parameters. Fields not applicable to a
// given mode are ignored by that mode's factory case.
struct ModemConfig {
    double pskBaudRate = 31.25;  // PSK31/63/125
    int    pskVariant   = 0;     // 0 = BPSK, 1 = QPSK (Phase 3 stretch goal)
};

class IModem {
public:
    virtual ~IModem() = default;

    // ── TX ──────────────────────────────────────────────────────────────
    virtual std::vector<float> modulateText(const std::string& text) = 0;

    // ── RX ──────────────────────────────────────────────────────────────
    // Feed one chunk of audio (already RX-gain-corrected by the caller).
    // Returns zero or more events accumulated while processing this chunk.
    virtual std::vector<ModemRxEvent> processAudioChunk(
        const std::vector<float>& samples) = 0;

    virtual void resetRx() = 0;

    // Suspend RX decode state-machine processing (e.g. while this station
    // is transmitting — half-duplex, avoid self-interference confusing
    // preamble/sync search). Advisory diagnostics (DCD, tone monitor) may
    // still run during suspension, matching pre-refactor DspPipeline
    // behavior where those ran before the m_transmitting gate. Default
    // no-op for modes that don't need it.
    virtual void setRxSuspended(bool) {}

    // ── Live status (polled by DspPipeline after each chunk) ──────────────
    virtual ModemRxState rxState()  const = 0;
    virtual bool         dcdActive() const = 0;
    virtual float        lastSnrDb() const = 0;

    // ── AFC ─────────────────────────────────────────────────────────────
    virtual void  setAfcEnabled(bool enabled) = 0;
    virtual bool  afcEnabled()  const = 0;
    virtual float afcOffsetHz() const = 0;

    // ── Squelch — optional, default no-op/0 for modes without a concept
    //    of a confidence-based decode squelch (e.g. MFSK relies on its
    //    own CRC/FEC convergence instead). Meaning of the 0.0-1.0 range
    //    is mode-specific; UI should treat 0.0 as "squelch off". ────────
    virtual void  setSquelchThreshold(float) {}
    virtual float squelchThreshold() const { return 0.0f; }

    // ── Tune audio — steady tone for adjusting TX level into the radio.
    //    Default empty for modes without a natural tune tone. ──────────────
    virtual std::vector<float> generateTuneAudio() const { return {}; }

    // ── Identification / UI passband hint ──────────────────────────────
    virtual ModemMode  mode()     const = 0;
    virtual std::string modeName() const = 0;
    virtual double passbandLowHz()  const = 0;
    virtual double passbandHighHz() const = 0;
};

std::unique_ptr<IModem> createModem(ModemMode mode, const ModemConfig& cfg = {});

} // namespace HavenFSK
