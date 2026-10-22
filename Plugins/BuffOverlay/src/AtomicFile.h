// Small file helpers: whole-file read and an atomic replace (temp file in the
// same folder, flushed, then MoveFileExW over the target), so a crash or a
// full disk never leaves a half-written settings file behind.
#pragma once

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace BuffOverlay {

// True when something exists at `path`, even if it cannot be opened right now
// (another program holds it, access denied). Only "not found" counts as absent,
// so a temporarily locked settings file is never mistaken for a first start.
inline bool PathPresent(const std::filesystem::path& path) {
    if (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    const DWORD err = ::GetLastError();
    return err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND;
}

inline std::optional<std::string> ReadWholeFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) return std::nullopt;
    return ss.str();
}

inline bool WriteFileAtomic(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace BuffOverlay
