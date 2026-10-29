// Buff Overlay — the plugin's tab in the host's Plugins window. One card per
// topic: Overlay, Trackers, then the selected tracker's What to watch / What
// to show / Look and position, and a collapsed Diagnostics card at the end.
// Every text is a key of lang/en.json.
#include "../BuffOverlayPlugin.h"

#include "../../../../POEFixer/imgui/IconsFontAwesome6.h"
#include "SettingsExtras.h"

#include "../../../../POEFixer/imgui/imgui_internal.h"

#include <shellapi.h>

#include <cfloat>
#include <set>
#include <string>

namespace BuffOverlay {
namespace {

namespace Ui = PluginUi;
namespace Ux = UiExtra;

constexpr const char* kSourceItems[] = { I18n::Key("watch_source_buff"), I18n::Key("watch_source_skill") };
constexpr const char* kMatchItems[] = { I18n::Key("watch_match_exact"), I18n::Key("watch_match_prefix"),
                                        I18n::Key("watch_match_contains") };
constexpr const char* kSkillValueItems[] = { I18n::Key("skill_value_stage"), I18n::Key("skill_value_total_uses"),
                                             I18n::Key("skill_value_available"), I18n::Key("skill_value_max_uses"),
                                             I18n::Key("skill_value_ready") };
constexpr const char* kCompareItems[] = { I18n::Key("compare_always"), I18n::Key("compare_equal"),
                                          I18n::Key("compare_not_equal"), I18n::Key("compare_greater"),
                                          I18n::Key("compare_greater_equal"), I18n::Key("compare_less"),
                                          I18n::Key("compare_less_equal") };
constexpr const char* kBoolItems[] = { I18n::Key("compare_always"), I18n::Key("compare_when_ready"),
                                       I18n::Key("compare_when_not_ready") };
constexpr const char* kTextItems[] = { I18n::Key("show_text_none"), I18n::Key("show_text_value"),
                                       I18n::Key("show_text_custom") };
constexpr const char* kMissingItems[] = { I18n::Key("watch_missing_hide"), I18n::Key("watch_missing_zero") };

// A combo whose items are keys, shown in the program language.
template <size_t N>
bool KeyCombo(const char* id, int& value, const char* const (&keys)[N]) {
    const char* items[N];
    for (size_t i = 0; i < N; ++i) items[i] = I18n::Tr(keys[i]);
    return Ui::Combo(id, value, items, static_cast<int>(N));
}

// "<icon> <text>###<id>": a button whose id does not change with its text.
std::string IconLabel(const char* icon, const char* text, const char* id) {
    return std::string(icon) + " " + text + "###" + id;
}

bool EqualsNoCase(const std::wstring& a, const wchar_t* b) { return _wcsicmp(a.c_str(), b) == 0; }

void OpenFolder(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // Opens Explorer and returns at once (no dialog on the overlay thread).
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// The tracker name field. The example tracker's shipped English name is shown in the
// program language (display only); typing that shown text back stores the English again.
bool LabelField(Tracker& t) {
    const char* shownDefault = I18n::Tr("tracker_example_label");
    std::string shown = t.label == kExampleTrackerLabel ? std::string(shownDefault) : t.label;
    if (!Ux::InputStringMax("##label", I18n::Tr("watch_name_hint"), shown, kMaxTextLen)) return false;
    t.label = shown == shownDefault ? std::string(kExampleTrackerLabel) : shown;
    return true;
}

} // namespace

void BuffOverlayPlugin::SetArrange(bool on) {
    m_arrange = on;
    if (!on) m_dragging = -1;
}

void BuffOverlayPlugin::DrawSettings() {
    SetImGuiContext();
    ReleaseRetiredIcons();
    RetryBlockedLoad();
    // Arrange frames are kept behind this window (see DrawArrangeFrames).
    m_settingsWindowId = ImGui::GetCurrentWindow()->RootWindow->ID;
    m_settingsWindowContext = ImGui::GetCurrentContext();
    if (m_saveError) Ux::ColoredWrapped(Ui::Colors().Warning, I18n::Tr(m_saveError));
    if (m_loadOrigin == LoadOrigin::Legacy) Ui::Hint(I18n::Tr("notice_settings_imported"));

    DrawSectionGeneral();
    DrawSectionTrackerList();
    if (m_selected >= 0 && m_selected < static_cast<int>(m_settings.trackers.size())) {
        Tracker& t = m_settings.trackers[static_cast<size_t>(m_selected)];
        ImGui::PushID(m_selected);
        DrawSectionWatch(t);
        DrawSectionShow(t);
        DrawSectionLook(t);
        ImGui::PopID();
    }
    DrawSectionDiagnostics();
    ImGui::Spacing();
    PluginUi::Hint("Originally created by 1k4ru5g3. Maintained by POEFixer.");
    ImGui::Spacing();
}

void BuffOverlayPlugin::DrawSectionGeneral() {
    Ui::BeginCard("##cardOverlay");
    Ui::Heading(ICON_FA_EYE, I18n::Tr("overlay_heading"));
    const char* arrangeLabel = I18n::Tr("overlay_arrange");
    const char* outline = I18n::Tr("overlay_outline");
    const char* opacityLabel = I18n::Tr("overlay_opacity");
    const char* status = I18n::Tr("overlay_status");
    const Ui::LabelColumn col{ arrangeLabel, outline, opacityLabel, status };

    col.RowNoWidth(arrangeLabel);
    bool arrange = m_arrange;
    if (ImGui::Checkbox("##arrange", &arrange)) SetArrange(arrange);
    Ui::Tip(I18n::Tr("overlay_arrange_tip"));

    col.RowNoWidth(outline);
    if (ImGui::Checkbox("##outline", &m_settings.textOutline)) MarkDirty(false);
    Ui::Tip(I18n::Tr("overlay_outline_tip"));

    col.Row(opacityLabel);
    int opacity = static_cast<int>(std::floor(m_settings.opacity * 100.0f + 0.5f));
    if (ImGui::SliderInt("##opacity", &opacity, 10, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
        m_settings.opacity = static_cast<float>(opacity) / 100.0f;
        MarkDirty(false);
    }
    Ui::Tip(I18n::Tr("overlay_opacity_tip"));

    col.RowNoWidth(status);
    if (!m_inGame) {
        Ui::Hint(I18n::Tr("overlay_status_not_in_area"));
    } else {
        int enabled = 0, showing = 0;
        for (size_t i = 0; i < m_settings.trackers.size(); ++i) {
            if (!m_settings.trackers[i].enabled) continue;
            ++enabled;
            if (i < m_states.size() && m_states[i].found) ++showing;
        }
        Ux::ValueText(I18n::TrF("overlay_status_matching", { { "showing", Num("%d", showing) },
                                                            { "enabled", Num("%d", enabled) } }).c_str());
    }
    Ui::EndCard();
    ImGui::Spacing();
}

void BuffOverlayPlugin::DrawSectionTrackerList() {
    Ui::BeginCard("##cardTrackers");
    Ui::Heading(ICON_FA_LIST, I18n::Tr("trackers_heading"));
    auto& trackers = m_settings.trackers;
    if (m_selected >= static_cast<int>(trackers.size())) m_selected = static_cast<int>(trackers.size()) - 1;

    if (trackers.empty()) {
        Ui::Hint(I18n::Tr("trackers_none"));
    } else {
        const float em = ImGui::GetFontSize();
        // Cells padded so the checkbox and the texts keep clear of the table's border.
        const ImVec2 cellPad(std::floor(em * 0.4f), std::floor(em * 0.2f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cellPad);
        const float rowH = ImGui::GetFrameHeight() + cellPad.y * 2.0f;
        const int rows = std::min(static_cast<int>(trackers.size()), 7);
        const float height = rowH * static_cast<float>(rows) + cellPad.y * 2.0f + 2.0f;
        // The game name gets its own column only where both columns stay readable;
        // otherwise it is in the row's tooltip.
        const bool patternColumn = ImGui::GetContentRegionAvail().x >= em * 30.0f;
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("##trackerTable", patternColumn ? 3 : 2, flags, ImVec2(0.0f, height))) {
            // Column ids only: the table shows no header row.
            ImGui::TableSetupColumn("##on", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            if (patternColumn) ImGui::TableSetupColumn("##pattern", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            for (size_t i = 0; i < trackers.size(); ++i) {
                Tracker& t = trackers[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##on", &t.enabled)) MarkDirty();
                Ui::Tip(I18n::Tr("trackers_enabled_tip"));
                ImGui::TableNextColumn();
                const bool active = m_inGame && i < m_states.size() && m_states[i].found;
                const Ui::Palette pal = Ui::Colors();
                Ux::StatusDot(!t.enabled ? pal.TextDisabled : active ? pal.Success : pal.TextSecondary);
                Ui::Tip(!t.enabled ? I18n::Tr("trackers_dot_disabled")
                        : active   ? I18n::Tr("trackers_dot_matching")
                                   : I18n::Tr("trackers_dot_not_matching"));
                ImGui::SameLine();
                const std::string id = std::string(ShownLabel(t, I18n::Tr("trackers_unnamed"))) + "###row";
                ImGui::AlignTextToFramePadding();
                // Selectable over the name column only, so the checkbox keeps its clicks.
                if (ImGui::Selectable(id.c_str(), m_selected == static_cast<int>(i), ImGuiSelectableFlags_None,
                                      ImVec2(0.0f, ImGui::GetFrameHeight())))
                    m_selected = static_cast<int>(i);
                const char* pattern = t.pattern.empty() ? I18n::Tr("trackers_no_pattern") : t.pattern.c_str();
                if (patternColumn) {
                    ImGui::TableNextColumn();
                    Ux::TextEllipsis(pattern, Ui::Colors().TextSecondary);
                }
                Ui::Tip(I18n::TrF("trackers_watches", { { "name", pattern } }).c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    const bool full = trackers.size() >= kMaxTrackers;
    ImGui::BeginDisabled(full);
    if (ImGui::Button(IconLabel(ICON_FA_PLUS, I18n::Tr("trackers_add"), "add").c_str())) {
        Tracker t;
        t.label = I18n::TrF("trackers_new_name", { { "number", Num("%zu", trackers.size() + 1) } });
        const ImVec2 d = GameScreenSize();
        t.x = std::floor(d.x * 0.5f) + static_cast<float>((trackers.size() % 8) * 24);
        t.y = std::floor(d.y * 0.4f) + static_cast<float>((trackers.size() % 8) * 24);
        Sanitize(t);
        trackers.push_back(std::move(t));
        m_selected = static_cast<int>(trackers.size()) - 1;
        EvaluateTrackers();
        MarkDirty();
    }
    ImGui::EndDisabled();
    if (full) Ui::Tip(I18n::TrF("trackers_max", { { "count", Num("%zu", kMaxTrackers) } }).c_str());

    const bool haveSel = m_selected >= 0 && m_selected < static_cast<int>(trackers.size());
    const std::string dupLabel = IconLabel(ICON_FA_COPY, I18n::Tr("trackers_duplicate"), "dup");
    Ux::SameLineOrWrap(Ui::ButtonWidth(dupLabel.c_str()));
    ImGui::BeginDisabled(!haveSel || full);
    if (Ui::SubtleButton(dupLabel.c_str()) && haveSel) {
        Tracker copy = trackers[static_cast<size_t>(m_selected)];
        // The copy's name is the user's own text from now on, in the language shown.
        const std::string base = ShownLabel(copy, "");
        copy.label = base.empty() ? std::string(I18n::Tr("trackers_copy_suffix"))
                                  : base + " " + I18n::Tr("trackers_copy_suffix");
        copy.x = std::min(copy.x + 24.0f, kMaxCoord);
        copy.y = std::min(copy.y + 24.0f, kMaxCoord);
        Sanitize(copy);
        trackers.insert(trackers.begin() + m_selected + 1, std::move(copy));
        ++m_selected;
        EvaluateTrackers();
        MarkDirty();
    }
    ImGui::EndDisabled();
    Ui::Tip(I18n::Tr("trackers_duplicate_tip"));

    const std::string removeLabel = IconLabel(ICON_FA_TRASH, I18n::Tr("trackers_remove"), "remove");
    Ux::SameLineOrWrap(Ui::ButtonWidth(removeLabel.c_str()));
    ImGui::BeginDisabled(!haveSel);
    if (Ui::SubtleButton(removeLabel.c_str())) ImGui::OpenPopup("##confirmRemove");
    ImGui::EndDisabled();
    const std::string question =
        haveSel ? I18n::TrF("trackers_remove_question",
                            { { "name", ShownLabel(trackers[static_cast<size_t>(m_selected)], I18n::Tr("trackers_unnamed")) } })
                : std::string(I18n::Tr("trackers_remove_this"));
    if (Ui::ConfirmPopup("##confirmRemove", question.c_str(), I18n::Tr("trackers_remove"), nullptr) && haveSel) {
        trackers.erase(trackers.begin() + m_selected);
        if (m_selected >= static_cast<int>(trackers.size())) m_selected = static_cast<int>(trackers.size()) - 1;
        EvaluateTrackers();
        MarkDirty();
    }
    Ui::EndCard();
    ImGui::Spacing();
}

void BuffOverlayPlugin::DrawSectionWatch(Tracker& t) {
    Ui::BeginCard("##cardWatch");
    Ui::Heading(ICON_FA_MAGNIFYING_GLASS, I18n::Tr("watch_heading"));
    const bool skill = static_cast<SourceMode>(t.source) == SourceMode::ActiveSkill;
    const char* name = I18n::Tr("watch_tracker_name");
    const char* watch = I18n::Tr("watch_source");
    const char* find = I18n::Tr("watch_pattern");
    const char* match = I18n::Tr("watch_match");
    const char* skillValue = I18n::Tr("watch_skill_value");
    const char* showWhen = I18n::Tr("watch_show_when");
    const char* compareWith = I18n::Tr("watch_compare_with");
    const char* several = I18n::Tr("watch_several");
    const char* notFound = I18n::Tr("watch_not_found");
    const Ui::LabelColumn col{ name, watch, find, match, skillValue, showWhen, compareWith, several, notFound };

    col.Row(name);
    if (LabelField(t)) MarkDirty(false);
    Ui::Tip(I18n::Tr("watch_tracker_name_tip"));

    col.Row(watch);
    if (KeyCombo("##source", t.source, kSourceItems)) { Sanitize(t); MarkDirty(); }
    Ui::Tip(I18n::Tr("watch_source_tip"));

    col.RowNoWidth(find);
    const std::string pickLabel = IconLabel(ICON_FA_LIST, I18n::Tr("watch_pick"), "pick");
    const float pickW = Ui::ButtonWidth(pickLabel.c_str());
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 4.0f,
                                     Ux::FieldWidth() - pickW - ImGui::GetStyle().ItemSpacing.x));
    if (Ux::InputStringMax("##pattern", skill ? I18n::Tr("watch_pattern_hint_skill") : I18n::Tr("watch_pattern_hint_buff"),
                           t.pattern, kMaxTextLen))
        MarkDirty();
    Ui::Tip(I18n::Tr("watch_pattern_tip"));
    ImGui::SameLine();
    if (Ui::SubtleButton(pickLabel.c_str())) {
        ImGui::OpenPopup("##patternPicker");
        WantLists();
        m_forceSample = true;
    }
    Ui::Tip(I18n::Tr("watch_pick_tip"));
    DrawPatternPicker(t);

    col.Row(match);
    if (KeyCombo("##match", t.matchMode, kMatchItems)) MarkDirty();
    Ui::Tip(I18n::Tr("watch_match_tip"));

    ImGui::BeginDisabled(!skill);
    col.Row(skillValue);
    if (KeyCombo("##skillValue", t.skillValue, kSkillValueItems)) {
        // Switching to/from the yes/no value: reset a numeric condition it cannot use.
        t.stageCompare = static_cast<int>(StageCompare::Any);
        t.stageValue = 0;
        MarkDirty();
    }
    Ui::Tip(I18n::Tr("watch_skill_value_tip"));

    const bool boolValue = static_cast<SkillValue>(t.skillValue) == SkillValue::CanBeUsed;
    col.Row(showWhen);
    if (boolValue) {
        int b = 0;
        if (t.stageCompare == static_cast<int>(StageCompare::Equal)) b = t.stageValue != 0 ? 1 : 2;
        if (KeyCombo("##boolCond", b, kBoolItems)) {
            t.stageCompare = b == 0 ? static_cast<int>(StageCompare::Any) : static_cast<int>(StageCompare::Equal);
            t.stageValue = b == 1 ? 1 : 0;
            MarkDirty();
        }
    } else {
        if (KeyCombo("##cond", t.stageCompare, kCompareItems)) MarkDirty();
    }
    Ui::Tip(I18n::Tr("watch_show_when_tip"));

    ImGui::BeginDisabled(boolValue || t.stageCompare == static_cast<int>(StageCompare::Any));
    col.Row(compareWith);
    if (ImGui::InputInt("##condValue", &t.stageValue)) MarkDirty();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!skill) Ui::Tip(I18n::Tr("watch_only_skills"));

    ImGui::BeginDisabled(skill);
    col.RowNoWidth(several);
    if (ImGui::Checkbox(I18n::TrId("watch_sum_charges"), &t.sumCharges)) MarkDirty();
    Ui::Tip(I18n::Tr("watch_sum_charges_tip"));
    ImGui::EndDisabled();

    col.Row(notFound);
    if (KeyCombo("##missing", t.missingMode, kMissingItems)) MarkDirty(false);
    Ui::Tip(I18n::Tr("watch_not_found_tip"));
    Ui::EndCard();
    ImGui::Spacing();
}

void BuffOverlayPlugin::DrawPatternPicker(Tracker& t) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 18.0f, 0.0f),
                                        ImVec2(ImGui::GetFontSize() * 36.0f, ImGui::GetFontSize() * 24.0f));
    if (!ImGui::BeginPopup("##patternPicker")) return;
    WantLists();
    const bool skill = static_cast<SourceMode>(t.source) == SourceMode::ActiveSkill;
    if (!m_inGame) {
        ImGui::TextDisabled("%s", skill ? I18n::Tr("picker_enter_area_skills") : I18n::Tr("picker_enter_area_buffs"));
    } else {
        std::set<std::string> names;
        if (skill) for (const auto& s : m_skills) { if (!s.name.empty()) names.insert(s.name); }
        else       for (const auto& b : m_buffs)  { if (!b.name.empty()) names.insert(b.name); }
        if (names.empty()) {
            ImGui::TextDisabled("%s", I18n::Tr("picker_nothing_active"));
        } else {
            ImGui::TextDisabled("%s", I18n::Tr("picker_click_name"));
            for (const std::string& n : names) {
                if (ImGui::Selectable(n.c_str())) {
                    std::string prefix;
                    if (!skill && SplitNumericSuffix(n, prefix)) {
                        t.pattern = prefix;
                        t.matchMode = static_cast<int>(MatchMode::Prefix);
                    } else {
                        t.pattern = n;
                        t.matchMode = static_cast<int>(MatchMode::Exact);
                    }
                    Utf8Truncate(t.pattern, kMaxTextLen);
                    MarkDirty();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
    }
    ImGui::EndPopup();
}

void BuffOverlayPlugin::DrawSectionShow(Tracker& t) {
    Ui::BeginCard("##cardShow");
    Ui::Heading(ICON_FA_TAGS, I18n::Tr("show_heading"));
    const bool skill = static_cast<SourceMode>(t.source) == SourceMode::ActiveSkill;
    DisplayParts parts = Decompose(static_cast<DisplayMode>(t.displayMode));
    bool partsChanged = false;
    const char* icon = I18n::Tr("show_icon");
    const char* textLabel = I18n::Tr("show_text");
    const char* timeLeft = I18n::Tr("show_time_left");
    const char* custom = I18n::Tr("show_custom_text");
    const char* iconFile = I18n::Tr("show_icon_file");
    const char* iconSize = I18n::Tr("show_icon_size");
    const char* preview = I18n::Tr("show_preview");
    const Ui::LabelColumn col{ icon, textLabel, timeLeft, custom, iconFile, iconSize, preview };

    col.RowNoWidth(icon);
    partsChanged |= ImGui::Checkbox("##icon", &parts.icon);
    Ui::Tip(I18n::Tr("show_icon_tip"));

    col.Row(textLabel);
    int text = static_cast<int>(parts.text);
    if (KeyCombo("##text", text, kTextItems)) { parts.text = static_cast<TextPart>(text); partsChanged = true; }
    Ui::Tip(I18n::Tr("show_text_tip"));

    ImGui::BeginDisabled(skill);
    col.RowNoWidth(timeLeft);
    bool time = parts.time && !skill;
    if (ImGui::Checkbox("##time", &time)) { parts.time = time; partsChanged = true; }
    ImGui::EndDisabled();
    Ui::Tip(skill ? I18n::Tr("show_time_left_skill_tip") : I18n::Tr("show_time_left_tip"));

    if (partsChanged) {
        t.displayMode = static_cast<int>(Compose(parts));
        Sanitize(t);
        parts = Decompose(static_cast<DisplayMode>(t.displayMode));
        MarkDirty(false);
    }

    ImGui::BeginDisabled(parts.text != TextPart::Custom);
    col.Row(custom);
    if (Ux::InputStringMax("##custom", I18n::Tr("show_custom_hint"), t.customText, kMaxTextLen)) MarkDirty(false);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!parts.icon);
    col.RowNoWidth(iconFile);
    const std::string chooseLabel = IconLabel(ICON_FA_FOLDER_OPEN, I18n::Tr("show_choose"), "chooseIcon");
    const float chooseW = Ui::ButtonWidth(chooseLabel.c_str());
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 4.0f,
                                     Ux::FieldWidth() - chooseW - ImGui::GetStyle().ItemSpacing.x));
    if (Ux::InputStringMax("##iconPath", "icons/my_icon.png", t.iconPath, kMaxPathLen)) MarkDirty(false);
    Ui::Tip(I18n::Tr("show_icon_file_tip"));
    ImGui::SameLine();
    if (Ui::SubtleButton(chooseLabel.c_str())) {
        ScanIconFolder();
        ImGui::OpenPopup("##iconPicker");
    }
    DrawIconPicker(t);

    if (parts.icon && !t.iconPath.empty()) {
        const IconTexture* tex = IconFor(t);
        Ux::Indent(col);
        if (tex && tex->srv) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", I18n::TrF("show_icon_loaded", { { "width", Num("%d", tex->width) },
                                                                     { "height", Num("%d", tex->height) } }).c_str());
        } else if (tex) {
            ImGui::AlignTextToFramePadding();
            const std::string error =
                tex->error.Empty() ? std::string(I18n::Tr("show_icon_not_loaded")) : Render(tex->error);
            ImGui::PushStyleColor(ImGuiCol_Text, Ui::Colors().Warning);
            ImGui::TextUnformatted(error.c_str());
            ImGui::PopStyleColor();
        }
        const char* reloadLabel = I18n::TrId("show_reload");
        Ux::SameLineOrWrap(col, Ui::ButtonWidth(reloadLabel));
        if (Ui::SubtleButton(reloadLabel)) {
            if (auto it = m_icons.find(t.iconPath); it != m_icons.end()) {
                RetireIcon(it->second);   // may be queued in this frame already
                m_icons.erase(it);
            }
        }
        Ui::Tip(I18n::Tr("show_reload_tip"));
    }

