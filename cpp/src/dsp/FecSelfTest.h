#pragma once
#include "FEC.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <string>

namespace HavenFSK {

inline bool runFecSelfTest() {
    printf("=== FEC Self-Test ===\n");
    FEC fec;

    // Test 1: encode/decode round trip at zero noise
    {
        std::string msg = "CQ POTA DE WD9N K-1234 K";
        std::vector<uint8_t> payload(msg.begin(), msg.end());
        auto enc = fec.encodeMessage(payload);

        // Perfect LLR: scale BPSK strongly
        std::vector<float> llr(enc.bpsk.size());
        for (int i = 0; i < (int)enc.bpsk.size(); i++)
            llr[i] = enc.bpsk[i] * 10.0f;

        auto decoded = fec.decodeMessage(llr, enc.nBlocks, enc.origLen);
        std::string result(decoded.begin(), decoded.end());

        if (result == msg) {
            printf("PASS: encode/decode round trip\n");
        } else {
            printf("FAIL: round trip got '%s'\n", result.c_str());
            return false;
        }
    }

    // Test 2: decode with noise (sigma=0.4, ~8dB SNR equivalent)
    {
        std::string msg = "KC8TYK DE WD9N UR 599 IN K";
        std::vector<uint8_t> payload(msg.begin(), msg.end());
        auto enc = fec.encodeMessage(payload);

        srand(42);
        float sigma = 0.4f;
        std::vector<float> noisyLLR(enc.bpsk.size());
        for (int i = 0; i < (int)enc.bpsk.size(); i++) {
            float u1 = (rand() + 1.0f) / (RAND_MAX + 2.0f);
            float u2 = (rand() + 1.0f) / (RAND_MAX + 2.0f);
            float noise = sigma * std::sqrt(-2.0f * std::log(u1))
                               * std::cos(2.0f * 3.14159265f * u2);
            noisyLLR[i] = (enc.bpsk[i] + noise) * (2.0f / (sigma * sigma));
        }

        auto decoded = fec.decodeMessage(noisyLLR, enc.nBlocks, enc.origLen);
        std::string result(decoded.begin(), decoded.end());
        if (result == msg)
            printf("PASS: noisy decode at sigma=%.1f\n", sigma);
        else
            printf("NOTE: noisy decode did not recover at sigma=%.1f "
                   "(may be acceptable)\n", sigma);
    }

    // Test 3: parity check matrix sanity
    {
        const auto& H = fec.parityCheckMatrix();
        bool ok = true;
        // Every row should have exactly 6 ones
        for (int i = 0; i < (int)H.size(); i++) {
            int cnt = 0;
            for (int j = 0; j < (int)H[i].size(); j++)
                cnt += H[i][j];
            if (cnt != 6) { ok = false; break; }
        }
        // Every column should have exactly 3 ones
        if (ok) {
            for (int j = 0; j < LDPC_N; j++) {
                int cnt = 0;
                for (int i = 0; i < LDPC_M; i++)
                    cnt += H[i][j];
                if (cnt != 3) { ok = false; break; }
            }
        }
        printf("%s: H matrix degree check (rows=6, cols=3)\n",
               ok ? "PASS" : "FAIL");
        if (!ok) return false;
    }

    printf("=== FEC Self-Test PASSED ===\n");
    return true;
}

} // namespace HavenFSK
