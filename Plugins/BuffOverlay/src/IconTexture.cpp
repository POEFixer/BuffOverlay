#include "IconTexture.h"

#include <Windows.h>
#include <wincodec.h>

#include <cstdio>
#include <system_error>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace BuffOverlay {
namespace {

// A failure with its HRESULT: `key` (I18n::Key) names it, its text shows the {code}.
LocText HrText(const char* key, HRESULT hr) {
    return LocText{ key, { { "code", Num("0x%08lX", static_cast<unsigned long>(hr)) } } };
}

} // namespace

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::filesystem::path ResolveIconPath(const std::filesystem::path& pluginDir, const std::string& utf8) {
    if (utf8.empty()) return {};
    std::filesystem::path p(Utf8ToWide(utf8));
    if (p.is_relative()) p = pluginDir / p;
    return p.lexically_normal();
}

bool LoadIconTexture(ID3D11Device* device, const std::filesystem::path& path, IconTexture& out) {
    out.srv.Reset();
    out.width = out.height = 0;
    out.attemptedPath = path.wstring();
    out.error = LocText{};

    if (!device) { out.error = LocText{ I18n::Key("icon_error_no_device") }; return false; }
    if (path.empty()) { out.error = LocText{ I18n::Key("icon_error_no_file") }; return false; }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) { out.error = LocText{ I18n::Key("icon_error_not_found") }; return false; }

    // The render thread may already have COM in either apartment; both work for WIC.
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) { out.error = HrText(I18n::Key("icon_error_com"), initHr); return false; }
    struct ComGuard { bool on; ~ComGuard() { if (on) CoUninitialize(); } } guard{ uninit };

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) { out.error = HrText(I18n::Key("icon_error_decoder"), hr); return false; }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
    if (FAILED(hr)) { out.error = HrText(I18n::Key("icon_error_not_image"), hr); return false; }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) { out.error = HrText(I18n::Key("icon_error_decode"), hr); return false; }

    UINT w = 0, h = 0;
    hr = frame->GetSize(&w, &h);
    if (FAILED(hr) || w == 0 || h == 0) { out.error = LocText{ I18n::Key("icon_error_no_size") }; return false; }
    if (w > kMaxIconPixels || h > kMaxIconPixels) {
        out.error = LocText{ I18n::Key("icon_error_too_large"),
                             { { "width", Num("%u", w) }, { "height", Num("%u", h) }, { "max", Num("%u", kMaxIconPixels) } } };
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { out.error = HrText(I18n::Key("icon_error_convert"), hr); return false; }

    const UINT stride = w * 4;
    std::vector<unsigned char> pixels(static_cast<size_t>(stride) * h);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) { out.error = HrText(I18n::Key("icon_error_pixels"), hr); return false; }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels.data();
    init.SysMemPitch = stride;

    ComPtr<ID3D11Texture2D> texture;
    hr = device->CreateTexture2D(&desc, &init, texture.GetAddressOf());
    if (FAILED(hr)) { out.error = HrText(I18n::Key("icon_error_texture"), hr); return false; }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(texture.Get(), &srvDesc, out.srv.GetAddressOf());
    if (FAILED(hr)) { out.error = HrText(I18n::Key("icon_error_texture_view"), hr); out.srv.Reset(); return false; }

    out.width = static_cast<int>(w);
    out.height = static_cast<int>(h);
    return true;
}

} // namespace BuffOverlay