    col.Row(iconSize);
    if (ImGui::SliderFloat("##iconSize", &t.iconSize, kMinIconSize, kMaxIconSize,
                           UnitFormat(I18n::Tr("unit_pixels"), "%.0f").c_str(), ImGuiSliderFlags_AlwaysClamp))
        MarkDirty(false);
    ImGui::EndDisabled();

    col.RowNoWidth(preview);
    DrawPreview(t, static_cast<size_t>(m_selected));
    Ui::EndCard();
    ImGui::Spacing();
}

void BuffOverlayPlugin::ScanIconFolder() {
    m_iconFiles.clear();
    const std::filesystem::path dir = DirectoryPath() / L"icons";
    std::error_code ec;
    m_iconFolderMissing = !std::filesystem::is_directory(dir, ec);
    if (m_iconFolderMissing) return;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::wstring ext = it->path().extension().wstring();
        if (!EqualsNoCase(ext, L".png") && !EqualsNoCase(ext, L".jpg") && !EqualsNoCase(ext, L".jpeg") &&
            !EqualsNoCase(ext, L".bmp"))
            continue;
        m_iconFiles.push_back("icons/" + WideToUtf8(it->path().filename().wstring()));
        if (m_iconFiles.size() >= 500) break;
    }
    std::sort(m_iconFiles.begin(), m_iconFiles.end());
}

