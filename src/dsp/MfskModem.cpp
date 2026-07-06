#include "MfskModem.h"
#include "DspLog.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HavenFSK {

MfskModem::MfskModem() {
    m_rxBuffer.reserve(SAMPLE_RATE * 5);  // pre-allocate 5 seconds

    // Demodulator self-test: score a locally-generated preamble.
    {
        auto preambleAudio = m_preamble.generate();
        auto softSyms = m_demodulator.demodulateToSoft(preambleAudio, 0);
        float score = m_preamble.softCorrelate(softSyms, 0);
        dspLog("MfskModem: demod self-test score=%.3f %s", score,
               score >= 0.5f ? "[PASS]" : "[FAIL — demodulator bug]");
        if (score < 0.5f) {
            std::string got;
            for (int i = 0; i < PREAMBLE_LENGTH && i < (int)softSyms.size(); ++i) {
                const auto& e = softSyms[i];
                int best = (int)(std::max_element(e.begin(), e.end()) - e.begin());
                got += std::to_string(best) + " ";
            }
            dspLog("  self-test got : %s", got.c_str());
            dspLog("  self-test want: 0 15 0 15 7 8 7 8 0 15 0 15 7 8 7 8");
        }
    }
}

// ── TX ────────────────────────────────────────────────────────────────────

std::vector<float> MfskModem::modulateText(const std::string& text) {
    return m_frame.assemble(text);
}

// ── RX ────────────────────────────────────────────────────────────────────

