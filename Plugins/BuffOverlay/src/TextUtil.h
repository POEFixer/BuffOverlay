// Text helpers that need no SDK (STL only), so the tests build them: a slider's display
// format from a translated unit text, and byte cuts that never split a UTF-8 character.
#pragma once

#include <cstring>
#include <string>
#include <string_view>

namespace BuffOverlay {

// The unit text ("{value} px") with its FIRST {value} replaced by the number format ("%.0f").
// The number format is code; a '%' of the translation is doubled so it is never a conversion,
// and a later {value} stays literal text: ImGui passes one number, so a second conversion
// would read an argument that is not there.
inline std::string UnitFormat(const char* unitText, const char* numberFormat) {
    const std::string_view text = unitText ? unitText : "";
    const std::string_view mark = "{value}";
    std::string out;
    bool placed = false;
    for (size_t i = 0; i < text.size();) {
        if (!placed && text.compare(i, mark.size(), mark) == 0) {
            out += numberFormat ? numberFormat : "";
            placed = true;
            i += mark.size();
            continue;
        }
        if (text[i] == '%') out += '%';
        out += text[i++];
    }
    return out;
}

// The length of the longest prefix of `s` of at most maxBytes bytes that ends on a character
// boundary: a UTF-8 continuation byte is never the first byte cut off.
inline size_t Utf8CutLength(std::string_view s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s.size();
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return cut;
}

// `s` cut to at most maxBytes bytes on a character boundary.
inline void Utf8Truncate(std::string& s, size_t maxBytes) { s.resize(Utf8CutLength(s, maxBytes)); }

// A NUL-terminated text that a byte limit (snprintf into a fixed buffer) may have cut inside
// its last character: that incomplete character is dropped.
inline void DropIncompleteUtf8Tail(char* s) {
    if (!s) return;
    size_t i = std::strlen(s);
    size_t continuation = 0;
    while (i > 0 && continuation < 3 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) {
        --i;
        ++continuation;
    }
    if (i == 0) return;
    const unsigned char lead = static_cast<unsigned char>(s[i - 1]);
    const size_t need = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
    if (need > 1 && continuation + 1 < need) s[i - 1] = '\0';
}

} // namespace BuffOverlay
