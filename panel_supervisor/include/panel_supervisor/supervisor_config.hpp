#pragma once

#include "panel_supervisor/brightness_levels.hpp"
#include "panel_supervisor/result.hpp"

#include <chrono>

namespace house_panel::supervisor {

// @canonical panel_supervisor_configuration -- validated policy configuration; no runtime defaults
// are hidden.
class SupervisorConfig {
public:
	/**
	 * Validates and constructs configuration.
	 *
	 * `grace_period` must be non-negative. Active and grace brightness values must
	 * be in the logical inclusive range 0..100; suspended brightness must be exactly
	 * zero so every off/suspend decision physically blanks the display. Failure returns
	 * PANEL_SUPERVISOR.INVALID_CONFIGURATION with every invalid field in context.
	 */
	[[nodiscard]] static Result<SupervisorConfig> create(
		std::chrono::milliseconds grace_period,
		BrightnessLevels brightness_levels,
		std::chrono::milliseconds presentation_timeout);

	[[nodiscard]] std::chrono::milliseconds grace_period() const noexcept;
	[[nodiscard]] const BrightnessLevels &brightness_levels() const noexcept;
	[[nodiscard]] std::chrono::milliseconds presentation_timeout() const noexcept;

private:
	SupervisorConfig(std::chrono::milliseconds grace_period,
			 BrightnessLevels brightness_levels,
			 std::chrono::milliseconds presentation_timeout) noexcept;

	std::chrono::milliseconds grace_period_;
	BrightnessLevels brightness_levels_;
	std::chrono::milliseconds presentation_timeout_;
};

} // namespace house_panel::supervisor
