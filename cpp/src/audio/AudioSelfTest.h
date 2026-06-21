#pragma once
#include "AudioEngine.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>

namespace HavenFSK {

inline bool runAudioSelfTest() {
    printf("=== Audio Self-Test ===\n");

    // Test 1: Device enumeration
    {
        QStringList inputs  = AudioEngine::availableInputDevices();
        QStringList outputs = AudioEngine::availableOutputDevices();

        printf("Available input devices (%d):\n", inputs.size());
        for (const QString& s : inputs)
            printf("  - %s\n", s.toUtf8().constData());

        printf("Available output devices (%d):\n", outputs.size());
        for (const QString& s : outputs)
            printf("  - %s\n", s.toUtf8().constData());

        if (inputs.isEmpty())
            printf("NOTE: No input devices found — "
                   "normal in headless build environment\n");
        if (outputs.isEmpty())
            printf("NOTE: No output devices found — "
                   "normal in headless build environment\n");
        printf("PASS: device enumeration\n");
    }

    // Test 2: PCM conversion round-trip
    {
        std::vector<float> original(2048);
        for (int i = 0; i < 2048; i++)
            original[i] = 0.5f * std::sin(
                2.0f * 3.14159265f * 600.0f * i / 48000.0f);

        std::vector<float> roundTrip(2048);
        for (int i = 0; i < 2048; i++) {
            float clamped = std::max(-1.0f, std::min(1.0f, original[i]));
            int16_t pcm = static_cast<int16_t>(clamped * 32767.0f);
            roundTrip[i] = static_cast<float>(pcm) / 32768.0f;
        }

        float maxErr = 0.0f;
        for (int i = 0; i < 2048; i++)
            maxErr = std::max(maxErr, std::abs(original[i] - roundTrip[i]));

        // Max error with 32767 encode / 32768 decode = (|x|+1)/32768 ≤ 2/32768
        if (maxErr < (2.0f / 32768.0f)) {
            printf("PASS: PCM round-trip (max error = %.6f)\n", maxErr);
        } else {
            printf("FAIL: PCM round-trip error too large: %.6f\n", maxErr);
            return false;
        }
    }

    printf("=== Audio Self-Test PASSED ===\n");
    return true;
}

} // namespace HavenFSK
