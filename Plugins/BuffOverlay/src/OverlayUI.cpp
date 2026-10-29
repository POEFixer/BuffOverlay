// Buff Overlay — what is drawn over the game: the trackers themselves and the
// arrange mode (drag frames and the "Done" banner).
#include "BuffOverlayPlugin.h"

#include "../../../POEFixer/imgui/IconsFontAwesome6.h"
#include "../../../POEFixer/imgui/imgui_internal.h"
#include "../../../POEFixer/plugin_sdk/PluginUi.h"

#include <cfloat>
#include <cmath>
#include <cstdio>

namespace BuffOverlay {
namespace {

ImU32 ToU32(const Color& c, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, std::clamp(c.a * alpha, 0.0f, 1.0f)));
}

ImVec2 Snap(ImVec2 p) { return ImVec2(std::floor(p.x + 0.5f), std::floor(p.y + 0.5f)); }

// Text with a dark outline so it reads on bright and dark scenes alike.
void OutlinedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, ImU32 outline,
                  bool withOutline, const char* text) {
    if (withOutline) {
        const float o = std::max(1.0f, std::floor(size / 18.0f + 0.5f));
        static const float kDirs[8][2] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
        for (const auto& d : kDirs)
            dl->AddText(font, size, ImVec2(pos.x + d[0] * o, pos.y + d[1] * o), outline, text);
    }
    dl->AddText(font, size, pos, col, text);
}

} // namespace

void BuffOverlayPlugin::BuildVisual(const Tracker& t, const TrackerState& s, float elapsed, bool placeholders, Visual& v) {
    DisplayParts parts = Decompose(static_cast<DisplayMode>(t.displayMode));
    v.tex = nullptr;
    if (parts.icon) {
        const IconTexture* tex = IconFor(t);
        if (tex && tex->srv) v.tex = tex;
        // Without a loaded icon the text moves left, except where the empty
        // slot is shown on purpose (arrange mode, settings preview).
        else if (!placeholders) parts.icon = false;
    }
    v.icon = parts.icon;
    const TrackerWords words{ I18n::Tr("overlay_ready"), I18n::Tr("overlay_not_ready"), I18n::Tr("overlay_seconds") };
    FormatTrackerText(t, s, elapsed, words, v.text, sizeof(v.text));
    v.fontSize = ImGui::GetFontSize() * t.fontScale;
    v.iconSize = parts.icon ? std::floor(t.iconSize + 0.5f) : 0.0f;
    ImVec2 textSize(0, 0);
    if (v.text[0]) {
        textSize = ImGui::GetFont()->CalcTextSizeA(v.fontSize, FLT_MAX, 0.0f, v.text);
        if (t.textBackground) {
            textSize.x += std::floor(v.fontSize * 0.25f + 0.5f) * 2.0f;
            textSize.y += std::floor(v.fontSize * 0.10f + 0.5f) * 2.0f;
        }
    }
    v.gap = (parts.icon && v.text[0]) ? std::floor(v.fontSize * 0.25f + 0.5f) : 0.0f;
    v.size = ImVec2(v.iconSize + v.gap + textSize.x, std::max(v.iconSize, textSize.y));
}

