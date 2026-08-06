#pragma once

#include "panel_supervisor/solar_state.hpp"
#include "panel_supervisor/supervisor_event.hpp"

#include <chrono>

namespace house_panel::supervisor {

// @canonical panel_policy_observation -- complete immutable input for one policy evaluation.
struct PolicyObservation {
	bool presence_detected;
	SolarState solar_state;
	std::chrono::system_clock::time_point observed_at;
	SupervisorEvent event;
};

} // namespace house_panel::supervisor
