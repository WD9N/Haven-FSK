#pragma once
#include <functional>
#include <string>

// DspLog — Qt-free logging shim for the DSP layer (ADR-003 / ADR-124).
//
// src/dsp must compile without Qt, but the modem/framing code has real
// diagnostic logging worth keeping. This shim decouples the two: DSP code
// calls dspLog()/dspWarn() (printf-style), and whatever is hosting the DSP
// layer decides where that text goes. The Qt application wires the sink to
// qDebug/qWarning in main.cpp; a standalone diagnostic tool (see the
// diag_sync.cpp precedent in DECISIONS.md) can wire it to printf or leave
// it at the default no-op.
//
// Not thread-safe by design: the sink is installed once at startup before
// any DSP object exists, and all DSP logging happens on the DSP thread.

namespace HavenFSK {

enum class DspLogLevel { Debug, Warning };

using DspLogSink = std::function<void(DspLogLevel, const std::string&)>;

// Install the sink (replaces the default no-op). Call once at startup.
void setDspLogSink(DspLogSink sink);

// printf-style logging. No trailing newline needed — the sink's backend
// (qDebug etc.) adds its own line handling.
void dspLog(const char* fmt, ...);
void dspWarn(const char* fmt, ...);

} // namespace HavenFSK
