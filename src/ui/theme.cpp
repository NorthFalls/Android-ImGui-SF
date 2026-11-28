#include "ui/theme.h"

#include <algorithm>

#include "imgui.h"

namespace sf {
namespace ui {
namespace {

ImVec4 Pick(bool dark, const ImVec4& light, const ImVec4& darkColor) {
    return dark ? darkColor : light;
}

float gScale = 1.0f;

}  // namespace

void ApplyTheme(float scale, ThemeMode mode) {
    gScale = std::clamp(scale, 0.5f, 3.0f);
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

    // Liquid Glass: translucent frosted surfaces over a soft gradient. The light
    // variant uses dark ink on pastel glass, the dark variant flips the ink and
    // sinks the surfaces into a deep navy glass while keeping the same accent.
    const bool dark = mode == ThemeMode::Dark;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = Pick(dark, ImVec4(0.11f, 0.115f, 0.14f, 1.00f),
                                 ImVec4(0.93f, 0.94f, 0.97f, 1.00f));
    colors[ImGuiCol_TextDisabled] = Pick(dark, ImVec4(0.42f, 0.43f, 0.48f, 0.90f),
                                         ImVec4(0.62f, 0.64f, 0.72f, 0.90f));
    colors[ImGuiCol_WindowBg] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.58f),
                                     ImVec4(0.055f, 0.063f, 0.086f, 0.78f));
    colors[ImGuiCol_ChildBg] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.38f),
                                    ImVec4(0.075f, 0.086f, 0.118f, 0.62f));
    colors[ImGuiCol_PopupBg] = Pick(dark, ImVec4(0.99f, 0.99f, 1.00f, 0.94f),
                                    ImVec4(0.07f, 0.08f, 0.11f, 0.96f));
    colors[ImGuiCol_Border] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.85f),
                                   ImVec4(1.00f, 1.00f, 1.00f, 0.20f));
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.55f),
                                    ImVec4(1.00f, 1.00f, 1.00f, 0.09f));
    colors[ImGuiCol_FrameBgHovered] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.75f),
                                           ImVec4(1.00f, 1.00f, 1.00f, 0.14f));
    colors[ImGuiCol_FrameBgActive] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.92f),
                                          ImVec4(1.00f, 1.00f, 1.00f, 0.21f));
    colors[ImGuiCol_TitleBg] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.40f),
                                    ImVec4(1.00f, 1.00f, 1.00f, 0.05f));
    colors[ImGuiCol_TitleBgActive] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.55f),
                                          ImVec4(1.00f, 1.00f, 1.00f, 0.10f));
    colors[ImGuiCol_TitleBgCollapsed] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.35f),
                                             ImVec4(1.00f, 1.00f, 1.00f, 0.04f));
    colors[ImGuiCol_MenuBarBg] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.45f),
                                      ImVec4(1.00f, 1.00f, 1.00f, 0.06f));
    colors[ImGuiCol_ScrollbarBg] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.25f),
                                        ImVec4(1.00f, 1.00f, 1.00f, 0.18f));
    colors[ImGuiCol_ScrollbarGrab] = Pick(dark, ImVec4(0.60f, 0.62f, 0.70f, 0.70f),
                                          ImVec4(0.55f, 0.58f, 0.66f, 0.60f));
    colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.74f, 0.66f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.74f, 0.66f, 0.95f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.24f, 0.66f, 0.59f, 1.00f);
    colors[ImGuiCol_Button] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.50f),
                                   ImVec4(1.00f, 1.00f, 1.00f, 0.09f));
    colors[ImGuiCol_ButtonHovered] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.78f),
                                          ImVec4(1.00f, 1.00f, 1.00f, 0.16f));
    colors[ImGuiCol_ButtonActive] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.95f),
                                         ImVec4(1.00f, 1.00f, 1.00f, 0.24f));
    colors[ImGuiCol_Header] = ImVec4(0.30f, 0.74f, 0.66f, 0.20f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.74f, 0.66f, 0.32f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.74f, 0.66f, 0.45f);
    colors[ImGuiCol_Separator] = Pick(dark, ImVec4(0.10f, 0.11f, 0.14f, 0.10f),
                                      ImVec4(1.00f, 1.00f, 1.00f, 0.12f));
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.74f, 0.66f, 0.60f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.30f, 0.74f, 0.66f, 0.90f);
    colors[ImGuiCol_Tab] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.35f),
                                ImVec4(1.00f, 1.00f, 1.00f, 0.08f));
    colors[ImGuiCol_TabHovered] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.70f),
                                       ImVec4(1.00f, 1.00f, 1.00f, 0.16f));
    colors[ImGuiCol_TabSelected] = Pick(dark, ImVec4(1.00f, 1.00f, 1.00f, 0.85f),
                                        ImVec4(1.00f, 1.00f, 1.00f, 0.24f));
    colors[ImGuiCol_PlotLines] = ImVec4(0.30f, 0.74f, 0.66f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.24f, 0.66f, 0.59f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.30f, 0.74f, 0.66f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.30f, 0.74f, 0.66f, 0.30f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.30f, 0.74f, 0.66f, 1.00f);

    style.ScaleAllSizes(scale);
    style.FontScaleMain = scale;
}

float Scale() { return gScale; }

}  // namespace ui
}  // namespace sf
