#pragma once

namespace house_panel::supervisor {

// @canonical panel_brightness_levels -- all configurable logical brightness outputs in percent.
struct BrightnessLevels {
	int present_day_percent;
	int present_night_percent;
	int grace_day_percent;
	int grace_night_percent;
	// SupervisorConfig requires this safety-critical output to be exactly zero.
	int suspended_percent;
};

} // namespace house_panel::supervisor
