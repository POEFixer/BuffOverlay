#include "Settings.h"

#include "AtomicFile.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <climits>
#include <cstdlib>

namespace BuffOverlay {
namespace {

using nlohmann::json;

std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool ParseInt(const std::string& s, int& out) {
    const std::string t = Trim(s);
    int v = 0;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    if (r.ec != std::errc() || r.ptr != t.data() + t.size()) return false;
    out = v;
    return true;
}

bool ParseFloat(const std::string& s, float& out) {
    const std::string t = Trim(s);
    float v = 0.0f;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    if (r.ec != std::errc() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool ParseColor(const std::string& s, Color& out) {
    float c[4] = { out.r, out.g, out.b, out.a };
    size_t start = 0;
    for (int n = 0; n < 4; ++n) {
        const size_t comma = s.find(',', start);
        const std::string part = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!ParseFloat(part, c[n])) return false;
        if (comma == std::string::npos) {
            if (n < 3) return false;
            break;
        }
        start = comma + 1;
    }
    out = Color{ c[0], c[1], c[2], c[3] };
    return true;
}

// Path strings as written by the original plugin may carry surrounding quotes.
std::string NormalizePath(std::string path) {
    path = Trim(path);
    while (path.size() >= 2) {
        const char f = path.front(), l = path.back();
        if ((f == '"' && l == '"') || (f == '\'' && l == '\''))
            path = Trim(path.substr(1, path.size() - 2));
        else
            break;
    }
    return path;
}

json ColorToJson(const Color& c) { return json::array({ c.r, c.g, c.b, c.a }); }

void ColorFromJson(const json& j, Color& c) {
    if (!j.is_array() || j.size() != 4) return;
    float v[4];
    for (size_t i = 0; i < 4; ++i) {
        if (!j[i].is_number()) return;
        v[i] = j[i].get<float>();
    }
    c = Color{ v[0], v[1], v[2], v[3] };
}

template <typename T>
void Read(const json& j, const char* key, T& out) {
    const auto it = j.find(key);
    if (it == j.end()) return;
    if constexpr (std::is_same_v<T, bool>) {
        if (it->is_boolean()) out = it->get<bool>();
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (it->is_string()) out = it->get<std::string>();
    } else if constexpr (std::is_integral_v<T>) {
        if (it->is_number_integer()) {
            const long long v = it->get<long long>();
            if (v >= INT_MIN && v <= INT_MAX) out = static_cast<T>(v);
        }
    } else {
        if (it->is_number()) out = it->get<T>();
    }
}

json TrackerToJson(const Tracker& t) {
    return json{
        { "enabled", t.enabled },
        { "label", t.label },
        { "pattern", t.pattern },
        { "source", t.source },
        { "matchMode", t.matchMode },
        { "skillValue", t.skillValue },
        { "stageCompare", t.stageCompare },
        { "stageValue", t.stageValue },
        { "displayMode", t.displayMode },
        { "missingMode", t.missingMode },
        { "sumCharges", t.sumCharges },
        { "customText", t.customText },
        { "iconPath", t.iconPath },
        { "x", t.x },
        { "y", t.y },
        { "fontScale", t.fontScale },
        { "iconSize", t.iconSize },
        { "color", ColorToJson(t.color) },
        { "textBackground", t.textBackground },
        { "backgroundColor", ColorToJson(t.backgroundColor) },
    };
}

Tracker TrackerFromJson(const json& j) {
    Tracker t;
    if (!j.is_object()) return t;
    Read(j, "enabled", t.enabled);
    Read(j, "label", t.label);
    Read(j, "pattern", t.pattern);
    Read(j, "source", t.source);
    Read(j, "matchMode", t.matchMode);
    Read(j, "skillValue", t.skillValue);
    Read(j, "stageCompare", t.stageCompare);
    Read(j, "stageValue", t.stageValue);
    Read(j, "displayMode", t.displayMode);
    Read(j, "missingMode", t.missingMode);
    Read(j, "sumCharges", t.sumCharges);
    Read(j, "customText", t.customText);
    Read(j, "iconPath", t.iconPath);
    Read(j, "x", t.x);
    Read(j, "y", t.y);
    Read(j, "fontScale", t.fontScale);
    Read(j, "iconSize", t.iconSize);
    if (const auto it = j.find("color"); it != j.end()) ColorFromJson(*it, t.color);
    Read(j, "textBackground", t.textBackground);
    if (const auto it = j.find("backgroundColor"); it != j.end()) ColorFromJson(*it, t.backgroundColor);
    t.iconPath = NormalizePath(t.iconPath);
    Sanitize(t);
    return t;
}

} // namespace

Tracker MakeExampleTracker() {
    Tracker t;
    t.label = "Ancestral Bond charges";
    t.pattern = "totem_ancestral_bond_reservation_";
    t.matchMode = static_cast<int>(MatchMode::Prefix);
    t.displayMode = static_cast<int>(DisplayMode::Charges);
    t.missingMode = static_cast<int>(MissingMode::ShowZero);
    t.x = 800.0f;
    t.y = 500.0f;
    t.fontScale = 2.5f;
    return t;
}

Settings ParseLegacySettings(const std::string& text) {
    Settings s;
    std::vector<std::pair<std::string, std::string>> kv;
    size_t trackerCount = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = line.substr(eq + 1);
        if (key == "TrackerCount") {
            int n = 0;
            if (ParseInt(val, n) && n > 0) trackerCount = static_cast<size_t>(n);
            continue;
        }
        kv.emplace_back(std::move(key), std::move(val));
    }
    s.trackers.resize(std::min(trackerCount, kMaxTrackers));

