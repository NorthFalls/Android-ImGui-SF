#include "ui/menu.h"

#include <cstdio>

#include "core/clock.h"
#include "imgui.h"
#include "ui/theme.h"

namespace sf {
namespace ui {
namespace {

constexpr ImU32 kAccent = IM_COL32(92, 199, 184, 255);
constexpr ImU32 kWarn = IM_COL32(232, 176, 82, 255);
constexpr ImU32 kBad = IM_COL32(226, 106, 106, 255);

constexpr const char* kBuildStamp = __DATE__ " " __TIME__;

ImU32 MeterColor(float fraction) {
    if (fraction >= 0.85f) return kBad;
    if (fraction >= 0.65f) return kWarn;
    return kAccent;
}

void DrawHud(MenuState& state) {
    if (!state.showHud) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float s = state.scale;
    const float dt = ImGui::GetIO().DeltaTime;
    const float target = state.hudExpanded ? 1.0f : 0.0f;
    state.hudAnim += (target - state.hudAnim) * std::min(1.0f, dt * 16.0f);
    if (state.hudAnim < 0.001f) state.hudAnim = 0.0f;
    if (state.hudAnim > 0.999f) state.hudAnim = 1.0f;
    const float a = state.hudAnim;

    const float collapsedW = 148.0f * s;
    const float collapsedH = 36.0f * s;
    const float expandedW = 330.0f * s;
    const float expandedH = 158.0f * s;
    const float w = collapsedW + (expandedW - collapsedW) * a;
    const float h = collapsedH + (expandedH - collapsedH) * a;
    const float x = viewport->Pos.x + (viewport->Size.x - w) * 0.5f;
    const float y = viewport->Pos.y + 10.0f * s;
    const ImVec2 p0(x, y);
    const ImVec2 p1(x + w, y + h);
    const float rounding = collapsedH * 0.5f + (30.0f * s - collapsedH * 0.5f) * a;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##dynamic_island", nullptr, flags)) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(p0.x - 2.0f * s, p0.y - 2.0f * s),
                            ImVec2(p1.x + 2.0f * s, p1.y + 2.0f * s), IM_COL32(0, 0, 0, 45),
                            rounding + 2.0f * s);
        draw->AddRectFilled(p0, p1, IM_COL32(8, 9, 12, 246), rounding);

        ImGui::SetCursorScreenPos(p0);
        if (ImGui::InvisibleButton("##island_click", ImVec2(w, h))) {
            state.hudExpanded = !state.hudExpanded;
        }
        const bool islandHovered = ImGui::IsItemHovered();

        const ImU32 accent = IM_COL32(76, 194, 168, 255);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.96f, 0.99f, 1.0f));
        if (a < 0.55f) {
            const float centerY = p0.y + h * 0.5f;
            draw->AddCircleFilled(ImVec2(p0.x + 22.0f * s, centerY), 3.6f * s, accent);
            char text[48];
            std::snprintf(text, sizeof(text), "%.0f fps", state.fps);
            ImGui::SetCursorScreenPos(
                ImVec2(p0.x + 34.0f * s, centerY - ImGui::GetTextLineHeight() * 0.5f));
            ImGui::TextUnformatted(text);
            ImGui::SameLine();
            ImGui::TextDisabled("%.1f ms", state.frameMs);
        } else {
            const float left = p0.x + 20.0f * s;
            const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            float ty = p0.y + 12.0f * s;
            draw->AddCircleFilled(ImVec2(left + 4.0f * s, ty + ImGui::GetTextLineHeight() * 0.5f),
                                  3.6f * s, accent);
            ImGui::SetCursorScreenPos(ImVec2(left + 16.0f * s, ty));
            ImGui::TextUnformatted("SfImGui");
            ImGui::SameLine();
            ImGui::TextDisabled("点击任意处收起");

            char text[96];
            std::snprintf(text, sizeof(text), "%.0f fps", state.fps);
            ImGui::SetCursorScreenPos(ImVec2(left, p0.y + 52.0f * s));
            ImGui::TextUnformatted(text);
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f ms/frame", state.frameMs);

            if (state.telemetry) {
                std::snprintf(text, sizeof(text), "CPU %.0f%%    RAM %.0f%%",
                              state.telemetry->cpu().usage * 100.0,
                              state.telemetry->mem().usedFraction * 100.0);
                ImGui::SetCursorScreenPos(ImVec2(left, p0.y + 84.0f * s));
                ImGui::TextUnformatted(text);
            }
            std::snprintf(text, sizeof(text), "%llu frames    %d x %d",
                          static_cast<unsigned long long>(state.frames), state.width, state.height);
            ImGui::SetCursorScreenPos(ImVec2(left, p0.y + 112.0f * s));
            ImGui::TextDisabled("%s", text);
            (void)lineHeight;
        }
        // Like the phone: a tap anywhere else collapses the island again.
        if (state.hudExpanded && !islandHovered && ImGui::IsMouseClicked(0)) {
            state.hudExpanded = false;
        }

        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void DrawFrameTab(MenuState& state) {
    ImGui::SeparatorText("帧率控制");

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##fpscap", &state.targetFps, 0, 120,
                     state.targetFps == 0 ? "帧率上限：不限" : "帧率上限：%d fps");

