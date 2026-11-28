#include "ui/menu.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "core/clock.h"
#include "imgui.h"
#include "sf/direct_surface.h"
#include "ui/theme.h"

namespace sf {
namespace ui {
namespace {

constexpr ImU32 kAccent = IM_COL32(92, 199, 184, 255);
constexpr ImU32 kWarn = IM_COL32(232, 176, 82, 255);
constexpr ImU32 kBad = IM_COL32(226, 106, 106, 255);

constexpr const char* kBuildStamp = __DATE__ " " __TIME__;

// Compact preset rows: a slider is precise but a touch screen favours the few
// values people actually pick, and a tap is faster than a drag.
bool PresetRow(const int* values, const char* const* labels, int count, int* current) {
    bool changed = false;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - spacing * (count - 1)) / count;
    for (int i = 0; i < count; ++i) {
        if (i > 0) ImGui::SameLine();
        const bool active = *current == values[i];
        ImGui::PushID(labels[i]);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.74f, 0.66f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.10f, 0.09f, 1.0f));
        }
        if (ImGui::Button(labels[i], ImVec2(width, 0.0f))) {
            *current = values[i];
            changed = true;
        }
        if (active) ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    return changed;
}

bool PresetRowF(const float* values, const char* const* labels, int count, float* current) {
    bool changed = false;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float width = (ImGui::GetContentRegionAvail().x - spacing * (count - 1)) / count;
    for (int i = 0; i < count; ++i) {
        if (i > 0) ImGui::SameLine();
        const bool active = std::fabs(*current - values[i]) < 0.001f;
        ImGui::PushID(labels[i]);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.74f, 0.66f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.10f, 0.09f, 1.0f));
        }
        if (ImGui::Button(labels[i], ImVec2(width, 0.0f))) {
            *current = values[i];
            changed = true;
        }
        if (active) ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    return changed;
}

ImU32 MeterColor(float fraction) {
    if (fraction >= 0.85f) return kBad;
    if (fraction >= 0.65f) return kWarn;
    return kAccent;
}

// Draw list colours are not touched by ImGuiStyle::Alpha, so the panels and the
// backdrop have to fade themselves when the opacity slider moves.
ImU32 ScaleAlpha(ImU32 color, float factor) {
    factor = std::clamp(factor, 0.0f, 1.0f);
    const ImU32 alpha = static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFF) * factor);
    return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

