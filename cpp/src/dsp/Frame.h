#pragma once
#include <vector>
#include <cstdint>
#include <string>

namespace HavenFSK {

class Frame {
public:
    static std::vector<uint8_t> build(const std::string& text);
    static bool parse(const std::vector<uint8_t>& data, std::string& text);
};

} // namespace HavenFSK
