#pragma once
#include "Varicode.h"
#include "Psk31Modulator.h"
#include "Psk31Demodulator.h"
#include "Psk31Modem.h"
#include "../Constants.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace HavenFSK {

// PSK31 self-test: internal loopback only (own modulator -> own
// demodulator -> own varicode decoder). This validates internal
// consistency of this implementation, NOT interoperability with a real
// PSK31 station or another implementation (e.g. fldigi) — that requires
// an actual on-air or audio-loopback test against real hardware/software,
// which is outside what an automated self-test can verify.
inline bool runPsk31SelfTest() {
    printf("=== PSK31 Self-Test (internal loopback only) ===\n");

    // Test 1: Varicode round trip (encode/decode, no audio)
    {
        std::string msg = "CQ CQ DE TEST";
        auto bits = Varicode::encode(msg);
        if (bits.empty()) {
            printf("FAIL: Varicode::encode returned empty bitstream\n");
            return false;
        }

        Varicode decoder;
        std::string decoded;
        for (bool b : bits) {
            auto ch = decoder.decodeBit(b);
            if (ch.has_value()) decoded.push_back(*ch);
        }

        if (decoded == msg) {
            printf("PASS: Varicode round trip (\"%s\")\n", msg.c_str());
        } else {
            printf("FAIL: Varicode round trip — sent \"%s\" got \"%s\"\n",
                   msg.c_str(), decoded.c_str());
            return false;
        }
    }

    // Test 2: full audio loopback (modulate -> demodulate sample-by-sample
    // -> varicode decode)
    {
        std::string msg = "HAVEN PSK31 TEST 123";

        // The demodulator spends its first ACQ_SYMBOLS (12) periods on
        // envelope-based timing acquisition and emits no bits, and the
        // matched filter finalizes each symbol one period late (its
        // pulse spans two periods) — so a bare bitstream needs a leading
        // phase-reversal preamble to acquire on and one trailing symbol
        // period to flush the last symbol. Real TX always provides both
        // (see Psk31Modem::modulateText); mirror that here and require
        // containment rather than exact equality.
        Psk31Modulator mod;
        std::vector<bool> bits(16, false);          // reversal preamble
        auto textBits = Varicode::encode(msg);
        bits.insert(bits.end(), textBits.begin(), textBits.end());
        auto audio = mod.modulateBits(bits);
        if (audio.empty()) {
            printf("FAIL: Psk31Modulator produced no audio\n");
            return false;
        }
        printf("  modulated %d samples for \"%s\"\n",
               (int)audio.size(), msg.c_str());
        // Silence flush: the matched filter finalizes one period late,
        // and the timing snap can sit a few samples past TX boundaries —
        // give it a few periods (the real path's postamble provides 32).
        audio.resize(audio.size()
                     + 4 * (size_t)(SAMPLE_RATE / PSK31_DEFAULT_BAUD), 0.0f);

        Psk31Demodulator demod;
        Varicode decoder;
        std::string decoded;
        for (float sample : audio) {
            auto result = demod.processSample(sample);
            if (!result.bitReady) continue;
            auto ch = decoder.decodeBit(result.bit);
            if (ch.has_value()) decoded.push_back(*ch);
        }

        if (decoded.find(msg) != std::string::npos) {
            printf("PASS: full audio loopback (\"%s\" within \"%s\")\n",
                   msg.c_str(), decoded.c_str());
        } else {
            printf("FAIL: full audio loopback — sent \"%s\" got \"%s\"\n",
                   msg.c_str(), decoded.c_str());
            printf("  NOTE: this exercises the Costas loop/timing recovery,\n"
                   "  which have not been validated against real on-air\n"
                   "  signals — a loopback failure here may reflect tuning\n"
                   "  needed in Psk31Demodulator, not necessarily a logic bug.\n");
            return false;
        }
    }

    // Test 3: chunked modem-level loopback — Psk31Modem::modulateText()
    // fed to a second Psk31Modem in AUDIO_CHUNK_SAMPLES chunks, exactly
    // as AudioEngine delivers live RX. Unlike Test 2 this covers the TX
    // preamble/postamble and the DCD/squelch gating in
    // processAudioChunk() — the missing-TX-preamble bug fixed in ce49557
    // lived precisely in the path Test 2 bypasses.
    {
        std::string msg = "CQ DE WD9N K";

        Psk31Modem tx;
        auto audio = tx.modulateText(msg);
        if (audio.empty()) {
            printf("FAIL: Psk31Modem::modulateText produced no audio\n");
            return false;
        }

        // Silence lead-in/tail as real RX conditions would have.
        std::vector<float> full(SAMPLE_RATE / 2, 0.0f);
        full.insert(full.end(), audio.begin(), audio.end());
        full.resize(full.size() + SAMPLE_RATE / 2, 0.0f);

        Psk31Modem rx;
        std::string decoded;
        for (size_t off = 0; off < full.size(); off += AUDIO_CHUNK_SAMPLES) {
            size_t end = std::min(full.size(),
                                  off + (size_t)AUDIO_CHUNK_SAMPLES);
            std::vector<float> chunk(full.begin() + off, full.begin() + end);
            chunk.resize(AUDIO_CHUNK_SAMPLES, 0.0f);
            for (const auto& ev : rx.processAudioChunk(chunk))
                if (ev.hasMessage) decoded += ev.text;
        }

        // The stream may carry preamble idle artifacts or a leading/
        // trailing space from the postamble — require the message to
        // appear intact within the decoded stream rather than exact
        // equality, mirroring how an operator reads flowing PSK31 text.
        if (decoded.find(msg) != std::string::npos) {
            printf("PASS: chunked modem loopback (\"%s\" within \"%s\")\n",
                   msg.c_str(), decoded.c_str());
        } else {
            printf("FAIL: chunked modem loopback — sent \"%s\" got \"%s\"\n",
                   msg.c_str(), decoded.c_str());
            return false;
        }
    }

    printf("=== PSK31 Self-Test: ALL PASS (loopback only — not interop-verified) ===\n");
    return true;
}

} // namespace HavenFSK