void DrawHud(MenuState& state) {
    if (!state.showHud) {
        // A hidden island must not keep the renderer out of its 30 fps idle
        // mode just because the expanded state was never cleared.
        state.hudExpanded = false;
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float s = state.scale;
    const float dt = ImGui::GetIO().DeltaTime;
    const float alpha = std::clamp(state.opacity, 0.2f, 1.0f);
    const float target = state.hudExpanded ? 1.0f : 0.0f;
    state.hudAnim += (target - state.hudAnim) * std::min(1.0f, dt * 16.0f);
    if (state.hudAnim < 0.001f) state.hudAnim = 0.0f;
    if (state.hudAnim > 0.999f) state.hudAnim = 1.0f;
    const float a = state.hudAnim;

    const float collapsedW = 148.0f * s;
    const float collapsedH = 36.0f * s;
    const float expandedW = 330.0f * s;
    const float expandedH = 200.0f * s;
    const float w = collapsedW + (expandedW - collapsedW) * a;
    const float h = collapsedH + (expandedH - collapsedH) * a;
    const float x = viewport->Pos.x + (viewport->Size.x - w) * 0.5f;
    const float y = viewport->Pos.y + 10.0f * s;
    const ImVec2 p0(x, y);
    const ImVec2 p1(x + w, y + h);
    const float rounding = collapsedH * 0.5f + (30.0f * s - collapsedH * 0.5f) * a;

    // The closed panel deserves a visible way back: a tap target inside the
    // expanded island instead of "restart the app".
    const bool showReopen = a > 0.55f && !state.visible;
    ImVec2 reopenMin(0.0f, 0.0f);
    ImVec2 reopenMax(0.0f, 0.0f);
    if (showReopen) {
        const float buttonW = 168.0f * s;
        const float buttonH = 30.0f * s;
        reopenMin = ImVec2(p0.x + (w - buttonW) * 0.5f, p0.y + 164.0f * s);
        reopenMax = ImVec2(reopenMin.x + buttonW, reopenMin.y + buttonH);
    }

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
                            ImVec2(p1.x + 2.0f * s, p1.y + 2.0f * s),
                            ScaleAlpha(IM_COL32(0, 0, 0, 45), alpha), rounding + 2.0f * s);
        draw->AddRectFilled(p0, p1, ScaleAlpha(IM_COL32(8, 9, 12, 246), alpha), rounding);

        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##island_click", ImVec2(w, h));
        const bool islandHovered = ImGui::IsItemHovered();
        const bool clickingIsland = ImGui::IsItemClicked();

        const ImU32 accent = ScaleAlpha(IM_COL32(76, 194, 168, 255), alpha);
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
            if (clickingIsland) state.hudExpanded = true;
        } else {
            const float left = p0.x + 20.0f * s;
            float ty = p0.y + 12.0f * s;
            draw->AddCircleFilled(ImVec2(left + 4.0f * s, ty + ImGui::GetTextLineHeight() * 0.5f),
                                  3.6f * s, accent);
            ImGui::SetCursorScreenPos(ImVec2(left + 16.0f * s, ty));
            ImGui::TextUnformatted("SfImGui");
            ImGui::SameLine();
            ImGui::TextDisabled(state.visible ? "点击任意处收起" : "面板已关闭");

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
            // Battery and thermal data were already sampled for the system tab;
            // the expanded island is exactly where a quick glance wants them.
            char hardware[128] = {};
            if (state.telemetry) {
                const ThermalStats& thermal = state.telemetry->thermal();
                const char* charge = thermal.charging ? " 充电中" : "";
                if (thermal.batteryPercent >= 0 && thermal.hasSoc) {
                    std::snprintf(hardware, sizeof(hardware), "电池 %d%%%s  ·  芯片 %.1f C",
                                  thermal.batteryPercent, charge, thermal.socCelsius);
                } else if (thermal.batteryPercent >= 0) {
                    std::snprintf(hardware, sizeof(hardware), "电池 %d%%%s", thermal.batteryPercent,
                                  charge);
                } else if (thermal.hasSoc) {
                    std::snprintf(hardware, sizeof(hardware), "芯片 %.1f C", thermal.socCelsius);
                }
            }
            if (hardware[0] != '\0') {
                ImGui::SetCursorScreenPos(ImVec2(left, p0.y + 112.0f * s));
                ImGui::TextDisabled("%s", hardware);
            }
            std::snprintf(text, sizeof(text), "%llu frames    %d x %d",
                          static_cast<unsigned long long>(state.frames), state.width, state.height);
            ImGui::SetCursorScreenPos(ImVec2(left, p0.y + 140.0f * s));
            ImGui::TextDisabled("%s", text);

            if (showReopen) {
                const float buttonH = reopenMax.y - reopenMin.y;
                draw->AddRectFilled(reopenMin, reopenMax,
                                    ScaleAlpha(IM_COL32(92, 199, 184, 235), alpha),
                                    buttonH * 0.5f);
                const ImVec2 textSize = ImGui::CalcTextSize("打开面板");
                draw->AddText(ImVec2(reopenMin.x + (reopenMax.x - reopenMin.x - textSize.x) * 0.5f,
                                     reopenMin.y + (buttonH - textSize.y) * 0.5f),
                              ScaleAlpha(IM_COL32(10, 20, 18, 255), alpha), "打开面板");
            }

            if (clickingIsland) state.hudExpanded = false;
        }

        // Like the phone: a tap anywhere else collapses the island again.
        const bool overReopen =
            showReopen && ImGui::IsMouseHoveringRect(reopenMin, reopenMax);
        if (showReopen && ImGui::IsMouseClicked(0) && overReopen) {
            state.visible = true;
            state.hudExpanded = false;
        } else if (state.hudExpanded && !islandHovered && !overReopen && ImGui::IsMouseClicked(0)) {
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

    static const int kFpsValues[] = {0, 30, 60, 90, 120};
    static const char* const kFpsLabels[] = {"跟随屏幕", "30", "60", "90", "120"};
    PresetRow(kFpsValues, kFpsLabels, 5, &state.targetFps);

    ImGui::Spacing();
    ImGui::TextDisabled("帧率曲线");
    FrameGraph("##fpsgraph", state.fpsHistory, 54.0f * state.scale, 120.0f, kAccent);

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

    ImGui::TextDisabled("主题");
    if (ImGui::RadioButton("浅色 Liquid Glass", state.theme == ThemeMode::Light)) {
        state.theme = ThemeMode::Light;
        state.themeDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("深色 Liquid Glass", state.theme == ThemeMode::Dark)) {
        state.theme = ThemeMode::Dark;
        state.themeDirty = true;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##scale", &state.scale, 0.75f, 2.0f, "界面缩放：%.2fx")) {
        state.themeDirty = true;
    }
    static const float kScaleValues[] = {0.75f, 1.0f, 1.25f, 1.5f};
    static const char* const kScaleLabels[] = {"0.75x", "1.0x", "1.25x", "1.5x"};
    if (PresetRowF(kScaleValues, kScaleLabels, 4, &state.scale)) state.themeDirty = true;

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##opacity", &state.opacity, 0.2f, 1.0f, "不透明度：%.2f");
    ImGui::Checkbox("显示灵动岛", &state.showHud);
    if (!state.showHud) {
        ImGui::TextDisabled("灵动岛关闭时面板不再提供关闭按钮，避免关闭后没有任何入口。");
    }

    ImGui::SeparatorText("输入");
    ImGui::TextDisabled("触摸事件由 android_main 收集后交给 ImGui 处理，始终启用。");

    ImGui::SeparatorText("设置");
    ImGui::TextDisabled("缩放、主题、不透明度与开关会自动保存，下次启动继续使用。");
    if (ImGui::Button("恢复默认设置", ImVec2(-FLT_MIN, 0))) {
        state.resetSettings = true;
    }
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
    MeterRow("负载", static_cast<float>(cpu.usage), value, MeterColor(static_cast<float>(cpu.usage)));
    FrameGraph("##cpugraph", state.cpuHistory, 44.0f * state.scale, 1.0f, kAccent);

    const int columns = 2;
    const int rows = (cpu.coreCount + columns - 1) / columns;
    if (cpu.coreCount > 0) {
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int core = row * columns + column;
                if (core >= cpu.coreCount) continue;
                if (column > 0)
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f + 8.0f * state.scale);
                ImGui::TextDisabled("核心 %d", core);
                ImGui::SameLine();
                ImGui::Text("%3.0f%%", cpu.cores[core] * 100.0);
            }
        }
    }

    ImGui::SeparatorText("内存");
    // MemAvailable can be reported larger than MemTotal by a few kernels; the
    // unsigned subtraction used to wrap into an absurd "used" figure.
    const uint64_t totalMb = mem.totalKb / 1024;
    const uint64_t freeMb = mem.availableKb / 1024;
    const uint64_t usedMb = totalMb > freeMb ? totalMb - freeMb : 0;
    std::snprintf(value, sizeof(value), "%llu MB / %llu MB",
                  static_cast<unsigned long long>(usedMb),
                  static_cast<unsigned long long>(totalMb));
    MeterRow("占用", static_cast<float>(mem.usedFraction), value,
             MeterColor(static_cast<float>(mem.usedFraction)));
    FrameGraph("##memgraph", state.memHistory, 44.0f * state.scale, 1.0f, kAccent);

    ImGui::SeparatorText("温度与电池");
    if (thermal.hasSoc) {
        KeyValuef("芯片温度", "%.1f C", thermal.socCelsius);
    } else {
        KeyValue("芯片温度", "不可用");
    }
    if (thermal.hasBattery) {
        // Capacity and temperature are separate files: a device may expose the
        // temperature while the capacity node is locked down.
        if (thermal.batteryPercent >= 0) {
            KeyValuef("电池", "%d%%%s  (%.1f C)", thermal.batteryPercent,
                      thermal.charging ? "  充电中" : "", thermal.batteryCelsius);
        } else {
            KeyValuef("电池", "电量未知  (%.1f C)", thermal.batteryCelsius);
        }
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
    KeyValuef("屏幕密度", "%d dpi", device->densityDpi);

    ImGui::SeparatorText("图形");
    KeyValue("GPU", state.glRenderer.c_str());
    KeyValue("OpenGL ES", state.glVersion.c_str());
    KeyValuef("画布", "%d x %d px", state.width, state.height);
}

