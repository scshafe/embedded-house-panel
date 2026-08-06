#pragma once

#include "panel_supervisor/presence_state.hpp"
#include "panel_supervisor/solar_state.hpp"
#include "panel_supervisor/system_state.hpp"
#include "panel_supervisor/ui_mode.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace house_panel::supervisor {

// @canonical panel_policy_decision -- complete desired state and side-effect intent for one
// observation.
struct PolicyDecision {
	PresenceState presence_state;
	SolarState solar_state;
	SystemState target_system_state;
	std::optional<std::chrono::system_clock::time_point> last_presence_detected_at;
	int brightness_percent;
	UiMode ui_mode;
	bool request_suspend;
	std::uint64_t policy_revision;
};

} // namespace house_panel::supervisor
