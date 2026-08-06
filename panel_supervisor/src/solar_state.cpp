#include "panel_supervisor/solar_state.hpp"

namespace house_panel::supervisor {

std::string_view to_string(const SolarState solar_state) noexcept
{
	switch (solar_state) {
	case SolarState::Unknown:
		return "unknown";
	case SolarState::Day:
		return "day";
	case SolarState::Night:
		return "night";
	}
	return "unrecognized_solar_state";
}

} // namespace house_panel::supervisor
