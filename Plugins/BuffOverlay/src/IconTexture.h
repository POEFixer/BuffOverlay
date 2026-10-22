// Custom tracker icons: PNG / JPG / BMP decoded with the Windows Imaging
// Component into a D3D11 texture the overlay can draw.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>

namespace BuffOverlay {

struct IconTexture {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    int width = 0;
    int height = 0;
    // The resolved path of the last load attempt (also kept on failure, so a
    // broken path is not retried every frame) and why it failed.
    std::wstring attemptedPath;
    std::string error;

    void Reset() {
        srv.Reset();
        width = height = 0;
        attemptedPath.clear();
        error.clear();
    }
};

// Images larger than this on either side are refused (an icon never needs it,
// and a huge file would stall the frame that decodes it).
constexpr unsigned kMaxIconPixels = 1024;

bool LoadIconTexture(ID3D11Device* device, const std::filesystem::path& path, IconTexture& out);

// Resolves a tracker's icon path: absolute paths stay, relative ones are taken
// from the plugin folder (e.g. "icons/flask.png"). UTF-8 in, wide path out.
std::filesystem::path ResolveIconPath(const std::filesystem::path& pluginDir, const std::string& utf8);

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

} // namespace BuffOverlay
