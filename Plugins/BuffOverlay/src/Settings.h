// Persistent settings: config/settings.json next to the DLL. The upstream
// key=value file (config/settings.txt) is read once when no JSON exists yet,
// so players who used the original plugin keep their trackers.
#pragma once

#include "Tracker.h"

#include <filesystem>
#include <string>
#include <vector>

namespace BuffOverlay {

struct Settings {
    std::vector<Tracker> trackers;
    bool textOutline = true;   // dark outline around overlay text
    float opacity = 1.0f;      // multiplies every overlay colour's alpha
};

enum class LoadOrigin {
    Defaults,   // no file at all: first start
    Json,       // config/settings.json
    Legacy,     // migrated from the upstream config/settings.txt
    Unreadable, // a file exists but could not be read: empty settings, never written over it
};

// Parses the upstream key=value format. Unknown keys and malformed values are
// skipped (the field keeps its default).
Settings ParseLegacySettings(const std::string& text);

// Parses settings.json content. Returns false (and leaves out untouched) when
// the text is not a JSON object.
bool ParseJsonSettings(const std::string& text, Settings& out);

std::string SerializeJsonSettings(const Settings& s);

// Loads from <dir>/config. Never throws; falls back to defaults.
Settings LoadSettingsFrom(const std::filesystem::path& dir, LoadOrigin& origin);

// Writes <dir>/config/settings.json atomically. Returns false on I/O failure.
bool SaveSettingsTo(const std::filesystem::path& dir, const Settings& s);

// Best effort: copies an unreadable config/settings.json to settings.json.unreadable
// before the first save replaces it.
void KeepUnreadableCopy(const std::filesystem::path& dir);

// The example tracker a first start shows.
Tracker MakeExampleTracker();

} // namespace BuffOverlay
