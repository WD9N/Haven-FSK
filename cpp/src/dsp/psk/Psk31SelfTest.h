#pragma once
#include "Varicode.h"
#include "Psk31Modulator.h"
#include "Psk31Demodulator.h"
#include <cstdio>
#include <string>

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

        Psk31Modulator mod;
        auto audio = mod.modulateText(msg);
        if (audio.empty()) {
            printf("FAIL: Psk31Modulator produced no audio\n");
            return false;
        }
        printf("  modulated %d samples for \"%s\"\n",
               (int)audio.size(), msg.c_str());

        Psk31Demodulator demod;
        Varicode decoder;
        std::string decoded;
        for (float sample : audio) {
            auto result = demod.processSample(sample);
            if (!result.bitReady) continue;
            auto ch = decoder.decodeBit(result.bit);
            if (ch.has_value()) decoded.push_back(*ch);
        }

        if (decoded == msg) {
            printf("PASS: full audio loopback (\"%s\")\n", msg.c_str());
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

    printf("=== PSK31 Self-Test: ALL PASS (loopback only — not interop-verified) ===\n");
    return true;
}

} // namespace HavenFSK
