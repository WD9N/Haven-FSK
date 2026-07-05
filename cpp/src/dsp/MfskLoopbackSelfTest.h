#pragma once
#include "MfskModem.h"
#include "Constants.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// Exercises the actual live RX state machine — MfskModem::processAudioChunk()
// fed AUDIO_CHUNK_SAMPLES-sized chunks exactly as
// AudioEngine::onRxDataAvailable() delivers them (see AudioEngine.cpp) —
// rather than FrameSelfTest.h's direct Frame::assemble()/parse() round trip,
// which bypasses PreambleSync/tryCompleteFrame() entirely. Written to verify
// the sliding-DFT preamble sync redesign (DECISIONS.md ADR-105) actually
// locks and decodes through the real chunked pipeline, not just in isolation.

namespace HavenFSK {

inline bool runMfskLoopbackSelfTest() {
    printf("=== MFSK Chunked Loopback Self-Test ===\n");

    MfskModem txModem;
    const std::string msg = "CQ POTA DE WD9N K-1234 K";
    auto audio = txModem.modulateText(msg);
    if (audio.empty()) {
        printf("FAIL: modulateText returned empty audio\n");
        return false;
    }
    printf("  TX audio: %d samples\n", (int)audio.size());

    // Lead-in/tail silence, as real air/loopback conditions would have.
    // Lead-in exceeds PRE_TRIGGER_SAMPLES (3s) deliberately: a real RX
    // session typically runs for well over 250ms before any real signal
    // arrives, giving MfskModem's rolling pretrigger buffer time to fill
    // naturally. A short lead-in can hit a narrow, pre-existing "cold
    // start" edge case (the very first candidate lock landing before the
    // pretrigger buffer has enough history, logged as "out of pretrigger
    // range") that a fresh RX session's first few hundred ms could
    // legitimately encounter — not something to paper over in a
    // realistic test, but not the scenario this test exists to verify
    // either (see DECISIONS.md, PreambleSync's m_runBestScore staleness
    // fix, which unmasked this once it stopped accidentally compensating
    // for it).
    std::vector<float> full(SAMPLE_RATE * 4, 0.0f);
    full.insert(full.end(), audio.begin(), audio.end());
    full.resize(full.size() + SAMPLE_RATE / 4, 0.0f);

    MfskModem rxModem;
    bool        gotMessage = false;
    bool        crcOk      = false;
    std::string gotText;

    for (size_t off = 0; off < full.size(); off += AUDIO_CHUNK_SAMPLES) {
        size_t end = std::min(full.size(), off + (size_t)AUDIO_CHUNK_SAMPLES);
        std::vector<float> chunk(full.begin() + off, full.begin() + end);
        chunk.resize(AUDIO_CHUNK_SAMPLES, 0.0f);
        auto events = rxModem.processAudioChunk(chunk);
        for (const auto& ev : events) {
            if (ev.hasMessage) {
                gotMessage = true;
                gotText    = ev.text;
                crcOk      = ev.crcOk;
            }
        }
    }

    if (!gotMessage) {
        printf("FAIL: chunked RX state machine never produced a message\n");
        return false;
    }
    if (!crcOk) {
        printf("FAIL: message decoded but CRC failed: '%s'\n", gotText.c_str());
        return false;
    }
    if (gotText != msg) {
        printf("FAIL: text mismatch\n  expected: '%s'\n  got:      '%s'\n",
               msg.c_str(), gotText.c_str());
        return false;
    }

    printf("PASS: chunked RX decoded '%s'\n", gotText.c_str());
    printf("=== MFSK Chunked Loopback Self-Test PASSED ===\n");
    return true;
}

} // namespace HavenFSK