    ImGui::Spacing();
    ImGui::TextDisabled("帧率曲线");
    FrameGraph("##fpsgraph", state.fpsHistory, 54.0f, 120.0f, kAccent);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.36f, 0.78f, 0.72f, 1.0f), "%.1f fps", state.fps);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("%.2f ms/frame", state.frameMs);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("%llu frames", static_cast<unsigned long long>(state.frames));

    ImGui::SeparatorText("外观");

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##scale", &state.scale, 0.75f, 2.0f, "界面缩放：%.2fx")) {
        state.themeDirty = true;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##opacity", &state.opacity, 0.2f, 1.0f, "不透明度：%.2f");
    ImGui::Checkbox("显示灵动岛", &state.showHud);

    ImGui::SeparatorText("输入");
    ImGui::Checkbox("接受触摸输入", &state.acceptInput);
    ImGui::TextDisabled("触摸事件由宿主经 nativeTouch() 传入。");
}

void DrawSystemTab(MenuState& state) {
    Telemetry* telemetry = state.telemetry;
    if (!telemetry) {
        ImGui::TextDisabled("未接入系统信息。");
        return;
    }

    const CpuStats& cpu = telemetry->cpu();
    const MemStats& mem = telemetry->mem();
    const ThermalStats& thermal = telemetry->thermal();

    ImGui::SeparatorText("处理器");
    char value[64];
    std::snprintf(value, sizeof(value), "%.0f%%  (%d cores)", cpu.usage * 100.0, cpu.coreCount);
    MeterRow("Load", static_cast<float>(cpu.usage), value, MeterColor(static_cast<float>(cpu.usage)));
    FrameGraph("##cpugraph", state.cpuHistory, 44.0f, 1.0f, kAccent);

    const int columns = 2;
    const int rows = (cpu.coreCount + columns - 1) / columns;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int core = row * columns + column;
            if (core >= cpu.coreCount) continue;
            if (column > 0) ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f + 8.0f);
            ImGui::TextDisabled("核心 %d", core);
            ImGui::SameLine();
            ImGui::Text("%3.0f%%", cpu.cores[core] * 100.0);
        }
    }

    ImGui::SeparatorText("内存");
    std::snprintf(value, sizeof(value), "%llu MB / %llu MB",
                  static_cast<unsigned long long>(mem.totalKb / 1024 - mem.availableKb / 1024),
                  static_cast<unsigned long long>(mem.totalKb / 1024));
    MeterRow("Used", static_cast<float>(mem.usedFraction), value,
             MeterColor(static_cast<float>(mem.usedFraction)));
    FrameGraph("##memgraph", state.memHistory, 44.0f, 1.0f, kAccent);

    ImGui::SeparatorText("温度与电池");
    if (thermal.hasSoc) {
        KeyValuef("芯片温度", "%.1f C", thermal.socCelsius);
    } else {
        KeyValue("芯片温度", "不可用");
    }
    if (thermal.hasBattery) {
        KeyValuef("电池", "%d%%  (%.1f C)", thermal.batteryPercent, thermal.batteryCelsius);
    } else {
        KeyValue("电池", "不可用");
    }

    ImGui::SeparatorText("运行时间");
    const double uptime = telemetry->uptimeSeconds();
    KeyValuef("系统", "%dd %02dh %02dm", static_cast<int>(uptime / 86400.0),
              static_cast<int>(uptime / 3600.0) % 24, static_cast<int>(uptime / 60.0) % 60);
}

void DrawDeviceTab(MenuState& state) {
    DeviceInfo* device = state.device;
    if (!device) {
        ImGui::TextDisabled("未接入设备信息。");
        return;
    }

    ImGui::SeparatorText("设备");
    KeyValue("型号", device->model.c_str());
    KeyValue("厂商", device->manufacturer.c_str());
    KeyValue("芯片", device->soc.c_str());
    KeyValue("ABI", device->abi.c_str());
    KeyValuef("安卓 API", "%d", device->sdkInt);
    KeyValue("内核", device->kernel.c_str());
    KeyValuef("页大小", "%d bytes", device->pageSize);

    ImGui::SeparatorText("图形");
    KeyValue("GPU", state.glRenderer.c_str());
    KeyValue("Vulkan", state.glVersion.c_str());
    KeyValuef("画布", "%d x %d px", state.width, state.height);
}

