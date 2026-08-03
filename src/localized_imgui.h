#pragma once

#include "localization.h"

#include <imgui.h>

#include <cstdarg>
#include <string>
#include <vector>

namespace ImGui {

inline bool BeginLocalized(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0) {
    const std::string translated = localizedText(name);
    return Begin(translated.c_str(), open, flags);
}
inline bool BeginMenuLocalized(const char* label, bool enabled = true) {
    const std::string translated = localizedText(label);
    return BeginMenu(translated.c_str(), enabled);
}
inline bool MenuItemLocalized(const char* label, const char* shortcut = nullptr,
                              bool selected = false, bool enabled = true) {
    const std::string translated = localizedText(label);
    return MenuItem(translated.c_str(), shortcut, selected, enabled);
}
inline bool MenuItemLocalized(const char* label, const char* shortcut,
                              bool* selected, bool enabled = true) {
    const std::string translated = localizedText(label);
    return MenuItem(translated.c_str(), shortcut, selected, enabled);
}
inline bool ButtonLocalized(const char* label, const ImVec2& size = {}) {
    const std::string translated = localizedText(label);
    return Button(translated.c_str(), size);
}
inline bool SmallButtonLocalized(const char* label) {
    const std::string translated = localizedText(label);
    return SmallButton(translated.c_str());
}
inline bool CheckboxLocalized(const char* label, bool* value) {
    const std::string translated = localizedText(label);
    return Checkbox(translated.c_str(), value);
}
inline bool RadioButtonLocalized(const char* label, bool active) {
    const std::string translated = localizedText(label);
    return RadioButton(translated.c_str(), active);
}
inline bool RadioButtonLocalized(const char* label, int* value, int buttonValue) {
    const std::string translated = localizedText(label);
    return RadioButton(translated.c_str(), value, buttonValue);
}
inline void OpenPopupLocalized(const char* id, ImGuiPopupFlags flags = 0) {
    const std::string translated = localizedText(id);
    OpenPopup(translated.c_str(), flags);
}
inline void OpenPopupLocalized(ImGuiID id, ImGuiPopupFlags flags = 0) { OpenPopup(id, flags); }
inline bool BeginPopupLocalized(const char* id, ImGuiWindowFlags flags = 0) {
    const std::string translated = localizedText(id);
    return BeginPopup(translated.c_str(), flags);
}
inline bool BeginPopupModalLocalized(const char* name, bool* open = nullptr,
                                    ImGuiWindowFlags flags = 0) {
    const std::string translated = localizedText(name);
    return BeginPopupModal(translated.c_str(), open, flags);
}
inline bool BeginComboLocalized(const char* label, const char* preview,
                               ImGuiComboFlags flags = 0) {
    const std::string translated = localizedText(label);
    return BeginCombo(translated.c_str(), preview, flags);
}
inline bool SelectableRaw(const char* label, bool selected = false,
                          ImGuiSelectableFlags flags = 0, const ImVec2& size = {}) {
    return Selectable(label, selected, flags, size);
}
inline bool SelectableLocalized(const char* label, bool selected = false,
                                ImGuiSelectableFlags flags = 0, const ImVec2& size = {}) {
    const std::string translated = localizedText(label);
    return Selectable(translated.c_str(), selected, flags, size);
}
inline bool SelectableLocalized(const char* label, bool* selected,
                                ImGuiSelectableFlags flags = 0, const ImVec2& size = {}) {
    const std::string translated = localizedText(label);
    return Selectable(translated.c_str(), selected, flags, size);
}
inline bool InputDoubleLocalized(const char* label, double* value, double step = 0.0,
                                 double stepFast = 0.0, const char* format = "%.6f",
                                 ImGuiInputTextFlags flags = 0) {
    const std::string translated = localizedText(label);
    return InputDouble(translated.c_str(), value, step, stepFast, format, flags);
}
inline bool InputIntLocalized(const char* label, int* value, int step = 1,
                              int stepFast = 100, ImGuiInputTextFlags flags = 0) {
    const std::string translated = localizedText(label);
    return InputInt(translated.c_str(), value, step, stepFast, flags);
}
inline bool ComboLocalized(const char* label, int* current,
                           const char* const items[], int count,
                           int popupHeight = -1) {
    const std::string translatedLabel = localizedText(label);
    std::vector<std::string> translatedItems;
    std::vector<const char*> itemPointers;
    translatedItems.reserve(static_cast<size_t>(count));
    itemPointers.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index)
        translatedItems.push_back(localizedText(items[index]));
    for (const std::string& item : translatedItems) itemPointers.push_back(item.c_str());
    return Combo(translatedLabel.c_str(), current, itemPointers.data(), count, popupHeight);
}
inline bool SliderScalarLocalized(const char* label, ImGuiDataType type, void* value,
                                  const void* minimum, const void* maximum,
                                  const char* format = nullptr, ImGuiSliderFlags flags = 0) {
    const std::string translated = localizedText(label);
    return SliderScalar(translated.c_str(), type, value, minimum, maximum, format, flags);
}
inline bool DragScalarLocalized(const char* label, ImGuiDataType type, void* value,
                                float speed = 1.0f, const void* minimum = nullptr,
                                const void* maximum = nullptr, const char* format = nullptr,
                                ImGuiSliderFlags flags = 0) {
    const std::string translated = localizedText(label);
    return DragScalar(translated.c_str(), type, value, speed, minimum, maximum, format, flags);
}
inline void TableSetupColumnLocalized(const char* label,
                                      ImGuiTableColumnFlags flags = 0,
                                      float width = 0.0f, ImGuiID id = 0) {
    const std::string translated = localizedText(label);
    TableSetupColumn(translated.c_str(), flags, width, id);
}
inline bool BeginPopupContextItemLocalized(const char* id = nullptr,
                                           ImGuiPopupFlags flags = 1) {
    if (id == nullptr) return BeginPopupContextItem(nullptr, flags);
    const std::string translated = localizedText(id);
    return BeginPopupContextItem(translated.c_str(), flags);
}
inline void ProgressBarLocalized(float fraction, const ImVec2& size = {-FLT_MIN, 0},
                                 const char* overlay = nullptr) {
    if (overlay == nullptr) { ProgressBar(fraction, size, nullptr); return; }
    const std::string translated = localizedText(overlay);
    ProgressBar(fraction, size, translated.c_str());
}
inline void SeparatorTextLocalized(const char* label) {
    const std::string translated = localizedText(label);
    SeparatorText(translated.c_str());
}
inline bool CollapsingHeaderLocalized(const char* label, ImGuiTreeNodeFlags flags = 0) {
    const std::string translated = localizedText(label);
    return CollapsingHeader(translated.c_str(), flags);
}
inline bool CollapsingHeaderLocalized(const char* label, bool* visible,
                                      ImGuiTreeNodeFlags flags = 0) {
    const std::string translated = localizedText(label);
    return CollapsingHeader(translated.c_str(), visible, flags);
}
inline bool TreeNodeExLocalized(const char* label, ImGuiTreeNodeFlags flags = 0) {
    const std::string translated = localizedText(label);
    return TreeNodeEx(translated.c_str(), flags);
}

