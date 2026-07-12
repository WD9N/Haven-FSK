#pragma once
#include <string>

// Shared amateur-callsign regex fragments — the single source of truth for
// "is this word callsign-shaped?" across the codebase (RxDisplay's
// linkifier/isCallsign, DspPipeline::parseSenderCallsign). These were
// previously four hand-synced literals; change the shape here only.
//
// Slash tolerance: real traffic carries operating modifiers — "WD9N/P"
// (portable), "K1ABC/7" (district), "EA8/WD9N" (DX prefix), "/M", "/MM",
// "/AM", "/QRP", "/R". The suffix alternation is a CLOSED set on purpose:
// an open "/[A-Z]+" would swallow the association segment of a bare SOTA
// ref ("W7W/SE-001") and linkify "W7W/SE" as a callsign. With the closed
// set that string degrades exactly as before (only "W7W" matches).
//
// Known accepted trade-offs (mirror the pre-slash behavior):
//  - A 6-char Maidenhead grid is core-shaped ("EN52XA"); callers reject
//    pure grid shapes separately. A grid with a modifier ("EN52XA/P")
//    slips through — nobody writes that.
//  - Special-event calls that ARE grid-shaped (GB19HQ) stay rejected.

namespace HavenFSK {

// Base callsign: 1-3 alnum, a digit, 0-3 alnum, trailing letter.
inline constexpr const char* CALLSIGN_CORE =
    "[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,3}[A-Z]";

// Optional leading DXCC prefix: "EA8/WD9N", "W4/G4ABC".
inline constexpr const char* CALLSIGN_PREFIX_OPT =
    "(?:[A-Z0-9]{1,4}/)?";

// Optional operating suffix (closed set — see header comment).
inline constexpr const char* CALLSIGN_SUFFIX_OPT =
    "(?:/(?:MM|AM|QRP|[PMR0-9]))?";

// The full slash-tolerant pattern (no capture groups). Callers add their
// own anchors: "\\b(" + ... + ")\\b" for scanning, "^" + ... + "$" for
// whole-word tests.
inline std::string callsignPattern() {
    return std::string(CALLSIGN_PREFIX_OPT) + CALLSIGN_CORE
         + CALLSIGN_SUFFIX_OPT;
}

} // namespace HavenFSK
