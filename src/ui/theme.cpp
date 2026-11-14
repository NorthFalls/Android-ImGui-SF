#include "ui/theme.h"

#include "imgui.h"

namespace sf {
namespace ui {

void ApplyTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();

    style.WindowPadding = ImVec2(16, 14);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 12.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    style.WindowRounding = 22.0f;
    style.ChildRounding = 16.0f;
    style.FrameRounding = 12.0f;
    style.PopupRounding = 16.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 12.0f;

    // Liquid Glass: translucent frosted surfaces over a pastel gradient, dark ink
    // for legibility, one cool accent for the interactive bits.
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.11f, 0.115f, 0.14f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.42f, 0.43f, 0.48f, 0.90f);
    colors[ImGuiCol_WindowBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.58f);
    colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.38f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.99f, 0.99f, 1.00f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.85f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.55f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.75f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.92f);
    colors[ImGuiCol_TitleBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.55f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 1.00f, 1.00f, 0.35f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.45f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.60f, 0.62f, 0.70f, 0.70f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.74f, 0.66f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.74f, 0.66f, 0.95f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.24f, 0.66f, 0.59f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(1.00f, 1.00f, 1.00f, 0.50f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.78f);
    colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.95f);
    colors[ImGuiCol_Header] = ImVec4(0.30f, 0.74f, 0.66f, 0.20f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.74f, 0.66f, 0.32f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.74f, 0.66f, 0.45f);
    colors[ImGuiCol_Separator] = ImVec4(0.10f, 0.11f, 0.14f, 0.10f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.74f, 0.66f, 0.60f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.30f, 0.74f, 0.66f, 0.90f);
    colors[ImGuiCol_Tab] = ImVec4(1.00f, 1.00f, 1.00f, 0.35f);
    colors[ImGuiCol_TabHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_TabSelected] = ImVec4(1.00f, 1.00f, 1.00f, 0.85f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.30f, 0.74f, 0.66f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.24f, 0.66f, 0.59f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.30f, 0.74f, 0.66f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.74f, 0.66f, 0.30f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.30f, 0.74f, 0.66f, 1.00f);

    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;
}

}  // namespace ui
}  // namespace sf
