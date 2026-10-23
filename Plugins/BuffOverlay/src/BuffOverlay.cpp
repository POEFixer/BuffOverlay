// Buff Overlay — lifecycle, settings persistence and the throttled game-data
// sample. Originally created by 1k4ru5g3 (github.com/1k4ru5g3/BuffOverlayPlugin);
// maintained by POEFixer.
#include "BuffOverlayPlugin.h"

#include "../../../POEFixer/imgui/imgui_internal.h"

// Plugin info: the host checks it before CreatePlugin; the publish tool reads it.
POEFIXER_PLUGIN_INFO(
    "BuffOverlay",
    "Buff Overlay",
    "1.0.3",
    "1k4ru5g3, POEFixer",
    "Shows chosen buffs and skill states as icons, charges, time left or custom text anywhere on screen, with a live preview and drag-to-place.")

namespace BuffOverlay {
namespace {

// Game data is read at most this often; the overlay interpolates time left
// between samples, so 20 reads per second look continuous.
constexpr auto kSampleInterval = std::chrono::milliseconds(50);
// Edits are written this long after the last change (and on disable/exit).
constexpr auto kSaveDelay = std::chrono::milliseconds(1000);
constexpr size_t kMaxIconCache = 64;
// A settings file that was locked at load is read again this often, this many times.
constexpr auto kLoadRetryEvery = std::chrono::seconds(2);
constexpr int kLoadRetries = 15;
constexpr const char* kUnreadableNotice =
    "config/settings.json exists but could not be read (another program may be using it). Your trackers are "
    "not shown and the file is left alone; it is read again in a moment. Changing a setting here replaces it "
    "(a copy is kept as settings.json.unreadable).";

BuffSample ToSample(const PluginSDK::Buff& b) {
    BuffSample s;
    s.name = b.Name;
    s.totalTime = b.TotalTime;
    s.timeLeft = b.TimeLeft;
    s.charges = b.Charges;
    return s;
}

SkillSample ToSample(const PluginSDK::ActiveSkill& k) {
    SkillSample s;
    s.name = k.Name;
    s.useStage = k.UseStage;
    s.totalUses = k.TotalUses;
    s.maxUses = k.MaxUses;
    s.activeCooldowns = k.TotalActiveCooldowns;
    s.canBeUsed = k.CanBeUsed;
    return s;
}

} // namespace

void BuffOverlayPlugin::SetImGuiContext() const {
    if (ctx()->ImGuiContext) ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
}

void BuffOverlayPlugin::OnEnable(bool /*isGameAttached*/) {
    SetImGuiContext();
    // Word wrapping reads character classes that this DLL's own ImGui copy
    // fills only when it builds a font atlas, which only the host does: without
    // this call every wrapped text breaks inside words.
    ImTextInitClassifiers();
    m_haveSample = false;
    m_arrange = false;
    m_inputWish = false;
    m_dirty = false;
    m_loadBlocked = false;
    m_loadRetriesLeft = kLoadRetries;
    m_loadRetryAt = Clock::now() + kLoadRetryEvery;
    LoadOrigin origin = LoadOrigin::Defaults;
    Settings loaded = LoadSettingsFrom(DirectoryPath(), origin);
    AdoptLoaded(origin, std::move(loaded));
}

void BuffOverlayPlugin::AdoptLoaded(LoadOrigin origin, Settings&& loaded) {
    m_settings = std::move(loaded);
    m_loadOrigin = origin;
    m_states.assign(m_settings.trackers.size(), TrackerState{});
    m_forceSample = true;
    m_selected = 0;
    m_dragging = -1;
    m_saveError.clear();
    if (origin == LoadOrigin::Unreadable) {
        m_loadBlocked = true;
        m_saveError = kUnreadableNotice;
        ctx()->Log.Warn("[BuffOverlay] config/settings.json exists but could not be read; it is not overwritten "
                        "and is read again shortly");
        return;
    }
    m_loadBlocked = false;
    // First start or migration from the original plugin's settings.txt: write
    // the JSON file now so the result is on disk even if the game crashes.
    if (origin != LoadOrigin::Json) {
        m_dirty = true;
        FlushSettingsIfDue(true);
    }
    if (origin == LoadOrigin::Legacy)
        ctx()->Log.Info("[BuffOverlay] Imported trackers from config/settings.txt into config/settings.json");
}

void BuffOverlayPlugin::RetryBlockedLoad() {
    // An edit in progress decides instead: it replaces the file when saved.
    if (!m_loadBlocked || m_loadRetriesLeft <= 0 || m_dirty) return;
    const Clock::time_point now = Clock::now();
    if (now < m_loadRetryAt) return;
    m_loadRetryAt = now + kLoadRetryEvery;
    --m_loadRetriesLeft;
    LoadOrigin origin = LoadOrigin::Defaults;
    Settings loaded = LoadSettingsFrom(DirectoryPath(), origin);
    if (origin == LoadOrigin::Unreadable) {
        if (m_loadRetriesLeft == 0)
            ctx()->Log.Warn("[BuffOverlay] config/settings.json is still unreadable; it is replaced only if a setting "
                            "is changed");
        return;
    }
    AdoptLoaded(origin, std::move(loaded));
    if (origin == LoadOrigin::Json) ctx()->Log.Info("[BuffOverlay] config/settings.json read on a later attempt");
}

void BuffOverlayPlugin::OnDisable() {
    FlushSettingsIfDue(true);
    if (m_inputWish) {
        ctx()->Overlay.SetWantsOverlayInput(false);
        m_inputWish = false;
    }
    m_arrange = false;
    m_dragging = -1;
    // Disabling can happen mid-frame after this frame's trackers were queued:
    // the textures go to the retired list (freed on a later frame or with the plugin).
    RetireAllIcons();
    ReleaseRetiredIcons();
    m_buffs.clear();
    m_skills.clear();
    m_states.clear();
    m_haveSample = false;
}

void BuffOverlayPlugin::SaveSettings() {
    // The host's periodic and exit saves never write the in-memory defaults
    // over a settings file that could not be read; a pending edit still flushes.
    if (!m_loadBlocked) m_dirty = true;
    FlushSettingsIfDue(true);
}

void BuffOverlayPlugin::MarkDirty(bool rulesChanged) {
    if (!m_dirty) m_dirtySince = Clock::now();
    m_dirty = true;
    if (m_loadBlocked) m_loadRetriesLeft = 0;   // the player's edit wins over a later read
    if (rulesChanged) m_forceSample = true;
}

void BuffOverlayPlugin::FlushSettingsIfDue(bool force) {
    if (!m_dirty) return;
    if (!force && Clock::now() - m_dirtySince < kSaveDelay) return;
    // Only an edit reaches here while blocked: keep the unreadable file aside first.
    if (m_loadBlocked) KeepUnreadableCopy(DirectoryPath());
    if (SaveSettingsTo(DirectoryPath(), m_settings)) {
        m_dirty = false;
        m_saveError.clear();
        if (m_loadBlocked) {
            m_loadBlocked = false;
            ctx()->Log.Warn("[BuffOverlay] Replaced the unreadable config/settings.json after a settings change");
        }
    } else {
        // Keep the edit pending and retry after the delay instead of every frame.
        m_dirtySince = Clock::now();
        m_saveError = "Could not write config/settings.json (is the folder read-only?). Retrying.";
    }
}

float BuffOverlayPlugin::SecondsSinceSample() const {
    if (!m_haveSample) return 0.0f;
    return std::chrono::duration<float>(Clock::now() - m_sampleTime).count();
}

void BuffOverlayPlugin::Sample(Clock::time_point now, bool inGame) {
    if (!inGame) {
        if (m_haveSample) {
            m_buffs.clear();
            m_skills.clear();
            m_haveSample = false;
            EvaluateTrackers();
        }
        return;
    }
    if (!m_forceSample && m_haveSample && now - m_sampleTime < kSampleInterval) return;
    m_forceSample = false;

    bool needBuffs = false, needSkills = false;
    for (const Tracker& t : m_settings.trackers) {
        if (!t.enabled || t.pattern.empty()) continue;
        if (static_cast<SourceMode>(t.source) == SourceMode::ActiveSkill) needSkills = true;
        else needBuffs = true;
    }
    if (ListsWanted(now)) needBuffs = needSkills = true;

    m_buffs.clear();
    m_skills.clear();
    if (needBuffs || needSkills) {
        // The player only: GetSnapshot() would also copy every entity in the area.
        const PluginSDK::Entity player = ctx()->Entities.GetPlayer();
        if (needBuffs && player.Components.HasBuffs()) {
            std::vector<PluginSDK::Buff> raw = ctx()->Components.EnumerateAggregatedBuffs(player.Components.Buffs);
            // An older host has no aggregated list; the per-instance list still works.
            if (raw.empty()) raw = ctx()->Components.EnumerateBuffs(player.Components.Buffs);
            m_buffs.reserve(raw.size());
            for (const auto& b : raw) m_buffs.push_back(ToSample(b));
        }
        if (needSkills && player.Components.HasActor()) {
            const std::vector<PluginSDK::ActiveSkill> raw = ctx()->Components.EnumerateActiveSkills(player.Components.Actor);
            m_skills.reserve(raw.size());
            for (const auto& k : raw) m_skills.push_back(ToSample(k));
        }
    }
    m_sampleTime = now;
    m_haveSample = true;
    EvaluateTrackers();
}

void BuffOverlayPlugin::EvaluateTrackers() {
    m_states.resize(m_settings.trackers.size());
    for (size_t i = 0; i < m_settings.trackers.size(); ++i)
        m_states[i] = Evaluate(m_settings.trackers[i], m_buffs, m_skills);
}

ImVec2 BuffOverlayPlugin::GameScreenSize() const {
    // Tracker positions are game pixels. The settings tab may be drawn in a
    // separate menu window, whose display size is that window's, not the game's.
    const PluginSDK::ScreenSize s = ctx()->Game.GetScreenSize();
    if (s.Width > 0.0f && s.Height > 0.0f) return ImVec2(s.Width, s.Height);
    return ImGui::GetIO().DisplaySize;   // no game window yet
}

void BuffOverlayPlugin::RetireIcon(IconTexture& tex) {
    if (tex.srv) m_retiredIcons.push_back(std::move(tex.srv));
    m_retiredTick = m_drawTick;
}

void BuffOverlayPlugin::RetireAllIcons() {
    for (auto& [path, tex] : m_icons) RetireIcon(tex);
    m_icons.clear();
}

void BuffOverlayPlugin::ReleaseRetiredIcons() {
    if (m_retiredIcons.empty()) return;
    // Only once every frame that may have drawn them has been rendered. The
    // settings preview and the trackers can be two ImGui contexts (a separate
    // menu window), each with its own frame numbers, both rendered at the end of
    // the host loop iteration: so count iterations (DrawUI calls) instead. Two
    // DrawUI calls after the retire mean its iteration has been rendered.
    if (m_drawTick - m_retiredTick < 2) return;
    m_retiredIcons.clear();
}

const IconTexture* BuffOverlayPlugin::IconFor(const Tracker& t) {
    if (t.iconPath.empty()) return nullptr;
    auto it = m_icons.find(t.iconPath);
    if (it == m_icons.end()) {
        if (m_icons.size() >= kMaxIconCache) {
            // Drop paths no tracker uses any more (half-typed paths) first; never
            // free a texture outright here, it may be queued in this frame.
            for (auto e = m_icons.begin(); e != m_icons.end();) {
                bool used = false;
                for (const Tracker& other : m_settings.trackers)
                    if (other.iconPath == e->first) { used = true; break; }
                if (used) { ++e; continue; }
                RetireIcon(e->second);
                e = m_icons.erase(e);
            }
            if (m_icons.size() >= kMaxIconCache) RetireAllIcons();
        }
        it = m_icons.emplace(t.iconPath, IconTexture{}).first;
        // Decoded once per path (a failure is remembered too, see Reload icon).
        LoadIconTexture(static_cast<ID3D11Device*>(ctx()->D3DDevice),
                        ResolveIconPath(DirectoryPath(), t.iconPath), it->second);
    }
    return &it->second;
}

void BuffOverlayPlugin::UpdateOverlayInputWish(bool inGame) {
    // Arranging needs mouse input on the overlay even with the menu closed.
    const bool want = m_arrange && inGame;
    if (want != m_inputWish) {
        ctx()->Overlay.SetWantsOverlayInput(want);
        m_inputWish = want;
    }
}

void BuffOverlayPlugin::DrawUI() {
    ++m_drawTick;   // one DrawUI per host loop iteration
    if (!ctx()->ImGuiContext) return;
    SetImGuiContext();
    ReleaseRetiredIcons();
    FlushSettingsIfDue(false);
    RetryBlockedLoad();

    const bool inGame = ctx()->Game.IsInGame();
    m_inGame = inGame;
    UpdateOverlayInputWish(inGame);
    const Clock::time_point now = Clock::now();
    Sample(now, inGame);
    if (!inGame) return;

    DrawTrackers();
    if (m_arrange) {
        DrawArrangeFrames();
        DrawArrangeBanner();
    }
}

} // namespace BuffOverlay

extern "C" PLUGIN_API PluginSDK::Plugin* CreatePlugin() {
    return new BuffOverlay::BuffOverlayPlugin();
}

extern "C" PLUGIN_API void DestroyPlugin(PluginSDK::Plugin* plugin) {
    delete plugin;
}
