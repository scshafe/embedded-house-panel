#pragma once

#include "panel_supervisor/presence_state.hpp"
#include "panel_supervisor/solar_state.hpp"
#include "panel_supervisor/system_state.hpp"
#include "panel_supervisor/ui_mode.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace house_panel::supervisor {

// @canonical panel_supervisor_state -- inspectable state committed by PanelSupervisor.
struct PanelState {
	PresenceState presence_state{PresenceState::Unknown};
	SolarState solar_state{SolarState::Unknown};
	SystemState system_state{SystemState::Starting};
	std::optional<std::chrono::system_clock::time_point> last_presence_detected_at;
	std::optional<int> applied_brightness_percent;
	std::optional<UiMode> notified_ui_mode;
	std::uint64_t policy_revision{0};
};

} // namespace house_panel::supervisor
