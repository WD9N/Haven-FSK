#include "Frame.h"

namespace HavenFSK {

std::vector<uint8_t> Frame::build(const std::string&) { return {}; }
bool Frame::parse(const std::vector<uint8_t>&, std::string&) { return false; }

} // namespace HavenFSK
