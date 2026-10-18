#include "panel_ui/display_rotation.hpp"

#include <algorithm>

namespace house_panel::panel_ui {

std::optional<DisplayRotation> parse_display_rotation(std::string_view text)
{
	if (text == "0") {
		return DisplayRotation::Degrees0;
	}
	if (text == "180") {
		return DisplayRotation::Degrees180;
	}
	return std::nullopt;
}

std::vector<std::uint32_t> rotate_raster(const std::vector<std::uint32_t> &pixels,
					 DisplayRotation rotation)
{
	if (rotation == DisplayRotation::Degrees0) {
		return pixels;
	}
	// A 180-degree turn of a row-major raster is the raster read backwards.
	return std::vector<std::uint32_t>(pixels.rbegin(), pixels.rend());
}

std::pair<int, int> logical_touch_point(int panel_x, int panel_y, int width, int height,
					DisplayRotation rotation)
{
	if (rotation == DisplayRotation::Degrees0) {
		return {panel_x, panel_y};
	}
	return {width - 1 - panel_x, height - 1 - panel_y};
}

} // namespace house_panel::panel_ui