std::vector<ModemRxEvent> MfskModem::processAudioChunk(
    const std::vector<float>& samples)
{
    std::vector<ModemRxEvent> events;

    std::vector<float> corrected = samples;

    // Impulse clipping at 2.5x RMS — suppresses lightning / power-line
    // transients before they corrupt a full symbol's worth of FFT energy.
    {
        float sumSq = 0.0f;
        for (float s : corrected) sumSq += s * s;
        float rms = std::sqrt(sumSq / static_cast<float>(corrected.size()));
        if (rms > 1e-7f) {
            const float clip = 2.5f * rms;
            for (float& s : corrected) {
                if      (s >  clip) s =  clip;
                else if (s < -clip) s = -clip;
            }
        }
    }

    // DCD — advisory only; does not gate the RX pipeline.
    m_dcdActive = m_dcd.update(corrected);

    // Tone monitor — active even while RX is suspended (TX in progress),
    // matching pre-refactor placement before the transmitting gate.
    if (m_toneMonitorActive) {
        m_monitorBuf.insert(m_monitorBuf.end(),
                            corrected.begin(), corrected.end());
        while (static_cast<int>(m_monitorBuf.size()) >= SAMPLES_PER_SYMBOL) {
            auto soft = m_demodulator.demodulateToSoft(m_monitorBuf, 0, 0);
            if (!soft.empty()) {
                const auto& e = soft[0];
                int detected = static_cast<int>(
                    std::max_element(e.begin(), e.end()) - e.begin());
                float total = 0.0f;
                for (float x : e) total += x;
                float frac = (total > 1e-10f) ? e[detected] / total : 0.0f;
                dspLog("ToneMon[%4d]: tone%2d (%7.2f Hz)  frac=%5.3f",
                       m_monitorSymCount++, detected,
                       BASE_FREQ + detected * SYMBOL_RATE, frac);
            }
            m_monitorBuf.erase(m_monitorBuf.begin(),
                               m_monitorBuf.begin() + SAMPLES_PER_SYMBOL);
        }
    }

    if (m_rxSuspended) return events;

    // ── Continuous preamble sync ────────────────────────────────────────
    // Fed every sample regardless of RX state — see PreambleSync.h /
    // DECISIONS.md ADR-105 for why this replaced the old discrete-offset
    // block-FFT search. Running it unconditionally (rather than pausing
    // during Collecting) means Idle never resumes with a stale or
    // discontinuous history: only the lock result is ignored while busy.
    PreambleLock lock;
    bool gotLock = false;
    for (float s : corrected) {
        if (m_sync.pushSample(s, lock))
            gotLock = true;   // keep the most recent lock found this chunk
    }

    // ── Collecting ────────────────────────────────────────────────────────
    if (m_rxState == ModemRxState::Collecting) {
        m_rxBuffer.insert(m_rxBuffer.end(), corrected.begin(), corrected.end());

        m_preTrigger.insert(m_preTrigger.end(), corrected.begin(), corrected.end());
        if (static_cast<int>(m_preTrigger.size()) > PRE_TRIGGER_SAMPLES) {
            int trim = static_cast<int>(m_preTrigger.size()) - PRE_TRIGGER_SAMPLES;
            m_preTrigger.erase(m_preTrigger.begin(), m_preTrigger.begin() + trim);
            m_preTriggerDropped += trim;
        }

        if (static_cast<int>(m_rxBuffer.size()) > MAX_BUFFER_SAMPLES) {
            dspWarn("MfskModem: buffer limit — resetting");
            resetRx();
            return events;
        }
        ++m_collectTicks;
        tryCompleteFrame(events);
        if (m_collectTicks > COLLECT_TIMEOUT_CHUNKS) {
            dspLog("MfskModem: collect timeout — discarding");
            resetRx();
        }
        return events;
    }

    // ── Idle: rolling raw-audio buffer (handoff source once locked) ────────
    m_preTrigger.insert(m_preTrigger.end(), corrected.begin(), corrected.end());
    if (static_cast<int>(m_preTrigger.size()) > PRE_TRIGGER_SAMPLES) {
        int trim = static_cast<int>(m_preTrigger.size()) - PRE_TRIGGER_SAMPLES;
        m_preTrigger.erase(m_preTrigger.begin(), m_preTrigger.begin() + trim);
        m_preTriggerDropped += trim;
    }

    if (++m_diagChunkCount >= DIAG_LOG_INTERVAL_CHUNKS) {
        m_diagChunkCount = 0;
        // Off by default — this is a "still idle, here's the best score
        // seen" heartbeat with no value once the sync mechanism itself is
        // trusted, and at ~once/second it dominates the terminal during
        // normal listening. Set HAVEN_VERBOSE_SYNC (any value) before
        // launch to bring it back for DSP-level debugging.
        static const bool verbose =
            std::getenv("HAVEN_VERBOSE_SYNC") != nullptr;
        if (verbose) {
            dspLog("MfskModem: preamble sync idle — best score %.3f "
                   "(threshold=%.3f)",
                   m_sync.lastScore(), PreambleSync::SCORE_THRESHOLD);
        }
    }

    if (gotLock) {
        long long offset = lock.sampleIndex - m_preTriggerDropped;
        long long need    = static_cast<long long>(PREAMBLE_LENGTH) * SAMPLES_PER_SYMBOL;
        if (offset < 0 || offset + need > static_cast<long long>(m_preTrigger.size())) {
            dspWarn("MfskModem: preamble lock offset %lld out of "
                    "pretrigger range (size=%d) — dropping",
                    offset, static_cast<int>(m_preTrigger.size()));
        } else {
            m_rxBuffer.assign(m_preTrigger.begin() + offset, m_preTrigger.end());

            ModemRxEvent preambleEvent;
            onPreambleLocked(lock, preambleEvent);
            events.push_back(preambleEvent);
            tryCompleteFrame(events);
        }
    }
    return events;
}

void MfskModem::resetRx() {
    m_rxBuffer.clear();
    // m_preTrigger is intentionally preserved.
    m_timingOffset     = 0;
    m_preambleSymOff   = -1;
    m_nBlocks          = -1;
    m_symsNeeded       = 0;
    m_demodBinOffset   = 0;
    m_collectTicks     = 0;
    m_lastCheckSamples = 0;
    m_cachedSoftSymbols.clear();
    setRxState(ModemRxState::Idle);
}

// ── Preamble-triggered RX pipeline ───────────────────────────────────────

