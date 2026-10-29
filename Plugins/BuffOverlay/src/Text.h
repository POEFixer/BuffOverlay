// Text helpers: the plugin's texts follow the program language (PluginI18n.h, through
// PluginSDK.h). Every text a user reads is a key of lang/en.json; numbers are formatted here,
// in code, into named arguments, never by a translation.
#pragma once

#include "../../../POEFixer/plugin_sdk/PluginSDK.h"
#include "TextUtil.h"

#include <sal.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace BuffOverlay {

namespace I18n = PluginSDK::I18n;

// printf for a text argument: Num("%.1f", seconds). The format is code, never a translation.
inline std::string Num(_In_z_ _Printf_format_string_ const char* format, ...) {
    char buf[64];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return buf;
}

// One {name} of a LocText: a final value (a file name, a number) or, with isKey, another key
// of lang/en.json, looked up when the text is drawn.
struct LocArg {
    std::string name;
    std::string value;
    bool isKey = false;
};

// A message kept as its key and arguments and translated only when it is drawn, so a message
// that stays on screen follows a language switch. Keys are written I18n::Key("...") where they
// are set (the gate checks them).
struct LocText {
    const char* key = nullptr;      // null: no text
    std::vector<LocArg> args;

    bool Empty() const { return key == nullptr; }
};

// The text of `t` in the program language.
inline std::string Render(const LocText& t) {
    if (!t.key) return {};
    std::string out;
    for (const char* p = I18n::Tr(t.key); *p;) {
        if (*p == '{') {
            if (const char* close = std::strchr(p + 1, '}')) {
                const std::string_view name(p + 1, static_cast<size_t>(close - p - 1));
                const LocArg* arg = nullptr;
                for (const LocArg& a : t.args)
                    if (a.name == name) {
                        arg = &a;
                        break;
                    }
                if (arg) {
                    out += arg->isKey ? std::string(I18n::Tr(arg->value.c_str())) : arg->value;
                    p = close + 1;
                    continue;
                }
            }
        }
        out += *p++;
    }
    return out;
}

} // namespace BuffOverlay
