#pragma once
#include <vector>

namespace HavenFSK {

class DCD {
public:
    DCD();
    bool process(const std::vector<float>& samples);
    bool isActive() const { return m_active; }
private:
    bool m_active{false};
};

} // namespace HavenFSK
