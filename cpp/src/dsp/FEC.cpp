#include "FEC.h"

namespace HavenFSK {

FEC::FEC() = default;

std::vector<uint8_t> FEC::encode(const std::vector<uint8_t>&) { return {}; }
std::vector<uint8_t> FEC::decode(const std::vector<uint8_t>&, bool& ok)
{
    ok = false;
    return {};
}

} // namespace HavenFSK
