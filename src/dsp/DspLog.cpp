#include "DspLog.h"
#include <cstdarg>
#include <cstdio>

namespace HavenFSK {

namespace {
    DspLogSink g_sink;  // default-constructed: no-op until installed

    void vlog(DspLogLevel level, const char* fmt, va_list args) {
        if (!g_sink) return;
        char buf[1024];
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        g_sink(level, std::string(buf));
    }
}

void setDspLogSink(DspLogSink sink) {
    g_sink = std::move(sink);
}

void dspLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(DspLogLevel::Debug, fmt, args);
    va_end(args);
}

void dspWarn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(DspLogLevel::Warning, fmt, args);
    va_end(args);
}

} // namespace HavenFSK
