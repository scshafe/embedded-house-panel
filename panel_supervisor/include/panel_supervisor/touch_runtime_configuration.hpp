#pragma once
#include "panel_supervisor/evdev_touch_source.hpp"
#include "panel_supervisor/touch_observation_authority.hpp"
#include <map>

namespace house_panel::supervisor {
inline constexpr std::array<const char *, 13> kTouchRuntimeEnvironmentFields{
	"BHP_PANEL_SUPERVISOR_TOUCH_INPUT_PATH", "BHP_PANEL_SUPERVISOR_TOUCH_INPUT_NAME",
	"BHP_PANEL_SUPERVISOR_TOUCH_PROTOCOL", "BHP_PANEL_SUPERVISOR_TOUCH_X_MIN",
	"BHP_PANEL_SUPERVISOR_TOUCH_X_MAX", "BHP_PANEL_SUPERVISOR_TOUCH_Y_MIN",
	"BHP_PANEL_SUPERVISOR_TOUCH_Y_MAX", "BHP_PANEL_SUPERVISOR_TOUCH_CONTACT_KEY",
	"BHP_PANEL_SUPERVISOR_TOUCH_SLOTS", "BHP_PANEL_SUPERVISOR_TOUCH_SOCKET",
	"BHP_PANEL_SUPERVISOR_TOUCH_TTL_MILLISECONDS",
	"BHP_PANEL_SUPERVISOR_TOUCH_MAX_CONTACT_MILLISECONDS",
	"BHP_PANEL_SUPERVISOR_TOUCH_MAX_OBSERVATIONS"};
struct TouchRuntimeConfiguration {
	TouchEvdevConfiguration device;
	std::string socket_path;
	TouchObservationAuthorityConfiguration authority;
	/** No keys means disabled. Any key requires the complete explicit configuration. */
	[[nodiscard]] static Result<std::optional<TouchRuntimeConfiguration>> parse(
		const std::map<std::string, std::string> &fields);
};
}
