// Buff Overlay: shows chosen buffs and active-skill values as icons and text
// at fixed screen positions.
#pragma once

#include "../../../POEFixer/plugin_sdk/PluginSDK.h"
#include "../../../POEFixer/imgui/imgui.h"

#include "IconTexture.h"
#include "Settings.h"
#include "Tracker.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace BuffOverlay {

constexpr const char* kPluginName = "Buff Overlay";

// A tracker's name as shown: the example tracker's shipped English name in the program
// language while it is unchanged (display only), `emptyText` for a tracker without a name.
inline const char* ShownLabel(const Tracker& t, const char* emptyText) {
    if (t.label.empty()) return emptyText;
    if (t.label == kExampleTrackerLabel) return I18n::Tr("tracker_example_label");
    return t.label.c_str();
}

class BuffOverlayPlugin final : public PluginSDK::Plugin {
public:
    const char* GetName() const override { return kPluginName; }
    void OnEnable(bool isGameAttached) override;
    void OnDisable() override;
    bool WantsOverlay() const override { return true; }
    void DrawSettings() override;
    void DrawUI() override;
    void SaveSettings() override;

private:
    using Clock = std::chrono::steady_clock;

    // ---- data (BuffOverlay.cpp) -------------------------------------------
    void SetImGuiContext() const;
    // rulesChanged: what a tracker matches changed, so re-read the game now.
    void MarkDirty(bool rulesChanged = true);
    void FlushSettingsIfDue(bool force);
    void AdoptLoaded(LoadOrigin origin, Settings&& loaded);
    void RetryBlockedLoad();
    void Sample(Clock::time_point now, bool inGame);
    void EvaluateTrackers();
    bool ListsWanted(Clock::time_point now) const { return now < m_listsWantedUntil; }
    void WantLists() { m_listsWantedUntil = Clock::now() + std::chrono::milliseconds(1500); }
    const IconTexture* IconFor(const Tracker& t);
    // An icon dropped from the cache may already be queued with AddImage in this
    // frame's draw lists: its texture is kept alive until a later frame.
    void RetireIcon(IconTexture& tex);
    void RetireAllIcons();
    void ReleaseRetiredIcons();
    void UpdateOverlayInputWish(bool inGame);
    float SecondsSinceSample() const;
    // The game's client size (tracker positions are game pixels).
    ImVec2 GameScreenSize() const;

    // ---- overlay (OverlayUI.cpp) ------------------------------------------
    struct Visual {
        bool icon = false;          // icon part requested
        const IconTexture* tex = nullptr;
        char text[192] = "";
        ImVec2 size{ 0, 0 };        // total size at scale 1 (pixels)
        float iconSize = 0, gap = 0, fontSize = 0;
    };
    void BuildVisual(const Tracker& t, const TrackerState& s, float elapsed, bool placeholders, Visual& v);
    void DrawVisual(ImDrawList* dl, const Tracker& t, const Visual& v, ImVec2 origin, float scale, float alpha) const;
    void DrawTrackers();
    void DrawArrangeFrames();
    void DrawArrangeBanner();

    // ---- settings tab (SettingsUI.cpp) ------------------------------------
    void DrawSectionGeneral();
    void DrawSectionTrackerList();
    void DrawSectionWatch(Tracker& t);
    void DrawSectionShow(Tracker& t);
    void DrawSectionLook(Tracker& t);
    void DrawSectionDiagnostics();
    void DrawPatternPicker(Tracker& t);
    void DrawIconPicker(Tracker& t);
    void DrawPreview(const Tracker& t, size_t index);
    void ScanIconFolder();
    void SetArrange(bool on);

    Settings m_settings;
    bool m_dirty = false;
    Clock::time_point m_dirtySince{};
    const char* m_saveError = nullptr;   // a key of lang/en.json; null: none
    LoadOrigin m_loadOrigin = LoadOrigin::Defaults;
    // The settings file exists but could not be read: nothing is written over it
    // until the player changes a setting; it is read again a few times meanwhile.
    bool m_loadBlocked = false;
    int m_loadRetriesLeft = 0;
    Clock::time_point m_loadRetryAt{};

    // Sampled game data (refreshed every kSampleIntervalMs, not every frame).
    std::vector<BuffSample> m_buffs;
    std::vector<SkillSample> m_skills;
    std::vector<TrackerState> m_states;
    Clock::time_point m_sampleTime{};
    bool m_haveSample = false;
    bool m_forceSample = true;
    bool m_inGame = false;
    Clock::time_point m_listsWantedUntil{};

    // Icon textures by the path as entered (resolved only on first use), shared
    // by trackers using the same file; the per-frame lookup allocates nothing.
    std::unordered_map<std::string, IconTexture> m_icons;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_retiredIcons;
    uint64_t m_drawTick = 0;      // DrawUI calls so far (one per host loop iteration)
    uint64_t m_retiredTick = 0;   // m_drawTick when the newest retired icon was dropped

    // Session-only UI state.
    bool m_arrange = false;
    bool m_inputWish = false;
    int m_selected = 0;
    int m_dragging = -1;
    ImGuiID m_settingsWindowId = 0;
    // The ImGui context that window lives in (only compared, never used): a
    // separate menu window has its own, where the id means nothing.
    const ImGuiContext* m_settingsWindowContext = nullptr;
    std::vector<std::string> m_iconFiles;
    bool m_iconFolderMissing = false;
};

} // namespace BuffOverlay
