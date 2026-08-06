#pragma once

#include "panel_supervisor/result.hpp"

#include <string>

namespace house_panel::supervisor::detail {

// @canonical sysfs_backlight_file_access -- logical mapping and exact backlight node I/O.
[[nodiscard]] Result<void>
set_backlight_brightness_percent_at_paths(const std::string &brightness_path,
					  const std::string &maximum_brightness_path,
					  int brightness_percent);

} // namespace house_panel::supervisor::detail