// Called once PreambleSync has reported a lock and MfskModem has sliced
// m_rxBuffer to start exactly at the preamble's first sample (so, unlike
// the old block-search code, m_timingOffset/m_preambleSymOff are always 0
// here — the whole point of the sliding-DFT redesign is that we already
// know the precise sample alignment, with no further offset search needed).
void MfskModem::onPreambleLocked(const PreambleLock& lock, ModemRxEvent& outEvent) {
    m_demodBinOffset = static_cast<int>(
        std::round(lock.freqOffsetHz * FFT_SIZE / SAMPLE_RATE));

    // Refine the frequency estimate using the block Demodulator's finer
    // (zero-padded) bins over the now-precisely-aligned preamble symbols —
    // PreambleSync's own bin resolution is exactly SYMBOL_RATE (31.25 Hz),
    // so this centroid refinement recovers sub-bin residual offset the
    // same way the old code did after its coarse hypothesis search.
    float total = lock.freqOffsetHz;
    {
        auto softPreamble = m_demodulator.demodulateToSoft(
            m_rxBuffer, 0, m_demodBinOffset);
        if (static_cast<int>(softPreamble.size()) >= PREAMBLE_LENGTH) {
            std::vector<std::vector<float>> preambleSoft(
                softPreamble.begin(), softPreamble.begin() + PREAMBLE_LENGTH);
            float residual = measureToneOffset(preambleSoft);
            total = lock.freqOffsetHz + residual;
            m_demodBinOffset = static_cast<int>(
                std::round(total * FFT_SIZE / SAMPLE_RATE));
        }
    }

    if (m_afcEnabled) {
        m_afcOffsetHz = std::max(-AFC_MAX_HZ, std::min(AFC_MAX_HZ, total));
        dspLog("MfskModem: AFC hyp=%.2f total=%.2f Hz binOff=%d",
               lock.freqOffsetHz, m_afcOffsetHz, m_demodBinOffset);
    } else {
        dspLog("MfskModem: AFC disabled — hyp=%.2f total=%.2f Hz "
               "binOff=%d (not persisted)",
               lock.freqOffsetHz, total, m_demodBinOffset);
    }

    m_timingOffset     = 0;
    m_timingOffsetBase = 0;
    m_fineTimingTick   = 0;
    m_preambleSymOff   = 0;
    m_nBlocks          = -1;
    m_symsNeeded       = 0;
    m_collectTicks     = 0;
    m_lastCheckSamples = 0;

    dspLog("MfskModem: preamble locked (score=%.3f) freq=%.2f Hz "
           "— collecting frame", lock.score, lock.freqOffsetHz);
    outEvent.preambleDetected = true;
    outEvent.preambleScore    = lock.score;
    setRxState(ModemRxState::Collecting);
}

