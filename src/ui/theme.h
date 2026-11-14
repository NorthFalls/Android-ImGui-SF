#pragma once

namespace sf {
namespace ui {

// Applies the dark, translucent style the overlay is known for. Safe to call
// again when the user changes the UI scale.
void ApplyTheme(float scale);

}  // namespace ui
}  // namespace sf
