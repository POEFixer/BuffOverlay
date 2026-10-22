// The few settings-tab helpers the SDK kit (POEFixer/plugin_sdk/PluginUi.h)
// does not have: a row continuation in a label column, wrap-if-it-does-not-fit
// for button rows, coloured wrapped text, a length-capped text field, a
// status dot, text cut with an ellipsis, a folding heading and a colour swatch
// that dims when disabled. Everything else comes from PluginUi.
#pragma once

#include "../../../../POEFixer/plugin_sdk/PluginUi.h"
#include "../../../../POEFixer/imgui/imgui_internal.h"

#include <cstdio>

#include <string>

namespace BuffOverlay::UiExtra {

// Width LabelColumn::Row gives its control: the rest of the row, 4 to 15 em.
// For a field that shares its row with a button (field + button = this width).
inline float FieldWidth() {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float em = ImGui::GetFontSize();
    return (std::max)(em * 4.0f, (std::min)(avail, em * 15.0f));
}

// Starts a row without a label: the cursor goes to the column's control edge.
inline void Indent(const PluginUi::LabelColumn& col) { col.RowNoWidth(""); }

// Next item on the same line when it fits, else on a new line (at the column's
// control edge when a column is given).
inline void SameLineOrWrap(float itemWidth) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < itemWidth) ImGui::NewLine();
}
inline void SameLineOrWrap(const PluginUi::LabelColumn& col, float itemWidth) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < itemWidth) {
        ImGui::NewLine();
        Indent(col);
    }
}

// Text in a colour, wrapped at the region edge (warnings, confirmations).
inline void ColoredWrapped(const ImVec4& color, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text ? text : "");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// Primary-colour text wrapped at the region edge (a value in a label row).
inline void ValueText(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text ? text : "");
    ImGui::PopTextWrapPos();
}

// PluginUi::InputString, cut to maxLen bytes after an edit.
inline bool InputStringMax(const char* id, const char* hint, std::string& text, size_t maxLen) {
    const bool changed = PluginUi::InputString(id, hint, text);
    if (changed && text.size() > maxLen) text.resize(maxLen);
    return changed;
}

// Small filled dot, vertically centred on a frame-height row.
inline void StatusDot(const ImVec4& color) {
    const float r = std::floor(ImGui::GetFontSize() * 0.28f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(std::floor(p.x + r) + 0.5f, std::floor(p.y + h * 0.5f) + 0.5f), r,
                                                ImGui::ColorConvertFloat4ToU32(color), 12);
    ImGui::Dummy(ImVec2(r * 2.0f, h));
}

// One line of text in `color`, cut with "..." at the region edge, on a
// frame-height row (lines up with checkboxes and selectables beside it).
inline void TextEllipsis(const char* text, const ImVec4& color) {
    if (!text) text = "";
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
    if (!ImGui::IsItemVisible()) return;
    const float y = p.y + ImGui::GetStyle().FramePadding.y;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(p.x, y), ImVec2(p.x + w, y + ts.y), p.x + w, text,
                              nullptr, &ts);
    ImGui::PopStyleColor();
}

// A heading that folds the rest of its card (closed until clicked): the
// accent title with an arrow and no filled bar, so it stays inside the card's
// padding like PluginUi::Heading. Returns true while open.
inline bool CollapsibleHeading(const char* icon, const char* title, const char* id) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s  %s###%s", icon ? icon : "", title ? title : "", id ? id : "");
    ImGui::PushStyleColor(ImGuiCol_Text, PluginUi::Colors().Accent);
    const bool open = ImGui::TreeNodeEx(buf, ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopStyleColor();
    return open;
}

// PluginUi::Swatch that also looks disabled: ImGui does not dim colour
// swatches inside BeginDisabled, so the shown colour gets the disabled alpha.
inline bool SwatchDimmable(const char* id, float rgba[4], bool disabled) {
    if (!disabled) return PluginUi::Swatch(id, rgba);
    float shown[4] = { rgba[0], rgba[1], rgba[2], rgba[3] * ImGui::GetStyle().DisabledAlpha };
    ImGui::BeginDisabled();
    PluginUi::Swatch(id, shown);
    ImGui::EndDisabled();
    return false;
}

} // namespace BuffOverlay::UiExtra
