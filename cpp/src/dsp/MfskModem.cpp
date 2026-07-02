#include "MfskModem.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

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
        qDebug() << "MfskModem: demod self-test score=" << score
                 << (score >= 0.5f ? "[PASS]" : "[FAIL — demodulator bug]");
        if (score < 0.5f) {
            QString got;
            for (int i = 0; i < PREAMBLE_LENGTH && i < (int)softSyms.size(); ++i) {
                const auto& e = softSyms[i];
                int best = (int)(std::max_element(e.begin(), e.end()) - e.begin());
                got += QString::number(best) + " ";
            }
            qDebug() << "  self-test got :" << got;
            qDebug() << "  self-test want: 0 15 0 15 7 8 7 8 0 15 0 15 7 8 7 8";
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
                qDebug() << QString("ToneMon[%1]: tone%2 (%3 Hz)  frac=%4")
                    .arg(m_monitorSymCount++, 4)
                    .arg(detected, 2)
                    .arg(BASE_FREQ + detected * SYMBOL_RATE, 7, 'f', 2)
                    .arg(frac, 5, 'f', 3);
            }
            m_monitorBuf.erase(m_monitorBuf.begin(),
                               m_monitorBuf.begin() + SAMPLES_PER_SYMBOL);
        }
    }

    if (m_rxSuspended) return events;

    // ── Collecting ────────────────────────────────────────────────────────
    if (m_rxState == ModemRxState::Collecting) {
        m_rxBuffer.insert(m_rxBuffer.end(), corrected.begin(), corrected.end());

        m_preTrigger.insert(m_preTrigger.end(), corrected.begin(), corrected.end());
        if (static_cast<int>(m_preTrigger.size()) > PRE_TRIGGER_SAMPLES)
            m_preTrigger.erase(m_preTrigger.begin(),
                               m_preTrigger.end() - PRE_TRIGGER_SAMPLES);

        if (static_cast<int>(m_rxBuffer.size()) > MAX_BUFFER_SAMPLES) {
            qWarning() << "MfskModem: buffer limit — resetting";
            resetRx();
            return events;
        }
        ++m_collectTicks;
        tryCompleteFrame(events);
        if (m_collectTicks > COLLECT_TIMEOUT_CHUNKS) {
            qDebug() << "MfskModem: collect timeout — discarding";
            resetRx();
        }
        return events;
    }

    // ── Idle: rolling buffer + continuous preamble scan ───────────────────
    m_preTrigger.insert(m_preTrigger.end(), corrected.begin(), corrected.end());
    if (static_cast<int>(m_preTrigger.size()) > PRE_TRIGGER_SAMPLES)
        m_preTrigger.erase(m_preTrigger.begin(),
                           m_preTrigger.end() - PRE_TRIGGER_SAMPLES);

    if (++m_scanTicks % SCAN_INTERVAL_CHUNKS == 0) {
        m_rxBuffer = m_preTrigger;
        ModemRxEvent preambleEvent;
        tryFindPreamble(preambleEvent);
        if (preambleEvent.preambleDetected) {
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
    m_scanTicks        = SCAN_INTERVAL_CHUNKS - 1;
    m_collectTicks     = 0;
    m_lastCheckSamples = 0;
    setRxState(ModemRxState::Idle);
}

// ── Preamble-triggered RX pipeline ───────────────────────────────────────

void MfskModem::tryFindPreamble(ModemRxEvent& outEvent) {
    if (static_cast<int>(m_rxBuffer.size()) <
        SAMPLES_PER_SYMBOL * PREAMBLE_LENGTH)
        return;

    if (m_scanTicks % 40 == 0) {
        float peak = 0.0f;
        for (float s : m_rxBuffer) peak = std::max(peak, std::abs(s));
        float dbFS = (peak > 1e-10f) ? 20.0f * std::log10(peak) : -96.0f;
        qDebug() << "MfskModem: scan buffer peak" << dbFS << "dBFS";
    }

    float hypCenter = m_afcEnabled ? m_afcOffsetHz : 0.0f;
    const std::vector<float> HYP_HZ = {
        hypCenter,
        hypCenter +  20.0f, hypCenter -  20.0f,
        hypCenter +  40.0f, hypCenter -  40.0f,
        hypCenter +  60.0f, hypCenter -  60.0f,
        hypCenter +  80.0f, hypCenter -  80.0f,
        hypCenter + 100.0f, hypCenter - 100.0f,
        hypCenter + 120.0f, hypCenter - 120.0f,
        hypCenter + 140.0f, hypCenter - 140.0f,
        hypCenter + 160.0f, hypCenter - 160.0f,
    };

    constexpr int TIMING_STEPS        = 16;
    constexpr int TIMING_STEP_SAMPLES = SAMPLES_PER_SYMBOL / TIMING_STEPS;

    int   bestSymOff  = -1;
    float bestScore   = 0.0f;
    int   bestTiming  = 0;
    float bestHypHz   = 0.0f;

    struct HypResult { float hz; float coarse; };
    std::vector<HypResult> hypResults;
    hypResults.reserve(HYP_HZ.size());

    float bestCoarseScore = 0.0f;
    float bestCoarseHz    = 0.0f;

    const int COARSE_SOFF_A = 0;
    const int COARSE_SOFF_B = SAMPLES_PER_SYMBOL / 2;

    for (float hypHz : HYP_HZ) {
        int afcBin = static_cast<int>(
            std::round(hypHz * FFT_SIZE / SAMPLE_RATE));

        float coarseBest = 0.0f;
        for (int csOff : {COARSE_SOFF_A, COARSE_SOFF_B}) {
            auto softCoarse = m_demodulator.demodulateToSoft(
                m_rxBuffer, csOff, afcBin);
            if (softCoarse.empty()) continue;
            for (int i = 0;
                 i <= static_cast<int>(softCoarse.size()) - PREAMBLE_LENGTH; ++i)
            {
                float s = m_preamble.softCorrelate(softCoarse, i);
                if (s > coarseBest) coarseBest = s;
            }
        }
        hypResults.push_back({hypHz, coarseBest});
        if (coarseBest > bestCoarseScore) {
            bestCoarseScore = coarseBest;
            bestCoarseHz    = hypHz;
        }
    }

    if (bestCoarseScore >= COARSE_PREAMBLE_THRESHOLD) {
        int afcBin = static_cast<int>(
            std::round(bestCoarseHz * FFT_SIZE / SAMPLE_RATE));

        for (int t = 0; t < TIMING_STEPS; ++t) {
            int sOff = t * TIMING_STEP_SAMPLES;
            auto softTry = m_demodulator.demodulateToSoft(
                m_rxBuffer, sOff, afcBin);
            if (softTry.empty()) continue;

            for (int i = 0;
                 i <= static_cast<int>(softTry.size()) - PREAMBLE_LENGTH; ++i)
            {
                float score = m_preamble.softCorrelate(softTry, i);
                if (score > bestScore) {
                    bestScore  = score;
                    bestSymOff = i;
                    bestTiming = sOff;
                    bestHypHz  = bestCoarseHz;
                }
            }
        }
    }

    const bool logThisScan =
        (m_scanTicks % 24 == 0) ||
        (bestScore >= 0.15f);

    if (logThisScan) {
        if (bestScore > 0.0f) {
            qDebug() << "MfskModem: preamble scan: soft=" << bestScore
                     << "sym=" << bestSymOff << "sampleOff=" << bestTiming
                     << "afc=" << bestHypHz << "Hz";

            if (bestScore >= 0.25f) {
                int afcBin = static_cast<int>(
                    std::round(bestHypHz * FFT_SIZE / SAMPLE_RATE));
                auto softBestDbg = m_demodulator.demodulateToSoft(
                    m_rxBuffer, bestTiming, afcBin);

                QString got, want, frac, maxf;
                for (int i = 0; i < PREAMBLE_LENGTH; ++i) {
                    int idx = bestSymOff + i;
                    if (idx < 0 || idx >= static_cast<int>(softBestDbg.size())) {
                        got  += "? ";
                        want += QString::number(PREAMBLE_SYMBOLS[i]) + " ";
                        frac += "? ";
                        maxf += "? ";
                        continue;
                    }
                    const auto& e = softBestDbg[idx];
                    int argMax = static_cast<int>(
                        std::max_element(e.begin(), e.end()) - e.begin());
                    float total = 0.0f;
                    for (float x : e) total += x;
                    float f  = (total > 1e-10f) ? e[PREAMBLE_SYMBOLS[i]] / total : 0.0f;
                    float mf = (total > 1e-10f) ? e[argMax]               / total : 0.0f;
                    got  += QString::number(argMax) + " ";
                    want += QString::number(PREAMBLE_SYMBOLS[i]) + " ";
                    frac += QString::number(f,  'f', 2) + " ";
                    maxf += QString::number(mf, 'f', 2) + " ";
                }
                qDebug() << "  got :" << got;
                qDebug() << "  want:" << want;
                qDebug() << "  frac:" << frac;
                qDebug() << "  maxf:" << maxf;
            }

            QString coarseInfo;
            for (const auto& r : hypResults)
                coarseInfo += QString("%1Hz:%2 ")
                    .arg(r.hz, 0, 'f', 0).arg(r.coarse, 0, 'f', 2);
            qDebug() << "  coarse:" << coarseInfo;

        } else {
            qDebug() << "MfskModem: preamble scan: ALL coarse checks failed"
                     << "(threshold=" << COARSE_PREAMBLE_THRESHOLD << "). Scores:";
            for (const auto& r : hypResults)
                qDebug() << "  hyp=" << r.hz << "Hz coarse=" << r.coarse;

            if (m_scanTicks % 24 == 0) {
                auto toneBalance = [](const std::vector<std::vector<float>>& syms) -> QString {
                    std::vector<float> toneSum(NUM_TONES, 0.0f);
                    float totalAll = 0.0f;
                    for (const auto& sym : syms) {
                        for (int t = 0; t < NUM_TONES; ++t) toneSum[t] += sym[t];
                        for (float x : sym) totalAll += x;
                    }
                    QString out;
                    if (totalAll > 1e-10f)
                        for (int t = 0; t < NUM_TONES; ++t)
                            out += QString::number(
                                NUM_TONES * toneSum[t] / totalAll, 'f', 2) + " ";
                    return out;
                };
                auto softDiag = m_demodulator.demodulateToSoft(m_rxBuffer, 0, 0);
                if (!softDiag.empty()) {
                    qDebug() << "  tonebal (1.0=flat):" << toneBalance(softDiag);
                }
            }
        }
    }

    if (bestScore < SOFT_PREAMBLE_THRESHOLD || bestSymOff < 0)
        return;

    // Refine frequency estimate from preamble soft symbols.
    {
        int afcBin = static_cast<int>(
            std::round(bestHypHz * FFT_SIZE / SAMPLE_RATE));
        auto softBest = m_demodulator.demodulateToSoft(
            m_rxBuffer, bestTiming, afcBin);
        int preambleEnd = bestSymOff + PREAMBLE_LENGTH;
        if (preambleEnd <= static_cast<int>(softBest.size())) {
            std::vector<std::vector<float>> preambleSoft(
                softBest.begin() + bestSymOff,
                softBest.begin() + preambleEnd);
            float residual = measureToneOffset(preambleSoft);
            float total    = bestHypHz + residual;

            m_demodBinOffset = static_cast<int>(
                std::round(total * FFT_SIZE / SAMPLE_RATE));

            if (m_afcEnabled) {
                m_afcOffsetHz = std::max(-AFC_MAX_HZ,
                                std::min(AFC_MAX_HZ, total));
                qDebug() << "MfskModem: AFC hyp=" << bestHypHz
                         << "residual=" << residual
                         << "total=" << m_afcOffsetHz << "Hz"
                         << "binOff=" << m_demodBinOffset;
            } else {
                qDebug() << "MfskModem: AFC disabled — hyp=" << bestHypHz
                         << "residual=" << residual
                         << "total=" << total << "Hz"
                         << "binOff=" << m_demodBinOffset << "(not persisted)";
            }
        }
    }

    m_timingOffset     = bestTiming;
    m_timingOffsetBase = bestTiming;
    m_fineTimingTick   = 0;
    m_preambleSymOff   = bestSymOff;
    m_nBlocks          = -1;
    m_symsNeeded       = 0;
    m_collectTicks     = 0;
    m_lastCheckSamples = 0;

    qDebug() << "MfskModem: preamble found (score=" << bestScore
             << ") — collecting frame";
    outEvent.preambleDetected = true;
    outEvent.preambleScore    = bestScore;
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
        applyFineTimingCorrection();
    }

    auto softSymbols = m_demodulator.demodulateToSoft(
        m_rxBuffer, m_timingOffset, m_demodBinOffset);
    int  frameStart  = m_preambleSymOff + PREAMBLE_LENGTH;

    // Header is sent 3x (Frame v2, see Frame.h HEADER_COPIES) — 4 syms
    // per copy = 12 total. This early decode duplicates Frame::parse()'s
    // header logic (deliberately — see ADR-098: DspPipeline/MfskModem
    // must not couple to Frame's private layout constants, so small
    // constants like this are kept in sync by convention, not shared).
    constexpr int HDR_TOTAL = 12;
    if (static_cast<int>(softSymbols.size()) < frameStart + HDR_TOTAL) {
        qDebug() << "MfskModem: waiting for header —"
                 << softSymbols.size() << "syms, need"
                 << (frameStart + HDR_TOTAL);
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
            qDebug() << "MfskModem: header copies differ — nBlocks candidates:"
                     << b1[0] << b1[1] << b1[2] << "— using bit-level majority vote";

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
            qDebug() << "MfskModem: header byte0=0x"
                     + QString::number(b0a, 16).rightJustified(2, '0')
                     + " expected 0x21 — discarding";
            resetRx();
            return;
        }

        m_nBlocks = b1a;

        constexpr int MAX_NBLOCKS = 125;
        if (m_nBlocks <= 0 || m_nBlocks > MAX_NBLOCKS) {
            qDebug() << "MfskModem: nBlocks" << m_nBlocks
                     << "out of range [1," << MAX_NBLOCKS << "] — discarding";
            resetRx();
            return;
        }

        m_symsNeeded = frameStart + Frame::frameSymsNeeded(m_nBlocks);

        qDebug() << "MfskModem: header decoded nBlocks=" << m_nBlocks
                 << "need" << m_symsNeeded << "total symbols";
    }

    if (static_cast<int>(softSymbols.size()) < m_symsNeeded) {
        qDebug() << "MfskModem: collecting"
                 << softSymbols.size() << "/" << m_symsNeeded;
        return;
    }

    qDebug() << "MfskModem: frame complete — decoding";
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

    qDebug() << "MfskModem: processing frame,"
             << softSymbols.size() << "symbols";

    ParseResult result = m_frame.parse(softSymbols);

    if (!result.error.empty()) {
        qDebug() << "MfskModem: frame parse error:"
                 << result.error.c_str();
        return;
    }

    if (!result.crcOk) {
        qDebug() << "MfskModem: CRC failed (converged=" << result.converged
                 << "fecIter=" << result.fecIterations
                 << ") — emitting for diagnosis";
    }

    outEvent.hasMessage    = true;
    outEvent.text          = result.text;
    outEvent.crcOk         = result.crcOk;
    outEvent.converged     = result.converged;
    outEvent.nBlocks       = result.nBlocks;
    outEvent.fecIterations = result.fecIterations;

    qDebug() << "MfskModem: decoded message:" << QString::fromStdString(result.text)
             << "(CRC OK, FEC converged:" << result.converged << ")";
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
        qDebug() << "MfskModem: fine timing -> early, offset=" << m_timingOffset
                 << "conf=" << confEarly;
    } else if (confLate > confCurrent && confLate > confEarly) {
        m_timingOffset = lateOffset;
        qDebug() << "MfskModem: fine timing -> late, offset=" << m_timingOffset
                 << "conf=" << confLate;
    }
}