void BuffOverlayPlugin::DrawVisual(ImDrawList* dl, const Tracker& t, const Visual& v, ImVec2 origin,
                                   float scale, float alpha) const {
    origin = Snap(origin);
    const float opacity = m_settings.opacity * alpha;
    const float h = v.size.y * scale;
    float x = origin.x;

    if (v.icon) {
        const float is = std::floor(v.iconSize * scale + 0.5f);
        const ImVec2 a = Snap(ImVec2(x, origin.y + (h - is) * 0.5f));
        const ImVec2 b(a.x + is, a.y + is);
        if (v.tex) {
            const ImTextureID id = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(v.tex->srv.Get()));
            dl->AddImage(ImTextureRef(id), a, b, ImVec2(0, 0), ImVec2(1, 1),
                         ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, std::clamp(opacity, 0.0f, 1.0f))));
        } else {
            // Placeholder (only reached in arrange mode and the settings preview).
            dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, static_cast<int>(110 * opacity)), is * 0.12f);
            // ImGui 1.93: rounding, thickness, flags; the stroke lies inside [a, b].
            dl->AddRect(a, b, IM_COL32(255, 255, 255, static_cast<int>(140 * opacity)), is * 0.12f, 1.0f);
            const float fs = is * 0.5f;
            const ImVec2 qs = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, ICON_FA_IMAGE);
            dl->AddText(ImGui::GetFont(), fs, Snap(ImVec2(a.x + (is - qs.x) * 0.5f, a.y + (is - qs.y) * 0.5f)),
                        IM_COL32(255, 255, 255, static_cast<int>(160 * opacity)), ICON_FA_IMAGE);
        }
        x = b.x + std::floor(v.gap * scale + 0.5f);
    }

    if (!v.text[0]) return;
    const float fs = v.fontSize * scale;
    ImFont* font = ImGui::GetFont();
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, v.text);
    float padX = 0.0f, padY = 0.0f;
    if (t.textBackground) {
        padX = std::floor(v.fontSize * 0.25f * scale + 0.5f);
        padY = std::floor(v.fontSize * 0.10f * scale + 0.5f);
    }
    const float blockH = ts.y + padY * 2.0f;
    const ImVec2 block = Snap(ImVec2(x, origin.y + (h - blockH) * 0.5f));
    if (t.textBackground) {
        dl->AddRectFilled(block, ImVec2(block.x + ts.x + padX * 2.0f, block.y + blockH),
                          ToU32(t.backgroundColor, opacity), std::floor(fs * 0.2f));
    }
    const ImU32 outline = IM_COL32(0, 0, 0, static_cast<int>(std::clamp(t.color.a * opacity * 0.85f, 0.0f, 1.0f) * 255.0f));
    OutlinedText(dl, font, fs, Snap(ImVec2(block.x + padX, block.y + padY)), ToU32(t.color, opacity), outline,
                 m_settings.textOutline, v.text);
}

void BuffOverlayPlugin::DrawTrackers() {
    if (m_states.size() != m_settings.trackers.size()) EvaluateTrackers();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float elapsed = SecondsSinceSample();
    Visual v;
    for (size_t i = 0; i < m_settings.trackers.size(); ++i) {
        const Tracker& t = m_settings.trackers[i];
        if (!t.enabled) continue;
        const TrackerState& s = m_states[i];
        const bool show = s.found || static_cast<MissingMode>(t.missingMode) == MissingMode::ShowZero || m_arrange;
        if (!show) continue;
        BuildVisual(t, s, elapsed, m_arrange, v);
        if (!v.icon && !v.text[0]) continue;  // icon-only tracker whose icon failed to load
        // Keep the tracker on screen even if the resolution shrank since it was placed.
        const float x = std::clamp(t.x, 0.0f, std::max(0.0f, display.x - v.size.x));
        const float y = std::clamp(t.y, 0.0f, std::max(0.0f, display.y - v.size.y));
        DrawVisual(dl, t, v, ImVec2(x, y), 1.0f, (m_arrange && !s.found) ? 0.6f : 1.0f);
    }
}

