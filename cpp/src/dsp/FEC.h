#pragma once
#include <vector>
#include <cstdint>

namespace HavenFSK {

class FEC {
public:
    FEC();
    std::vector<uint8_t> encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decode(const std::vector<uint8_t>& data, bool& ok);
};

} // namespace HavenFSK
