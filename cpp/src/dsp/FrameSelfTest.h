#pragma once
#include "Frame.h"
#include "Demodulator.h"
#include <cstdio>
#include <string>

namespace HavenFSK {

inline bool runFrameSelfTest() {
    printf("=== Frame Self-Test ===\n");

    Frame frame;

    // Test 1: CRC known vector
    {
        std::vector<uint8_t> data = {
            '1','2','3','4','5','6','7','8','9'
        };
        uint16_t crc = Frame::crc16(data);
        if (crc == 0x29B1) {
            printf("PASS: CRC-16 test vector (0x29B1)\n");
        } else {
            printf("FAIL: CRC-16 expected 0x29B1, got 0x%04X\n", crc);
            return false;
        }
    }

    // Test 2: assemble/parse round trip
    {
        std::string msg = "CQ POTA DE WD9N K-1234 K";

        auto audio = frame.assemble(msg);

        if (audio.empty()) {
            printf("FAIL: assemble returned empty audio\n");
            return false;
        }
        printf("PASS: assemble produced %d samples\n", (int)audio.size());

        Demodulator demod;
        auto softAll = demod.demodulateToSoft(audio);

        int preambleSyms = 16;
        if ((int)softAll.size() <= preambleSyms) {
            printf("FAIL: not enough symbols after demodulation\n");
            return false;
        }

        std::vector<std::vector<float>> softPayload(
            softAll.begin() + preambleSyms, softAll.end());

        auto result = frame.parse(softPayload);

        if (!result.error.empty()) {
            printf("FAIL: parse error: %s\n", result.error.c_str());
            return false;
        }
        if (!result.crcOk) {
            printf("FAIL: CRC check failed\n");
            return false;
        }
        if (result.text != msg) {
            printf("FAIL: text mismatch\n");
            printf("  expected: '%s'\n", msg.c_str());
            printf("  got:      '%s'\n", result.text.c_str());
            return false;
        }
        printf("PASS: assemble/parse round trip, CRC OK\n");
        printf("      text: '%s'\n", result.text.c_str());
    }

    printf("=== Frame Self-Test PASSED ===\n");
    return true;
}

} // namespace HavenFSK
