#pragma once
#include <vector>
#include <cstdint>

namespace HavenFSK {

class Preamble {
public:
    static std::vector<float> generate();
    static double correlate(const std::vector<float>& samples, int offset);
};

} // namespace HavenFSK