void DrawDisplayTab(MenuState& state) {
    if (!state.startupNote.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.24f, 0.20f, 1.0f));
        ImGui::TextWrapped("%s", state.startupNote.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    ImGui::SeparatorText("表面");
    const char* backend = state.backend.empty() ? "NativeActivity 窗口" : state.backend.c_str();
    KeyValue("渲染后端", backend);
    if (!state.backendDetail.empty()) ImGui::TextWrapped("%s", state.backendDetail.c_str());
    KeyValuef("尺寸", "%d x %d px", state.width, state.height);
    KeyValuef("不透明度", "%.2f", state.opacity);
    KeyValuef("帧率上限", "%d fps", state.targetFps);

    ImGui::SeparatorText("SurfaceFlinger 直连");
    ImGui::TextDisabled(
        "本版本只提供 root 二进制宿主：启动时解析 libgui.so 的 SurfaceComposerClient，直接创建 Surface "
        "提交给 SurfaceFlinger，没有应用窗口可回退。任意一步失败都会在启动它的终端里打印原因。");
    if (ImGui::Button("重新探测 libgui.so", ImVec2(-FLT_MIN, 0))) {
        const sf::direct::ProbeResult probe = sf::direct::Probe();
        state.probeResult = probe.detail;
    }
    if (!state.probeResult.empty()) ImGui::TextWrapped("%s", state.probeResult.c_str());

    ImGui::Spacing();
    if (ImGui::Button("重置菜单位置", ImVec2(-FLT_MIN, 0))) {
        state.resetPosition = true;
    }
    ImGui::TextDisabled("拖动标题栏移动面板。");

    ImGui::SeparatorText("绘制说明");
    ImGui::TextWrapped(
        "面板由 EGL + OpenGL ES3 绘制在当前渲染后端提供的表面上，eglSwapInterval 跟随屏幕刷新；"
        "表面尺寸变化（旋转、分屏）时会重建 EGL 表面。");
}

