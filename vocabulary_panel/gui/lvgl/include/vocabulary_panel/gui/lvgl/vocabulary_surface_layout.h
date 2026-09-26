#pragma once

namespace vocabulary_panel::gui::lvgl::layout {
// Shared by subordinate widgets and shell hit testing; coordinates are relative
// to the shell's content rectangle. Keep touch targets on the visible controls.
inline constexpr int content_x = 40;
inline constexpr int content_y = 144;
inline constexpr int content_width = 944;
inline constexpr int content_height = 432;
inline constexpr int inset = 24;
inline constexpr int text_width = 896;
inline constexpr int actions_y = 344;
inline constexpr int action_height = 64;
inline constexpr int action_width = 212;
inline constexpr int action_gap = 16;
inline constexpr int primary_width = 260;
inline constexpr int action_x(int index) { return inset + index * (action_width + action_gap); }
} // namespace vocabulary_panel::gui::lvgl::layout