inline void TextLocalized(const char* format, ...) {
    const std::string translated = localizedText(format);
    va_list args; va_start(args, format); TextV(translated.c_str(), args); va_end(args);
}
inline void TextWrappedLocalized(const char* format, ...) {
    const std::string translated = localizedText(format);
    va_list args; va_start(args, format); TextWrappedV(translated.c_str(), args); va_end(args);
}
inline void TextDisabledLocalized(const char* format, ...) {
    const std::string translated = localizedText(format);
    va_list args; va_start(args, format); TextDisabledV(translated.c_str(), args); va_end(args);
}
inline void TextColoredLocalized(const ImVec4& color, const char* format, ...) {
    const std::string translated = localizedText(format);
    va_list args; va_start(args, format); TextColoredV(color, translated.c_str(), args); va_end(args);
}
inline void TextUnformattedLocalized(const char* text, const char* textEnd = nullptr) {
    if (textEnd != nullptr) {
        const std::string translated = localizedText(
            std::string_view(text, static_cast<size_t>(textEnd - text)));
        TextUnformatted(translated.c_str());
    } else {
        const std::string translated = localizedText(text);
        TextUnformatted(translated.c_str());
    }
}
inline void BulletTextLocalized(const char* format, ...) {
    const std::string translated = localizedText(format);
    va_list args; va_start(args, format); BulletTextV(translated.c_str(), args); va_end(args);
}
inline void SetTooltipLocalized(const char* format, ...) {
    const std::string translated = localizedText(format);
    va_list args; va_start(args, format); SetTooltipV(translated.c_str(), args); va_end(args);
}
inline void SetItemTooltipLocalized(const char* format, ...) {
    const std::string translated = localizedText(format);
    va_list args; va_start(args, format); SetItemTooltipV(translated.c_str(), args); va_end(args);
}

} // namespace ImGui

// Keep the call sites readable and guarantee that every standard ImGui label
// passes through the same translation layer.
#define Begin BeginLocalized
#define BeginMenu BeginMenuLocalized
#define MenuItem MenuItemLocalized
#define Button ButtonLocalized
#define SmallButton SmallButtonLocalized
#define Checkbox CheckboxLocalized
#define RadioButton RadioButtonLocalized
#define OpenPopup OpenPopupLocalized
#define BeginPopup BeginPopupLocalized
#define BeginPopupModal BeginPopupModalLocalized
#define BeginCombo BeginComboLocalized
#define Selectable SelectableLocalized
#define InputDouble InputDoubleLocalized
#define InputInt InputIntLocalized
#define Combo ComboLocalized
#define SliderScalar SliderScalarLocalized
#define DragScalar DragScalarLocalized
#define TableSetupColumn TableSetupColumnLocalized
#define BeginPopupContextItem BeginPopupContextItemLocalized
#define ProgressBar ProgressBarLocalized
#define SeparatorText SeparatorTextLocalized
#define CollapsingHeader CollapsingHeaderLocalized
#define TreeNodeEx TreeNodeExLocalized
#define Text TextLocalized
#define TextWrapped TextWrappedLocalized
#define TextDisabled TextDisabledLocalized
#define TextColored TextColoredLocalized
#define TextUnformatted TextUnformattedLocalized
#define BulletText BulletTextLocalized
#define SetTooltip SetTooltipLocalized
#define SetItemTooltip SetItemTooltipLocalized