void MfskModem::tryCompleteFrame(std::vector<ModemRxEvent>& outEvents) {
    int currentSamples = static_cast<int>(m_rxBuffer.size());
    if (currentSamples - m_lastCheckSamples < SAMPLES_PER_SYMBOL &&
        m_nBlocks > 0)
        return;
    m_lastCheckSamples = currentSamples;

    if (m_fineTimingEnabled && ++m_fineTimingTick >= FINE_TIMING_INTERVAL_CALLS) {
        m_fineTimingTick = 0;
        applyFineTimingCorrection();  // clears m_cachedSoftSymbols if it shifts m_timingOffset
    }

    // Demodulate only the newly-arrived samples since the last check and
    // append them to the persistent cache, instead of re-demodulating the
    // entire (growing) m_rxBuffer from scratch every time — see
    // m_cachedSoftSymbols's doc comment in MfskModem.h.
    int newSampleOffset = m_timingOffset +
        static_cast<int>(m_cachedSoftSymbols.size()) * SAMPLES_PER_SYMBOL;
    auto newSymbols = m_demodulator.demodulateToSoft(
        m_rxBuffer, newSampleOffset, m_demodBinOffset);
    m_cachedSoftSymbols.insert(m_cachedSoftSymbols.end(),
                               newSymbols.begin(), newSymbols.end());
    auto& softSymbols = m_cachedSoftSymbols;
    int  frameStart  = m_preambleSymOff + PREAMBLE_LENGTH;

    // Header is sent 3x (Frame v2, see Frame.h HEADER_COPIES) — 4 syms
    // per copy = 12 total. This early decode duplicates Frame::parse()'s
    // header logic (deliberately — see ADR-098: DspPipeline/MfskModem
    // must not couple to Frame's private layout constants, so small
    // constants like this are kept in sync by convention, not shared).
    constexpr int HDR_TOTAL = 12;
    if (static_cast<int>(softSymbols.size()) < frameStart + HDR_TOTAL) {
        // Throttled — this fires on nearly every audio chunk while
        // waiting (once every ~42ms), and unthrottled log calls are
        // expensive enough (disk flush + console write, all on the main
        // thread — see main.cpp's messageHandler) to compete with timely
        // audio consumption. m_collectTicks already increments once per
        // chunk in processAudioChunk(), so it's a free throttle counter.
        if (m_collectTicks % 10 == 0)
            dspLog("MfskModem: waiting for header — %d syms, need %d",
                   static_cast<int>(softSymbols.size()),
                   frameStart + HDR_TOTAL);
        return;
    }

    if (m_nBlocks < 0) {
        auto argmx = [](const std::vector<float>& e) {
            return static_cast<int>(
                std::max_element(e.begin(), e.end()) - e.begin());
        };
        auto grayNibble = [&](int toneIdx) -> int {
            return grayDecode(static_cast<uint8_t>(toneIdx));
        };
        int s = frameStart;
        auto decodeByte = [&](int symOff) -> uint8_t {
            return static_cast<uint8_t>(
                (grayNibble(argmx(softSymbols[symOff]))     << 4) |
                 grayNibble(argmx(softSymbols[symOff + 1])));
        };
        uint8_t b0[3], b1[3];
        for (int c = 0; c < 3; ++c) {
            b0[c] = decodeByte(s + c * 4);
            b1[c] = decodeByte(s + c * 4 + 2);
        }

        if (!(b0[0] == b0[1] && b0[1] == b0[2] && b1[0] == b1[1] && b1[1] == b1[2]))
            dspLog("MfskModem: header copies differ — nBlocks candidates: "
                   "%d %d %d — using bit-level majority vote",
                   b1[0], b1[1], b1[2]);

        // Bit-level majority vote across the 3 copies (>=2 of 3 wins per bit).
        auto majorityByte = [](uint8_t a, uint8_t b, uint8_t c) -> uint8_t {
            uint8_t out = 0;
            for (int bit = 7; bit >= 0; --bit) {
                int ones = ((a >> bit) & 1) + ((b >> bit) & 1) + ((c >> bit) & 1);
                out = static_cast<uint8_t>((out << 1) | (ones >= 2 ? 1 : 0));
            }
            return out;
        };
        uint8_t b0a = majorityByte(b0[0], b0[1], b0[2]);
        uint8_t b1a = majorityByte(b1[0], b1[1], b1[2]);

        constexpr uint8_t EXPECTED_HDR0 = 0x21;  // version=2, FEC_ENABLED=1
        if (b0a != EXPECTED_HDR0) {
            dspLog("MfskModem: header byte0=0x%02x expected 0x21 "
                   "— discarding", b0a);
            resetRx();
            return;
        }

        m_nBlocks = b1a;

        constexpr int MAX_NBLOCKS = 125;
        if (m_nBlocks <= 0 || m_nBlocks > MAX_NBLOCKS) {
            dspLog("MfskModem: nBlocks %d out of range [1,%d] — discarding",
                   m_nBlocks, MAX_NBLOCKS);
            resetRx();
            return;
        }

        m_symsNeeded = frameStart + Frame::frameSymsNeeded(m_nBlocks);

        dspLog("MfskModem: header decoded nBlocks=%d need %d total symbols",
               m_nBlocks, m_symsNeeded);
    }

    if (static_cast<int>(softSymbols.size()) < m_symsNeeded) {
        // Throttled — see the "waiting for header" comment above.
        if (m_collectTicks % 10 == 0)
            dspLog("MfskModem: collecting %d / %d",
                   static_cast<int>(softSymbols.size()), m_symsNeeded);
        return;
    }

    dspLog("MfskModem: frame complete — decoding");
    std::vector<std::vector<float>> frameSymbols(
        softSymbols.begin() + frameStart,
        softSymbols.begin() + m_symsNeeded);

    ModemRxEvent msgEvent;
    processFrame(frameSymbols, msgEvent);
    if (msgEvent.hasMessage) outEvents.push_back(msgEvent);
    resetRx();
}