void DrawAboutTab(MenuState& state) {
    ImGui::SeparatorText("构建");
    KeyValue("产物", state.artifact.c_str());
    KeyValue("构建时间", kBuildStamp);
    KeyValue("Dear ImGui", IMGUI_VERSION);
    KeyValuef("初始化耗时", "%d ms", state.initMs);
    KeyValue("主题", state.theme == ThemeMode::Dark ? "深色 Liquid Glass" : "浅色 Liquid Glass");
    KeyValuef("已渲染帧数", "%llu", static_cast<unsigned long long>(state.frames));

    if (!state.crashReport.empty()) {
        ImGui::SeparatorText("上次启动崩溃记录");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.18f, 0.18f, 1.0f));
        ImGui::TextWrapped("%s", state.crashReport.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("完整记录保存在应用文件目录的 native_crash_prev.txt，可直接发给开发者。");
    }

    ImGui::SeparatorText("关于本项目");
    ImGui::TextWrapped(
        "SfImGui 是一个面向 Android 9+ 的原生界面框架：用 EGL + OpenGL ES3 把 Dear ImGui "
        "绘制到 NativeActivity 的窗口表面上，全部代码由 C/C++ 组成，宿主不需要 Java 与 JNI。");
    ImGui::TextWrapped(
        "它只读取手机自身的 /proc 与 /sys 统计信息，不 attach、不读取、不写入任何其它进程。");
    ImGui::TextDisabled("MIT 许可。Dear ImGui 版权归 Omar Cornut 所有。");
}

}  // namespace