    for (const auto& [key, val] : kv) {
        // "Preview" and "Debug" were persisted upstream; both are session-only now.
        if (key.size() < 4 || key[0] != 'T') continue;
        const size_t dot = key.find('.');
        if (dot == std::string::npos || dot < 2) continue;
        int idx = -1;
        if (!ParseInt(key.substr(1, dot - 1), idx) || idx < 0 || static_cast<size_t>(idx) >= s.trackers.size())
            continue;
        Tracker& t = s.trackers[static_cast<size_t>(idx)];
        const std::string field = key.substr(dot + 1);
        int iv = 0;
        float fv = 0.0f;
        if (field == "Enabled") t.enabled = Trim(val) == "1";
        else if (field == "Label") t.label = val;
        else if (field == "Pattern") t.pattern = val;
        else if (field == "SourceMode") { if (ParseInt(val, iv)) t.source = iv; }
        else if (field == "MatchMode") { if (ParseInt(val, iv)) t.matchMode = iv; }
        else if (field == "ActiveSkillValue") { if (ParseInt(val, iv)) t.skillValue = iv; }
        else if (field == "StageCompare") { if (ParseInt(val, iv)) t.stageCompare = iv; }
        else if (field == "StageValue") { if (ParseInt(val, iv)) t.stageValue = iv; }
        else if (field == "DisplayMode") { if (ParseInt(val, iv)) t.displayMode = iv; }
        else if (field == "MissingMode") { if (ParseInt(val, iv)) t.missingMode = iv; }
        else if (field == "CustomText") t.customText = val;
        else if (field == "IconPath") t.iconPath = NormalizePath(val);
        else if (field == "X") { if (ParseFloat(val, fv)) t.x = fv; }
        else if (field == "Y") { if (ParseFloat(val, fv)) t.y = fv; }
        else if (field == "FontScale") { if (ParseFloat(val, fv)) t.fontScale = fv; }
        else if (field == "IconSize") { if (ParseFloat(val, fv)) t.iconSize = fv; }
        else if (field == "SumCharges") t.sumCharges = Trim(val) == "1";
        else if (field == "Color") ParseColor(val, t.color);
        else if (field == "TextBackground") t.textBackground = Trim(val) == "1";
        else if (field == "BackgroundColor") ParseColor(val, t.backgroundColor);
    }
    for (Tracker& t : s.trackers) Sanitize(t);
    return s;
}

bool ParseJsonSettings(const std::string& text, Settings& out) {
    const json j = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (!j.is_object()) return false;
    Settings s;
    Read(j, "textOutline", s.textOutline);
    Read(j, "opacity", s.opacity);
    s.opacity = std::isfinite(s.opacity) ? std::clamp(s.opacity, 0.1f, 1.0f) : 1.0f;
    if (const auto it = j.find("trackers"); it != j.end() && it->is_array()) {
        for (const json& tj : *it) {
            if (s.trackers.size() >= kMaxTrackers) break;
            s.trackers.push_back(TrackerFromJson(tj));
        }
    }
    out = std::move(s);
    return true;
}

std::string SerializeJsonSettings(const Settings& s) {
    json trackers = json::array();
    for (const Tracker& t : s.trackers) trackers.push_back(TrackerToJson(t));
    const json j{
        { "version", 1 },
        { "textOutline", s.textOutline },
        { "opacity", s.opacity },
        { "trackers", std::move(trackers) },
    };
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}

Settings LoadSettingsFrom(const std::filesystem::path& dir, LoadOrigin& origin) {
    const std::filesystem::path cfg = dir / L"config";
    Settings s;
    if (const auto text = ReadWholeFile(cfg / L"settings.json")) {
        if (ParseJsonSettings(*text, s)) {
            origin = LoadOrigin::Json;
            return s;
        }
        // A damaged file is kept aside instead of being overwritten silently.
        std::error_code ec;
        std::filesystem::copy_file(cfg / L"settings.json", cfg / L"settings.json.corrupt",
                                   std::filesystem::copy_options::overwrite_existing, ec);
    } else if (PathPresent(cfg / L"settings.json")) {
        // Present but not readable (locked, denied): not a first start.
        origin = LoadOrigin::Unreadable;
        return s;
    }
    if (const auto text = ReadWholeFile(cfg / L"settings.txt")) {
        origin = LoadOrigin::Legacy;
        return ParseLegacySettings(*text);
    }
    if (PathPresent(cfg / L"settings.txt")) {
        origin = LoadOrigin::Unreadable;
        return s;
    }
    origin = LoadOrigin::Defaults;
    s.trackers.push_back(MakeExampleTracker());
    return s;
}

bool SaveSettingsTo(const std::filesystem::path& dir, const Settings& s) {
    return WriteFileAtomic(dir / L"config" / L"settings.json", SerializeJsonSettings(s));
}

void KeepUnreadableCopy(const std::filesystem::path& dir) {
    const std::filesystem::path cfg = dir / L"config";
    ::CopyFileW((cfg / L"settings.json").c_str(), (cfg / L"settings.json.unreadable").c_str(), FALSE);
}

} // namespace BuffOverlay