void DrawOverlayTab(MenuState& state) {
    ImGui::SeparatorText("图层");
    KeyValue("合成器", "SurfaceFlinger");
    KeyValuef("尺寸", "%d x %d px", state.width, state.height);
    KeyValuef("不透明度", "%.2f", state.opacity);
    KeyValuef("帧率上限", "%d fps", state.targetFps);

    ImGui::Spacing();
    if (ImGui::Button("重置菜单位置", ImVec2(-FLT_MIN, 0))) {
        state.resetPosition = true;
    }
    ImGui::TextDisabled("拖动标题栏移动面板。");

    ImGui::SeparatorText("合成说明");
    ImGui::TextWrapped(
        "面板通过 EGL + OpenGL ES3 绘制到窗口表面，清屏 alpha 为 0，只有控件本身可见；"
        "窗口尺寸变化（旋转、分屏）时会重建 EGL 表面。");
}

void DrawAboutTab(MenuState& state) {
    ImGui::SeparatorText("构建");
    KeyValue("产物", state.artifact.c_str());
    KeyValue("构建时间", kBuildStamp);
    KeyValue("Dear ImGui", IMGUI_VERSION);
    KeyValuef("已渲染帧数", "%llu", static_cast<unsigned long long>(state.frames));

    ImGui::SeparatorText("关于本项目");
    ImGui::TextWrapped(
        "SfImGui 是一个面向 Android 9+ 的原生悬浮层框架：用 Vulkan 把 Dear ImGui 画进 "
        "SurfaceFlinger 合成的图层，并暴露一小组 JNI 接口给宿主使用。");
    ImGui::TextWrapped(
        "它只读取手机自身的 /proc 与 /sys 统计信息，不 attach、不读取、不写入任何其它进程。");
    ImGui::TextDisabled("MIT 许可。Dear ImGui 版权归 Omar Cornut 所有。");
}

}  // namespace

void Draw(MenuState& state) {
    if (state.themeDirty) {
        ApplyTheme(state.scale);
        state.themeDirty = false;
    }
    ImGui::GetStyle().Alpha = state.opacity;

    if (state.telemetry) {
        state.fpsHistory.push(state.fps);
        state.cpuHistory.push(static_cast<float>(state.telemetry->cpu().usage));
        state.memHistory.push(static_cast<float>(state.telemetry->mem().usedFraction));
    }

    // Liquid Glass backdrop: pastel gradient with two soft light blobs, drawn
    // behind every window so the translucent panels have something to frost.
    {
        const ImGuiViewport* background = ImGui::GetMainViewport();
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        const ImVec2 topLeft = background->Pos;
        const ImVec2 bottomRight =
            ImVec2(background->Pos.x + background->Size.x, background->Pos.y + background->Size.y);
        draw->AddRectFilledMultiColor(topLeft, bottomRight, IM_COL32(212, 228, 255, 255),
                                      IM_COL32(230, 220, 255, 255), IM_COL32(255, 234, 244, 255),
                                      IM_COL32(212, 242, 250, 255));
        draw->AddCircleFilled(
            ImVec2(topLeft.x + background->Size.x * 0.22f, topLeft.y + background->Size.y * 0.18f),
            background->Size.x * 0.42f, IM_COL32(255, 255, 255, 70), 64);
        draw->AddCircleFilled(
            ImVec2(topLeft.x + background->Size.x * 0.82f, topLeft.y + background->Size.y * 0.78f),
            background->Size.x * 0.46f, IM_COL32(120, 150, 255, 26), 64);
    }

    DrawHud(state);

    if (!state.visible) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float winWidth = std::min(420.0f * state.scale, viewport->Size.x - 24.0f * state.scale);
    const float winHeight = std::min(560.0f * state.scale, viewport->Size.y - 24.0f * state.scale);
    if (state.resetPosition) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + (viewport->Size.x - winWidth) * 0.5f,
                   viewport->Pos.y + (viewport->Size.y - winHeight) * 0.5f),
            ImGuiCond_Always);
        state.resetPosition = false;
    }
    ImGui::SetNextWindowSize(ImVec2(winWidth, winHeight), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("SfImGui  |  Vulkan 悬浮层", &state.visible,
                     ImGuiWindowFlags_NoCollapse)) {
        Badge("运行中", IM_COL32(10, 20, 18, 255), IM_COL32(92, 199, 184, 235));
        ImGui::SameLine();
        ImGui::Text("%.0f fps", state.fps);
        ImGui::SameLine();
        ImGui::TextDisabled("%.1f ms", state.frameMs);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextDisabled("%d x %d", state.width, state.height);

        if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("渲染")) {
                DrawFrameTab(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("系统")) {
                DrawSystemTab(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("设备")) {
                DrawDeviceTab(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("悬浮层")) {
                DrawOverlayTab(state);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("关于")) {
                DrawAboutTab(state);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

}  // namespace ui
}  // namespace sf
