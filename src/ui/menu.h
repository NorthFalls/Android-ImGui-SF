#pragma once

#include <cstdint>
#include <string>

#include "sys/device_info.h"
#include "sys/telemetry.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace sf {
namespace ui {

struct MenuState {
    bool visible = true;
    bool showHud = true;
    // Root host: leave the frame transparent instead of painting the backdrop.
    bool transparent = false;
    bool acceptInput = true;
    float opacity = 1.0f;
    float scale = 1.0f;
    int targetFps = 0;  // 0 = follow the display refresh rate
    ThemeMode theme = ThemeMode::Light;
    bool themeDirty = false;
    bool resetPosition = false;
    bool resetSettings = false;
    // Persisted choice for the direct SurfaceFlinger backend. android_main
    // reads it before the engine starts, so it applies on the next launch.
    bool directSurface = false;
    bool hudExpanded = false;
    float hudAnim = 0.0f;

    float fps = 0.0f;
    float frameMs = 0.0f;
    uint64_t frames = 0;
    int width = 0;
    int height = 0;
    int initMs = 0;
    Telemetry* telemetry = nullptr;
    DeviceInfo* device = nullptr;
    std::string artifact;
    std::string backend;
    std::string backendDetail;
    // Startup recovery report: why the previous launch failed and the crash tail
    // it left behind. Both are surfaced in the panel after a self-recovery.
    std::string startupNote;
    std::string crashReport;
    std::string probeResult;
    std::string glRenderer;
    std::string glVersion;

    RingBuffer fpsHistory;
    RingBuffer cpuHistory;
    RingBuffer memHistory;
};

void Draw(MenuState& state);

}  // namespace ui
}  // namespace sf
