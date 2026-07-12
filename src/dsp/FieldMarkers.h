#pragma once
#include <string>
#include <vector>
#include <cstddef>

// Inline structured field markers (ADR-133, spec §6.1).
//
// A marked field on the wire is:  0x1F <id> <value> 0x1E
// The value is the visible prose itself — data exists exactly once.
// Markers are an application-layer payload convention; the frame
// format is unaffected and receivers that ignore markers still
// recover the full text (after stripping).
//
// Qt-free on purpose: used by the pipeline (sender extraction) and,
// via QString conversion at the boundary, by the UI layers.

namespace HavenFSK {

constexpr char FIELD_START = '\x1F';  // ASCII Unit Separator
constexpr char FIELD_END   = '\x1E';  // ASCII Record Separator

// Field IDs (spec §6.1). One ASCII byte each.
namespace FieldId {
    constexpr char Sender    = 'd';  // transmitting station callsign
    constexpr char Recipient = 'c';  // addressed-to callsign
    constexpr char Rs        = 'r';  // signal report (pair-scoped)
    constexpr char Grid      = 'g';
    constexpr char Pota      = 'p';  // space-separated refs
    constexpr char Sota      = 's';
    constexpr char Name      = 'n';
    constexpr char Qth       = 'q';
    constexpr char Fd        = 'f';  // Field Day exchange
    constexpr char State     = 't';  // US state / primary subdivision
    constexpr char County    = 'y';  // county
}

// The only pair-scoped field: meaningful solely between the stations
// named by d/c. Everything else is a broadcast fact about the sender.
inline bool isPairScoped(char id) { return id == FieldId::Rs; }

inline bool isKnownFieldId(char id) {
    switch (id) {
        case FieldId::Sender: case FieldId::Recipient: case FieldId::Rs:
        case FieldId::Grid:   case FieldId::Pota:      case FieldId::Sota:
        case FieldId::Name:   case FieldId::Qth:       case FieldId::Fd:
        case FieldId::State:  case FieldId::County:
            return true;
        default:
            return false;
    }
}

struct MarkedField {
    char        id;
    std::string value;
    size_t      displayPos;  // offset of value in the stripped text
};

struct MarkedMessage {
    std::string              display;  // text with all markers removed
    std::vector<MarkedField> fields;   // in order of appearance
};

// Wrap one value. Empty values produce no markers (nothing to tag).
inline std::string wrapField(char id, const std::string& value) {
    if (value.empty()) return value;
    std::string out;
    out.reserve(value.size() + 3);
    out += FIELD_START;
    out += id;
    out += value;
    out += FIELD_END;
    return out;
}

// Parse marked text into display text + field list. Tolerant of
// damage that frame CRC normally rules out anyway: an unterminated
// field runs to end of text; a stray FIELD_END is dropped; a
// FIELD_START at end of text is dropped. Unknown IDs keep their value
// as visible text but are not reported as fields (spec §6.1).
inline MarkedMessage parseMarkedText(const std::string& text) {
    MarkedMessage out;
    out.display.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        char ch = text[i];
        if (ch == FIELD_END) { i++; continue; }
        if (ch != FIELD_START) { out.display += ch; i++; continue; }

        if (i + 1 >= text.size()) break;   // dangling start marker
        char id = text[i + 1];
        i += 2;

        size_t valStart = i;
        while (i < text.size() && text[i] != FIELD_END
                               && text[i] != FIELD_START)
            i++;
        std::string value = text.substr(valStart, i - valStart);
        if (i < text.size() && text[i] == FIELD_END) i++;

        if (!value.empty() && isKnownFieldId(id))
            out.fields.push_back({id, value, out.display.size()});
        out.display += value;
    }
    return out;
}

// Display text only (both marker bytes removed, values kept).
inline std::string stripMarkers(const std::string& text) {
    return parseMarkedText(text).display;
}

// First value for a given field id, empty if absent.
inline std::string fieldValue(const MarkedMessage& msg, char id) {
    for (const auto& f : msg.fields)
        if (f.id == id) return f.value;
    return {};
}

} // namespace HavenFSK
