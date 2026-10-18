#include "panel_ui/display_rotation.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using namespace house_panel::panel_ui;

int test_only_exact_values_parse()
{
	return parse_display_rotation("0") == DisplayRotation::Degrees0 &&
			       parse_display_rotation("180") == DisplayRotation::Degrees180 &&
			       !parse_display_rotation("") && !parse_display_rotation("90") &&
			       !parse_display_rotation("180 ") && !parse_display_rotation("-180")
		       ? 0
		       : 1;
}

int test_zero_leaves_the_raster_unchanged()
{
	const std::vector<std::uint32_t> pixels{1, 2, 3, 4, 5, 6};
	return rotate_raster(pixels, DisplayRotation::Degrees0) == pixels ? 0 : 1;
}

int test_half_turn_moves_each_corner_to_its_opposite()
{
	// 3 x 2 raster: row 0 is 1 2 3, row 1 is 4 5 6.
	const std::vector<std::uint32_t> pixels{1, 2, 3, 4, 5, 6};
	const auto rotated = rotate_raster(pixels, DisplayRotation::Degrees180);
	const bool expected = rotated == std::vector<std::uint32_t>{6, 5, 4, 3, 2, 1};
	return expected && rotate_raster(rotated, DisplayRotation::Degrees180) == pixels ? 0 : 1;
}

int test_touch_follows_the_raster()
{
	constexpr int width = 1024;
	constexpr int height = 600;
	const auto top_left = logical_touch_point(0, 0, width, height, DisplayRotation::Degrees180);
	const auto bottom_right =
		logical_touch_point(width - 1, height - 1, width, height, DisplayRotation::Degrees180);
	const auto inside = logical_touch_point(100, 50, width, height, DisplayRotation::Degrees180);
	const auto unrotated = logical_touch_point(100, 50, width, height, DisplayRotation::Degrees0);
	return top_left == std::pair<int, int>{width - 1, height - 1} &&
			       bottom_right == std::pair<int, int>{0, 0} &&
			       inside == std::pair<int, int>{923, 549} &&
			       unrotated == std::pair<int, int>{100, 50}
		       ? 0
		       : 1;
}

} // namespace

int main()
{
	const int failures = test_only_exact_values_parse() + test_zero_leaves_the_raster_unchanged() +
			     test_half_turn_moves_each_corner_to_its_opposite() +
			     test_touch_follows_the_raster();
	if (failures != 0) {
		std::cerr << "display_rotation_test failures: " << failures << '\n';
	}
	return failures == 0 ? 0 : 1;
}