void MfskModem::processFrame(
    const std::vector<std::vector<float>>& softSymbols,
    ModemRxEvent& outEvent)
{
    if (softSymbols.empty()) return;

    dspLog("MfskModem: processing frame, %d symbols",
           static_cast<int>(softSymbols.size()));

    ParseResult result = m_frame.parse(softSymbols);

    if (!result.error.empty()) {
        dspLog("MfskModem: frame parse error: %s", result.error.c_str());
        return;
    }

    if (!result.crcOk) {
        dspLog("MfskModem: CRC failed (converged=%s fecIter=%d) "
               "— emitting for diagnosis",
               result.converged ? "true" : "false", result.fecIterations);
    }

    outEvent.hasMessage    = true;
    outEvent.text          = result.text;
    outEvent.crcOk         = result.crcOk;
    outEvent.converged     = result.converged;
    outEvent.nBlocks       = result.nBlocks;
    outEvent.fecIterations = result.fecIterations;

    // Report the actual CRC outcome — this used to say "CRC OK"
    // unconditionally, even right after logging a CRC failure above.
    dspLog("MfskModem: decoded message: %s (CRC %s, FEC converged: %s)",
           result.text.c_str(),
           result.crcOk ? "OK" : "FAILED",
           result.converged ? "true" : "false");
}

// ── AFC implementation ────────────────────────────────────────────────────

float MfskModem::measureToneOffset(
    const std::vector<std::vector<float>>& softSymbols) const
{
    if (softSymbols.empty()) return 0.0f;
    float totalOffset = 0.0f;
    int   count       = 0;

    for (const auto& energies : softSymbols) {
        if (static_cast<int>(energies.size()) != NUM_TONES) continue;

        int winner = 0;
        for (int i = 1; i < NUM_TONES; i++)
            if (energies[i] > energies[winner]) winner = i;

        float eL = (winner > 0) ? energies[winner - 1] : 0.0f;
        float eR = (winner < NUM_TONES - 1) ? energies[winner + 1] : 0.0f;
        float eS = eL + energies[winner] + eR;
        if (eS < 1e-10f) continue;

        float centroid = (eR - eL) / eS;
        totalOffset += centroid * static_cast<float>(SYMBOL_RATE);
        count++;
    }

    return (count > 0) ? (totalOffset / count) : 0.0f;
}

// ── Fine timing recovery (experimental) ───────────────────────────────────

float MfskModem::measureDecodeConfidence(
    const std::vector<std::vector<float>>& softSymbols) const
{
    if (softSymbols.empty()) return 0.0f;
    float totalFrac = 0.0f;
    int   count     = 0;
    for (const auto& e : softSymbols) {
        if (e.size() != static_cast<size_t>(NUM_TONES)) continue;
        float total = 0.0f;
        for (float x : e) total += x;
        if (total < 1e-10f) continue;
        float maxE = *std::max_element(e.begin(), e.end());
        totalFrac += maxE / total;
        ++count;
    }
    return (count > 0) ? (totalFrac / count) : 0.0f;
}

