#pragma once

#include <cstdint>
#include <string>

#include "sys/device_info.h"
#include "sys/telemetry.h"
#include "ui/widgets.h"

namespace sf {
namespace ui {

struct MenuState {
    bool visible = true;
    bool menuEnabled = false;  // launcher switch: show the ImGui panel
    bool showHud = true;
    bool acceptInput = true;
    float opacity = 1.0f;
    float scale = 1.0f;
    int targetFps = 0;  // 0 = follow the display refresh rate
    bool themeDirty = false;
    bool resetPosition = false;
    bool hudExpanded = false;
    float hudAnim = 0.0f;

    float fps = 0.0f;
    float frameMs = 0.0f;
    uint64_t frames = 0;
    int width = 0;
    int height = 0;
    Telemetry* telemetry = nullptr;
    DeviceInfo* device = nullptr;
    std::string artifact;
    std::string glRenderer;
    std::string glVersion;

    RingBuffer fpsHistory;
    RingBuffer cpuHistory;
    RingBuffer memHistory;
};

void Draw(MenuState& state);

}  // namespace ui
}  // namespace sf
