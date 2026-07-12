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

// Static per-mode capability declarations (ADR-134 / ROADMAP Phase 2).
// UI and logging adapt to these instead of special-casing mode names or
// enum values — a new mode states what it can do and the platform
// responds; no call site should ever need editing to add a mode.
struct ModemCapabilities {
    // Discrete, CRC-checked frames: RX presents one timestamped row per
    // message with CRC/FEC badges, and per-message parsing (sender ID,
    // auto-logging) applies. false = continuous character stream (text
    // flows in place; only shape-heuristic links and manual "Log as"
    // assignment apply).
    bool framedMessages = false;

    // Carries ADR-133 inline field markers: the TX editor serializes
    // tagged spans into 0x1F/0x1E markers, and RX parses them for
    // auto-logging. Must never be true for a mode whose character set
    // cannot carry control bytes transparently (e.g. varicode).
    bool inlineMarkers = false;

    // A sender callsign can be attributed to each decoded message
    // (marker-declared or heuristic). Requires framedMessages.
    bool senderIdentification = false;
};

constexpr ModemCapabilities modemCapabilities(ModemMode mode) {
    switch (mode) {
        case ModemMode::Mfsk16: return {true,  true,  true };
        case ModemMode::Psk31:  return {false, false, false};
    }
    return {};
}

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

    // Adaptive sync-threshold notice (MFSK false-lock defense): when the
    // modem adjusts its preamble detection threshold it reports the new
    // value and why, so the UI can keep the operator informed. reason
    // empty = no adjustment in this event.
    float       syncThresholdNow = 0.0f;
    std::string syncAdjustReason;
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
    ModemCapabilities capabilities() const { return modemCapabilities(mode()); }
    virtual double passbandLowHz()  const = 0;
    virtual double passbandHighHz() const = 0;
};

std::unique_ptr<IModem> createModem(ModemMode mode, const ModemConfig& cfg = {});

} // namespace HavenFSK