void MfskModem::applyFineTimingCorrection() {
    // Compare decode confidence at the current offset against a small
    // probe step earlier/later; drift toward whichever wins. Clamped to
    // FINE_TIMING_MAX_DRIFT total deviation from the preamble-detected
    // offset so this can only nudge, not run away.
    auto softCurrent = m_demodulator.demodulateToSoft(
        m_rxBuffer, m_timingOffset, m_demodBinOffset);
    float confCurrent = measureDecodeConfidence(softCurrent);

    int earlyOffset = m_timingOffset - FINE_TIMING_PROBE_STEP;
    int lateOffset  = m_timingOffset + FINE_TIMING_PROBE_STEP;

    float confEarly = 0.0f, confLate = 0.0f;
    if (earlyOffset >= 0 &&
        std::abs(earlyOffset - m_timingOffsetBase) <= FINE_TIMING_MAX_DRIFT) {
        confEarly = measureDecodeConfidence(
            m_demodulator.demodulateToSoft(m_rxBuffer, earlyOffset, m_demodBinOffset));
    }
    if (std::abs(lateOffset - m_timingOffsetBase) <= FINE_TIMING_MAX_DRIFT) {
        confLate = measureDecodeConfidence(
            m_demodulator.demodulateToSoft(m_rxBuffer, lateOffset, m_demodBinOffset));
    }

    if (confEarly > confCurrent && confEarly >= confLate) {
        m_timingOffset = earlyOffset;
        m_cachedSoftSymbols.clear();  // everything cached used the old offset
        dspLog("MfskModem: fine timing -> early, offset=%d conf=%.3f",
               m_timingOffset, confEarly);
    } else if (confLate > confCurrent && confLate > confEarly) {
        m_timingOffset = lateOffset;
        m_cachedSoftSymbols.clear();  // everything cached used the old offset
        dspLog("MfskModem: fine timing -> late, offset=%d conf=%.3f",
               m_timingOffset, confLate);
    }
}

// ── Tone monitor & sweep audio ────────────────────────────────────────────

void MfskModem::setToneMonitor(bool active) {
    m_toneMonitorActive = active;
    if (active) {
        m_monitorBuf.clear();
        m_monitorSymCount = 0;
        dspLog("MfskModem: tone monitor ON — one debug line per received symbol");
    } else {
        m_monitorBuf.clear();
        dspLog("MfskModem: tone monitor OFF (%d symbols logged)",
               m_monitorSymCount);
    }
}

std::vector<float> MfskModem::generateDiagnosticAudio() const {
    // 16 tones x 500 ms each = 8 s total, continuous phase.
    const int samplesPerTone = SAMPLE_RATE / 2;
    std::vector<float> audio;
    audio.reserve(NUM_TONES * samplesPerTone);

    double phase = 0.0;
    for (int t = 0; t < NUM_TONES; ++t) {
        double freq = BASE_FREQ + t * SYMBOL_RATE;
        double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
        for (int i = 0; i < samplesPerTone; ++i) {
            audio.push_back(static_cast<float>(std::sin(phase)));
            phase += phaseInc;
            if (phase >  M_PI) phase -= 2.0 * M_PI;
            if (phase < -M_PI) phase += 2.0 * M_PI;
        }
    }

    dspLog("MfskModem: tone sweep audio: %d samples = %.2f s  "
           "(16 tones x 500ms, tone0=500Hz ... tone15=968.75Hz)",
           static_cast<int>(audio.size()),
           audio.size() / static_cast<double>(SAMPLE_RATE));
    return audio;
}