// ── Tone monitor & sweep audio ────────────────────────────────────────────

void MfskModem::setToneMonitor(bool active) {
    m_toneMonitorActive = active;
    if (active) {
        m_monitorBuf.clear();
        m_monitorSymCount = 0;
        qDebug() << "MfskModem: tone monitor ON"
                 << "— one debug line per received symbol";
    } else {
        m_monitorBuf.clear();
        qDebug() << "MfskModem: tone monitor OFF"
                 << "(" << m_monitorSymCount << "symbols logged)";
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

    qDebug() << "MfskModem: tone sweep audio:"
             << audio.size() << "samples ="
             << (audio.size() / static_cast<double>(SAMPLE_RATE)) << "s  "
             << "(16 tones x 500ms, tone0=500Hz ... tone15=968.75Hz)";
    return audio;
}

void MfskModem::runDiagnosticSelfTest() {
    qDebug() << "";
    qDebug() << "==============================";
    qDebug() << " TONE SWEEP TEST";
    qDebug() << "==============================";
    qDebug() << "500ms of pure sine at each of the 16 HAVEN tones, demodulated.";
    qDebug() << "PASS = demodulator correctly identifies the tone.";
    qDebug() << "Expected frac ~0.87 (8x zero-pad sinc sidelobes absorb ~13%)";

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
            qDebug() << QString("  tone%1 (%2 Hz) — demodulateToSoft returned empty [FAIL]")
                        .arg(t, 2).arg(freq, 7, 'f', 2);
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

        qDebug() << QString("  tone%1 (%2 Hz) -> detected=%3  frac@expected=%4  maxFrac=%5  [%6]")
            .arg(t, 2).arg(freq, 7, 'f', 2).arg(detected, 2)
            .arg(fracExp, 5, 'f', 3).arg(fracMax, 5, 'f', 3)
            .arg(pass ? "PASS" : "FAIL");
    }
    qDebug() << (allPass
        ? "Result: ALL PASS — demodulator maps all 16 tones correctly"
        : "Result: FAILURES — demodulator has a tone bin mapping error");

    qDebug() << "";
    qDebug() << "------------------------------";
    qDebug() << " PREAMBLE SCAN INJECTION TEST";
    qDebug() << "------------------------------";

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
    qDebug() << "  Preamble injected at sample" << silenceSamples
             << "= symbol" << expectedSym;
    qDebug() << QString("  Best softCorrelate = %1 at sym=%2  (threshold=%3)")
                .arg(bestScore, 0, 'f', 4).arg(bestSym)
                .arg(SOFT_PREAMBLE_THRESHOLD);
    qDebug() << (bestScore >= SOFT_PREAMBLE_THRESHOLD
        ? "  Result: WOULD DETECT — scanner and threshold are correct"
        : "  Result: WOULD MISS — scanner or threshold has an issue");

    if (bestSym >= 0 && bestSym + PREAMBLE_LENGTH <= (int)softAll.size()) {
        QString got, want;
        for (int i = 0; i < PREAMBLE_LENGTH; ++i) {
            const auto& e = softAll[bestSym + i];
            int argMax = static_cast<int>(
                std::max_element(e.begin(), e.end()) - e.begin());
            got  += QString::number(argMax) + " ";
            want += QString::number(PREAMBLE_SYMBOLS[i]) + " ";
        }
        qDebug() << "  got :" << got;
        qDebug() << "  want:" << want;
    }
    qDebug() << "==============================";
    qDebug() << "";
}

// ── Helpers ───────────────────────────────────────────────────────────────

void MfskModem::setRxState(ModemRxState newState) {
    if (m_rxState != newState) {
        m_rxState = newState;
        qDebug() << "MfskModem: RX state ->"
                 << (newState == ModemRxState::Idle ? "Idle" : "Collecting");
    }
}

} // namespace HavenFSK
