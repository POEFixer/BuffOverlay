// Buff Overlay — the plugin's tab in the host's Plugins window. One card per
// topic: Overlay, Trackers, then the selected tracker's What to watch / What
// to show / Look and position, and a collapsed Diagnostics card at the end.
#include "../BuffOverlayPlugin.h"

#include "../../../../POEFixer/imgui/IconsFontAwesome6.h"
#include "SettingsExtras.h"

#include "../../../../POEFixer/imgui/imgui_internal.h"

#include <shellapi.h>

#include <cfloat>
#include <cstdio>
#include <set>

namespace BuffOverlay {
namespace {

namespace Ui = PluginUi;
namespace Ux = UiExtra;

constexpr const char* kSourceItems[] = { "A buff on your character", "An active skill" };
constexpr const char* kMatchItems[] = { "Exact name", "Starts with", "Contains" };
constexpr const char* kSkillValueItems[] = { "Use stage", "Total uses", "Available charges", "Max uses", "Ready to use" };
constexpr const char* kCompareItems[] = { "Always", "Value =", "Value is not", "Value >", "Value >=", "Value <", "Value <=" };
constexpr const char* kBoolItems[] = { "Always", "When ready", "When not ready" };
constexpr const char* kTextItems[] = { "None", "Value", "Custom text" };
constexpr const char* kMissingItems[] = { "Hide the tracker", "Show it with 0" };

bool EqualsNoCase(const std::wstring& a, const wchar_t* b) { return _wcsicmp(a.c_str(), b) == 0; }

void OpenFolder(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // Opens Explorer and returns at once (no dialog on the overlay thread).
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
    if (!m_saveError.empty()) Ux::ColoredWrapped(Ui::Colors().Warning, m_saveError.c_str());
    if (m_loadOrigin == LoadOrigin::Legacy)
        Ui::Hint("Your trackers from the original Buff Overlay were imported into config/settings.json.");

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
    Ui::Heading(ICON_FA_EYE, "Overlay");
    const Ui::LabelColumn col{ "Arrange on screen", "Text outline", "Opacity", "Status" };

    col.RowNoWidth("Arrange on screen");
    bool arrange = m_arrange;
    if (ImGui::Checkbox("##arrange", &arrange)) SetArrange(arrange);
    Ui::Tip("Shows every enabled tracker with a frame, even when its buff is not active,\n"
            "so you can drag it to where you want it. Works with the menu open or closed;\n"
            "press Done at the top of the screen (or untick this) when finished.");

    col.RowNoWidth("Text outline");
    if (ImGui::Checkbox("##outline", &m_settings.textOutline)) MarkDirty(false);
    Ui::Tip("Draws a thin dark outline around tracker text so it stays\nreadable on bright and dark parts of the map.");

    col.Row("Opacity");
    int opacity = static_cast<int>(std::floor(m_settings.opacity * 100.0f + 0.5f));
    if (ImGui::SliderInt("##opacity", &opacity, 10, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
        m_settings.opacity = static_cast<float>(opacity) / 100.0f;
        MarkDirty(false);
    }
    Ui::Tip("How see-through every tracker is drawn (icons, text and backgrounds).");

    col.RowNoWidth("Status");
    if (!m_inGame) {
        Ui::Hint("Not in an area: trackers appear once you are in game.");
    } else {
        int enabled = 0, showing = 0;
        for (size_t i = 0; i < m_settings.trackers.size(); ++i) {
            if (!m_settings.trackers[i].enabled) continue;
            ++enabled;
            if (i < m_states.size() && m_states[i].found) ++showing;
        }
        char line[96];
        std::snprintf(line, sizeof(line), "%d of %d enabled trackers match right now", showing, enabled);
        Ux::ValueText(line);
    }
    Ui::EndCard();
    ImGui::Spacing();
}

void BuffOverlayPlugin::DrawSectionTrackerList() {
    Ui::BeginCard("##cardTrackers");
    Ui::Heading(ICON_FA_LIST, "Trackers");
    auto& trackers = m_settings.trackers;
    if (m_selected >= static_cast<int>(trackers.size())) m_selected = static_cast<int>(trackers.size()) - 1;

    if (trackers.empty()) {
        Ui::Hint("No trackers yet. Add one, then choose the buff or skill it watches.");
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
            ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            if (patternColumn) ImGui::TableSetupColumn("pattern", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            for (size_t i = 0; i < trackers.size(); ++i) {
                Tracker& t = trackers[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##on", &t.enabled)) MarkDirty();
                Ui::Tip("Show this tracker on the overlay.");
                ImGui::TableNextColumn();
                const bool active = m_inGame && i < m_states.size() && m_states[i].found;
                const Ui::Palette pal = Ui::Colors();
                Ux::StatusDot(!t.enabled ? pal.TextDisabled : active ? pal.Success : pal.TextSecondary);
                Ui::Tip(!t.enabled ? "Disabled" : active ? "Matching right now" : "Not matching right now");
                ImGui::SameLine();
                const char* label = t.label.empty() ? "(unnamed tracker)" : t.label.c_str();
                char id[300];
                std::snprintf(id, sizeof(id), "%s###row", label);
                ImGui::AlignTextToFramePadding();
                // Selectable over the name column only, so the checkbox keeps its clicks.
                if (ImGui::Selectable(id, m_selected == static_cast<int>(i), ImGuiSelectableFlags_None,
                                      ImVec2(0.0f, ImGui::GetFrameHeight())))
                    m_selected = static_cast<int>(i);
                const char* pattern = t.pattern.empty() ? "no name set" : t.pattern.c_str();
                if (patternColumn) {
                    ImGui::TableNextColumn();
                    Ux::TextEllipsis(pattern, Ui::Colors().TextSecondary);
                }
                char watches[300];
                std::snprintf(watches, sizeof(watches), "Watches: %s", pattern);
                Ui::Tip(watches);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    const bool full = trackers.size() >= kMaxTrackers;
    ImGui::BeginDisabled(full);
    if (ImGui::Button(ICON_FA_PLUS " Add tracker###add")) {
        Tracker t;
        char name[32];
        std::snprintf(name, sizeof(name), "Tracker %zu", trackers.size() + 1);
        t.label = name;
        const ImVec2 d = ImGui::GetIO().DisplaySize;
        t.x = std::floor(d.x * 0.5f) + static_cast<float>((trackers.size() % 8) * 24);
        t.y = std::floor(d.y * 0.4f) + static_cast<float>((trackers.size() % 8) * 24);
        Sanitize(t);
        trackers.push_back(std::move(t));
        m_selected = static_cast<int>(trackers.size()) - 1;
        EvaluateTrackers();
        MarkDirty();
    }
    ImGui::EndDisabled();
    if (full) Ui::Tip("At most 64 trackers.");

    const bool haveSel = m_selected >= 0 && m_selected < static_cast<int>(trackers.size());
    const char* dupLabel = ICON_FA_COPY " Duplicate###dup";
    Ux::SameLineOrWrap(Ui::ButtonWidth(dupLabel));
    ImGui::BeginDisabled(!haveSel || full);
    if (Ui::SubtleButton(dupLabel) && haveSel) {
        Tracker copy = trackers[static_cast<size_t>(m_selected)];
        copy.label += " (copy)";
        copy.x = std::min(copy.x + 24.0f, kMaxCoord);
        copy.y = std::min(copy.y + 24.0f, kMaxCoord);
        Sanitize(copy);
        trackers.insert(trackers.begin() + m_selected + 1, std::move(copy));
        ++m_selected;
        EvaluateTrackers();
        MarkDirty();
    }
    ImGui::EndDisabled();
    Ui::Tip("Adds a copy of the selected tracker right below it.");

    const char* removeLabel = ICON_FA_TRASH " Remove###remove";
    Ux::SameLineOrWrap(Ui::ButtonWidth(removeLabel));
    ImGui::BeginDisabled(!haveSel);
    if (Ui::SubtleButton(removeLabel)) ImGui::OpenPopup("##confirmRemove");
    ImGui::EndDisabled();
    char question[300] = "Remove this tracker?";
    if (haveSel)
        std::snprintf(question, sizeof(question), "Remove \"%s\"?", trackers[static_cast<size_t>(m_selected)].label.c_str());
    if (Ui::ConfirmPopup("##confirmRemove", question, "Remove", "Cancel") && haveSel) {
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
    Ui::Heading(ICON_FA_MAGNIFYING_GLASS, "What to watch");
    const bool skill = static_cast<SourceMode>(t.source) == SourceMode::ActiveSkill;
    const Ui::LabelColumn col{ "Tracker name", "Watch", "Name to find", "Match", "Skill value", "Show when",
                               "Compare with", "Several matches", "When not found" };

    col.Row("Tracker name");
    if (Ux::InputStringMax("##label", "e.g. Ancestral Bond", t.label, kMaxTextLen)) MarkDirty(false);
    Ui::Tip("Your name for this tracker. Shown in the list and in arrange mode.");

    col.Row("Watch");
    if (Ui::Combo("##source", t.source, kSourceItems, 2)) { Sanitize(t); MarkDirty(); }
    Ui::Tip("A buff: anything listed on your character's buff bar, including charges\n"
            "and stacks. An active skill: a skill's state, such as its stage or\n"
            "remaining uses.");

    col.RowNoWidth("Name to find");
    const char* pickLabel = ICON_FA_LIST " Pick###pick";
    const float pickW = Ui::ButtonWidth(pickLabel);
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 4.0f,
                                     Ux::FieldWidth() - pickW - ImGui::GetStyle().ItemSpacing.x));
    if (Ux::InputStringMax("##pattern", skill ? "skill id" : "buff id, e.g. totem_ancestral_bond_reservation_",
                           t.pattern, kMaxTextLen))
        MarkDirty();
    Ui::Tip("The game's internal name of the buff or skill (not the name shown in\n"
            "game). Use Pick to choose from what is active on your character now.");
    ImGui::SameLine();
    if (Ui::SubtleButton(pickLabel)) {
        ImGui::OpenPopup("##patternPicker");
        WantLists();
        m_forceSample = true;
    }
    Ui::Tip("Lists the buffs (or skills) active on your character right now.");
    DrawPatternPicker(t);

    col.Row("Match");
    if (Ui::Combo("##match", t.matchMode, kMatchItems, 3)) MarkDirty();
    Ui::Tip("Exact name: the whole name must be equal.\n"
            "Starts with: for names that end in a changing number, such as\n"
            "totem_ancestral_bond_reservation_79: enter the part before the number.\n"
            "Contains: the text may appear anywhere in the name.\n"
            "Upper and lower case must match.");

    ImGui::BeginDisabled(!skill);
    col.Row("Skill value");
    if (Ui::Combo("##skillValue", t.skillValue, kSkillValueItems, 5)) {
        // Switching to/from the yes/no value: reset a numeric condition it cannot use.
        t.stageCompare = static_cast<int>(StageCompare::Any);
        t.stageValue = 0;
        MarkDirty();
    }
    Ui::Tip("Which number of the skill the tracker shows and tests.\n"
            "Available charges = max uses minus uses on cooldown.\n"
            "Ready to use = yes when the skill can be used now.");

    const bool boolValue = static_cast<SkillValue>(t.skillValue) == SkillValue::CanBeUsed;
    col.Row("Show when");
    if (boolValue) {
        int b = 0;
        if (t.stageCompare == static_cast<int>(StageCompare::Equal)) b = t.stageValue != 0 ? 1 : 2;
        if (Ui::Combo("##boolCond", b, kBoolItems, 3)) {
            t.stageCompare = b == 0 ? static_cast<int>(StageCompare::Any) : static_cast<int>(StageCompare::Equal);
            t.stageValue = b == 1 ? 1 : 0;
            MarkDirty();
        }
    } else {
        if (Ui::Combo("##cond", t.stageCompare, kCompareItems, 7)) MarkDirty();
    }
    Ui::Tip("The tracker counts as found only while the skill value meets this\n"
            "condition; otherwise the When not found rule applies.");

    ImGui::BeginDisabled(boolValue || t.stageCompare == static_cast<int>(StageCompare::Any));
    col.Row("Compare with");
    if (ImGui::InputInt("##condValue", &t.stageValue)) MarkDirty();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!skill) Ui::Tip("Only for active skills.");

    ImGui::BeginDisabled(skill);
    col.RowNoWidth("Several matches");
    if (ImGui::Checkbox("Add up their charges###sum", &t.sumCharges)) MarkDirty();
    Ui::Tip("When more than one buff matches, show the sum of their charges.\n"
            "Off: show the first matching buff only.");
    ImGui::EndDisabled();

    col.Row("When not found");
    if (Ui::Combo("##missing", t.missingMode, kMissingItems, 2)) MarkDirty(false);
    Ui::Tip("What the overlay shows while the buff is absent (or the skill\ncondition is not met).");
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
        ImGui::TextDisabled("Enter an area in game to list your %s.", skill ? "skills" : "buffs");
    } else {
        std::set<std::string> names;
        if (skill) for (const auto& s : m_skills) { if (!s.name.empty()) names.insert(s.name); }
        else       for (const auto& b : m_buffs)  { if (!b.name.empty()) names.insert(b.name); }
        if (names.empty()) {
            ImGui::TextDisabled("Nothing active right now.");
        } else {
            ImGui::TextDisabled("Click a name to use it.");
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
                    if (t.pattern.size() > kMaxTextLen) t.pattern.resize(kMaxTextLen);
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
    Ui::Heading(ICON_FA_TAGS, "What to show");
    const bool skill = static_cast<SourceMode>(t.source) == SourceMode::ActiveSkill;
    DisplayParts parts = Decompose(static_cast<DisplayMode>(t.displayMode));
    bool partsChanged = false;
    const Ui::LabelColumn col{ "Icon", "Text", "Time left", "Custom text", "Icon file", "Icon size", "Preview" };

    col.RowNoWidth("Icon");
    partsChanged |= ImGui::Checkbox("##icon", &parts.icon);
    Ui::Tip("Show a picture of your choice before the text.");

    col.Row("Text");
    int text = static_cast<int>(parts.text);
    if (Ui::Combo("##text", text, kTextItems, 3)) { parts.text = static_cast<TextPart>(text); partsChanged = true; }
    Ui::Tip("Value: the buff's charges or stacks (for a skill, the chosen\nskill value). Custom text: your own word, such as BOND.");

    ImGui::BeginDisabled(skill);
    col.RowNoWidth("Time left");
    bool time = parts.time && !skill;
    if (ImGui::Checkbox("##time", &time)) { parts.time = time; partsChanged = true; }
    ImGui::EndDisabled();
    Ui::Tip(skill ? "Active skills have no duration." :
                    "Seconds until the buff ends (minutes:seconds above a minute).\n"
                    "Buffs without a duration show an infinity sign.");

    if (partsChanged) {
        t.displayMode = static_cast<int>(Compose(parts));
        Sanitize(t);
        parts = Decompose(static_cast<DisplayMode>(t.displayMode));
        MarkDirty(false);
    }

    ImGui::BeginDisabled(parts.text != TextPart::Custom);
    col.Row("Custom text");
    if (Ux::InputStringMax("##custom", "e.g. BOND", t.customText, kMaxTextLen)) MarkDirty(false);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!parts.icon);
    col.RowNoWidth("Icon file");
    const char* chooseLabel = ICON_FA_FOLDER_OPEN " Choose###chooseIcon";
    const float chooseW = Ui::ButtonWidth(chooseLabel);
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 4.0f,
                                     Ux::FieldWidth() - chooseW - ImGui::GetStyle().ItemSpacing.x));
    if (Ux::InputStringMax("##iconPath", "icons/my_icon.png", t.iconPath, kMaxPathLen)) MarkDirty(false);
    Ui::Tip("A PNG, JPG or BMP file (at most 1024 x 1024). Put files into the\n"
            "plugin's icons folder and pick them with Choose, or enter a full path.");
    ImGui::SameLine();
    if (Ui::SubtleButton(chooseLabel)) {
        ScanIconFolder();
        ImGui::OpenPopup("##iconPicker");
    }
    DrawIconPicker(t);

    if (parts.icon && !t.iconPath.empty()) {
        const IconTexture* tex = IconFor(t);
        Ux::Indent(col);
        if (tex && tex->srv) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Loaded, %d x %d", tex->width, tex->height);
        } else if (tex) {
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, Ui::Colors().Warning);
            ImGui::TextUnformatted(tex->error.empty() ? "Not loaded" : tex->error.c_str());
            ImGui::PopStyleColor();
        }
        Ux::SameLineOrWrap(col, Ui::ButtonWidth("Reload###reloadIcon"));
        if (Ui::SubtleButton("Reload###reloadIcon")) {
            if (auto it = m_icons.find(t.iconPath); it != m_icons.end()) {
                RetireIcon(it->second);   // may be queued in this frame already
                m_icons.erase(it);
            }
        }
        Ui::Tip("Reads the file again, e.g. after you replaced the picture.");
    }

