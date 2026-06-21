#pragma once
#include <vector>
#include <cstdint>

namespace HavenFSK {

class Modulator {
public:
    Modulator();
    std::vector<float> modulate(const std::vector<uint8_t>& data);
};

} // namespace HavenFSK