void BuffOverlayPlugin::DrawArrangeFrames() {
    ImGuiIO& io = ImGui::GetIO();
    const float pad = std::floor(ImGui::GetFontSize() * 0.35f + 0.5f);
    const float elapsed = SecondsSinceSample();
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(PluginUi::Colors().Accent);
    Visual v;
    char name[48];
    for (size_t i = 0; i < m_settings.trackers.size(); ++i) {
        Tracker& t = m_settings.trackers[i];
        if (!t.enabled) continue;
        BuildVisual(t, i < m_states.size() ? m_states[i] : TrackerState{}, elapsed, true, v);
        const ImVec2 size(std::max(v.size.x, ImGui::GetFontSize()) + pad * 2.0f,
                          std::max(v.size.y, ImGui::GetFontSize()) + pad * 2.0f);
        const float x = std::clamp(t.x, 0.0f, std::max(0.0f, io.DisplaySize.x - v.size.x));
        const float y = std::clamp(t.y, 0.0f, std::max(0.0f, io.DisplaySize.y - v.size.y));

        std::snprintf(name, sizeof(name), "##BuffOverlayArrange%zu", i);
        ImGui::SetNextWindowPos(ImVec2(x - pad, y - pad), ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin(name, nullptr, flags)) {
            // A new window opens in front of everything; keep the frames behind
            // the settings window so they never cover (or catch clicks on) it.
            // A separate menu window is another ImGui context (and OS window).
            if (ImGui::IsWindowAppearing() && m_settingsWindowId != 0 &&
                m_settingsWindowContext == ImGui::GetCurrentContext()) {
                if (ImGuiWindow* settings = ImGui::FindWindowByID(m_settingsWindowId))
                    ImGui::BringWindowToDisplayBehind(ImGui::GetCurrentWindow(), settings);
            }
            ImGui::InvisibleButton("##drag", size);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            if (ImGui::IsItemActivated()) {
                m_dragging = static_cast<int>(i);
                m_selected = static_cast<int>(i);
            }
            if (active && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
                t.x = std::clamp(x + io.MouseDelta.x, 0.0f, std::max(0.0f, io.DisplaySize.x - v.size.x));
                t.y = std::clamp(y + io.MouseDelta.y, 0.0f, std::max(0.0f, io.DisplaySize.y - v.size.y));
            }
            if (ImGui::IsItemDeactivated() && m_dragging == static_cast<int>(i)) {
                t.x = std::floor(t.x + 0.5f);
                t.y = std::floor(t.y + 0.5f);
                m_dragging = -1;
                MarkDirty(false);
            }
            if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 a = Snap(ImGui::GetWindowPos());
            const ImVec2 b(a.x + std::floor(size.x), a.y + std::floor(size.y));
            const bool strong = hovered || active || m_selected == static_cast<int>(i);
            dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, strong ? 70 : 40), pad);
            dl->AddRect(a, b, strong ? accent : IM_COL32(255, 255, 255, 150), pad, 1.0f);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        // Name tag under the frame so trackers are easy to tell apart.
        const char* label = ShownLabel(t, I18n::Tr("arrange_tracker"));
        const float fs = ImGui::GetFontSize() * 0.85f;
        const ImVec2 ls = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
        ImVec2 lp = Snap(ImVec2(x - pad, y + v.size.y + pad + 2.0f));
        if (lp.y + ls.y > io.DisplaySize.y) lp.y = std::floor(y - pad - ls.y - 2.0f);
        ImDrawList* fg = ImGui::GetBackgroundDrawList();
        fg->AddRectFilled(ImVec2(lp.x - 3.0f, lp.y - 1.0f), ImVec2(lp.x + ls.x + 3.0f, lp.y + ls.y + 1.0f),
                          IM_COL32(0, 0, 0, 170), 3.0f);
        fg->AddText(ImGui::GetFont(), fs, lp, IM_COL32(255, 255, 255, 230), label);
    }
}

void BuffOverlayPlugin::DrawArrangeBanner() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(std::floor(io.DisplaySize.x * 0.5f), std::floor(ImGui::GetFontSize() * 3.0f)),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, std::max(ImGui::GetStyle().WindowRounding, 6.0f));
    if (ImGui::Begin("##BuffOverlayArrangeBanner", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, PluginUi::Colors().Accent);
        ImGui::TextUnformatted(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "  Buff Overlay");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(I18n::Tr("arrange_banner"));
        ImGui::SameLine();
        if (ImGui::Button(I18n::TrId("arrange_done"))) SetArrange(false);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace BuffOverlay
