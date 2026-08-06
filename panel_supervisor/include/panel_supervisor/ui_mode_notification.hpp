#pragma once

#include "panel_supervisor/presence_state.hpp"
#include "panel_supervisor/solar_state.hpp"
#include "panel_supervisor/system_state.hpp"
#include "panel_supervisor/ui_mode.hpp"

#include <chrono>

namespace house_panel::supervisor {

// @canonical ui_mode_notification -- complete policy snapshot crossing the UI process boundary.
struct UiModeNotification {
	UiMode ui_mode;
	PresenceState presence_state;
	SolarState solar_state;
	SystemState system_state;
	int effective_brightness_percent;
	std::chrono::system_clock::time_point observed_at;
};

} // namespace house_panel::supervisor
