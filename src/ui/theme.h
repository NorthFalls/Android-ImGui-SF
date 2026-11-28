#pragma once

namespace sf {
namespace ui {

// Liquid Glass ships in two colour variants. The variant is owned by the UI
// state and passed into ApplyTheme(), so the framework never keeps hidden
// global state that the host cannot reset.
enum class ThemeMode {
    Light = 0,
    Dark = 1,
};

// Applies the palette and the metric scaling. Safe to call again whenever the
// UI scale or the colour variant changes.
void ApplyTheme(float scale, ThemeMode mode);

// The scale that the current theme was built with. Widgets use it for the few
// spacing values that cannot ride on ImGuiStyle::ScaleAllSizes().
float Scale();

}  // namespace ui
}  // namespace sf
