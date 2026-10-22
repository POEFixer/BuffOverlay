// Tracker model and the pure rules that turn a buff / active-skill list into
// what one tracker shows. No ImGui, no host calls: tests/ builds this alone.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace BuffOverlay {

enum class MatchMode : int { Exact = 0, Prefix = 1, Contains = 2 };
enum class SourceMode : int { Buff = 0, ActiveSkill = 1 };
enum class StageCompare : int { Any = 0, Equal = 1, NotEqual = 2, Greater = 3, GreaterEqual = 4, Less = 5, LessEqual = 6 };
enum class SkillValue : int { UseStage = 0, TotalUses = 1, AvailableCharges = 2, MaxUses = 3, CanBeUsed = 4 };
enum class MissingMode : int { Hide = 0, ShowZero = 1 };

// The stored display mode keeps the upstream numbering (0..10) so existing
// settings load unchanged. The settings tab edits it as three independent
// parts (icon, text, time left) through Compose/Decompose below.
enum class DisplayMode : int {
    Charges = 0, CustomText = 1, Icon = 2, IconAndCharges = 3, IconAndText = 4,
    TimeLeft = 5, ChargesAndTimeLeft = 6, CustomTextAndTimeLeft = 7,
    IconAndTimeLeft = 8, IconChargesAndTimeLeft = 9, IconTextAndTimeLeft = 10,
};
constexpr int kDisplayModeCount = 11;

enum class TextPart : int { None = 0, Value = 1, Custom = 2 };

struct DisplayParts {
    bool icon = false;
    TextPart text = TextPart::Value;
    bool time = false;
};

inline DisplayParts Decompose(DisplayMode m) {
    switch (m) {
        case DisplayMode::Charges:                return { false, TextPart::Value,  false };
        case DisplayMode::CustomText:             return { false, TextPart::Custom, false };
        case DisplayMode::Icon:                   return { true,  TextPart::None,   false };
        case DisplayMode::IconAndCharges:         return { true,  TextPart::Value,  false };
        case DisplayMode::IconAndText:            return { true,  TextPart::Custom, false };
        case DisplayMode::TimeLeft:               return { false, TextPart::None,   true  };
        case DisplayMode::ChargesAndTimeLeft:     return { false, TextPart::Value,  true  };
        case DisplayMode::CustomTextAndTimeLeft:  return { false, TextPart::Custom, true  };
        case DisplayMode::IconAndTimeLeft:        return { true,  TextPart::None,   true  };
        case DisplayMode::IconChargesAndTimeLeft: return { true,  TextPart::Value,  true  };
        case DisplayMode::IconTextAndTimeLeft:    return { true,  TextPart::Custom, true  };
    }
    return {};
}

// Every combination maps to a stored mode; "nothing at all" becomes the value.
inline DisplayMode Compose(DisplayParts p) {
    if (!p.icon && p.text == TextPart::None && !p.time) p.text = TextPart::Value;
    if (p.icon) {
        if (p.time) {
            return p.text == TextPart::Value  ? DisplayMode::IconChargesAndTimeLeft
                 : p.text == TextPart::Custom ? DisplayMode::IconTextAndTimeLeft
                                              : DisplayMode::IconAndTimeLeft;
        }
        return p.text == TextPart::Value  ? DisplayMode::IconAndCharges
             : p.text == TextPart::Custom ? DisplayMode::IconAndText
                                          : DisplayMode::Icon;
    }
    if (p.time) {
        return p.text == TextPart::Value  ? DisplayMode::ChargesAndTimeLeft
             : p.text == TextPart::Custom ? DisplayMode::CustomTextAndTimeLeft
                                          : DisplayMode::TimeLeft;
    }
    return p.text == TextPart::Custom ? DisplayMode::CustomText : DisplayMode::Charges;
}

struct Color { float r = 1, g = 1, b = 1, a = 1; };

// One tracker as stored in settings.json. Plain data, safe to copy.
struct Tracker {
    bool enabled = true;
    std::string label = "Tracker";
    std::string pattern;
    int source = static_cast<int>(SourceMode::Buff);
    int matchMode = static_cast<int>(MatchMode::Prefix);
    int skillValue = static_cast<int>(SkillValue::UseStage);
    int stageCompare = static_cast<int>(StageCompare::Any);
    int stageValue = 0;
    int displayMode = static_cast<int>(DisplayMode::Charges);
    int missingMode = static_cast<int>(MissingMode::Hide);
    bool sumCharges = true;
    std::string customText = "BUFF";
    std::string iconPath;
    float x = 500.0f;
    float y = 300.0f;
    float fontScale = 2.0f;
    float iconSize = 42.0f;
    Color color{ 1, 1, 1, 1 };
    bool textBackground = false;
    Color backgroundColor{ 0.0f, 0.0f, 0.0f, 0.70f };
};

