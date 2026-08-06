#pragma once

#include "panel_supervisor/backlight.hpp"

#include <string>

namespace house_panel::supervisor {

// @canonical sysfs_backlight -- validated logical-percent mapping to explicit brightness nodes.
class SysfsBacklight final : public Backlight {
public:
	/**
	 * Validates explicit brightness and max_brightness paths.
	 *
	 * Paths must be the same exact single-component device under
	 * `/sys/class/backlight/<device>/{brightness,max_brightness}`. Invalid input returns
	 * PANEL_SUPERVISOR.BACKLIGHT_CONFIGURATION_INVALID.
	 */
	[[nodiscard]] static Result<SysfsBacklight> create(std::string brightness_path,
							   std::string maximum_brightness_path);

	/**
	 * Re-reads max_brightness, maps 0..100 percent with nearest-integer rounding,
	 * and writes the exact mapped integer to brightness.
	 *
	 * Invalid percentage, malformed/zero maximum, or file access failure returns
	 * a typed error. Values are never silently clamped.
	 */
	[[nodiscard]] Result<void> set_brightness_percent(int brightness_percent) override;

	[[nodiscard]] const std::string &brightness_path() const noexcept;
	[[nodiscard]] const std::string &maximum_brightness_path() const noexcept;

private:
	SysfsBacklight(std::string brightness_path, std::string maximum_brightness_path);

	std::string brightness_path_;
	std::string maximum_brightness_path_;
};

} // namespace house_panel::supervisor
