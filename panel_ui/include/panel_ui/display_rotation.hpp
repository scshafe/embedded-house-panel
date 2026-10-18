#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace house_panel::panel_ui {

/** Whole-panel rotation applied at the display boundary, after rendering. */
enum class DisplayRotation { Degrees0, Degrees180 };

/** Accepts exactly "0" or "180"; anything else is rejected, never defaulted. */
[[nodiscard]] std::optional<DisplayRotation> parse_display_rotation(std::string_view text);

// @canonical panel_display_rotation -- the raster and touch transform for a rotated panel.
// The renderer and hit testing stay in logical coordinates; only the pixels sent
// to the panel and the touch points read from it are transformed, by the same rotation.

/** Returns the row-major raster as the rotated panel must show it. Size is unchanged. */
[[nodiscard]] std::vector<std::uint32_t> rotate_raster(const std::vector<std::uint32_t> &pixels,
						       DisplayRotation rotation);

/** Maps a point read from the panel (0..width-1, 0..height-1) to logical coordinates. */
[[nodiscard]] std::pair<int, int> logical_touch_point(int panel_x, int panel_y, int width, int height,
						      DisplayRotation rotation);

} // namespace house_panel::panel_ui