void Draw(MenuState& state) {
    if (state.resetSettings) {
        state.resetSettings = false;
        state.scale = 1.0f;
        state.opacity = 1.0f;
        state.targetFps = 0;
        state.showHud = true;
        state.acceptInput = true;
        state.theme = ThemeMode::Light;
        // Resetting the UI also drops the experimental renderer: it is stored in
        // the same settings file and "defaults" must include the backend choice.
        state.directSurface = false;
        state.themeDirty = true;
    }
    if (state.themeDirty) {
        ApplyTheme(state.scale, state.theme);
        state.themeDirty = false;
    }
    ImGui::GetStyle().Alpha = std::clamp(state.opacity, 0.2f, 1.0f);

    if (state.telemetry) {
        state.fpsHistory.push(state.fps);
        state.cpuHistory.push(static_cast<float>(state.telemetry->cpu().usage));
        state.memHistory.push(static_cast<float>(state.telemetry->mem().usedFraction));
    }

    // Liquid Glass backdrop: a soft gradient with two light blobs, drawn behind
    // every window so the translucent panels have something to frost. The dark
    // variant sinks the same composition into deep navy glass. The root host is a
    // system-wide overlay, so it keeps the frame transparent instead.
    if (!state.transparent) {
        const float alpha = std::clamp(state.opacity, 0.2f, 1.0f);
        const bool dark = state.theme == ThemeMode::Dark;
        const ImGuiViewport* background = ImGui::GetMainViewport();
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        const ImVec2 topLeft = background->Pos;
        const ImVec2 bottomRight =
            ImVec2(background->Pos.x + background->Size.x, background->Pos.y + background->Size.y);
        if (dark) {
            draw->AddRectFilledMultiColor(
                topLeft, bottomRight, ScaleAlpha(IM_COL32(14, 18, 30, 255), alpha),
                ScaleAlpha(IM_COL32(24, 20, 44, 255), alpha),
                ScaleAlpha(IM_COL32(36, 24, 48, 255), alpha),
                ScaleAlpha(IM_COL32(12, 30, 40, 255), alpha));
            draw->AddCircleFilled(
                ImVec2(topLeft.x + background->Size.x * 0.22f, topLeft.y + background->Size.y * 0.18f),
                background->Size.x * 0.42f, ScaleAlpha(IM_COL32(255, 255, 255, 22), alpha), 64);
            draw->AddCircleFilled(
                ImVec2(topLeft.x + background->Size.x * 0.82f, topLeft.y + background->Size.y * 0.78f),
                background->Size.x * 0.46f, ScaleAlpha(IM_COL32(90, 130, 255, 40), alpha), 64);
        } else {
            draw->AddRectFilledMultiColor(
                topLeft, bottomRight, ScaleAlpha(IM_COL32(212, 228, 255, 255), alpha),
                ScaleAlpha(IM_COL32(230, 220, 255, 255), alpha),
                ScaleAlpha(IM_COL32(255, 234, 244, 255), alpha),
                ScaleAlpha(IM_COL32(212, 242, 250, 255), alpha));
            draw->AddCircleFilled(
                ImVec2(topLeft.x + background->Size.x * 0.22f, topLeft.y + background->Size.y * 0.18f),
                background->Size.x * 0.42f, ScaleAlpha(IM_COL32(255, 255, 255, 70), alpha), 64);
            draw->AddCircleFilled(
                ImVec2(topLeft.x + background->Size.x * 0.82f, topLeft.y + background->Size.y * 0.78f),
                background->Size.x * 0.46f, ScaleAlpha(IM_COL32(120, 150, 255, 26), alpha), 64);
        }
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

    // The title-bar close button is only offered while the island still has an
    // entry point to bring the panel back.
    bool* closeButton = state.showHud ? &state.visible : nullptr;
    if (ImGui::Begin("SfImGui  |  OpenGL ES 面板", closeButton, ImGuiWindowFlags_NoCollapse)) {
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
            if (ImGui::BeginTabItem("显示")) {
                DrawDisplayTab(state);
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