void BuffOverlayPlugin::DrawIconPicker(Tracker& t) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 16.0f, 0.0f),
                                        ImVec2(ImGui::GetFontSize() * 32.0f, ImGui::GetFontSize() * 24.0f));
    if (!ImGui::BeginPopup("##iconPicker")) return;
    if (m_iconFiles.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
        ImGui::TextDisabled("%s", m_iconFolderMissing ? I18n::Tr("icons_folder_missing") : I18n::Tr("icons_folder_empty"));
        ImGui::PopTextWrapPos();
    } else {
        for (const std::string& f : m_iconFiles) {
            if (ImGui::Selectable(f.c_str(), f == t.iconPath)) {
                t.iconPath = f;
                MarkDirty(false);
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::Separator();
    if (Ui::SubtleButton(IconLabel(ICON_FA_FOLDER_OPEN, I18n::Tr("icons_open_folder"), "openIcons").c_str())) {
        OpenFolder(DirectoryPath() / L"icons");
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void BuffOverlayPlugin::DrawPreview(const Tracker& t, size_t index) {
    const TrackerState s = index < m_states.size() ? m_states[index] : TrackerState{};
    Visual v;
    BuildVisual(t, s, SecondsSinceSample(), true, v);
    const float pad = std::floor(ImGui::GetStyle().FramePadding.y * 2.0f + 0.5f);
    const float availW = std::max(ImGui::GetFontSize() * 4.0f, ImGui::GetContentRegionAvail().x);
    const float maxH = ImGui::GetFontSize() * 5.0f;
    float scale = 1.0f;
    if (v.size.x > 0.0f) scale = std::min(scale, (availW - pad * 2.0f) / v.size.x);
    if (v.size.y > 0.0f) scale = std::min(scale, maxH / v.size.y);
    scale = std::max(scale, 0.1f);
    const ImVec2 box(std::min(availW, std::max(v.size.x * scale + pad * 2.0f, ImGui::GetFontSize() * 6.0f)),
                     std::floor(v.size.y * scale + pad * 2.0f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + box.x, p.y + box.y), IM_COL32(18, 18, 22, 235), ImGui::GetStyle().FrameRounding);
    DrawVisual(dl, t, v, ImVec2(p.x + pad, p.y + pad), scale, s.found ? 1.0f : 0.6f);
    ImGui::Dummy(box);
    Ui::Tip(s.found ? I18n::Tr("show_preview_live") : I18n::Tr("show_preview_dimmed"));
}

void BuffOverlayPlugin::DrawSectionLook(Tracker& t) {
    Ui::BeginCard("##cardLook");
    Ui::Heading(ICON_FA_PALETTE, I18n::Tr("look_heading"));
    const char* textSize = I18n::Tr("look_text_size");
    const char* textColour = I18n::Tr("look_text_colour");
    const char* background = I18n::Tr("look_background");
    const char* position = I18n::Tr("look_position");
    const Ui::LabelColumn col{ textSize, textColour, background, position };

    col.Row(textSize);
    if (ImGui::SliderFloat("##fontScale", &t.fontScale, kMinFontScale, kMaxFontScale, "%.1fx", ImGuiSliderFlags_AlwaysClamp))
        MarkDirty(false);
    Ui::Tip(I18n::Tr("look_text_size_tip"));

    col.RowNoWidth(textColour);
    if (Ui::Swatch("##color", &t.color.r)) MarkDirty(false);

    col.RowNoWidth(background);
    if (ImGui::Checkbox("##bg", &t.textBackground)) MarkDirty(false);
    Ui::Tip(I18n::Tr("look_background_tip"));
    ImGui::SameLine();
    if (Ux::SwatchDimmable("##bgColor", &t.backgroundColor.r, !t.textBackground)) MarkDirty(false);

    col.Row(position);
    float pos[2] = { t.x, t.y };
    if (ImGui::DragFloat2("##pos", pos, 1.0f, 0.0f, kMaxCoord, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
        t.x = std::floor(pos[0] + 0.5f);
        t.y = std::floor(pos[1] + 0.5f);
        MarkDirty(false);
    }
    Ui::Tip(I18n::Tr("look_position_tip"));

    Ux::Indent(col);
    if (Ui::SubtleButton(IconLabel(ICON_FA_CROSSHAIRS, I18n::Tr("look_center"), "center").c_str())) {
        const ImVec2 d = GameScreenSize();
        t.x = std::floor(d.x * 0.5f);
        t.y = std::floor(d.y * 0.5f);
        MarkDirty(false);
    }
    const std::string arrangeLabel = m_arrange
        ? IconLabel(ICON_FA_CHECK, I18n::Tr("look_done_arranging"), "arr")
        : IconLabel(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, I18n::Tr("overlay_arrange"), "arr");
    Ux::SameLineOrWrap(col, Ui::ButtonWidth(arrangeLabel.c_str()));
    if (Ui::SubtleButton(arrangeLabel.c_str())) SetArrange(!m_arrange);
    Ui::EndCard();
    ImGui::Spacing();
}

void BuffOverlayPlugin::DrawSectionDiagnostics() {
    if (Ui::BeginDiagnostics(I18n::Tr("diag_tip"))) {
        WantLists();
        if (!m_inGame) {
            Ui::Hint(I18n::Tr("diag_not_in_area"));
        } else {
            Ui::Hint(I18n::Tr("diag_hint"));
            const float rowH = ImGui::GetTextLineHeightWithSpacing();
            // Fixed columns of width 0 fit their translated header and their cells every frame.
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;

            ImGui::TextUnformatted(I18n::TrF("diag_buffs", { { "count", Num("%zu", m_buffs.size()) } }).c_str());
            const int bRows = std::clamp(static_cast<int>(m_buffs.size()), 1, 10) + 1;
            if (ImGui::BeginTable("##diagBuffs", 3, flags, ImVec2(0.0f, rowH * static_cast<float>(bRows) + rowH * 0.5f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(I18n::TrId("diag_col_name"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(I18n::TrId("diag_col_charges"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
                ImGui::TableSetupColumn(I18n::TrId("diag_col_time_left"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
                ImGui::TableHeadersRow();
                const float elapsed = SecondsSinceSample();
                for (size_t i = 0; i < m_buffs.size(); ++i) {
                    const BuffSample& b = m_buffs[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(b.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                        ImGui::SetClipboardText(b.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", b.charges);
                    ImGui::TableNextColumn();
                    if (IsPermanent(b)) ImGui::TextUnformatted(ICON_FA_INFINITY);
                    else ImGui::TextUnformatted(I18n::TrF("unit_seconds", { { "value", Num("%.1f", std::max(0.0f, b.timeLeft - elapsed)) } }).c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::TextUnformatted(I18n::TrF("diag_skills", { { "count", Num("%zu", m_skills.size()) } }).c_str());
            const int sRows = std::clamp(static_cast<int>(m_skills.size()), 1, 10) + 1;
            if (ImGui::BeginTable("##diagSkills", 6, flags, ImVec2(0.0f, rowH * static_cast<float>(sRows) + rowH * 0.5f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(I18n::TrId("diag_col_name"), ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(I18n::TrId("diag_col_stage"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
                ImGui::TableSetupColumn(I18n::TrId("diag_col_uses"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
                ImGui::TableSetupColumn(I18n::TrId("diag_col_available"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
                ImGui::TableSetupColumn(I18n::TrId("diag_col_max"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
                ImGui::TableSetupColumn(I18n::TrId("diag_col_ready"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < m_skills.size(); ++i) {
                    const SkillSample& k = m_skills[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(k.name.empty() ? I18n::Tr("diag_no_name") : k.name.c_str(), false,
                                          ImGuiSelectableFlags_SpanAllColumns))
                        ImGui::SetClipboardText(k.name.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%d", k.useStage);
                    ImGui::TableNextColumn(); ImGui::Text("%d", k.totalUses);
                    ImGui::TableNextColumn(); ImGui::Text("%d", SkillValueOf(k, SkillValue::AvailableCharges));
                    ImGui::TableNextColumn(); ImGui::Text("%d", k.maxUses);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(k.canBeUsed ? I18n::Tr("diag_yes") : I18n::Tr("diag_no"));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", I18n::TrF("diag_settings_file", { { "path", "config/settings.json" } }).c_str());
        const std::string openLabel = IconLabel(ICON_FA_FOLDER_OPEN, I18n::Tr("diag_open_folder"), "openCfg");
        Ux::SameLineOrWrap(Ui::ButtonWidth(openLabel.c_str()));
        if (Ui::SubtleButton(openLabel.c_str())) OpenFolder(DirectoryPath() / L"config");
        Ui::EndDiagnostics();
    }
    ImGui::Spacing();
}

} // namespace BuffOverlay