// Ranges every loaded or edited value is clamped into.
constexpr float kMinFontScale = 0.5f, kMaxFontScale = 5.0f;
constexpr float kMinIconSize = 12.0f, kMaxIconSize = 128.0f;
constexpr float kMaxCoord = 16384.0f;
constexpr size_t kMaxTrackers = 64;
constexpr size_t kMaxTextLen = 256;
constexpr size_t kMaxPathLen = 1024;

inline float Clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 1.0f; }

inline void Sanitize(Tracker& t) {
    auto clampInt = [](int v, int lo, int hi) { return std::clamp(v, lo, hi); };
    t.source = clampInt(t.source, 0, 1);
    t.matchMode = clampInt(t.matchMode, 0, 2);
    t.skillValue = clampInt(t.skillValue, 0, 4);
    t.stageCompare = clampInt(t.stageCompare, 0, 6);
    t.displayMode = clampInt(t.displayMode, 0, kDisplayModeCount - 1);
    t.missingMode = clampInt(t.missingMode, 0, 1);
    auto finiteOr = [](float v, float d) { return std::isfinite(v) ? v : d; };
    t.x = std::clamp(finiteOr(t.x, 500.0f), 0.0f, kMaxCoord);
    t.y = std::clamp(finiteOr(t.y, 300.0f), 0.0f, kMaxCoord);
    t.fontScale = std::clamp(finiteOr(t.fontScale, 2.0f), kMinFontScale, kMaxFontScale);
    t.iconSize = std::clamp(finiteOr(t.iconSize, 42.0f), kMinIconSize, kMaxIconSize);
    for (Color* c : { &t.color, &t.backgroundColor }) {
        c->r = Clamp01(c->r); c->g = Clamp01(c->g); c->b = Clamp01(c->b); c->a = Clamp01(c->a);
    }
    if (t.label.size() > kMaxTextLen) t.label.resize(kMaxTextLen);
    if (t.pattern.size() > kMaxTextLen) t.pattern.resize(kMaxTextLen);
    if (t.customText.size() > kMaxTextLen) t.customText.resize(kMaxTextLen);
    if (t.iconPath.size() > kMaxPathLen) t.iconPath.resize(kMaxPathLen);
    // Active skills carry no remaining time: drop the time part for them.
    if (static_cast<SourceMode>(t.source) == SourceMode::ActiveSkill) {
        DisplayParts p = Decompose(static_cast<DisplayMode>(t.displayMode));
        if (p.time) {
            p.time = false;
            t.displayMode = static_cast<int>(Compose(p));
        }
    }
}

inline bool Matches(const std::string& name, const std::string& pattern, MatchMode mode) {
    if (pattern.empty()) return false;
    switch (mode) {
        case MatchMode::Exact:    return name == pattern;
        case MatchMode::Prefix:   return name.size() >= pattern.size() && name.compare(0, pattern.size(), pattern) == 0;
        case MatchMode::Contains: return name.find(pattern) != std::string::npos;
    }
    return false;
}

inline bool StageMatches(int actual, StageCompare mode, int expected) {
    switch (mode) {
        case StageCompare::Any:          return true;
        case StageCompare::Equal:        return actual == expected;
        case StageCompare::NotEqual:     return actual != expected;
        case StageCompare::Greater:      return actual > expected;
        case StageCompare::GreaterEqual: return actual >= expected;
        case StageCompare::Less:         return actual < expected;
        case StageCompare::LessEqual:    return actual <= expected;
    }
    return true;
}

// The minimum a tracker needs from a buff / skill (decoupled from the SDK
// structs so tests can feed plain values).
struct BuffSample {
    std::string name;
    float totalTime = 0.0f;
    float timeLeft = 0.0f;
    int charges = 0;
};

struct SkillSample {
    std::string name;
    int useStage = 0;
    int totalUses = 0;
    int maxUses = 0;
    int activeCooldowns = 0;
    bool canBeUsed = false;
};

inline bool IsPermanent(const BuffSample& b) {
    return b.totalTime <= 0.0f || !std::isfinite(b.timeLeft) || !std::isfinite(b.totalTime);
}

