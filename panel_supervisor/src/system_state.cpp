#include "panel_supervisor/system_state.hpp"

namespace house_panel::supervisor {

std::string_view to_string(const SystemState system_state) noexcept
{
	switch (system_state) {
	case SystemState::Starting:
		return "starting";
	case SystemState::Active:
		return "active";
	case SystemState::Suspended:
		return "suspended";
	case SystemState::RecomputingAfterResume:
		return "recomputing_after_resume";
	case SystemState::Faulted:
		return "faulted";
	}
	return "unrecognized_system_state";
}

} // namespace house_panel::supervisor
