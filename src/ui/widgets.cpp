#include "ui/widgets.h"

#include <cstdarg>
#include <cstdio>

namespace sf {
namespace ui {

void FrameGraph(const char* id, const RingBuffer& buffer, float height, float maxValue, ImU32 color) {
    ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.06f, 0.08f, 0.55f));
    ImGui::PlotLines(id, buffer.data(), buffer.size(), 0, nullptr, 0.0f, maxValue,
                     ImVec2(-FLT_MIN, height));
    ImGui::PopStyleColor(2);
}

void MeterRow(const char* label, float fraction, const char* valueText, ImU32 color) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;

    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(valueText);
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.14f, 0.18f, 0.90f));
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 6.0f), "");
    ImGui::PopStyleColor(2);
}

void KeyValue(const char* key, const char* value) {
    ImGui::TextUnformatted(key);
    ImGui::SameLine(132.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(value && *value ? value : "-");
    ImGui::PopStyleColor();
}

void KeyValuef(const char* key, const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    KeyValue(key, buffer);
}

void SectionHeader(const char* text) {
    ImGui::Spacing();
    ImGui::SeparatorText(text);
}

void Badge(const char* text, ImU32 textColor, ImU32 bgColor) {
    const ImVec2 pad(7.0f, 3.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, 5.0f);
    draw->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), textColor, text);

    ImGui::Dummy(size);
}

}  // namespace ui
}  // namespace sf