inline int SkillValueOf(const SkillSample& s, SkillValue v) {
    switch (v) {
        case SkillValue::UseStage:         return s.useStage;
        case SkillValue::TotalUses:        return s.totalUses;
        case SkillValue::AvailableCharges: return std::max(0, s.maxUses - s.activeCooldowns);
        case SkillValue::MaxUses:          return s.maxUses;
        case SkillValue::CanBeUsed:        return s.canBeUsed ? 1 : 0;
    }
    return s.useStage;
}

// "totem_ancestral_bond_reservation_79" -> prefix "totem_ancestral_bond_reservation_":
// names whose trailing number changes are matched by their prefix.
inline bool SplitNumericSuffix(const std::string& name, std::string& prefix) {
    size_t end = name.size();
    while (end > 0 && name[end - 1] >= '0' && name[end - 1] <= '9') --end;
    if (end == name.size() || end < 2 || name[end - 1] != '_') return false;
    prefix = name.substr(0, end);
    return true;
}

// What a tracker resolved to at one sample.
struct TrackerState {
    bool found = false;
    int value = 0;
    bool timed = false;       // at least one matching buff has a finite duration
    bool permanent = false;   // at least one matching buff is permanent
    float timeLeft = 0.0f;    // longest remaining time among timed matches
};

inline TrackerState Evaluate(const Tracker& t, const std::vector<BuffSample>& buffs,
                             const std::vector<SkillSample>& skills) {
    TrackerState s;
    const MatchMode mode = static_cast<MatchMode>(t.matchMode);
    if (static_cast<SourceMode>(t.source) == SourceMode::ActiveSkill) {
        const SkillValue v = static_cast<SkillValue>(t.skillValue);
        for (const SkillSample& sk : skills) {
            if (!Matches(sk.name, t.pattern, mode)) continue;
            const int candidate = SkillValueOf(sk, v);
            if (!StageMatches(candidate, static_cast<StageCompare>(t.stageCompare), t.stageValue)) continue;
            s.found = true;
            s.value = candidate;
            break;
        }
        return s;
    }
    for (const BuffSample& b : buffs) {
        if (!Matches(b.name, t.pattern, mode)) continue;
        if (!s.found) {
            s.found = true;
            s.value = b.charges;
        } else if (t.sumCharges) {
            s.value += b.charges;
        }
        // Several matches: show the longest remaining duration, since the
        // tracked effect lasts until the last matching timed buff ends.
        if (IsPermanent(b)) {
            s.permanent = true;
        } else {
            const float remaining = std::max(0.0f, b.timeLeft);
            if (!s.timed || remaining > s.timeLeft) s.timeLeft = remaining;
            s.timed = true;
        }
        if (!t.sumCharges) break;
    }
    return s;
}

// Formats the text part of a tracker into buf (no allocation; called per
// frame). elapsed = seconds since the sample, subtracted from the time left.
inline void FormatTrackerText(const Tracker& t, const TrackerState& s, float elapsed,
                              char* buf, size_t size) {
    if (!buf || size == 0) return;
    buf[0] = '\0';
    const DisplayParts parts = Decompose(static_cast<DisplayMode>(t.displayMode));
    const bool skill = static_cast<SourceMode>(t.source) == SourceMode::ActiveSkill;

    char value[32] = "";
    if (parts.text == TextPart::Value) {
        if (skill && static_cast<SkillValue>(t.skillValue) == SkillValue::CanBeUsed)
            std::snprintf(value, sizeof(value), "%s", (s.found && s.value != 0) ? "ready" : "not ready");
        else
            std::snprintf(value, sizeof(value), "%d", s.found ? s.value : 0);
    }

    char time[32] = "";
    if (parts.time && !skill) {
        // Permanent wins over a timed match only when nothing timed matched.
        if (s.found && !s.timed && s.permanent) {
            // The host font carries no U+221E; the merged icon font has an infinity sign.
            std::snprintf(time, sizeof(time), "%s", "\xef\x94\xb4");  // ICON_FA_INFINITY
        } else {
            float left = s.found ? s.timeLeft - std::max(0.0f, elapsed) : 0.0f;
            if (left < 0.0f) left = 0.0f;
            if (left >= 60.0f)
                std::snprintf(time, sizeof(time), "%d:%02d", static_cast<int>(left) / 60, static_cast<int>(left) % 60);
            else
                std::snprintf(time, sizeof(time), "%.1fs", left);
        }
    }

    const char* text = parts.text == TextPart::Custom ? t.customText.c_str()
                     : parts.text == TextPart::Value  ? value : "";
    if (text[0] && time[0])
        std::snprintf(buf, size, "%s  %s", text, time);
    else
        std::snprintf(buf, size, "%s", text[0] ? text : time);
}

} // namespace BuffOverlay