    col.Row("Icon size");
    if (ImGui::SliderFloat("##iconSize", &t.iconSize, kMinIconSize, kMaxIconSize, "%.0f px", ImGuiSliderFlags_AlwaysClamp))
        MarkDirty(false);
    ImGui::EndDisabled();

    col.RowNoWidth("Preview");
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
        ImGui::TextDisabled(m_iconFolderMissing ? "The icons folder does not exist yet. Open it, copy PNG, JPG or BMP files into it, then press Choose again."
                                                : "No PNG, JPG or BMP files in the icons folder yet.");
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
    if (Ui::SubtleButton(ICON_FA_FOLDER_OPEN " Open icons folder###openIcons")) {
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
    Ui::Tip(s.found ? "Live: this is what the overlay shows now." :
                      "Not active right now; shown dimmed the way it will look.");
}

void BuffOverlayPlugin::DrawSectionLook(Tracker& t) {
    Ui::BeginCard("##cardLook");
    Ui::Heading(ICON_FA_PALETTE, "Look and position");
    const Ui::LabelColumn col{ "Text size", "Text colour", "Background", "Position" };

    col.Row("Text size");
    if (ImGui::SliderFloat("##fontScale", &t.fontScale, kMinFontScale, kMaxFontScale, "%.1fx", ImGuiSliderFlags_AlwaysClamp))
        MarkDirty(false);
    Ui::Tip("Text height as a multiple of the menu font.");

    col.RowNoWidth("Text colour");
    if (Ui::Swatch("##color", &t.color.r)) MarkDirty(false);

    col.RowNoWidth("Background");
    if (ImGui::Checkbox("##bg", &t.textBackground)) MarkDirty(false);
    Ui::Tip("A rounded panel behind the text.");
    ImGui::SameLine();
    if (Ux::SwatchDimmable("##bgColor", &t.backgroundColor.r, !t.textBackground)) MarkDirty(false);

    col.Row("Position");
    float pos[2] = { t.x, t.y };
    if (ImGui::DragFloat2("##pos", pos, 1.0f, 0.0f, kMaxCoord, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
        t.x = std::floor(pos[0] + 0.5f);
        t.y = std::floor(pos[1] + 0.5f);
        MarkDirty(false);
    }
    Ui::Tip("Top-left corner in screen pixels (X, Y). Drag the numbers, type\nthem, or use Arrange on screen to drag the tracker itself.");

    Ux::Indent(col);
    if (Ui::SubtleButton(ICON_FA_CROSSHAIRS " Center on screen###center")) {
        const ImVec2 d = ImGui::GetIO().DisplaySize;
        t.x = std::floor(d.x * 0.5f);
        t.y = std::floor(d.y * 0.5f);
        MarkDirty(false);
    }
    const char* arrangeLabel = m_arrange ? ICON_FA_CHECK " Done arranging###arr" : ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Arrange on screen###arr";
    Ux::SameLineOrWrap(col, Ui::ButtonWidth(arrangeLabel));
    if (Ui::SubtleButton(arrangeLabel)) SetArrange(!m_arrange);
    Ui::EndCard();
    ImGui::Spacing();
}

void BuffOverlayPlugin::DrawSectionDiagnostics() {
    if (Ui::BeginDiagnostics("The buffs and active skills the overlay sees on your character, and the settings file.")) {
        WantLists();
        if (!m_inGame) {
            Ui::Hint("Enter an area in game to see your active buffs and skills.");
        } else {
            Ui::Hint("Everything the overlay can see on your character right now. Click a name to copy it.");
            const float rowH = ImGui::GetTextLineHeightWithSpacing();
            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
            auto numW = [](const char* header) { return ImGui::CalcTextSize(header).x + ImGui::GetStyle().CellPadding.x * 2.0f; };

            ImGui::Text("Buffs (%zu)", m_buffs.size());
            const int bRows = std::clamp(static_cast<int>(m_buffs.size()), 1, 10) + 1;
            if (ImGui::BeginTable("##diagBuffs", 3, flags, ImVec2(0.0f, rowH * static_cast<float>(bRows) + rowH * 0.5f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Charges", ImGuiTableColumnFlags_WidthFixed, numW("Charges"));
                ImGui::TableSetupColumn("Time left", ImGuiTableColumnFlags_WidthFixed, numW("Time left"));
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
                    else ImGui::Text("%.1f s", std::max(0.0f, b.timeLeft - elapsed));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Text("Active skills (%zu)", m_skills.size());
            const int sRows = std::clamp(static_cast<int>(m_skills.size()), 1, 10) + 1;
            if (ImGui::BeginTable("##diagSkills", 6, flags, ImVec2(0.0f, rowH * static_cast<float>(sRows) + rowH * 0.5f))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, numW("Stage"));
                ImGui::TableSetupColumn("Uses", ImGuiTableColumnFlags_WidthFixed, numW("Uses"));
                ImGui::TableSetupColumn("Available", ImGuiTableColumnFlags_WidthFixed, numW("Available"));
                ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, numW("Max"));
                ImGui::TableSetupColumn("Ready", ImGuiTableColumnFlags_WidthFixed, numW("Ready"));
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < m_skills.size(); ++i) {
                    const SkillSample& k = m_skills[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(k.name.empty() ? "(no name)" : k.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                        ImGui::SetClipboardText(k.name.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%d", k.useStage);
                    ImGui::TableNextColumn(); ImGui::Text("%d", k.totalUses);
                    ImGui::TableNextColumn(); ImGui::Text("%d", SkillValueOf(k, SkillValue::AvailableCharges));
                    ImGui::TableNextColumn(); ImGui::Text("%d", k.maxUses);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(k.canBeUsed ? "yes" : "no");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Settings: config/settings.json");
        const char* openLabel = ICON_FA_FOLDER_OPEN " Open folder###openCfg";
        Ux::SameLineOrWrap(Ui::ButtonWidth(openLabel));
        if (Ui::SubtleButton(openLabel)) OpenFolder(DirectoryPath() / L"config");
        Ui::EndDiagnostics();
    }
    ImGui::Spacing();
}

} // namespace BuffOverlay
