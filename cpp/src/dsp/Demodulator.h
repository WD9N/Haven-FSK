#pragma once
#include <vector>
#include <cstdint>

namespace HavenFSK {

class Demodulator {
public:
    Demodulator();
    std::vector<uint8_t> demodulate(const std::vector<float>& samples);
};

} // namespace HavenFSK