void MfskModem::runDiagnosticSelfTest() {
    dspLog("");
    dspLog("==============================");
    dspLog(" TONE SWEEP TEST");
    dspLog("==============================");
    dspLog("500ms of pure sine at each of the 16 HAVEN tones, demodulated.");
    dspLog("PASS = demodulator correctly identifies the tone.");
    dspLog("Expected frac ~0.87 (8x zero-pad sinc sidelobes absorb ~13%%)");

    bool allPass = true;
    for (int t = 0; t < NUM_TONES; ++t) {
        double freq = BASE_FREQ + t * SYMBOL_RATE;

        const int nSamples = SAMPLE_RATE / 2;
        std::vector<float> audio(nSamples);
        double phase = 0.0;
        double phaseInc = 2.0 * M_PI * freq / SAMPLE_RATE;
        for (float& s : audio) {
            s = 0.5f * static_cast<float>(std::sin(phase));
            phase += phaseInc;
        }

        auto soft = m_demodulator.demodulateToSoft(audio, 0, 0);
        if (soft.empty()) {
            dspLog("  tone%2d (%7.2f Hz) — demodulateToSoft returned empty [FAIL]",
                   t, freq);
            allPass = false;
            continue;
        }

        const auto& sym = soft[0];
        int detected = static_cast<int>(
            std::max_element(sym.begin(), sym.end()) - sym.begin());
        float total = 0.0f;
        for (float e : sym) total += e;
        float fracExp = (total > 1e-10f) ? sym[t] / total : 0.0f;
        float fracMax = (total > 1e-10f) ?
            *std::max_element(sym.begin(), sym.end()) / total : 0.0f;

        bool pass = (detected == t);
        if (!pass) allPass = false;

        dspLog("  tone%2d (%7.2f Hz) -> detected=%2d  frac@expected=%5.3f  "
               "maxFrac=%5.3f  [%s]",
               t, freq, detected, fracExp, fracMax, pass ? "PASS" : "FAIL");
    }
    dspLog("%s", allPass
        ? "Result: ALL PASS — demodulator maps all 16 tones correctly"
        : "Result: FAILURES — demodulator has a tone bin mapping error");

    dspLog("");
    dspLog("------------------------------");
    dspLog(" PREAMBLE SCAN INJECTION TEST");
    dspLog("------------------------------");

    auto preambleAudio = m_preamble.generate();
    const int silenceSamples = SAMPLE_RATE / 2;
    std::vector<float> testBuf(silenceSamples, 0.0f);
    testBuf.insert(testBuf.end(), preambleAudio.begin(), preambleAudio.end());
    testBuf.resize(testBuf.size() + SAMPLE_RATE, 0.0f);

    auto softAll = m_demodulator.demodulateToSoft(testBuf, 0, 0);

    float bestScore = 0.0f;
    int   bestSym   = -1;
    for (int i = 0; i <= static_cast<int>(softAll.size()) - PREAMBLE_LENGTH; ++i) {
        float s = m_preamble.softCorrelate(softAll, i);
        if (s > bestScore) { bestScore = s; bestSym = i; }
    }

    int expectedSym = silenceSamples / SAMPLES_PER_SYMBOL;
    dspLog("  Preamble injected at sample %d = symbol %d",
           silenceSamples, expectedSym);
    dspLog("  Best softCorrelate = %.4f at sym=%d  (threshold=%.3f)",
           bestScore, bestSym, PreambleSync::SCORE_THRESHOLD);
    dspLog("%s", bestScore >= PreambleSync::SCORE_THRESHOLD
        ? "  Result: WOULD DETECT — scanner and threshold are correct"
        : "  Result: WOULD MISS — scanner or threshold has an issue");

    if (bestSym >= 0 && bestSym + PREAMBLE_LENGTH <= (int)softAll.size()) {
        std::string got, want;
        for (int i = 0; i < PREAMBLE_LENGTH; ++i) {
            const auto& e = softAll[bestSym + i];
            int argMax = static_cast<int>(
                std::max_element(e.begin(), e.end()) - e.begin());
            got  += std::to_string(argMax) + " ";
            want += std::to_string(PREAMBLE_SYMBOLS[i]) + " ";
        }
        dspLog("  got : %s", got.c_str());
        dspLog("  want: %s", want.c_str());
    }
    dspLog("==============================");
    dspLog("");
}

// ── Helpers ───────────────────────────────────────────────────────────────

void MfskModem::setRxState(ModemRxState newState) {
    if (m_rxState != newState) {
        m_rxState = newState;
        dspLog("MfskModem: RX state -> %s",
               newState == ModemRxState::Idle ? "Idle" : "Collecting");
    }
}

} // namespace HavenFSK
